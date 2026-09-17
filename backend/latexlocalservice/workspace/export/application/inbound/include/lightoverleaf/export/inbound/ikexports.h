#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>
namespace lightoverleaf::exporting {
class IKExportStore;
struct KExportFile { std::string m_fileId; std::string m_content; };
struct KExportDestination { std::string m_token; std::string m_displayName; std::uint64_t m_expiresAtUnixMs=0; };
struct KExportFailure { std::string m_fileId; std::string m_code; };
struct KExportCommand { std::string m_sessionId; std::string m_destinationToken; std::uint64_t m_generation=0; std::vector<KExportFile> m_files; };
struct KExportResult { std::uint64_t m_generation=0; bool m_complete=false; std::vector<std::string> m_writtenFiles; std::vector<KExportFailure> m_failures; };
class IKExports { public: virtual ~IKExports()=default; virtual KResult<KExportDestination> authorize(const std::string& sessionId,const std::string& nativeSelection,std::uint64_t nowUnixMs)=0; virtual KResult<KExportResult> exportProject(const KExportCommand&,std::uint64_t nowUnixMs,std::stop_token stop={})=0; virtual void revokeSession(const std::string& sessionId) noexcept=0; };
std::shared_ptr<IKExports> createExports(std::shared_ptr<IKExportStore> store);
}
