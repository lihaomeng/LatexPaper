#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>
#include <cstdint>

namespace lightoverleaf::build
{
class IKBuildSnapshotStore;
class IKCompilerBackend;
class IKBuildArtifactPublisher;
enum class KBuildEngine { PdfLatex, XeLatex, LuaLatex };
enum class KBuildTerminal { Succeeded, Failed, Cancelled, TimedOut, CompilerUnavailable };
enum class KBuildState { Running, Succeeded, Failed, Cancelled, TimedOut, CompilerUnavailable };
enum class KBuildPhase { Snapshot, Detect, Compile, Artifact, Render, Complete };
enum class KDiagnosticSeverity { Info, Warning, Error };
struct KCompilerCapability
{
    std::string m_toolchainId;
    std::string m_displayName;
    std::vector<KBuildEngine> m_engines;
};
struct KBuildDiagnostic
{
    KDiagnosticSeverity m_severity = KDiagnosticSeverity::Error;
    std::string m_fileId;
    std::size_t m_line = 0;
    std::string m_message;
};
struct KBuildOverlayFile
{
    std::string m_fileId;
    std::string m_content;
};
struct KBuildCommand
{
    std::string m_jobId;
    std::string m_snapshotId;
    std::string m_mainFileId;
    KBuildEngine m_engine = KBuildEngine::PdfLatex;
    unsigned int m_timeoutMs = 120000;
    std::vector<KBuildOverlayFile> m_overlayFiles;
    std::string m_scopeId;
    std::uint64_t m_generation = 1;
};
struct KBuildResult
{
    std::string m_jobId;
    KBuildTerminal m_terminal = KBuildTerminal::Failed;
    int m_exitCode = -1;
    std::string m_output;
    bool m_outputTruncated = false;
    std::vector<KBuildDiagnostic> m_diagnostics;
    std::string m_artifactId;
    bool m_syncTexAvailable = false;
    std::uint64_t m_generation = 1;
    KBuildPhase m_phase = KBuildPhase::Compile;
};
struct KBuildStatus
{
    std::string m_jobId;
    KBuildState m_state = KBuildState::Running;
    std::string m_output;
    bool m_outputTruncated = false;
    std::uint64_t m_generation = 1;
    KBuildPhase m_phase = KBuildPhase::Snapshot;
};
class IKBuilds
{
public:
    virtual ~IKBuilds() = default;
    virtual KResult<std::vector<KCompilerCapability>> detect(std::stop_token stop = {}) = 0;
    virtual KResult<KBuildResult> start(const KBuildCommand& command, std::stop_token stop = {}) = 0;
    virtual KResult<KBuildStatus> status(const std::string& jobId) const = 0;
    virtual KResult<bool> cancel(const std::string& jobId) = 0;
};
std::shared_ptr<IKBuilds> createBuilds(std::shared_ptr<IKBuildSnapshotStore> snapshots,
    std::shared_ptr<IKCompilerBackend> backend,
    std::shared_ptr<IKBuildArtifactPublisher> publisher = {});
}
