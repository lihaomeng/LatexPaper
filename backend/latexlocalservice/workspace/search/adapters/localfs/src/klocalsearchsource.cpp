#include <lightoverleaf/search/adapters/klocalsearchsource.h>
#include <lightoverleaf/search/domain/ksearchpolicy.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <set>

namespace lightoverleaf::search
{
namespace fs = std::filesystem;
namespace
{
std::wstring fromUtf8(const std::string& value)
{
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count) != count) return {};
    return result;
}
std::string toUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) != count) return {};
    return result;
}
bool supported(const fs::path& path)
{
    std::wstring extension = path.extension().wstring();
    for (wchar_t& c : extension) if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';
    static const std::set<std::wstring> extensions{L".tex", L".bib", L".sty", L".cls", L".txt", L".md"};
    return extensions.contains(extension);
}
bool below(const fs::path& root, const fs::path& candidate)
{
    const std::wstring left = root.native(), right = candidate.native();
    return right.size() > left.size() && _wcsnicmp(left.c_str(), right.c_str(), left.size()) == 0 &&
        (right[left.size()] == L'\\' || right[left.size()] == L'/');
}
}

class KLocalSearchSource final : public IKSearchSource
{
public:
    explicit KLocalSearchSource(fs::path root) : m_root(std::move(root)) {}
    KResult<std::vector<KSearchDocument>> readAll(std::stop_token stop) override
    {
        try
        {
            std::vector<KSearchDocument> result;
            std::error_code error;
            fs::recursive_directory_iterator iterator(m_root,
                fs::directory_options::skip_permission_denied, error), end;
            if (error) return KError{KErrorCode::Unavailable, "search.readFailure", true};
            for (; iterator != end; iterator.increment(error))
            {
                if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "search.cancelled", true};
                if (error) { error.clear(); continue; }
                const auto status = iterator->symlink_status(error);
                if (error) { error.clear(); continue; }
                const DWORD attributes = GetFileAttributesW(iterator->path().c_str());
                if (fs::is_symlink(status) || (attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0))
                { if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) iterator.disable_recursion_pending(); continue; }
                if (iterator->is_directory(error))
                {
                    if (iterator->path().filename() == L".lightoverleaf-trash") iterator.disable_recursion_pending();
                    continue;
                }
                if (error || !iterator->is_regular_file(error) || error || !supported(iterator->path())) continue;
                const auto bytes = iterator->file_size(error);
                if (error || bytes > kMaxSearchDocumentBytes) { error.clear(); continue; }
                const fs::path canonical = fs::weakly_canonical(iterator->path(), error);
                if (error || !below(m_root, canonical)) { error.clear(); continue; }
                std::ifstream input(canonical, std::ios::binary);
                if (!input) continue;
                std::string content(static_cast<std::size_t>(bytes), '\0');
                input.read(content.data(), static_cast<std::streamsize>(content.size()));
                if (!input && !input.eof()) continue;
                fs::path relative = fs::relative(canonical, m_root, error);
                if (error) { error.clear(); continue; }
                std::wstring generic = relative.generic_wstring();
                std::string fileId = toUtf8(generic);
                if (fileId.empty() || fileId.size() > 4096) continue;
                result.push_back({std::move(fileId), std::move(content)});
                if (result.size() > kMaxSearchDocuments)
                    return KError{KErrorCode::ResourceExhausted, "search.tooManyDocuments", false};
            }
            return result;
        }
        catch (...)
        {
            return KError{KErrorCode::Unavailable, "search.readFailure", true};
        }
    }
private:
    fs::path m_root;
};

KResult<std::shared_ptr<IKSearchSource>> createLocalSearchSource(const KLocalSearchSourceOptions& options)
{
    const std::wstring wide = fromUtf8(options.m_rootUtf8);
    if (wide.empty()) return KError{KErrorCode::InvalidArgument, "search.invalidRoot", false};
    std::error_code error;
    const fs::path root = fs::canonical(fs::path(wide), error);
    if (error || !fs::is_directory(root, error))
        return KError{KErrorCode::InvalidArgument, "search.invalidRoot", false};
    return std::shared_ptr<IKSearchSource>(std::make_shared<KLocalSearchSource>(root));
}
}
