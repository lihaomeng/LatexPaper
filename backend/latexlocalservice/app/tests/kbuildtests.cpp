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
class KRootSource final : public IKCompilerRootSource
{
public:
    std::vector<std::string> roots() const override { return m_roots; }

public:
    std::vector<std::string> m_roots;
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
    const fs::path texBin = tex / L"2026" / L"bin" / L"windows";
    const fs::path miktex = base / L"runtime" / L"miktex";
    const fs::path miktexBin = miktex / L"texmfs" / L"install" / L"miktex" / L"bin" / L"x64";
    std::error_code error;
    fs::remove_all(base, error); fs::create_directories(workspace, error); fs::create_directories(texBin, error);
    fs::create_directories(miktexBin, error);
    fs::copy_file(fs::path(LOL_FAKE_COMPILER_PATH), texBin / L"pdflatex.exe", fs::copy_options::overwrite_existing, error);
    fs::copy_file(fs::path(LOL_FAKE_COMPILER_PATH), miktexBin / L"pdflatex.exe", fs::copy_options::overwrite_existing, error);
    check(!error, "install fake pdflatex fixture");
    write(workspace / L"main.tex", "OK");
    auto rootSource = std::make_shared<KRootSource>();
    KResult<KWindowsBuildAdapters> made = createWindowsBuildAdapters(
        {utf8(workspace), utf8(cache), {}, rootSource});
    check(std::holds_alternative<KWindowsBuildAdapters>(made), "windows adapters factory");
    if (const auto* adapters = std::get_if<KWindowsBuildAdapters>(&made))
    {
        auto publisher=std::make_shared<KPublisher>();
        auto builds = createBuilds(adapters->m_snapshots, adapters->m_compiler,publisher);
        rootSource->m_roots = {utf8(tex)};
        KResult<std::vector<KCompilerCapability>> detected = builds->detect();
        check(std::holds_alternative<std::vector<KCompilerCapability>>(detected) &&
            std::get<std::vector<KCompilerCapability>>(detected).size() == 1 &&
            std::get<std::vector<KCompilerCapability>>(detected).front().m_engines.size() == 1,
            "runtime-configured versioned TeX root detection");
        rootSource->m_roots = {utf8(miktex)};
        KResult<std::vector<KCompilerCapability>> detectedMiKTeX = builds->detect();
        check(std::holds_alternative<std::vector<KCompilerCapability>>(detectedMiKTeX) &&
            std::get<std::vector<KCompilerCapability>>(detectedMiKTeX).size() == 1 &&
            std::get<std::vector<KCompilerCapability>>(detectedMiKTeX).front().m_toolchainId == "miktex" &&
            std::get<std::vector<KCompilerCapability>>(detectedMiKTeX).front().m_displayName == "MiKTeX pdfLaTeX",
            "source-built MiKTeX runtime detection");
        rootSource->m_roots = {utf8(tex)};
        KResult<KBuildResult> success = builds->start({"job-ok", "snapshot-ok", "main.tex", KBuildEngine::PdfLatex, 3000});
        check(std::holds_alternative<KBuildResult>(success) &&
            std::get<KBuildResult>(success).m_terminal == KBuildTerminal::Succeeded &&
            std::get<KBuildResult>(success).m_output.find("\xef\xbf\xbd") != std::string::npos &&
            !fs::exists(cache / L"snapshot-ok") && publisher->called &&
            std::get<KBuildResult>(success).m_artifactId=="artifact-1" &&
            std::get<KBuildResult>(success).m_syncTexAvailable,
            "snapshot compile, artifact publication, utf8 sanitization and cleanup");
        KResult<KBuildStatus> completedStatus = builds->status("job-ok");
        check(std::holds_alternative<KBuildStatus>(completedStatus) &&
            std::get<KBuildStatus>(completedStatus).m_state == KBuildState::Succeeded &&
            !std::get<KBuildStatus>(completedStatus).m_output.empty(),
            "completed build status retained");
        fs::copy_file(fs::path(LOL_FAKE_COMPILER_PATH), texBin / L"latexmk.exe",
            fs::copy_options::overwrite_existing, error);
        KResult<KBuildResult> latexmk = builds->start(
            {"job-latexmk", "snapshot-latexmk", "main.tex", KBuildEngine::PdfLatex, 3000});
        check(std::holds_alternative<KBuildResult>(latexmk) &&
            std::get<KBuildResult>(latexmk).m_terminal == KBuildTerminal::Succeeded &&
            std::get<KBuildResult>(latexmk).m_output.find("launcher:latexmk") == std::string::npos,
            "pdflatex remains direct when latexmk is present");
        const auto& latexmkDiagnostics = std::get<KBuildResult>(latexmk).m_diagnostics;
        check(latexmkDiagnostics.size() >= 2 &&
            latexmkDiagnostics[1].m_severity == KDiagnosticSeverity::Warning,
            "diagnostic warning severity");
        write(workspace / L"main.tex", "LARGE_INVALID");
        KResult<KBuildResult> bounded = builds->start(
            {"job-bounded", "snapshot-bounded", "main.tex", KBuildEngine::PdfLatex, 3000});
        check(std::holds_alternative<KBuildResult>(bounded) &&
            std::get<KBuildResult>(bounded).m_terminal == KBuildTerminal::Succeeded &&
            std::get<KBuildResult>(bounded).m_output.size() <= 1024U * 1024U &&
            std::get<KBuildResult>(bounded).m_outputTruncated,
            "utf8 replacement remains inside rpc log bound");
        write(workspace / L"main.tex", "ERROR");
        KResult<KBuildResult> failed = builds->start({"job-fail", "snapshot-fail", "main.tex", KBuildEngine::PdfLatex, 3000});
        check(std::holds_alternative<KBuildResult>(failed) &&
            std::get<KBuildResult>(failed).m_terminal == KBuildTerminal::Failed &&
            !std::get<KBuildResult>(failed).m_diagnostics.empty(), "failed compile diagnostic");
        write(workspace / L"main.tex", "SLOW");
        const auto before = std::chrono::steady_clock::now();
        KResult<KBuildResult> timed = builds->start({"job-timeout", "snapshot-timeout", "main.tex", KBuildEngine::PdfLatex, 1000});
        check(std::holds_alternative<KBuildResult>(timed) &&
            std::get<KBuildResult>(timed).m_terminal == KBuildTerminal::TimedOut &&
            std::chrono::steady_clock::now() - before < std::chrono::seconds(3), "timeout terminates compiler job");
        KResult<KBuildResult> cancelled;
        std::jthread runner([&] { cancelled = builds->start(
            {"job-cancel", "snapshot-cancel", "main.tex", KBuildEngine::PdfLatex, 10000}); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        KResult<KBuildStatus> runningStatus = builds->status("job-cancel");
        check(std::holds_alternative<KBuildStatus>(runningStatus) &&
            std::get<KBuildStatus>(runningStatus).m_state == KBuildState::Running &&
            std::get<KBuildStatus>(runningStatus).m_output.find("fake diagnostic") != std::string::npos,
            "running build exposes bounded live output");
        KResult<bool> cancellation = builds->cancel("job-cancel");
        check(std::holds_alternative<bool>(cancellation) && std::get<bool>(cancellation), "explicit cancel accepted");
        runner.join();
        check(std::holds_alternative<KBuildResult>(cancelled) &&
            std::get<KBuildResult>(cancelled).m_terminal == KBuildTerminal::Cancelled, "cancel terminates compiler job");
        KResult<KBuildStatus> cancelledStatus = builds->status("job-cancel");
        check(std::holds_alternative<KBuildStatus>(cancelledStatus) &&
            std::get<KBuildStatus>(cancelledStatus).m_state == KBuildState::Cancelled,
            "cancelled build status retained");
        check(std::holds_alternative<KError>(builds->status("job-unknown")),
            "unknown build status is explicit");
        rootSource->m_roots = {utf8(base / L"missing-tex")};
        KResult<KBuildResult> unavailable = builds->start(
            {"job-missing", "snapshot-missing", "main.tex", KBuildEngine::PdfLatex, 3000});
        check(std::holds_alternative<KBuildResult>(unavailable) &&
            std::get<KBuildResult>(unavailable).m_terminal == KBuildTerminal::CompilerUnavailable,
            "missing engine is explicit");
    }
    const fs::path draftCache = base / L"draft-cache";
    KResult<KWindowsBuildAdapters> draftMade = createWindowsBuildAdapters(
        {std::string(), utf8(draftCache), {utf8(tex)}, {}});
    check(std::holds_alternative<KWindowsBuildAdapters>(draftMade),
        "draft adapters allow an empty workspace root");
    if (const auto* draftAdapters = std::get_if<KWindowsBuildAdapters>(&draftMade))
    {
        auto draftPublisher = std::make_shared<KPublisher>();
        auto draftBuilds = createBuilds(draftAdapters->m_snapshots,
            draftAdapters->m_compiler, draftPublisher);
        KResult<KBuildResult> draft = draftBuilds->start({"job-draft", "snapshot-draft",
            "main.tex", KBuildEngine::PdfLatex, 3000, {{"main.tex", "OK"}}});
        check(std::holds_alternative<KBuildResult>(draft) &&
            std::get<KBuildResult>(draft).m_terminal == KBuildTerminal::Succeeded &&
            std::get<KBuildResult>(draft).m_artifactId == "artifact-1" &&
            !fs::exists(draftCache / L"snapshot-draft"),
            "draft overlay compiles without a user workspace and cleans snapshot");
        KResult<KBuildResult> generationOne;
        KResult<KBuildResult> generationTwo;
        KResult<KBuildResult> generationThree;
        std::jthread first([&] { generationOne = draftBuilds->start({"job-generation-1",
            "snapshot-generation-1", "main.tex", KBuildEngine::PdfLatex, 10000,
            {{"main.tex", "SLOW"}}, "draft-latest", 1}); });
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        std::jthread second([&] { generationTwo = draftBuilds->start({"job-generation-2",
            "snapshot-generation-2", "main.tex", KBuildEngine::PdfLatex, 10000,
            {{"main.tex", "SLOW"}}, "draft-latest", 2}); });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        std::jthread third([&] { generationThree = draftBuilds->start({"job-generation-3",
            "snapshot-generation-3", "main.tex", KBuildEngine::PdfLatex, 3000,
            {{"main.tex", "OK"}}, "draft-latest", 3}); });
        first.join(); second.join(); third.join();
        check(std::holds_alternative<KBuildResult>(generationOne) &&
            std::get<KBuildResult>(generationOne).m_terminal == KBuildTerminal::Cancelled &&
            std::holds_alternative<KBuildResult>(generationTwo) &&
            std::get<KBuildResult>(generationTwo).m_terminal == KBuildTerminal::Cancelled &&
            std::holds_alternative<KBuildResult>(generationThree) &&
            std::get<KBuildResult>(generationThree).m_terminal == KBuildTerminal::Succeeded &&
            std::get<KBuildResult>(generationThree).m_generation == 3 &&
            std::get<KBuildResult>(generationThree).m_phase == KBuildPhase::Artifact,
            "latest generation wins with one active and one pending slot");
        KResult<KBuildResult> stale = draftBuilds->start({"job-generation-stale",
            "snapshot-generation-stale", "main.tex", KBuildEngine::PdfLatex, 3000,
            {{"main.tex", "OK"}}, "draft-latest", 2});
        check(std::holds_alternative<KError>(stale) &&
            std::get<KError>(stale).m_code == KErrorCode::Conflict,
            "stale generation is rejected before snapshot materialization");
        KResult<KBuildResult> duplicate = draftBuilds->start({"job-duplicate",
            "snapshot-duplicate", "main.tex", KBuildEngine::PdfLatex, 3000,
            {{"main.tex", "OK"}, {"MAIN.TEX", "OK"}}});
        check(std::holds_alternative<KError>(duplicate) &&
            std::get<KError>(duplicate).m_code == KErrorCode::Conflict,
            "case-insensitive duplicate overlay is rejected before materialization");
    }
    fs::remove_all(base, error);
    return failures == 0 ? 0 : 1;
}
