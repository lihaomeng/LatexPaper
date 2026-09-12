#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace lightoverleaf::build
{
class IKBuildSnapshotStore;
class IKCompilerBackend;
class IKBuildArtifactPublisher;
enum class KBuildEngine { PdfLatex, XeLatex, LuaLatex };
enum class KBuildTerminal { Succeeded, Failed, Cancelled, TimedOut, CompilerUnavailable };
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
struct KBuildCommand
{
    std::string m_jobId;
    std::string m_snapshotId;
    std::string m_mainFileId;
    KBuildEngine m_engine = KBuildEngine::XeLatex;
    unsigned int m_timeoutMs = 120000;
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
};
class IKBuilds
{
public:
    virtual ~IKBuilds() = default;
    virtual KResult<std::vector<KCompilerCapability>> detect(std::stop_token stop = {}) = 0;
    virtual KResult<KBuildResult> start(const KBuildCommand& command, std::stop_token stop = {}) = 0;
    virtual KResult<bool> cancel(const std::string& jobId) = 0;
};
std::shared_ptr<IKBuilds> createBuilds(std::shared_ptr<IKBuildSnapshotStore> snapshots,
    std::shared_ptr<IKCompilerBackend> backend,
    std::shared_ptr<IKBuildArtifactPublisher> publisher = {});
}
