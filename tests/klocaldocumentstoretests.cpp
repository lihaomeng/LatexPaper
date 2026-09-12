#include <lightoverleaf/document/adapters/klocaldocumentstore.h>
#include <lightoverleaf/document/inbound/ikdocuments.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace lightoverleaf;
using namespace lightoverleaf::document;

namespace
{
class KTestDirectory
{
public:
    KTestDirectory()
    {
        m_path = std::filesystem::temp_directory_path() /
            (L"LightOverLeaf-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(m_path / L"章节");
    }
    ~KTestDirectory()
    {
        std::error_code error;
        std::filesystem::permissions(m_path, std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add, error);
        std::filesystem::remove_all(m_path, error);
    }
    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void write(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("fixture write");
}

std::string read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

template<class T> bool failed(const KResult<T>& result, KErrorCode code)
{
    const KError* error = std::get_if<KError>(&result);
    return error && error->m_code == code;
}
}

int main()
{
    int failures = 0;
    const auto check = [&](bool valid, const char* label)
    {
        if (!valid) { ++failures; std::cerr << label << '\n'; }
    };
    KTestDirectory root;
    const std::string bom = std::string("\xEF\xBB\xBF") + "original";
    write(root.path() / L"main.tex", bom);
    write(root.path() / L"empty.tex", "");
    write(root.path() / L"章节" / L"正文.tex", "你好");
    KResult<std::shared_ptr<IKDocumentStore>> created = createLocalDocumentStore({utf8(root.path()), 1024});
    check(std::holds_alternative<std::shared_ptr<IKDocumentStore>>(created), "factory");
    const auto store = std::get<std::shared_ptr<IKDocumentStore>>(created);
    const auto documents = createDocuments(store);
    const KResult<KDocumentSnapshot> opened = documents->open("main.tex");
    check(std::holds_alternative<KDocumentSnapshot>(opened), "open");
    const KDocumentSnapshot original = std::get<KDocumentSnapshot>(opened);
    check(original.m_content == "original" && original.m_utf8Bom && original.m_revision.size() == 64, "BOM and SHA-256 revision");
    check(std::holds_alternative<KDocumentSnapshot>(documents->open("章节/正文.tex")), "Chinese path");
    const KResult<KDocumentSnapshot> empty = documents->open("empty.tex");
    check(std::holds_alternative<KDocumentSnapshot>(empty) &&
        std::get<KDocumentSnapshot>(empty).m_content.empty(), "empty file");
    write(root.path() / L"main.tex", std::string("\xEF\xBB\xBF") + "external");
    check(failed(documents->save({"main.tex", "overwrite", original.m_revision, 1}), KErrorCode::Conflict),
        "external edit conflict");
    check(read(root.path() / L"main.tex") == std::string("\xEF\xBB\xBF") + "external", "conflict preserves external content");
    const KDocumentSnapshot external = std::get<KDocumentSnapshot>(documents->open("main.tex"));
    const KResult<KDocumentSaved> saved = documents->save({"main.tex", "saved", external.m_revision, 2});
    check(std::holds_alternative<KDocumentSaved>(saved), "atomic save");
    check(read(root.path() / L"main.tex") == std::string("\xEF\xBB\xBF") + "saved", "BOM preserved");
    check(std::get<KDocumentSaved>(saved).m_revision != external.m_revision, "revision changed");
    check(failed(store->read("../outside.tex", {}), KErrorCode::InvalidArgument), "lexical escape rejected");
    write(root.path() / L"invalid.tex", std::string("\xC0\xAF"));
    check(failed(documents->open("invalid.tex"), KErrorCode::InvalidEncoding), "invalid UTF-8 rejected");
    write(root.path() / L"large.tex", std::string(1025, 'x'));
    check(failed(store->read("large.tex", {}), KErrorCode::ResourceExhausted), "read bound");
    std::stop_source stop;
    stop.request_stop();
    check(failed(store->read("main.tex", stop.get_token()), KErrorCode::Cancelled), "read cancellation");
    const DWORD attributes = GetFileAttributesW((root.path() / L"main.tex").c_str());
    SetFileAttributesW((root.path() / L"main.tex").c_str(), attributes | FILE_ATTRIBUTE_READONLY);
    const KDocumentSnapshot readonly = std::get<KDocumentSnapshot>(documents->open("main.tex"));
    check(failed(documents->save({"main.tex", "blocked", readonly.m_revision, 3}), KErrorCode::Unavailable),
        "read-only replacement fails");
    SetFileAttributesW((root.path() / L"main.tex").c_str(), attributes);
    check(read(root.path() / L"main.tex") == std::string("\xEF\xBB\xBF") + "saved", "failed save preserves source");
    std::size_t tempCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root.path()))
        if (entry.path().filename().wstring().starts_with(L".lol-")) ++tempCount;
    check(tempCount == 0, "temporary files cleaned");
    check(failed(createLocalDocumentStore({utf8(root.path() / L"missing"), 1024}), KErrorCode::NotFound), "missing root");
    return failures == 0 ? 0 : 1;
}
