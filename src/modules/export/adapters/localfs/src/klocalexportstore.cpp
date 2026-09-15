#include <lightoverleaf/export/adapters/klocalexportstore.h>
#include <lightoverleaf/export/domain/kexportpolicy.h>
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
namespace lightoverleaf::exporting {
namespace fs=std::filesystem; namespace {
KError storageError(const char* key="export.storageFailure"){return {KErrorCode::Unavailable,key,true};}
std::wstring wide(const std::string& value){if(value.empty())return {};const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(count<=0)return {};std::wstring result(static_cast<std::size_t>(count),L'\0');return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count)==count?result:std::wstring{};}
std::string utf8(const fs::path& value){const auto encoded=value.filename().u8string();return {reinterpret_cast<const char*>(encoded.data()),encoded.size()};}
std::string token(){std::array<unsigned char,32> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)return {};constexpr char digits[]="0123456789abcdef";std::string result(bytes.size()*2,'0');for(std::size_t i=0;i<bytes.size();++i){result[i*2]=digits[bytes[i]>>4];result[i*2+1]=digits[bytes[i]&15];}return result;}
bool emptyDirectory(const fs::path& path){std::error_code error;if(!fs::is_directory(path,error)||error)return false;return fs::directory_iterator(path,error)==fs::directory_iterator()&&!error;}
bool writeNew(const fs::path& path,const std::string& content){HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;std::size_t offset=0;bool ok=true;while(offset<content.size()){const DWORD count=static_cast<DWORD>(std::min<std::size_t>(content.size()-offset,1U<<20));DWORD written=0;if(!WriteFile(file,content.data()+offset,count,&written,nullptr)||written!=count){ok=false;break;}offset+=written;}if(ok)ok=FlushFileBuffers(file)!=FALSE;CloseHandle(file);return ok;}
struct KAuthorization{std::string session;fs::path destination;std::uint64_t expires=0;bool consumed=false;};
}
class KLocalExportStore final:public IKExportStore { public:
 KResult<KExportStoreDestination> authorize(const std::string& sessionId,const std::string& nativeSelection,std::uint64_t expires) override {
  const std::wstring selected=wide(nativeSelection);if(selected.empty())return KError{KErrorCode::InvalidEncoding,"export.invalidDestination",false};std::error_code error;fs::path destination=fs::weakly_canonical(fs::path(selected),error);if(error||destination.empty()||destination.parent_path().empty()||destination.parent_path()==destination||!emptyDirectory(destination))return KError{KErrorCode::Conflict,"export.destinationNotEmpty",false};const DWORD attributes=GetFileAttributesW(destination.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0)return KError{KErrorCode::InvalidArgument,"export.unsafeDestination",false};
  std::string id=token();if(id.empty())return storageError("export.tokenFailure");std::string display=utf8(destination);if(display.empty())display="export";{std::scoped_lock lock(m_mutex);m_authorizations.emplace(id,KAuthorization{sessionId,std::move(destination),expires,false});}return KExportStoreDestination{id,std::move(display),expires};
 }
 KResult<KExportStoreResult> write(const KExportStoreCommand& command,std::stop_token stop) override {
  fs::path destination;{std::scoped_lock lock(m_mutex);const auto found=m_authorizations.find(command.m_destinationToken);if(found==m_authorizations.end()||found->second.consumed)return KError{KErrorCode::Conflict,"export.tokenConsumed",false};if(found->second.session!=command.m_sessionId)return KError{KErrorCode::Conflict,"export.tokenSessionMismatch",false};if(command.m_nowUnixMs>found->second.expires)return KError{KErrorCode::Conflict,"export.tokenExpired",false};found->second.consumed=true;destination=found->second.destination;}
  if(stop.stop_requested())return KError{KErrorCode::Cancelled,"export.cancelled",true};if(!emptyDirectory(destination))return KExportStoreResult{command.m_generation,false,{},{{"","export.destinationChanged"}}};const fs::path staging=destination.parent_path()/(L".lightoverleaf-export-"+wide(command.m_destinationToken.substr(0,16)));std::error_code error;if(fs::exists(staging,error)||!fs::create_directory(staging,error)||error)return KExportStoreResult{command.m_generation,false,{},{{"","export.stagingFailure"}}};struct Cleanup{fs::path value;bool keep=false;~Cleanup(){if(!keep){std::error_code error;fs::remove_all(value,error);}}}cleanup{staging};
  for(const auto& file:command.m_files){if(stop.stop_requested())return KError{KErrorCode::Cancelled,"export.cancelled",true};if(!validExportFileId(file.m_fileId))return KError{KErrorCode::InvalidArgument,"export.invalidFile",false};const std::wstring relative=wide(file.m_fileId);if(relative.empty())return KError{KErrorCode::InvalidEncoding,"export.invalidFile",false};const fs::path target=staging/fs::path(relative);fs::create_directories(target.parent_path(),error);if(error||!writeNew(target,file.m_content))return KExportStoreResult{command.m_generation,false,{},{{file.m_fileId,"export.writeFailure"}}};}
  if(!emptyDirectory(destination)||!RemoveDirectoryW(destination.c_str()))return KExportStoreResult{command.m_generation,false,{},{{"","export.destinationChanged"}}};if(!MoveFileExW(staging.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)){CreateDirectoryW(destination.c_str(),nullptr);return KExportStoreResult{command.m_generation,false,{},{{"","export.commitFailure"}}};}cleanup.keep=true;KExportStoreResult result;result.m_generation=command.m_generation;result.m_complete=true;for(const auto& file:command.m_files)result.m_writtenFiles.push_back(file.m_fileId);return result;
 }
 void revokeSession(const std::string& sessionId) noexcept override {std::scoped_lock lock(m_mutex);for(auto it=m_authorizations.begin();it!=m_authorizations.end();){if(it->second.session==sessionId)it=m_authorizations.erase(it);else ++it;}}
 private:std::mutex m_mutex;std::map<std::string,KAuthorization> m_authorizations;
};
std::shared_ptr<IKExportStore> createLocalExportStore(){return std::make_shared<KLocalExportStore>();}
}
