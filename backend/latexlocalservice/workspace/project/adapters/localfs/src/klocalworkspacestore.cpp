#include <lightoverleaf/workspace/adapters/klocalworkspacestore.h>
#include <lightoverleaf/workspace/domain/kworkspacepolicy.h>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
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

struct KTrashTarget
{
    std::string m_id;
    std::filesystem::path m_payload;
    std::filesystem::path m_metadata;
    std::string m_originalFileId;
    bool m_directory = false;
    std::uint64_t m_deletedAtUnixMs = 0;
};

std::string hexEncode(const std::string& value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const unsigned char byte : value)
    {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}

bool hexDecode(const std::string& value, std::string& result)
{
    if (value.empty() || value.size() % 2 != 0 || value.size() > 2048)
        return false;
    result.clear();
    result.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2)
    {
        const auto digit = [](const char byte) -> int
        {
            if (byte >= '0' && byte <= '9') return byte - '0';
            if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
            return -1;
        };
        const int high = digit(value[index]);
        const int low = digit(value[index + 1]);
        if (high < 0 || low < 0) return false;
        result.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

KResult<KTrashTarget> trashDestination(const std::filesystem::path& root,
    const std::string& originalFileId, bool directory)
{
    const std::filesystem::path trash = root / L".lightoverleaf-trash";
    if (!CreateDirectoryW(trash.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return mutationError();
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return error(KErrorCode::Internal, "workspace.randomFailure");
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (unsigned char byte : random)
    {
        id += static_cast<char>(hex[byte >> 4]);
        id += static_cast<char>(hex[byte & 15]);
    }
    const std::wstring wideId(id.begin(), id.end());
    const std::uint64_t deleted = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    return KTrashTarget{id, trash / (wideId + L".payload"), trash / (wideId + L".restore"),
        originalFileId, directory, deleted};
}

bool writeTrashMetadata(const KTrashTarget& target)
{
    const std::string content = "LOR1\n" + std::string(target.m_directory ? "D\n" : "F\n") +
        std::to_string(target.m_deletedAtUnixMs) + "\n" + hexEncode(target.m_originalFileId) + "\n";
    HANDLE file = CreateFileW(target.m_metadata.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL writeOk = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written,
        nullptr);
    const BOOL flushOk = writeOk && written == content.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!flushOk) DeleteFileW(target.m_metadata.c_str());
    return flushOk != FALSE;
}

std::optional<KTrashTarget> readTrashMetadata(const std::filesystem::path& metadata)
{
    std::error_code sizeError;
    const std::uintmax_t bytes = std::filesystem::file_size(metadata, sizeError);
    if (sizeError || bytes == 0 || bytes > 4096) return std::nullopt;
    std::ifstream input(metadata, std::ios::binary);
    std::string magic, type, time, encoded;
    if (!std::getline(input, magic) || !std::getline(input, type) || !std::getline(input, time) ||
        !std::getline(input, encoded) || magic != "LOR1" || (type != "F" && type != "D"))
        return std::nullopt;
    std::uint64_t deleted = 0;
    const auto converted = std::from_chars(time.data(), time.data() + time.size(), deleted);
    std::string original;
    const std::wstring stem = metadata.stem().wstring();
    if (converted.ec != std::errc{} || converted.ptr != time.data() + time.size() || deleted == 0 ||
        stem.size() != 32 || !hexDecode(encoded, original) || !allowedMutationPath(original))
        return std::nullopt;
    std::string id;
    id.reserve(stem.size());
    for (const wchar_t byte : stem)
    {
        if (byte > 127) return std::nullopt;
        id.push_back(static_cast<char>(byte));
    }
    const std::filesystem::path payload = metadata.parent_path() / (stem + L".payload");
    const DWORD attributes = GetFileAttributesW(payload.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != (type == "D")))
        return std::nullopt;
    return KTrashTarget{id, payload, metadata, std::move(original), type == "D", deleted};
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
    std::optional<KTrashTarget> trashTarget;
    if (command.m_kind == KStoredMutationKind::RenameFile)
    {
        if (!toWide(command.m_destination, destinationName)) return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
        destination = root / destinationName;
    }
    else if (command.m_kind == KStoredMutationKind::RemoveFile)
    {
        KResult<KTrashTarget> trashed = trashDestination(root, command.m_fileId, false);
        if (const KError* failure = std::get_if<KError>(&trashed)) return *failure;
        trashTarget = std::get<KTrashTarget>(std::move(trashed));
        destination = trashTarget->m_payload;
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
    if (trashTarget && !writeTrashMetadata(*trashTarget)) return mutationError();
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
    if (!moved)
    {
        if (trashTarget) DeleteFileW(trashTarget->m_metadata.c_str());
        return failure;
    }
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
    std::optional<KTrashTarget> trashTarget;
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
        KResult<KTrashTarget> trashed = trashDestination(root, command.m_directoryId, true);
        if (const KError* failure = std::get_if<KError>(&trashed)) return *failure;
        trashTarget = std::get<KTrashTarget>(std::move(trashed));
        destination = trashTarget->m_payload;
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
    if (trashTarget && !writeTrashMetadata(*trashTarget)) return mutationError();
    const std::wstring target = destination.wstring();
    const std::size_t bytes = sizeof(FILE_RENAME_INFO) + target.size() * sizeof(wchar_t);
    std::vector<std::uint64_t> buffer((bytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
    std::copy(target.begin(), target.end(), rename->FileName);
    if (!SetFileInformationByHandle(handle, FileRenameInfo, rename, static_cast<DWORD>(bytes)))
    {
        if (trashTarget) DeleteFileW(trashTarget->m_metadata.c_str());
        return mutationError();
    }
    return true;
}

KResult<std::vector<KStoredTrashEntry>> listTrashEntries(const std::string& selection,
    const std::string& workspaceId, std::stop_token stop)
{
    std::wstring selected;
    if (!toWide(selection, selected)) return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
    std::error_code filesystemError;
    const std::filesystem::path root = std::filesystem::canonical(selected, filesystemError);
    if (filesystemError) return error(KErrorCode::NotFound, "workspace.notFound");
    KResult<std::string> rootHash = hash(toUtf8(root));
    if (const KError* failure = std::get_if<KError>(&rootHash)) return *failure;
    if (workspaceId != "workspace-" + std::get<std::string>(rootHash).substr(0, 32))
        return error(KErrorCode::InvalidArgument, "workspace.changedRoot");
    std::vector<KStoredTrashEntry> entries;
    const std::filesystem::path trash = root / L".lightoverleaf-trash";
    if (!std::filesystem::exists(trash, filesystemError)) return entries;
    for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(trash))
    {
        if (stop.stop_requested()) return error(KErrorCode::Cancelled, "workspace.cancelled", true);
        if (!item.is_regular_file(filesystemError) || item.path().extension() != L".restore") continue;
        const std::optional<KTrashTarget> parsed = readTrashMetadata(item.path());
        if (parsed) entries.push_back({parsed->m_id, parsed->m_originalFileId,
            parsed->m_directory, parsed->m_deletedAtUnixMs});
        if (entries.size() >= 1000) break;
    }
    return entries;
}

KResult<bool> restoreTrashEntry(const std::string& selection, const std::string& workspaceId,
    const std::string& trashId, std::stop_token stop)
{
    if (trashId.size() != 32 || !std::all_of(trashId.begin(), trashId.end(), [](const unsigned char byte)
        { return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'); }))
        return error(KErrorCode::InvalidArgument, "workspace.invalidTrashId");
    std::wstring selected;
    if (!toWide(selection, selected)) return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
    std::error_code filesystemError;
    const std::filesystem::path root = std::filesystem::canonical(selected, filesystemError);
    if (filesystemError) return error(KErrorCode::NotFound, "workspace.notFound");
    KResult<std::string> rootHash = hash(toUtf8(root));
    if (const KError* failure = std::get_if<KError>(&rootHash)) return *failure;
    if (workspaceId != "workspace-" + std::get<std::string>(rootHash).substr(0, 32))
        return error(KErrorCode::InvalidArgument, "workspace.changedRoot");
    const std::wstring wideId(trashId.begin(), trashId.end());
    const std::filesystem::path metadata = root / L".lightoverleaf-trash" / (wideId + L".restore");
    const std::optional<KTrashTarget> parsed = readTrashMetadata(metadata);
    if (!parsed) return error(KErrorCode::NotFound, "workspace.trashNotFound");
    std::wstring destinationName;
    if (!toWide(parsed->m_originalFileId, destinationName))
        return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
    const std::filesystem::path destination = root / destinationName;
    KPathLocks locks;
    if (!locks.lockAncestors(destination.parent_path()))
        return error(KErrorCode::InvalidArgument, "workspace.unsafeParent");
    if (stop.stop_requested()) return error(KErrorCode::Cancelled, "workspace.cancelled", true);
    HANDLE handle = CreateFileW(parsed->m_payload.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return mutationError();
    const auto closeHandle = [](void* value) { CloseHandle(value); };
    std::unique_ptr<void, decltype(closeHandle)> ownedHandle(handle, closeHandle);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) ||
        (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != parsed->m_directory))
        return error(KErrorCode::InvalidArgument, "workspace.invalidTrashEntry");
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
    DeleteFileW(parsed->m_metadata.c_str());
    return true;
}

class KLocalWorkspaceStore final : public IKWorkspaceStore
{
public:
    explicit KLocalWorkspaceStore(KLocalWorkspaceStoreOptions options) : m_options(options) {}
    ~KLocalWorkspaceStore() override { closeChangeNotification(); }
    KResult<KStoredWorkspace> open(const std::string& selection, std::stop_token stop) override
    {
        try
        {
            KResult<KStoredWorkspace> result = scan(selection, m_options, stop);
            const KStoredWorkspace* workspace = std::get_if<KStoredWorkspace>(&result);
            if (!workspace) return result;
            std::wstring selected;
            if (!toWide(selection, selected)) return error(KErrorCode::InvalidArgument, "workspace.invalidPath");
            HANDLE notification = FindFirstChangeNotificationW(selected.c_str(), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION);
            if (notification == INVALID_HANDLE_VALUE)
                return error(KErrorCode::Unavailable, "workspace.watcherUnavailable", true);
            closeChangeNotification();
            m_changeNotification = notification;
            m_watchedWorkspaceId = workspace->m_id;
            return result;
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
    void close(const std::string& workspaceId) noexcept override
    {
        if (workspaceId == m_watchedWorkspaceId) closeChangeNotification();
    }
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
    KResult<std::vector<KStoredTrashEntry>> listTrash(const std::string& selection,
        const std::string& workspaceId, std::stop_token stop) override
    {
        try { return listTrashEntries(selection, workspaceId, stop); }
        catch (...) { return error(KErrorCode::Internal, "workspace.trashFailure"); }
    }
    KResult<bool> restoreTrash(const std::string& selection, const std::string& workspaceId,
        const std::string& trashId, std::stop_token stop) override
    {
        try { return restoreTrashEntry(selection, workspaceId, trashId, stop); }
        catch (...) { return error(KErrorCode::Internal, "workspace.trashFailure"); }
    }
    KResult<bool> pollChanges(const std::string&, const std::string& workspaceId,
        std::stop_token stop) override
    {
        if (stop.stop_requested()) return error(KErrorCode::Cancelled, "workspace.cancelled", true);
        if (workspaceId != m_watchedWorkspaceId || m_changeNotification == INVALID_HANDLE_VALUE)
            return error(KErrorCode::Unavailable, "workspace.watcherUnavailable", true);
        const DWORD wait = WaitForSingleObject(m_changeNotification, 0);
        if (wait == WAIT_TIMEOUT) return false;
        if (wait != WAIT_OBJECT_0 || !FindNextChangeNotification(m_changeNotification))
        {
            closeChangeNotification();
            return error(KErrorCode::Unavailable, "workspace.watcherFailure", true);
        }
        return true;
    }

private:
    void closeChangeNotification() noexcept
    {
        if (m_changeNotification != INVALID_HANDLE_VALUE)
            FindCloseChangeNotification(m_changeNotification);
        m_changeNotification = INVALID_HANDLE_VALUE;
        m_watchedWorkspaceId.clear();
    }

private:
    const KLocalWorkspaceStoreOptions m_options;
    HANDLE m_changeNotification = INVALID_HANDLE_VALUE;
    std::string m_watchedWorkspaceId;
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
