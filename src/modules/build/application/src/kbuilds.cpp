#include <lightoverleaf/build/inbound/ikbuilds.h>
#include <lightoverleaf/build/outbound/ikbuildsnapshotstore.h>
#include <lightoverleaf/build/outbound/ikcompilerbackend.h>
#include <lightoverleaf/build/outbound/ikbuildartifactpublisher.h>
#include <lightoverleaf/build/domain/kbuildpolicy.h>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

namespace lightoverleaf::build
{
namespace
{
using KStopCallback = std::stop_callback<std::function<void()>>;
}
class KBuilds final : public IKBuilds
{
public:
    KBuilds(std::shared_ptr<IKBuildSnapshotStore> snapshots, std::shared_ptr<IKCompilerBackend> backend,
        std::shared_ptr<IKBuildArtifactPublisher> publisher)
        : m_snapshots(std::move(snapshots)), m_backend(std::move(backend)),
          m_publisher(std::move(publisher)) {}
    ~KBuilds() override
    {
        std::scoped_lock lock(m_mutex);
        for (auto& [jobId, source] : m_active) source.request_stop();
    }
    KResult<std::vector<KCompilerCapability>> detect(std::stop_token stop) override
    {
        if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "build.cancelled", true};
        try
        {
            KResult<std::vector<KDetectedCompiler>> found = m_backend->detect(stop);
            if (const KError* error = std::get_if<KError>(&found)) return *error;
            std::vector<KCompilerCapability> capabilities;
            for (KDetectedCompiler& compiler : std::get<std::vector<KDetectedCompiler>>(found))
            {
                KCompilerCapability projected{std::move(compiler.m_toolchainId),
                    std::move(compiler.m_displayName), {}};
                for (const KCompilerEngine engine : compiler.m_engines)
                    projected.m_engines.push_back(engine == KCompilerEngine::PdfLatex ? KBuildEngine::PdfLatex :
                        engine == KCompilerEngine::XeLatex ? KBuildEngine::XeLatex : KBuildEngine::LuaLatex);
                capabilities.push_back(std::move(projected));
            }
            return capabilities;
        }
        catch (...) { return KError{KErrorCode::Internal, "build.backendFailure", false}; }
    }
    KResult<KBuildResult> start(const KBuildCommand& command, std::stop_token stop) override
    {
        if (!validBuildToken(command.m_jobId) || !validBuildToken(command.m_snapshotId) ||
            !validBuildFileId(command.m_mainFileId) || command.m_timeoutMs < kMinBuildTimeoutMs ||
            command.m_timeoutMs > kMaxBuildTimeoutMs)
            return KError{KErrorCode::InvalidArgument, "build.invalidArgument", false};
        std::stop_source source;
        {
            std::scoped_lock lock(m_mutex);
            if (m_active.contains(command.m_jobId))
                return KError{KErrorCode::Conflict, "build.jobExists", false};
            m_active.emplace(command.m_jobId, source);
        }
        const auto finish = [&]
        {
            std::scoped_lock lock(m_mutex);
            m_active.erase(command.m_jobId);
        };
        KStopCallback external(stop, [source]() mutable { source.request_stop(); });
        try
        {
            KResult<bool> prepared = m_snapshots->prepare(command.m_snapshotId, source.get_token());
            if (const KError* error = std::get_if<KError>(&prepared)) { finish(); return *error; }
            if (!std::get<bool>(prepared)) { finish(); return KError{KErrorCode::Internal, "build.snapshotFailure", false}; }
            struct KRelease { IKBuildSnapshotStore* store; std::string id; ~KRelease() { store->release(id); } }
                release{m_snapshots.get(), command.m_snapshotId};
            const KCompilerEngine engine = command.m_engine == KBuildEngine::PdfLatex ? KCompilerEngine::PdfLatex :
                command.m_engine == KBuildEngine::XeLatex ? KCompilerEngine::XeLatex : KCompilerEngine::LuaLatex;
            KResult<KCompilerRunResult> executed = m_backend->run({command.m_jobId, command.m_snapshotId,
                command.m_mainFileId, engine, command.m_timeoutMs}, source.get_token());
            finish();
            if (const KError* error = std::get_if<KError>(&executed)) return *error;
            KCompilerRunResult value = std::get<KCompilerRunResult>(std::move(executed));
            if (value.m_output.size() > kMaxBuildLogBytes || value.m_diagnostics.size() > 1000)
                return KError{KErrorCode::ResourceExhausted, "build.outputTooLarge", false};
            const KBuildTerminal terminal = value.m_terminal == KCompilerTerminal::Succeeded ? KBuildTerminal::Succeeded :
                value.m_terminal == KCompilerTerminal::Cancelled ? KBuildTerminal::Cancelled :
                value.m_terminal == KCompilerTerminal::TimedOut ? KBuildTerminal::TimedOut :
                value.m_terminal == KCompilerTerminal::CompilerUnavailable ? KBuildTerminal::CompilerUnavailable : KBuildTerminal::Failed;
            std::vector<KBuildDiagnostic> projected;
            for (KCompilerDiagnostic& diagnostic : value.m_diagnostics)
                projected.push_back({diagnostic.m_severity == KCompilerDiagnosticSeverity::Info ? KDiagnosticSeverity::Info :
                    diagnostic.m_severity == KCompilerDiagnosticSeverity::Warning ? KDiagnosticSeverity::Warning : KDiagnosticSeverity::Error,
                    std::move(diagnostic.m_fileId), diagnostic.m_line, std::move(diagnostic.m_message)});
            std::string artifactId;
            bool syncTexAvailable = false;
            if (terminal == KBuildTerminal::Succeeded && m_publisher)
            {
                if (value.m_pdf.empty()) return KError{KErrorCode::NotFound, "build.pdfMissing", false};
                KResult<KPublishedBuildArtifact> published = m_publisher->publish(command.m_jobId,
                    value.m_pdf, value.m_syncTex);
                if (const KError* error = std::get_if<KError>(&published)) return *error;
                KPublishedBuildArtifact artifact =
                    std::get<KPublishedBuildArtifact>(std::move(published));
                artifactId = std::move(artifact.m_artifactId);
                syncTexAvailable = artifact.m_syncTexAvailable;
            }
            return KBuildResult{command.m_jobId, terminal, value.m_exitCode,
                std::move(value.m_output), value.m_outputTruncated, std::move(projected),
                std::move(artifactId), syncTexAvailable};
        }
        catch (...)
        {
            finish();
            return KError{KErrorCode::Internal, "build.backendFailure", false};
        }
    }
    KResult<bool> cancel(const std::string& jobId) override
    {
        if (!validBuildToken(jobId)) return KError{KErrorCode::InvalidArgument, "build.invalidArgument", false};
        std::scoped_lock lock(m_mutex);
        const auto found = m_active.find(jobId);
        if (found == m_active.end()) return false;
        return found->second.request_stop();
    }
private:
    std::shared_ptr<IKBuildSnapshotStore> m_snapshots;
    std::shared_ptr<IKCompilerBackend> m_backend;
    std::shared_ptr<IKBuildArtifactPublisher> m_publisher;
    std::mutex m_mutex;
    std::map<std::string, std::stop_source> m_active;
};

std::shared_ptr<IKBuilds> createBuilds(std::shared_ptr<IKBuildSnapshotStore> snapshots,
    std::shared_ptr<IKCompilerBackend> backend, std::shared_ptr<IKBuildArtifactPublisher> publisher)
{
    return snapshots && backend ? std::make_shared<KBuilds>(std::move(snapshots),
        std::move(backend), std::move(publisher)) : nullptr;
}
}
