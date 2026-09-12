#include <lightoverleaf/navigation/inbound/iknavigation.h>
#include <lightoverleaf/navigation/outbound/iknavigationbackends.h>
#include <lightoverleaf/navigation/domain/knavigationpolicy.h>
namespace lightoverleaf::navigation {
class KNavigation final:public IKNavigation { public:
 KNavigation(std::shared_ptr<IKSyncTexDataSource>s,std::shared_ptr<IKSyncTexBackend>b):m_source(std::move(s)),m_backend(std::move(b)){}
 KResult<KPdfLocation> forward(const std::string&id,const KSourceLocation&s)const override {
  if(id.empty()||id.size()>96||!validNavigationFileId(s.m_fileId)||!s.m_line||!s.m_column)return KError{KErrorCode::InvalidArgument,"navigation.invalidArgument",false};
  auto data=m_source->read(id);if(auto*e=std::get_if<KError>(&data))return *e;
  auto mapped=m_backend->forward(std::get<KSyncTexArtifact>(data),{s.m_fileId,s.m_line,s.m_column});if(auto*e=std::get_if<KError>(&mapped))return *e;
  auto v=std::get<KBackendPdf>(mapped);return KPdfLocation{v.m_page,v.m_x,v.m_y};}
 KResult<KSourceLocation> reverse(const std::string&id,const KPdfLocation&p)const override {
  if(id.empty()||id.size()>96||!p.m_page||p.m_x<0||p.m_y<0)return KError{KErrorCode::InvalidArgument,"navigation.invalidArgument",false};
  auto data=m_source->read(id);if(auto*e=std::get_if<KError>(&data))return *e;
  auto mapped=m_backend->reverse(std::get<KSyncTexArtifact>(data),{p.m_page,p.m_x,p.m_y});if(auto*e=std::get_if<KError>(&mapped))return *e;
  auto v=std::get<KBackendSource>(mapped);return KSourceLocation{std::move(v.m_fileId),v.m_line,v.m_column};}
 private:std::shared_ptr<IKSyncTexDataSource>m_source;std::shared_ptr<IKSyncTexBackend>m_backend;};
std::shared_ptr<IKNavigation> createNavigation(std::shared_ptr<IKSyncTexDataSource>s,std::shared_ptr<IKSyncTexBackend>b){return s&&b?std::make_shared<KNavigation>(std::move(s),std::move(b)):nullptr;}
}
