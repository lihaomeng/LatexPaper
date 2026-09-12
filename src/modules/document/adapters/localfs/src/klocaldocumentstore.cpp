#include <lightoverleaf/document/adapters/klocaldocumentstore.h>
#include <lightoverleaf/document/domain/kdocumentpolicy.h>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <mutex>
#include <vector>

namespace lightoverleaf::document
{
namespace
{
class KScopedHandle
{
public:
    explicit KScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) : m_handle(handle) {}
    ~KScopedHandle() { if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle); }
    KScopedHandle(const KScopedHandle&) = delete;
    KScopedHandle& operator=(const KScopedHandle&) = delete;
    KScopedHandle(KScopedHandle&& other) noexcept : m_handle(other.release()) {}
    KScopedHandle& operator=(KScopedHandle&& other) noexcept
    {
        if (this == &other) return *this;
        if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
        m_handle = other.release();
        return *this;
    }
    HANDLE get() const { return m_handle; }
    bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }
    HANDLE release()
    {
        const HANDLE result = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return result;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

class KTempFileGuard
{
public:
    explicit KTempFileGuard(std::filesystem::path path) : m_path(std::move(path)) {}
    ~KTempFileGuard() { if (!m_committed && !m_path.empty()) DeleteFileW(m_path.c_str()); }
    void commit() { m_committed = true; }

private:
    std::filesystem::path m_path;
    bool m_committed = false;
};

struct KRawDocument
{
    std::vector<unsigned char> m_bytes;
    std::string m_revision;
};

KError error(KErrorCode code, const char* key, bool retryable = false)
{
    return {code, key, retryable};
}

bool toWide(const std::string& utf8, std::wstring& output)
{
    if (utf8.empty()) return false;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0) return false;
    output.resize(static_cast<std::size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), output.data(), count) == count;
}

bool sameComponent(const std::filesystem::path& left, const std::filesystem::path& right)
{
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool withinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    auto rootPart = root.begin();
    auto candidatePart = candidate.begin();
    for (; rootPart != root.end(); ++rootPart, ++candidatePart)
        if (candidatePart == candidate.end() || !sameComponent(*rootPart, *candidatePart)) return false;
    return true;
}

KResult<std::filesystem::path> resolveExisting(const std::filesystem::path& root, const std::string& fileId)
{
    if (!validFileId(fileId)) return error(KErrorCode::InvalidArgument, "document.invalidArgument");
    std::wstring relative;
    if (!toWide(fileId, relative)) return error(KErrorCode::InvalidArgument, "document.invalidArgument");
    std::replace(relative.begin(), relative.end(), L'/', L'\\');
    std::filesystem::path current = root;
    std::size_t start = 0;
    while (start < relative.size())
    {
        const std::size_t separator = relative.find(L'\\', start);
        const std::size_t end = separator == std::wstring::npos ? relative.size() : separator;
        current /= relative.substr(start, end - start);
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return error(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ?
                KErrorCode::NotFound : KErrorCode::Unavailable, "document.fileUnavailable", true);
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return error(KErrorCode::InvalidArgument, "document.pathOutsideWorkspace");
        if (separator == std::wstring::npos) break;
        start = separator + 1;
    }
    std::error_code filesystemError;
    const std::filesystem::path canonical = std::filesystem::canonical(current, filesystemError);
    if (filesystemError || !withinRoot(root, canonical))
        return error(KErrorCode::InvalidArgument, "document.pathOutsideWorkspace");
    if (!std::filesystem::is_regular_file(canonical, filesystemError) || filesystemError)
        return error(KErrorCode::NotFound, "document.fileNotFound");
    return canonical;
}

KResult<std::string> sha256(const std::vector<unsigned char>& bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
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
        status = BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0);
    }
    if (status >= 0 && !bytes.empty())
        status = BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest.data(), hashBytes, 0);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0 || digest.size() != 32) return error(KErrorCode::Internal, "document.hashFailure");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const unsigned char byte : digest)
    {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}

KResult<KRawDocument> readRaw(const std::filesystem::path& path, std::size_t maxBytes, std::stop_token stop)
{
    if (stop.stop_requested()) return error(KErrorCode::Cancelled, "document.cancelled", true);
    KScopedHandle handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!handle.valid())
        return error(GetLastError() == ERROR_FILE_NOT_FOUND ? KErrorCode::NotFound : KErrorCode::Unavailable,
            "document.fileUnavailable", true);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle.get(), &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > static_cast<unsigned long long>(maxBytes) + 3ULL)
        return error(KErrorCode::ResourceExhausted, "document.tooLarge");
    KRawDocument raw;
    raw.m_bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < raw.m_bytes.size())
    {
        if (stop.stop_requested()) return error(KErrorCode::Cancelled, "document.cancelled", true);
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(raw.m_bytes.size() - offset, 64 * 1024));
        DWORD received = 0;
        if (!ReadFile(handle.get(), raw.m_bytes.data() + offset, requested, &received, nullptr) || received == 0)
            return error(KErrorCode::Unavailable, "document.readFailure", true);
        offset += received;
    }
    KResult<std::string> revision = sha256(raw.m_bytes);
    if (const KError* failure = std::get_if<KError>(&revision)) return *failure;
    raw.m_revision = std::move(std::get<std::string>(revision));
    return raw;
}

KResult<std::filesystem::path> createSiblingTemp(const std::filesystem::path& target, KScopedHandle& handle)
{
    for (unsigned int attempt = 0; attempt < 16; ++attempt)
    {
        std::array<unsigned char, 16> random{};
        if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            return error(KErrorCode::Internal, "document.randomFailure");
        constexpr wchar_t hex[] = L"0123456789abcdef";
        std::wstring name = L".lol-";
        name.reserve(5 + random.size() * 2 + 4);
        for (const unsigned char byte : random)
        {
            name.push_back(hex[byte >> 4]);
            name.push_back(hex[byte & 15]);
        }
        name += L".tmp";
        const std::filesystem::path candidate = target.parent_path() / name;
        KScopedHandle created(CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY, nullptr));
        if (created.valid())
        {
            handle = KScopedHandle(created.release());
            return candidate;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return error(KErrorCode::Unavailable, "document.tempCreateFailure", true);
    }
    return error(KErrorCode::ResourceExhausted, "document.tempNameExhausted", true);
}

KResult<bool> writeRaw(HANDLE handle, const std::vector<unsigned char>& bytes, std::stop_token stop)
{
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        if (stop.stop_requested()) return error(KErrorCode::Cancelled, "document.cancelled", true);
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 64 * 1024));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, requested, &written, nullptr) || written == 0)
            return error(KErrorCode::Unavailable, "document.writeFailure", true);
        offset += written;
    }
    if (!FlushFileBuffers(handle)) return error(KErrorCode::Unavailable, "document.flushFailure", true);
    return true;
}

class KLocalDocumentStore final : public IKDocumentStore
{
public:
    KLocalDocumentStore(std::filesystem::path root, std::size_t maxBytes)
        : m_root(std::move(root)), m_maxBytes(maxBytes) {}
    KResult<KStoredDocument> read(const std::string& fileId, std::stop_token stop) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        KResult<std::filesystem::path> resolved = resolveExisting(m_root, fileId);
        if (const KError* failure = std::get_if<KError>(&resolved)) return *failure;
        KResult<KRawDocument> result = readRaw(std::get<std::filesystem::path>(resolved), m_maxBytes, stop);
        if (const KError* failure = std::get_if<KError>(&result)) return *failure;
        KRawDocument raw = std::move(std::get<KRawDocument>(result));
        const bool bom = raw.m_bytes.size() >= 3 && raw.m_bytes[0] == 0xEF &&
            raw.m_bytes[1] == 0xBB && raw.m_bytes[2] == 0xBF;
        const std::size_t offset = bom ? 3 : 0;
        std::string content;
        if (raw.m_bytes.size() > offset)
            content.assign(reinterpret_cast<const char*>(raw.m_bytes.data() + offset), raw.m_bytes.size() - offset);
        if (content.size() > m_maxBytes) return error(KErrorCode::ResourceExhausted, "document.tooLarge");
        if (!validUtf8(content)) return error(KErrorCode::InvalidEncoding, "document.invalidEncoding");
        return KStoredDocument{std::move(content), std::move(raw.m_revision), bom};
    }
    KResult<KStoredDocument> replace(const std::string& fileId, const std::string& content,
        const std::string& expectedRevision, std::stop_token stop) override
    {
        if (!validFileId(fileId) || !validRevision(expectedRevision) ||
            content.size() > m_maxBytes || !validUtf8(content))
            return error(KErrorCode::InvalidArgument, "document.invalidArgument");
        if (stop.stop_requested()) return error(KErrorCode::Cancelled, "document.cancelled", true);
        std::lock_guard<std::mutex> lock(m_mutex);
        KResult<std::filesystem::path> resolved = resolveExisting(m_root, fileId);
        if (const KError* failure = std::get_if<KError>(&resolved)) return *failure;
        const std::filesystem::path target = std::get<std::filesystem::path>(resolved);
        KResult<KRawDocument> beforeResult = readRaw(target, m_maxBytes, stop);
        if (const KError* failure = std::get_if<KError>(&beforeResult)) return *failure;
        KRawDocument before = std::move(std::get<KRawDocument>(beforeResult));
        if (before.m_revision != expectedRevision)
            return error(KErrorCode::Conflict, "document.conflict");
        const bool bom = before.m_bytes.size() >= 3 && before.m_bytes[0] == 0xEF &&
            before.m_bytes[1] == 0xBB && before.m_bytes[2] == 0xBF;
        std::vector<unsigned char> bytes;
        bytes.reserve(content.size() + (bom ? 3 : 0));
        if (bom) bytes.insert(bytes.end(), {0xEF, 0xBB, 0xBF});
        bytes.insert(bytes.end(), content.begin(), content.end());
        KScopedHandle tempHandle;
        KResult<std::filesystem::path> tempResult = createSiblingTemp(target, tempHandle);
        if (const KError* failure = std::get_if<KError>(&tempResult)) return *failure;
        const std::filesystem::path temp = std::get<std::filesystem::path>(tempResult);
        KTempFileGuard cleanup(temp);
        KResult<bool> writeResult = writeRaw(tempHandle.get(), bytes, stop);
        if (const KError* failure = std::get_if<KError>(&writeResult)) return *failure;
        CloseHandle(tempHandle.release());
        if (stop.stop_requested()) return error(KErrorCode::Cancelled, "document.cancelled", true);
        KResult<KRawDocument> checkedResult = readRaw(target, m_maxBytes, stop);
        if (const KError* failure = std::get_if<KError>(&checkedResult)) return *failure;
        if (std::get<KRawDocument>(checkedResult).m_revision != expectedRevision)
            return error(KErrorCode::Conflict, "document.conflict");
        if (!ReplaceFileW(target.c_str(), temp.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
            return error(KErrorCode::Unavailable, "document.replaceFailure", true);
        cleanup.commit();
        KResult<std::string> revision = sha256(bytes);
        if (const KError* failure = std::get_if<KError>(&revision)) return *failure;
        return KStoredDocument{content, std::move(std::get<std::string>(revision)), bom};
    }

private:
    const std::filesystem::path m_root;
    const std::size_t m_maxBytes = 0;
    std::mutex m_mutex;
};
}

KResult<std::shared_ptr<IKDocumentStore>> createLocalDocumentStore(const KLocalDocumentStoreOptions& options)
{
    if (options.m_rootUtf8.empty() || options.m_rootUtf8.size() > 32768 ||
        options.m_maxBytes == 0 || options.m_maxBytes > 64 * 1024 * 1024)
        return error(KErrorCode::InvalidArgument, "document.invalidStoreOptions");
    std::wstring rootText;
    if (!toWide(options.m_rootUtf8, rootText))
        return error(KErrorCode::InvalidArgument, "document.invalidStoreOptions");
    std::error_code filesystemError;
    const std::filesystem::path root = std::filesystem::canonical(rootText, filesystemError);
    if (filesystemError || !std::filesystem::is_directory(root, filesystemError) || filesystemError)
        return error(KErrorCode::NotFound, "document.workspaceNotFound");
    return std::shared_ptr<IKDocumentStore>(std::make_shared<KLocalDocumentStore>(root, options.m_maxBytes));
}
}
