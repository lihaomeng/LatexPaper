#include <lightoverleaf/workspace/adapters/klocalworkspacestore.h>
#include <lightoverleaf/workspace/domain/kworkspacepolicy.h>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <vector>

namespace lightoverleaf::workspace
{
namespace
{
KError error(KErrorCode code, const char* key, bool retryable = false)
{
    return {code, key, retryable};
}

bool toWide(const std::string& utf8, std::wstring& output)
{
    if (utf8.empty() || utf8.size() > kMaxWorkspaceSelectionBytes) return false;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0) return false;
    output.resize(static_cast<std::size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), output.data(), count) == count;
}

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

KResult<std::string> hash(const std::string& input)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE state = nullptr;
    DWORD objectBytes = 0;
    DWORD hashBytes = 0;
    DWORD copied = 0;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &copied, 0);
    if (status >= 0) status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashBytes), sizeof(hashBytes), &copied, 0);
    if (status >= 0)
    {
        object.resize(objectBytes);
        digest.resize(hashBytes);
        status = BCryptCreateHash(algorithm, &state, object.data(), objectBytes, nullptr, 0, 0);
    }
    if (status >= 0 && !input.empty())
        status = BCryptHashData(state, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
            static_cast<ULONG>(input.size()), 0);
    if (status >= 0) status = BCryptFinishHash(state, digest.data(), hashBytes, 0);
    if (state) BCryptDestroyHash(state);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0 || digest.size() != 32) return error(KErrorCode::Internal, "workspace.hashFailure");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const unsigned char byte : digest)
    {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}

struct KScanDirectory
{
    std::filesystem::path m_path;
    std::size_t m_depth = 0;
};

struct KScannedEntry
{
    KStoredWorkspaceEntry m_entry;
    std::int64_t m_writeStamp = 0;
};

KResult<KStoredWorkspace> scan(const std::string& selection,
    const KLocalWorkspaceStoreOptions& options, std::stop_token stop)
{
    std::wstring selected;
    if (!validWorkspaceText(selection) || !toWide(selection, selected))
        return error(KErrorCode::InvalidArgument, "workspace.invalidSelection");
    std::error_code filesystemError;
    const std::filesystem::path root = std::filesystem::canonical(selected, filesystemError);
    if (filesystemError || !std::filesystem::is_directory(root, filesystemError) || filesystemError)
        return error(KErrorCode::NotFound, "workspace.notFound");
    std::vector<KScanDirectory> pending{{root, 0}};
    std::vector<KScannedEntry> scanned;
    while (!pending.empty())
    {
        if (stop.stop_requested()) return error(KErrorCode::Cancelled, "workspace.cancelled", true);
        const KScanDirectory current = std::move(pending.back());
        pending.pop_back();
        std::filesystem::directory_iterator iterator(current.m_path,
            std::filesystem::directory_options::none, filesystemError);
        if (filesystemError) return error(KErrorCode::Unavailable, "workspace.scanFailure", true);
        for (const std::filesystem::directory_entry& source : iterator)
        {
            if (current.m_depth == 0 && CompareStringOrdinal(source.path().filename().c_str(), -1,
                L".lightoverleaf-trash", -1, TRUE) == CSTR_EQUAL) continue;
            if (stop.stop_requested()) return error(KErrorCode::Cancelled, "workspace.cancelled", true);
            const DWORD attributes = GetFileAttributesW(source.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
                return error(KErrorCode::Unavailable, "workspace.scanFailure", true);
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
            const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!directory && (attributes & FILE_ATTRIBUTE_DEVICE) != 0) continue;
            const std::filesystem::path relative = source.path().lexically_relative(root);
            const std::string fileId = toUtf8(relative);
            if (!validWorkspaceFileId(fileId))
                return error(KErrorCode::InvalidArgument, "workspace.invalidEntry");
            if (scanned.size() >= options.m_maxEntries)
                return error(KErrorCode::ResourceExhausted, "workspace.tooManyEntries");
            std::uint64_t size = 0;
            if (!directory)
            {
                size = source.file_size(filesystemError);
                if (filesystemError) return error(KErrorCode::Unavailable, "workspace.scanFailure", true);
            }
            const auto writeTime = source.last_write_time(filesystemError);
            if (filesystemError) return error(KErrorCode::Unavailable, "workspace.scanFailure", true);
            scanned.push_back({{fileId, directory, size},
                static_cast<std::int64_t>(writeTime.time_since_epoch().count())});
            if (directory)
            {
                if (current.m_depth >= options.m_maxDepth)
                    return error(KErrorCode::ResourceExhausted, "workspace.tooDeep");
                pending.push_back({source.path(), current.m_depth + 1});
            }
        }
    }
    std::sort(scanned.begin(), scanned.end(), [](const KScannedEntry& left, const KScannedEntry& right)
    {
        return left.m_entry.m_fileId < right.m_entry.m_fileId;
    });
    std::ostringstream fingerprint;
    std::vector<KStoredWorkspaceEntry> entries;
    entries.reserve(scanned.size());
    for (KScannedEntry& value : scanned)
    {
        fingerprint << value.m_entry.m_fileId.size() << ':' << value.m_entry.m_fileId << ':'
            << value.m_entry.m_directory << ':' << value.m_entry.m_sizeBytes << ':' << value.m_writeStamp << ';';
        entries.push_back(std::move(value.m_entry));
    }
    KResult<std::string> rootHash = hash(toUtf8(root));
    if (const KError* failure = std::get_if<KError>(&rootHash)) return *failure;
    KResult<std::string> treeHash = hash(fingerprint.str());
    if (const KError* failure = std::get_if<KError>(&treeHash)) return *failure;
    std::string displayName = toUtf8(root.filename());
    if (displayName.empty()) displayName = "workspace";
    return KStoredWorkspace{"workspace-" + std::get<std::string>(rootHash).substr(0, 32),
        std::move(displayName), "revision-" + std::get<std::string>(treeHash).substr(0, 32), std::move(entries)};
}

class KPathLocks
{
public:
    KPathLocks() { m_handles.reserve(256); }
    KPathLocks(const KPathLocks&) = delete;
    KPathLocks& operator=(const KPathLocks&) = delete;
    ~KPathLocks() { for (HANDLE handle : m_handles) CloseHandle(handle); }
    bool lockDirectory(const std::filesystem::path& path)
    {
        if (m_handles.size() >= 256) return false;
        HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            CloseHandle(handle);
            return false;
        }
        m_handles.push_back(handle);
        return true;
    }
    bool lockAncestors(const std::filesystem::path& parent)
    {
        std::filesystem::path current = parent.root_path();
        if (!lockDirectory(current)) return false;
        for (const auto& part : parent.relative_path())
        {
            current /= part;
            if (!lockDirectory(current)) return false;
        }
        return true;
    }

private:
    std::vector<HANDLE> m_handles;
};

KError mutationError()
{
    const DWORD code = GetLastError();
    if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS)
        return error(KErrorCode::Conflict, "workspace.exists");
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        return error(KErrorCode::NotFound, "workspace.notFound");
    return error(KErrorCode::Unavailable, "workspace.mutationFailed", true);
}

bool allowedMutationPath(const std::string& fileId)
{
    if (!validWorkspaceFileId(fileId)) return false;
    std::wstring wide;
    if (!toWide(fileId, wide)) return false;
    const std::wstring first = wide.substr(0, wide.find(L'/'));
    return CompareStringOrdinal(first.c_str(), -1, L".lightoverleaf-trash", -1, TRUE) != CSTR_EQUAL;
}

bool nestedPath(const std::filesystem::path& child, const std::filesystem::path& parent)
{
    const std::wstring childName = child.lexically_normal().native();
    const std::wstring parentName = parent.lexically_normal().native();
    if (childName.size() <= parentName.size() ||
        CompareStringOrdinal(childName.data(), static_cast<int>(parentName.size()),
            parentName.data(), static_cast<int>(parentName.size()), TRUE) != CSTR_EQUAL)
        return false;
    const wchar_t separator = childName[parentName.size()];
    return separator == L'\\' || separator == L'/';
}

KResult<std::filesystem::path> trashDestination(const std::filesystem::path& root,
    const std::filesystem::path& source)
{
    const std::filesystem::path trash = root / L".lightoverleaf-trash";
    if (!CreateDirectoryW(trash.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return mutationError();
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return error(KErrorCode::Internal, "workspace.randomFailure");
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring name;
    name.reserve(32 + 1 + source.filename().native().size());
    for (unsigned char byte : random)
    {
        name += hex[byte >> 4];
        name += hex[byte & 15];
    }
    return trash / (name + L"-" + source.filename().wstring());
}

KResult<bool> mutateFile(const std::string& selection, const KStoredMutation& command, std::stop_token stop)
{
    if (!allowedMutationPath(command.m_fileId) ||
        (command.m_kind != KStoredMutationKind::RenameFile && !command.m_destination.empty()) ||
        (command.m_kind == KStoredMutationKind::RenameFile && !allowedMutationPath(command.m_destination)))
        return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
    std::wstring selected, sourceName, destinationName;
    if (!toWide(selection, selected) || !toWide(command.m_fileId, sourceName))
        return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
    std::error_code filesystemError;
    const std::filesystem::path root = std::filesystem::canonical(selected, filesystemError);
    if (filesystemError) return error(KErrorCode::NotFound, "workspace.notFound");
    KResult<std::string> rootHash = hash(toUtf8(root));
    if (const KError* failure = std::get_if<KError>(&rootHash)) return *failure;
    if (command.m_workspaceId != "workspace-" + std::get<std::string>(rootHash).substr(0, 32))
        return error(KErrorCode::InvalidArgument, "workspace.changedRoot");
    const std::filesystem::path source = root / sourceName;
    KPathLocks locks;
    if (!locks.lockAncestors(source.parent_path()))
        return error(KErrorCode::InvalidArgument, "workspace.unsafeParent");
    if (stop.stop_requested()) return error(KErrorCode::Cancelled, "workspace.cancelled", true);
    if (command.m_kind == KStoredMutationKind::CreateEmpty)
    {
        HANDLE created = CreateFileW(source.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (created == INVALID_HANDLE_VALUE) return mutationError();
        CloseHandle(created);
        return true;
    }
    std::filesystem::path destination;
    if (command.m_kind == KStoredMutationKind::RenameFile)
    {
        if (!toWide(command.m_destination, destinationName)) return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
        destination = root / destinationName;
    }
    else if (command.m_kind == KStoredMutationKind::RemoveFile)
    {
        KResult<std::filesystem::path> trashed = trashDestination(root, source);
        if (const KError* failure = std::get_if<KError>(&trashed)) return *failure;
        destination = std::get<std::filesystem::path>(std::move(trashed));
    }
    else return error(KErrorCode::InvalidArgument, "workspace.invalidOperation");
    if (!locks.lockAncestors(destination.parent_path()))
        return error(KErrorCode::InvalidArgument, "workspace.unsafeParent");
    HANDLE handle = CreateFileW(source.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return mutationError();
    const auto closeHandle = [](void* value) { CloseHandle(value); };
    std::unique_ptr<void, decltype(closeHandle)> ownedHandle(handle, closeHandle);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    {
        return error(KErrorCode::InvalidArgument, "workspace.notRegularFile");
    }
    if (stop.stop_requested())
    {
        return error(KErrorCode::Cancelled, "workspace.cancelled", true);
    }
    const std::wstring target = destination.wstring();
    const std::size_t bytes = sizeof(FILE_RENAME_INFO) + target.size() * sizeof(wchar_t);
    std::vector<std::uint64_t> buffer((bytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
    std::copy(target.begin(), target.end(), rename->FileName);
    const BOOL moved = SetFileInformationByHandle(handle, FileRenameInfo, rename, static_cast<DWORD>(bytes));
    const KError failure = moved ? KError{} : mutationError();
    if (!moved) return failure;
    return true;
}

KResult<bool> mutateDirectory(const std::string& selection,
    const KStoredDirectoryMutation& command, std::stop_token stop)
{
    if (!allowedMutationPath(command.m_directoryId) ||
        (command.m_kind != KStoredDirectoryMutationKind::Rename && !command.m_destination.empty()) ||
        (command.m_kind == KStoredDirectoryMutationKind::Rename &&
            !allowedMutationPath(command.m_destination)))
        return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
    std::wstring selected, sourceName, destinationName;
    if (!toWide(selection, selected) || !toWide(command.m_directoryId, sourceName))
        return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
    std::error_code filesystemError;
    const std::filesystem::path root = std::filesystem::canonical(selected, filesystemError);
    if (filesystemError) return error(KErrorCode::NotFound, "workspace.notFound");
    KResult<std::string> rootHash = hash(toUtf8(root));
    if (const KError* failure = std::get_if<KError>(&rootHash)) return *failure;
    if (command.m_workspaceId != "workspace-" + std::get<std::string>(rootHash).substr(0, 32))
        return error(KErrorCode::InvalidArgument, "workspace.changedRoot");
    const std::filesystem::path source = root / sourceName;
    KPathLocks locks;
    if (!locks.lockAncestors(source.parent_path()))
        return error(KErrorCode::InvalidArgument, "workspace.unsafeParent");
    if (stop.stop_requested()) return error(KErrorCode::Cancelled, "workspace.cancelled", true);
    if (command.m_kind == KStoredDirectoryMutationKind::Create)
    {
        if (!CreateDirectoryW(source.c_str(), nullptr)) return mutationError();
        return true;
    }
    std::filesystem::path destination;
    if (command.m_kind == KStoredDirectoryMutationKind::Rename)
    {
        if (!toWide(command.m_destination, destinationName))
            return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
        destination = root / destinationName;
        if (nestedPath(destination, source))
            return error(KErrorCode::InvalidArgument, "workspace.invalidDestination");
    }
    else if (command.m_kind == KStoredDirectoryMutationKind::Remove)
    {
        KResult<std::filesystem::path> trashed = trashDestination(root, source);
        if (const KError* failure = std::get_if<KError>(&trashed)) return *failure;
        destination = std::get<std::filesystem::path>(std::move(trashed));
    }
    else return error(KErrorCode::InvalidArgument, "workspace.invalidOperation");
    if (!locks.lockAncestors(destination.parent_path()))
        return error(KErrorCode::InvalidArgument, "workspace.unsafeParent");
    HANDLE handle = CreateFileW(source.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return mutationError();
    const auto closeHandle = [](void* value) { CloseHandle(value); };
    std::unique_ptr<void, decltype(closeHandle)> ownedHandle(handle, closeHandle);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return error(KErrorCode::InvalidArgument, "workspace.notDirectory");
    if (stop.stop_requested())
        return error(KErrorCode::Cancelled, "workspace.cancelled", true);
    const std::wstring target = destination.wstring();
    const std::size_t bytes = sizeof(FILE_RENAME_INFO) + target.size() * sizeof(wchar_t);
    std::vector<std::uint64_t> buffer((bytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
    std::copy(target.begin(), target.end(), rename->FileName);
    if (!SetFileInformationByHandle(handle, FileRenameInfo, rename, static_cast<DWORD>(bytes)))
        return mutationError();
    return true;
}

class KLocalWorkspaceStore final : public IKWorkspaceStore
{
public:
    explicit KLocalWorkspaceStore(KLocalWorkspaceStoreOptions options) : m_options(options) {}
    KResult<KStoredWorkspace> open(const std::string& selection, std::stop_token stop) override
    {
        try
        {
            return scan(selection, m_options, stop);
        }
        catch (...)
        {
            return error(KErrorCode::Internal, "workspace.scanFailure");
        }
    }
    KResult<KStoredWorkspace> refresh(const std::string& selection,
        const std::string& workspaceId, std::stop_token stop) override
    {
        KResult<KStoredWorkspace> result = scan(selection, m_options, stop);
        const KStoredWorkspace* scanned = std::get_if<KStoredWorkspace>(&result);
        if (scanned && scanned->m_id != workspaceId)
            return error(KErrorCode::InvalidArgument, "workspace.changedRoot");
        return result;
    }
    void close(const std::string&) noexcept override {}
    KResult<bool> mutate(const std::string& selection, const KStoredMutation& command, std::stop_token stop) override
    {
        try
        {
            return mutateFile(selection, command, stop);
        }
        catch (...)
        {
            return error(KErrorCode::Internal, "workspace.mutationFailure");
        }
    }
    KResult<bool> mutateDirectory(const std::string& selection,
        const KStoredDirectoryMutation& command, std::stop_token stop) override
    {
        try
        {
            return workspace::mutateDirectory(selection, command, stop);
        }
        catch (...)
        {
            return error(KErrorCode::Internal, "workspace.mutationFailure");
        }
    }

private:
    const KLocalWorkspaceStoreOptions m_options;
};
}

std::shared_ptr<IKWorkspaceStore> createLocalWorkspaceStore(const KLocalWorkspaceStoreOptions& options)
{
    if (options.m_maxEntries == 0 || options.m_maxEntries > kMaxWorkspaceEntries ||
        options.m_maxDepth == 0 || options.m_maxDepth > 64)
        return nullptr;
    return std::make_shared<KLocalWorkspaceStore>(options);
}
}
