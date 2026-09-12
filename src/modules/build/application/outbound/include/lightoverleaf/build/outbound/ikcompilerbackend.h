#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <stop_token>
#include <functional>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>

namespace lightoverleaf::build
{
enum class KCompilerEngine { PdfLatex, XeLatex, LuaLatex };
enum class KCompilerTerminal { Succeeded, Failed, Cancelled, TimedOut, CompilerUnavailable };
enum class KCompilerDiagnosticSeverity { Info, Warning, Error };
struct KDetectedCompiler
{
    std::string m_toolchainId;
    std::string m_displayName;
    std::vector<KCompilerEngine> m_engines;
};
struct KCompilerDiagnostic
{
    KCompilerDiagnosticSeverity m_severity = KCompilerDiagnosticSeverity::Error;
    std::string m_fileId;
    std::size_t m_line = 0;
    std::string m_message;
};
struct KCompilerRun
{
    std::string m_jobId;
    std::string m_snapshotId;
    std::string m_mainFileId;
    KCompilerEngine m_engine = KCompilerEngine::XeLatex;
    unsigned int m_timeoutMs = 120000;
    std::function<void(std::string_view, bool)> m_onOutput;
};
struct KCompilerRunResult
{
    KCompilerTerminal m_terminal = KCompilerTerminal::Failed;
    int m_exitCode = -1;
    std::string m_output;
    bool m_outputTruncated = false;
    std::vector<KCompilerDiagnostic> m_diagnostics;
    std::vector<std::uint8_t> m_pdf;
    std::vector<std::uint8_t> m_syncTex;
};
class IKCompilerBackend
{
public:
    virtual ~IKCompilerBackend() = default;
    virtual KResult<std::vector<KDetectedCompiler>> detect(std::stop_token stop) = 0;
    virtual KResult<KCompilerRunResult> run(const KCompilerRun& command, std::stop_token stop) = 0;
};
}
