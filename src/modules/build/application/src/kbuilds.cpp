#include <lightoverleaf/build/inbound/ikbuilds.h>
#include <lightoverleaf/build/outbound/ikbuildsnapshotstore.h>
#include <lightoverleaf/build/outbound/ikcompilerbackend.h>
#include <lightoverleaf/build/outbound/ikbuildartifactpublisher.h>
#include <lightoverleaf/build/domain/kbuildpolicy.h>
#include <algorithm>
#include <functional>
#include <deque>
#include <map>
#include <mutex>
#include <optional>

namespace lightoverleaf::build
{
namespace
{
using KStopCallback = std::stop_callback<std::function<void()>>;
constexpr std::size_t kCompletedStatusLimit = 20;

KBuildState buildState(KBuildTerminal terminal)
{
    return terminal == KBuildTerminal::Succeeded ? KBuildState::Succeeded :
        terminal == KBuildTerminal::Cancelled ? KBuildState::Cancelled :
        terminal == KBuildTerminal::TimedOut ? KBuildState::TimedOut :
        terminal == KBuildTerminal::CompilerUnavailable ? KBuildState::CompilerUnavailable :
        KBuildState::Failed;
}

struct KActiveBuild
{
    std::stop_source m_stop;
    KBuildStatus m_status;
};
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
        for (auto& [jobId, active] : m_active) active.m_stop.request_stop();
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
            m_active.emplace(command.m_jobId,
                KActiveBuild{source, {command.m_jobId, KBuildState::Running, {}, false}});
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
            const auto onOutput = [this, jobId = command.m_jobId](std::string_view output,
                bool truncated)
            {
                std::scoped_lock lock(m_mutex);
                const auto found = m_active.find(jobId);
                if (found == m_active.end()) return;
                found->second.m_status.m_output.assign(output);
                found->second.m_status.m_outputTruncated = truncated;
            };
            KResult<KCompilerRunResult> executed = m_backend->run({command.m_jobId, command.m_snapshotId,
                command.m_mainFileId, engine, command.m_timeoutMs, onOutput}, source.get_token());
            if (const KError* error = std::get_if<KError>(&executed)) { finish(); return *error; }
            KCompilerRunResult value = std::get<KCompilerRunResult>(std::move(executed));
            if (value.m_output.size() > kMaxBuildLogBytes || value.m_diagnostics.size() > 1000)
            {
                finish();
                return KError{KErrorCode::ResourceExhausted, "build.outputTooLarge", false};
            }
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
                if (value.m_pdf.empty()) { finish(); return KError{KErrorCode::NotFound, "build.pdfMissing", false}; }
                KResult<KPublishedBuildArtifact> published = m_publisher->publish(command.m_jobId,
                    value.m_pdf, value.m_syncTex);
                if (const KError* error = std::get_if<KError>(&published)) { finish(); return *error; }
                KPublishedBuildArtifact artifact =
                    std::get<KPublishedBuildArtifact>(std::move(published));
                artifactId = std::move(artifact.m_artifactId);
                syncTexAvailable = artifact.m_syncTexAvailable;
            }
            KBuildResult result{command.m_jobId, terminal, value.m_exitCode,
                std::move(value.m_output), value.m_outputTruncated, std::move(projected),
                std::move(artifactId), syncTexAvailable};
            remember(result);
            return result;
        }
        catch (...)
        {
            finish();
            return KError{KErrorCode::Internal, "build.backendFailure", false};
        }
    }
    KResult<KBuildStatus> status(const std::string& jobId) const override
    {
        if (!validBuildToken(jobId))
            return KError{KErrorCode::InvalidArgument, "build.invalidArgument", false};
        std::scoped_lock lock(m_mutex);
        const auto active = m_active.find(jobId);
        if (active != m_active.end()) return active->second.m_status;
        const auto completed = m_completed.find(jobId);
        if (completed != m_completed.end()) return completed->second;
        return KError{KErrorCode::NotFound, "build.jobNotFound", false};
    }
    KResult<bool> cancel(const std::string& jobId) override
    {
        if (!validBuildToken(jobId)) return KError{KErrorCode::InvalidArgument, "build.invalidArgument", false};
        std::scoped_lock lock(m_mutex);
        const auto found = m_active.find(jobId);
        if (found == m_active.end()) return false;
        return found->second.m_stop.request_stop();
    }
private:
    void remember(const KBuildResult& result)
    {
        std::scoped_lock lock(m_mutex);
        m_active.erase(result.m_jobId);
        m_completed.insert_or_assign(result.m_jobId, KBuildStatus{result.m_jobId,
            buildState(result.m_terminal), result.m_output, result.m_outputTruncated});
        m_completedOrder.erase(std::remove(m_completedOrder.begin(), m_completedOrder.end(),
            result.m_jobId), m_completedOrder.end());
        m_completedOrder.push_back(result.m_jobId);
        while (m_completedOrder.size() > kCompletedStatusLimit)
        {
            m_completed.erase(m_completedOrder.front());
            m_completedOrder.pop_front();
        }
    }

private:
    std::shared_ptr<IKBuildSnapshotStore> m_snapshots;
    std::shared_ptr<IKCompilerBackend> m_backend;
    std::shared_ptr<IKBuildArtifactPublisher> m_publisher;
    mutable std::mutex m_mutex;
    std::map<std::string, KActiveBuild> m_active;
    std::map<std::string, KBuildStatus> m_completed;
    std::deque<std::string> m_completedOrder;
};

std::shared_ptr<IKBuilds> createBuilds(std::shared_ptr<IKBuildSnapshotStore> snapshots,
    std::shared_ptr<IKCompilerBackend> backend, std::shared_ptr<IKBuildArtifactPublisher> publisher)
{
    return snapshots && backend ? std::make_shared<KBuilds>(std::move(snapshots),
        std::move(backend), std::move(publisher)) : nullptr;
}
}
