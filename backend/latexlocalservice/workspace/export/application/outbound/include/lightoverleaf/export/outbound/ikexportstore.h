#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>
namespace lightoverleaf::exporting {
struct KExportStoreDestination { std::string m_token; std::string m_displayName; std::uint64_t m_expiresAtUnixMs=0; };
struct KExportStoreFile { std::string m_fileId; std::string m_content; };
struct KExportStoreFailure { std::string m_fileId; std::string m_code; };
struct KExportStoreCommand { std::string m_sessionId; std::string m_destinationToken; std::uint64_t m_generation=0; std::uint64_t m_nowUnixMs=0; std::vector<KExportStoreFile> m_files; };
struct KExportStoreResult { std::uint64_t m_generation=0; bool m_complete=false; std::vector<std::string> m_writtenFiles; std::vector<KExportStoreFailure> m_failures; };
class IKExportStore { public: virtual ~IKExportStore()=default; virtual KResult<KExportStoreDestination> authorize(const std::string& sessionId,const std::string& nativeSelection,std::uint64_t expiresAtUnixMs)=0; virtual KResult<KExportStoreResult> write(const KExportStoreCommand&,std::stop_token)=0; virtual void revokeSession(const std::string& sessionId) noexcept=0; };
}
