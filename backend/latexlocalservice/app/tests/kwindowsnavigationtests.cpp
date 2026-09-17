#include <lightoverleaf/navigation/adapters/kwindowssynctexbackend.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

using namespace lightoverleaf;
using namespace lightoverleaf::navigation;
namespace fs = std::filesystem;

namespace
{
std::string utf8(const fs::path& value)
{
    const std::u8string encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

KSyncTexArtifact artifact(const char* mode)
{
    const std::string text(mode);
    return {{'%', 'P', 'D', 'F', '-', '1', '.', '7'},
        std::vector<std::uint8_t>(text.begin(), text.end())};
}
}

int main()
{
    int failures = 0;
    const auto check = [&](bool value, const char* label)
    {
        if (!value)
        {
            ++failures;
            std::cerr << label << '\n';
        }
    };
    const fs::path base = fs::temp_directory_path() /
        (L"LightOverLeaf-synctex-中文-" + std::to_wstring(GetCurrentProcessId()));
    const fs::path cache = base / L"cache";
    std::error_code error;
    fs::remove_all(base, error);

    KResult<std::shared_ptr<IKSyncTexBackend>> missing = createWindowsSyncTexBackend(
        {utf8(cache), {}, utf8(base / L"missing.exe")});
    check(std::holds_alternative<KError>(missing) &&
        std::get<KError>(missing).m_code == KErrorCode::Unavailable,
        "missing executable is explicit");

    const fs::path miKTeXRoot = base / L"runtime" / L"miktex";
    const fs::path miKTeXBin = miKTeXRoot / L"texmfs" / L"install" /
        L"miktex" / L"bin" / L"x64";
    fs::create_directories(miKTeXBin, error);
    fs::copy_file(fs::path(LOL_FAKE_SYNCTEX_PATH), miKTeXBin / L"synctex.exe",
        fs::copy_options::overwrite_existing, error);
    KResult<std::shared_ptr<IKSyncTexBackend>> officialLayout = createWindowsSyncTexBackend(
        {utf8(base / L"official-cache"), {utf8(miKTeXRoot)}, {}, false});
    check(!error && std::holds_alternative<std::shared_ptr<IKSyncTexBackend>>(officialLayout),
        "source-built MiKTeX SyncTeX layout works without system discovery");

    KResult<std::shared_ptr<IKSyncTexBackend>> made = createWindowsSyncTexBackend(
        {utf8(cache), {}, LOL_FAKE_SYNCTEX_PATH});
    check(std::holds_alternative<std::shared_ptr<IKSyncTexBackend>>(made),
        "windows SyncTeX backend factory");
    if (const auto* backend = std::get_if<std::shared_ptr<IKSyncTexBackend>>(&made))
    {
        KResult<KBackendPdf> forward = (*backend)->forward(artifact("OK"),
            {"chapters/main.tex", 9, 2});
        if (const KError* value = std::get_if<KError>(&forward))
            std::cerr << "forward error: " << value->m_messageKey << '\n';
        check(std::holds_alternative<KBackendPdf>(forward) &&
            std::get<KBackendPdf>(forward).m_page == 3 &&
            std::get<KBackendPdf>(forward).m_x == 12.5 &&
            std::get<KBackendPdf>(forward).m_y == 44.25,
            "forward query parses process output");
        KResult<KBackendSource> reverse = (*backend)->reverse(artifact("OK"),
            {3, 12.5, 44.25});
        if (const KError* value = std::get_if<KError>(&reverse))
            std::cerr << "reverse error: " << value->m_messageKey << '\n';
        check(std::holds_alternative<KBackendSource>(reverse) &&
            std::get<KBackendSource>(reverse).m_fileId == "sections/main.tex" &&
            std::get<KBackendSource>(reverse).m_line == 17 &&
            std::get<KBackendSource>(reverse).m_column == 4,
            "reverse query normalizes relative file id");
        KSyncTexArtifact invalid = artifact("OK");
        invalid.m_pdf = {'n', 'o'};
        check(std::holds_alternative<KError>((*backend)->forward(invalid,
            {"main.tex", 1, 1})), "invalid PDF rejected before process launch");
        KResult<KBackendPdf> failed = (*backend)->forward(artifact("FAIL"),
            {"main.tex", 1, 1});
        check(std::holds_alternative<KError>(failed) &&
            std::get<KError>(failed).m_code == KErrorCode::Unavailable,
            "nonzero process exit mapped");
        KResult<KBackendPdf> huge = (*backend)->forward(artifact("HUGE"),
            {"main.tex", 1, 1});
        check(std::holds_alternative<KError>(huge) &&
            std::get<KError>(huge).m_code == KErrorCode::ResourceExhausted,
            "process output is bounded");
        KResult<KBackendSource> bad = (*backend)->reverse(artifact("BAD"),
            {1, 1, 1});
        check(std::holds_alternative<KError>(bad), "absolute source output rejected");
        const auto before = std::chrono::steady_clock::now();
        KResult<KBackendPdf> timed = (*backend)->forward(artifact("SLOW"),
            {"main.tex", 1, 1});
        check(std::holds_alternative<KError>(timed) &&
            std::get<KError>(timed).m_code == KErrorCode::Cancelled &&
            std::chrono::steady_clock::now() - before < std::chrono::seconds(8),
            "timeout terminates process tree");
        const bool cacheEmpty = fs::is_directory(cache, error) &&
            fs::directory_iterator(cache, error) == fs::directory_iterator();
        check(!error && cacheEmpty, "private query directories are removed");
    }
    fs::remove_all(base, error);
    return failures == 0 ? 0 : 1;
}
