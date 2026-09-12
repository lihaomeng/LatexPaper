#include <lightoverleaf/build/inbound/ikbuilds.h>
#include <lightoverleaf/build/adapters/kwindowsbuildadapters.h>
#include <lightoverleaf/build/outbound/ikbuildartifactpublisher.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace lightoverleaf;
using namespace lightoverleaf::build;
namespace fs = std::filesystem;

namespace
{
class KPublisher final : public IKBuildArtifactPublisher
{
public:
    KResult<KPublishedBuildArtifact> publish(const std::string&, std::span<const std::uint8_t> pdf,
        std::span<const std::uint8_t> sync) override
    { called = true; return pdf.empty() ? KResult<KPublishedBuildArtifact>{KError{KErrorCode::NotFound,"pdf",false}} :
        KResult<KPublishedBuildArtifact>{KPublishedBuildArtifact{"artifact-1",!sync.empty()}}; }
    bool called=false;
};
std::string utf8(const fs::path& value)
{
    const std::u8string encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}
void write(const fs::path& path, const char* content)
{
    std::ofstream output(path, std::ios::binary); output << content;
}
}

int main()
{
    int failures = 0;
    const auto check = [&](bool value, const char* label) { if (!value) { ++failures; std::cerr << label << '\n'; } };
    const fs::path base = fs::temp_directory_path() /
        (L"LightOverLeaf-build-中文-" + std::to_wstring(GetCurrentProcessId()));
    const fs::path workspace = base / L"workspace";
    const fs::path cache = base / L"cache";
    const fs::path tex = base / L"tex";
    std::error_code error;
    fs::remove_all(base, error); fs::create_directories(workspace, error); fs::create_directories(tex, error);
    fs::copy_file(fs::path(LOL_FAKE_COMPILER_PATH), tex / L"xelatex.exe", fs::copy_options::overwrite_existing, error);
    check(!error, "install fake xelatex fixture");
    write(workspace / L"main.tex", "OK");
    KResult<KWindowsBuildAdapters> made = createWindowsBuildAdapters({utf8(workspace), utf8(cache), {utf8(tex)}});
    check(std::holds_alternative<KWindowsBuildAdapters>(made), "windows adapters factory");
    if (const auto* adapters = std::get_if<KWindowsBuildAdapters>(&made))
    {
        auto publisher=std::make_shared<KPublisher>();
        auto builds = createBuilds(adapters->m_snapshots, adapters->m_compiler,publisher);
        KResult<std::vector<KCompilerCapability>> detected = builds->detect();
        check(std::holds_alternative<std::vector<KCompilerCapability>>(detected) &&
            std::get<std::vector<KCompilerCapability>>(detected).size() == 1 &&
            std::get<std::vector<KCompilerCapability>>(detected).front().m_engines.size() == 1,
            "configured backend detection");
        KResult<KBuildResult> success = builds->start({"job-ok", "snapshot-ok", "main.tex", KBuildEngine::XeLatex, 3000});
        check(std::holds_alternative<KBuildResult>(success) &&
            std::get<KBuildResult>(success).m_terminal == KBuildTerminal::Succeeded &&
            std::get<KBuildResult>(success).m_output.find("\xef\xbf\xbd") != std::string::npos &&
            !fs::exists(cache / L"snapshot-ok") && publisher->called &&
            std::get<KBuildResult>(success).m_artifactId=="artifact-1" &&
            std::get<KBuildResult>(success).m_syncTexAvailable,
            "snapshot compile, artifact publication, utf8 sanitization and cleanup");
        write(workspace / L"main.tex", "ERROR");
        KResult<KBuildResult> failed = builds->start({"job-fail", "snapshot-fail", "main.tex", KBuildEngine::XeLatex, 3000});
        check(std::holds_alternative<KBuildResult>(failed) &&
            std::get<KBuildResult>(failed).m_terminal == KBuildTerminal::Failed &&
            !std::get<KBuildResult>(failed).m_diagnostics.empty(), "failed compile diagnostic");
        write(workspace / L"main.tex", "SLOW");
        const auto before = std::chrono::steady_clock::now();
        KResult<KBuildResult> timed = builds->start({"job-timeout", "snapshot-timeout", "main.tex", KBuildEngine::XeLatex, 1000});
        check(std::holds_alternative<KBuildResult>(timed) &&
            std::get<KBuildResult>(timed).m_terminal == KBuildTerminal::TimedOut &&
            std::chrono::steady_clock::now() - before < std::chrono::seconds(3), "timeout terminates compiler job");
        KResult<KBuildResult> cancelled;
        std::jthread runner([&] { cancelled = builds->start(
            {"job-cancel", "snapshot-cancel", "main.tex", KBuildEngine::XeLatex, 10000}); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        KResult<bool> cancellation = builds->cancel("job-cancel");
        check(std::holds_alternative<bool>(cancellation) && std::get<bool>(cancellation), "explicit cancel accepted");
        runner.join();
        check(std::holds_alternative<KBuildResult>(cancelled) &&
            std::get<KBuildResult>(cancelled).m_terminal == KBuildTerminal::Cancelled, "cancel terminates compiler job");
        KResult<KBuildResult> unavailable = builds->start(
            {"job-missing", "snapshot-missing", "main.tex", KBuildEngine::PdfLatex, 3000});
        check(std::holds_alternative<KBuildResult>(unavailable) &&
            std::get<KBuildResult>(unavailable).m_terminal == KBuildTerminal::CompilerUnavailable,
            "missing engine is explicit");
    }
    fs::remove_all(base, error);
    return failures == 0 ? 0 : 1;
}
