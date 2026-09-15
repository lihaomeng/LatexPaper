#include <chrono>
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
#include <set>
#include <condition_variable>

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
struct KScopeState
{
    std::string m_activeJob;
    std::string m_pendingJob;
    std::uint64_t m_latestGeneration = 0;
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
        m_cv.notify_all();
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
        const std::string scopeId = command.m_scopeId.empty() ? command.m_jobId : command.m_scopeId;
        if (!validBuildToken(command.m_jobId) || !validBuildToken(command.m_snapshotId) ||
            !validBuildToken(scopeId) || command.m_generation == 0 ||
            command.m_generation > 9007199254740991ULL ||
            !validBuildFileId(command.m_mainFileId) || command.m_timeoutMs < kMinBuildTimeoutMs ||
            command.m_timeoutMs > kMaxBuildTimeoutMs)
            return KError{KErrorCode::InvalidArgument, "build.invalidArgument", false};
        if (command.m_engine != KBuildEngine::PdfLatex)
            return KError{KErrorCode::InvalidArgument, "build.unsupportedEngine", false};
        if (command.m_overlayFiles.size() > kMaxBuildOverlayFiles)
            return KError{KErrorCode::ResourceExhausted, "build.overlayTooLarge", false};
        std::size_t overlayBytes = 0;
        std::set<std::string> overlayIds;
        std::vector<KBuildSnapshotOverlayFile> snapshotOverlay;
        snapshotOverlay.reserve(command.m_overlayFiles.size());
        for (const KBuildOverlayFile& file : command.m_overlayFiles)
        {
            if (!validBuildFileId(file.m_fileId))
                return KError{KErrorCode::InvalidArgument, "build.invalidOverlay", false};
            if (file.m_content.size() > kMaxBuildOverlayFileBytes ||
                file.m_content.size() > kMaxBuildOverlayBytes - overlayBytes)
                return KError{KErrorCode::ResourceExhausted, "build.overlayTooLarge", false};
            std::string normalizedId = file.m_fileId;
            std::transform(normalizedId.begin(), normalizedId.end(), normalizedId.begin(),
                [](unsigned char byte)
                {
                    return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a') :
                        static_cast<char>(byte);
                });
            if (!overlayIds.insert(std::move(normalizedId)).second)
                return KError{KErrorCode::Conflict, "build.duplicateOverlay", false};
            overlayBytes += file.m_content.size();
            snapshotOverlay.push_back({file.m_fileId, file.m_content});
        }
        std::stop_source source;
        KStopCallback external(stop, [this, source]() mutable
        {
            source.request_stop();
            m_cv.notify_all();
        });
        bool acquired = false;
        bool superseded = false;
        {
            std::unique_lock lock(m_mutex);
            if (m_active.contains(command.m_jobId) || m_completed.contains(command.m_jobId))
                return KError{KErrorCode::Conflict, "build.jobExists", false};
            KScopeState& scope = m_scopes[scopeId];
            if (command.m_generation <= scope.m_latestGeneration)
                return KError{KErrorCode::Conflict, "build.staleGeneration", false};
            scope.m_latestGeneration = command.m_generation;
            m_active.emplace(command.m_jobId, KActiveBuild{source,
                {command.m_jobId, KBuildState::Running, {}, false,
                    command.m_generation, KBuildPhase::Snapshot}});
            if (scope.m_activeJob.empty())
            {
                scope.m_activeJob = command.m_jobId;
                acquired = true;
            }
            else
            {
                if (!scope.m_pendingJob.empty())
                {
                    m_superseded.insert(scope.m_pendingJob);
                    const auto prior = m_active.find(scope.m_pendingJob);
                    if (prior != m_active.end()) prior->second.m_stop.request_stop();
                }
                scope.m_pendingJob = command.m_jobId;
                const auto active = m_active.find(scope.m_activeJob);
                if (active != m_active.end()) active->second.m_stop.request_stop();
                m_cv.notify_all();
                m_cv.wait(lock, [&]
                {
                    return scope.m_activeJob == command.m_jobId ||
                        m_superseded.contains(command.m_jobId) || source.stop_requested();
                });
                superseded = m_superseded.erase(command.m_jobId) != 0;
                acquired = scope.m_activeJob == command.m_jobId && !source.stop_requested();
                if (!acquired && scope.m_pendingJob == command.m_jobId)
                    scope.m_pendingJob.clear();
            }
        }
        if (!acquired)
        {
            KBuildResult skipped;
            skipped.m_jobId = command.m_jobId;
            skipped.m_terminal = KBuildTerminal::Cancelled;
            skipped.m_output = superseded ? "build.superseded" : "build.cancelled";
            skipped.m_generation = command.m_generation;
            skipped.m_phase = KBuildPhase::Compile;
            remember(skipped, scopeId);
            return skipped;
        }
        const auto finish = [&]
        {
            completeSlot(scopeId, command.m_jobId);
            std::scoped_lock lock(m_mutex);
            m_active.erase(command.m_jobId);
        };
        try
        {
            KResult<bool> prepared = m_snapshots->prepare(command.m_snapshotId,
                snapshotOverlay, source.get_token());
            if (const KError* error = std::get_if<KError>(&prepared)) { finish(); return *error; }
            if (!std::get<bool>(prepared)) { finish(); return KError{KErrorCode::Internal, "build.snapshotFailure", false}; }
            setPhase(command.m_jobId, KBuildPhase::Compile);
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
            KCompilerRunResult value;
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(command.m_timeoutMs);
            constexpr unsigned int kMaxPasses = 5;
            for (unsigned int pass = 1; pass <= kMaxPasses; ++pass)
            {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                if (source.stop_requested() || remaining <= 0)
                {
                    value.m_terminal = source.stop_requested() ? KCompilerTerminal::Cancelled :
                        KCompilerTerminal::TimedOut;
                    break;
                }
                const std::string heading = "[pdfLaTeX pass " + std::to_string(pass) + "/5]\n";
                const auto passOutput = [&onOutput, &heading](std::string_view output, bool truncated)
                {
                    onOutput(heading + std::string(output), truncated);
                };
                KResult<KCompilerRunResult> executed = m_backend->run({command.m_jobId,
                    command.m_snapshotId, command.m_mainFileId, engine,
                    static_cast<unsigned int>(remaining), passOutput}, source.get_token());
                if (const KError* error = std::get_if<KError>(&executed)) { finish(); return *error; }
                value = std::get<KCompilerRunResult>(std::move(executed));
                if (value.m_output.size() <= kMaxBuildLogBytes - heading.size())
                    value.m_output.insert(0, heading);
                if (value.m_terminal != KCompilerTerminal::Succeeded || !value.m_rerunRequired) break;
                if (pass == kMaxPasses)
                    value.m_diagnostics.push_back({KCompilerDiagnosticSeverity::Warning,
                        command.m_mainFileId, 0, "Maximum passes reached; references may not be stable."});
            }
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
                setPhase(command.m_jobId, KBuildPhase::Artifact);
                if (value.m_pdf.empty()) { finish(); return KError{KErrorCode::NotFound, "build.pdfMissing", false}; }
                KResult<KPublishedBuildArtifact> published = m_publisher->publish(command.m_jobId,
                    value.m_pdf, value.m_syncTex);
                if (const KError* error = std::get_if<KError>(&published)) { finish(); return *error; }
                KPublishedBuildArtifact artifact =
                    std::get<KPublishedBuildArtifact>(std::move(published));
                artifactId = std::move(artifact.m_artifactId);
                syncTexAvailable = artifact.m_syncTexAvailable;
            }
            KBuildResult result;
            result.m_jobId = command.m_jobId;
            result.m_terminal = terminal;
            result.m_exitCode = value.m_exitCode;
            result.m_output = std::move(value.m_output);
            result.m_outputTruncated = value.m_outputTruncated;
            result.m_diagnostics = std::move(projected);
            result.m_artifactId = std::move(artifactId);
            result.m_syncTexAvailable = syncTexAvailable;
            result.m_generation = command.m_generation;
            result.m_phase = terminal == KBuildTerminal::Succeeded ?
                KBuildPhase::Artifact : KBuildPhase::Compile;
            remember(result, scopeId);
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
        const bool accepted = found->second.m_stop.request_stop();
        m_cv.notify_all();
        return accepted;
    }
private:
    void setPhase(const std::string& jobId, KBuildPhase phase)
    {
        std::scoped_lock lock(m_mutex);
        const auto found = m_active.find(jobId);
        if (found != m_active.end()) found->second.m_status.m_phase = phase;
    }
    void completeSlot(const std::string& scopeId, const std::string& jobId)
    {
        {
            std::scoped_lock lock(m_mutex);
            const auto found = m_scopes.find(scopeId);
            if (found != m_scopes.end())
            {
                KScopeState& scope = found->second;
                if (scope.m_pendingJob == jobId) scope.m_pendingJob.clear();
                if (scope.m_activeJob == jobId)
                {
                    scope.m_activeJob = std::move(scope.m_pendingJob);
                    scope.m_pendingJob.clear();
                }
            }
        }
        m_cv.notify_all();
    }
    void remember(const KBuildResult& result, const std::string& scopeId)
    {
        completeSlot(scopeId, result.m_jobId);
        std::scoped_lock lock(m_mutex);
        m_active.erase(result.m_jobId);
        m_completed.insert_or_assign(result.m_jobId, KBuildStatus{result.m_jobId,
            buildState(result.m_terminal), result.m_output, result.m_outputTruncated,
            result.m_generation, result.m_phase});
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
    std::map<std::string, KScopeState> m_scopes;
    std::set<std::string> m_superseded;
    std::condition_variable m_cv;
};

std::shared_ptr<IKBuilds> createBuilds(std::shared_ptr<IKBuildSnapshotStore> snapshots,
    std::shared_ptr<IKCompilerBackend> backend, std::shared_ptr<IKBuildArtifactPublisher> publisher)
{
    return snapshots && backend ? std::make_shared<KBuilds>(std::move(snapshots),
        std::move(backend), std::move(publisher)) : nullptr;
}
}
