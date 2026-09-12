#include <lightoverleaf/document/adapters/klocaldocumentstore.h>
#include <lightoverleaf/document/inbound/ikdocuments.h>
#include <lightoverleaf/search/adapters/klocalsearchsource.h>
#include <lightoverleaf/search/inbound/iksearch.h>
#include <lightoverleaf/workspace/adapters/klocalworkspacestore.h>
#include <lightoverleaf/workspace/inbound/ikworkspaces.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace lightoverleaf;
namespace fs = std::filesystem;

namespace
{
using KClock = std::chrono::steady_clock;

std::string utf8(const fs::path& path)
{
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

double milliseconds(KClock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(KClock::now() - begin).count();
}
}

int main()
{
    int failures = 0;
    const auto check = [&](bool value, const char* label)
    {
        if (!value) { ++failures; std::cerr << label << '\n'; }
    };
    const fs::path root = fs::temp_directory_path() /
        (L"LightOverLeaf-performance-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    check(!error, "create performance fixture");
    for (int index = 0; index < 1000; ++index)
    {
        std::ofstream output(root / (L"chapter-" + std::to_wstring(index) + L".tex"),
            std::ios::binary);
        output << "\\section{Chapter " << index << "}\nordinary content";
        if (index == 999) output << " unique-performance-needle";
    }

    auto workspaces = workspace::createWorkspaces(workspace::createLocalWorkspaceStore());
    const KClock::time_point openBegin = KClock::now();
    const KResult<workspace::KWorkspaceState> opened = workspaces->open(utf8(root));
    const double treeMs = milliseconds(openBegin);
    check(std::holds_alternative<workspace::KWorkspaceState>(opened) &&
        std::get<workspace::KWorkspaceState>(opened).m_entries.size() == 1000,
        "1000-file workspace tree");

    KResult<std::shared_ptr<search::IKSearchSource>> source =
        search::createLocalSearchSource({utf8(root)});
    check(std::holds_alternative<std::shared_ptr<search::IKSearchSource>>(source),
        "create performance search source");
    double searchMs = -1.0;
    if (const auto* value = std::get_if<std::shared_ptr<search::IKSearchSource>>(&source))
    {
        auto service = search::createSearch(*value);
        const KClock::time_point searchBegin = KClock::now();
        const KResult<search::KSearchResult> result =
            service->run({"unique-performance-needle", true, 10});
        searchMs = milliseconds(searchBegin);
        check(std::holds_alternative<search::KSearchResult>(result) &&
            std::get<search::KSearchResult>(result).m_hits.size() == 1,
            "1000-file search");
    }

    KResult<std::shared_ptr<document::IKDocumentStore>> store =
        document::createLocalDocumentStore({utf8(root)});
    check(std::holds_alternative<std::shared_ptr<document::IKDocumentStore>>(store),
        "create performance document store");
    double fileOpenMs = -1.0;
    double saveMs = -1.0;
    if (const auto* value = std::get_if<std::shared_ptr<document::IKDocumentStore>>(&store))
    {
        auto documents = document::createDocuments(*value);
        const KClock::time_point fileOpenBegin = KClock::now();
        const KResult<document::KDocumentSnapshot> document =
            documents->open("chapter-500.tex");
        fileOpenMs = milliseconds(fileOpenBegin);
        if (const auto* snapshot = std::get_if<document::KDocumentSnapshot>(&document))
        {
            const KClock::time_point saveBegin = KClock::now();
            const KResult<document::KDocumentSaved> saved = documents->save(
                {snapshot->m_fileId, snapshot->m_content + "\nedit", snapshot->m_revision, 1});
            saveMs = milliseconds(saveBegin);
            check(std::holds_alternative<document::KDocumentSaved>(saved),
                "atomic save performance path");
        }
        else check(false, "ordinary file open");
    }

    std::cout << std::fixed << std::setprecision(3)
        << "PERF tree_1000_ms=" << treeMs
        << " search_1000_ms=" << searchMs
        << " file_open_ms=" << fileOpenMs
        << " atomic_save_ms=" << saveMs << '\n';
    check(treeMs <= 3000.0, "1000-file tree remains operable");
    check(searchMs >= 0.0 && searchMs <= 3000.0, "1000-file search remains operable");
    check(fileOpenMs >= 0.0 && fileOpenMs <= 100.0, "ordinary file open backend target");
    check(saveMs >= 0.0 && saveMs <= 500.0, "atomic save backend target");
    fs::remove_all(root, error);
    return failures == 0 ? 0 : 1;
}
