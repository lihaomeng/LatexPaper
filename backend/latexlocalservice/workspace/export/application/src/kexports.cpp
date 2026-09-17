#include <lightoverleaf/export/inbound/ikexports.h>
#include <lightoverleaf/export/outbound/ikexportstore.h>
#include <lightoverleaf/export/domain/kexportpolicy.h>
#include <algorithm>
#include <set>
namespace lightoverleaf::exporting {
class KExports final:public IKExports { public: explicit KExports(std::shared_ptr<IKExportStore> store):m_store(std::move(store)){}
 KResult<KExportDestination> authorize(const std::string& sessionId,const std::string& nativeSelection,std::uint64_t now) override {
  if(!validExportToken(sessionId)||nativeSelection.empty()||nativeSelection.size()>4096||now>9007199254140991ULL)return KError{KErrorCode::InvalidArgument,"export.invalidDestination",false};
  auto result=m_store->authorize(sessionId,nativeSelection,now+600000);if(const auto* error=std::get_if<KError>(&result))return *error;auto value=std::get<KExportStoreDestination>(std::move(result));
  if(!validExportToken(value.m_token)||value.m_displayName.empty()||value.m_displayName.size()>256||value.m_expiresAtUnixMs<=now)return KError{KErrorCode::Internal,"export.invalidAuthorization",false};
  return KExportDestination{std::move(value.m_token),std::move(value.m_displayName),value.m_expiresAtUnixMs};
 }
 KResult<KExportResult> exportProject(const KExportCommand& command,std::uint64_t now,std::stop_token stop) override {
  if(!validExportToken(command.m_sessionId)||!validExportToken(command.m_destinationToken)||command.m_generation==0||command.m_generation>9007199254740991ULL||command.m_files.empty()||command.m_files.size()>kMaxExportFiles)return KError{KErrorCode::InvalidArgument,"export.invalidArgument",false};
  std::size_t bytes=0;std::set<std::string> ids;KExportStoreCommand mapped; mapped.m_sessionId=command.m_sessionId;mapped.m_destinationToken=command.m_destinationToken;mapped.m_generation=command.m_generation;mapped.m_nowUnixMs=now;mapped.m_files.reserve(command.m_files.size());
  for(const auto& file:command.m_files){if(!validExportFileId(file.m_fileId))return KError{KErrorCode::InvalidArgument,"export.invalidFile",false};if(file.m_content.size()>kMaxExportFileBytes||file.m_content.size()>kMaxExportBytes-bytes)return KError{KErrorCode::ResourceExhausted,"export.tooLarge",false};std::string key=file.m_fileId;std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):static_cast<char>(c);});if(!ids.insert(std::move(key)).second)return KError{KErrorCode::Conflict,"export.duplicateFile",false};bytes+=file.m_content.size();mapped.m_files.push_back({file.m_fileId,file.m_content});}
  auto result=m_store->write(mapped,stop);if(const auto* error=std::get_if<KError>(&result))return *error;auto value=std::get<KExportStoreResult>(std::move(result));KExportResult projected;projected.m_generation=value.m_generation;projected.m_complete=value.m_complete;projected.m_writtenFiles=std::move(value.m_writtenFiles);for(auto& failure:value.m_failures)projected.m_failures.push_back({std::move(failure.m_fileId),std::move(failure.m_code)});return projected;
 }
 void revokeSession(const std::string& sessionId) noexcept override {m_store->revokeSession(sessionId);}
 private: std::shared_ptr<IKExportStore> m_store;
};
std::shared_ptr<IKExports> createExports(std::shared_ptr<IKExportStore> store){return store?std::make_shared<KExports>(std::move(store)):nullptr;}
}
