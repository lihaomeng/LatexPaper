#include <lightoverleaf/search/inbound/iksearch.h>
#include <lightoverleaf/search/outbound/iksearchsource.h>
#include <lightoverleaf/search/adapters/klocalsearchsource.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace lightoverleaf;
using namespace lightoverleaf::search;
namespace fs = std::filesystem;

namespace
{
class KFakeSource final : public IKSearchSource
{
public:
    KResult<std::vector<KSearchDocument>> readAll(std::stop_token) override { return documents; }
    std::vector<KSearchDocument> documents;
};
}

int main()
{
    int failures = 0;
    const auto check = [&](bool value, const char* label) { if (!value) { ++failures; std::cerr << label << '\n'; } };
    auto fake = std::make_shared<KFakeSource>();
    fake->documents = {{"main.tex", "Alpha beta\n中文 搜索\nalpha ALPHA"}, {"refs.bib", "title={Alpha}"}};
    auto search = createSearch(fake);
    KResult<KSearchResult> insensitive = search->run({"alpha", false, 10});
    check(std::holds_alternative<KSearchResult>(insensitive) &&
        std::get<KSearchResult>(insensitive).m_hits.size() == 4, "case-insensitive fake search");
    KResult<KSearchResult> unicode = search->run({"搜索", true, 10});
    check(std::holds_alternative<KSearchResult>(unicode) &&
        std::get<KSearchResult>(unicode).m_hits.front().m_line == 2, "unicode search");
    KResult<KSearchResult> filename = search->run({"refs", false, 10});
    check(std::holds_alternative<KSearchResult>(filename) &&
        std::get<KSearchResult>(filename).m_hits.size() == 1 &&
        std::get<KSearchResult>(filename).m_hits.front().m_preview == "[文件名] refs.bib",
        "filename search");
    KResult<KSearchResult> bounded = search->run({"alpha", false, 2});
    check(std::holds_alternative<KSearchResult>(bounded) && std::get<KSearchResult>(bounded).m_truncated &&
        std::get<KSearchResult>(bounded).m_hits.size() == 2, "bounded search results");
    fake->documents = {{"bad.tex", std::string("bad\xff", 4)}};
    check(std::holds_alternative<KError>(search->run({"bad", true, 10})), "invalid utf8 source rejected");
    std::stop_source stop; stop.request_stop();
    check(std::holds_alternative<KError>(search->run({"alpha", false, 10}, stop.get_token())), "search cancellation");

    const fs::path root = fs::temp_directory_path() /
        (L"LightOverLeaf-search-中文-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    fs::remove_all(root, error); fs::create_directories(root / L"chapters", error);
    { std::ofstream file(root / L"chapters" / L"一.tex", std::ios::binary); file << "needle here\n"; }
    { std::ofstream file(root / L"ignored.bin", std::ios::binary); file << "needle"; }
    const std::string rootUtf8 = root.u8string().empty() ? std::string{} :
        std::string(reinterpret_cast<const char*>(root.u8string().data()), root.u8string().size());
    KResult<std::shared_ptr<IKSearchSource>> local = createLocalSearchSource({rootUtf8});
    check(std::holds_alternative<std::shared_ptr<IKSearchSource>>(local), "local source factory");
    if (const auto* source = std::get_if<std::shared_ptr<IKSearchSource>>(&local))
    {
        auto localSearch = createSearch(*source);
        KResult<KSearchResult> result = localSearch->run({"needle", true, 10});
        check(std::holds_alternative<KSearchResult>(result) &&
            std::get<KSearchResult>(result).m_hits.size() == 1 &&
            std::get<KSearchResult>(result).m_hits.front().m_fileId == "chapters/一.tex", "local source boundary");
    }
    fs::remove_all(root, error);
    return failures == 0 ? 0 : 1;
}
