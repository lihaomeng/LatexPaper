#include <lightoverleaf/navigation/adapters/kbasicnavigationbackend.h>
#include <lightoverleaf/navigation/domain/knavigationpolicy.h>
#include <cmath>
#include <sstream>
namespace lightoverleaf::navigation { namespace {
struct KPoint{std::string file;std::size_t line=0,page=0;double x=0,y=0;};
KResult<std::vector<KPoint>> parse(std::span<const std::uint8_t>d){
 std::string text(reinterpret_cast<const char*>(d.data()),d.size());if(!text.starts_with("LOLSYNC1\n"))return KError{KErrorCode::Unavailable,"navigation.unsupportedSyncTex",false};
 std::istringstream in(text);std::string row;std::getline(in,row);std::vector<KPoint>points;
 while(std::getline(in,row)){if(points.size()>=kMaxSyncPoints)return KError{KErrorCode::ResourceExhausted,"navigation.tooManyPoints",false};
  std::istringstream f(row);std::string k,file,line,page,x,y;if(!std::getline(f,k,'|')||k!="F"||!std::getline(f,file,'|')||!std::getline(f,line,'|')||!std::getline(f,page,'|')||!std::getline(f,x,'|')||!std::getline(f,y))continue;
  try{KPoint p{file,static_cast<std::size_t>(std::stoull(line)),static_cast<std::size_t>(std::stoull(page)),std::stod(x),std::stod(y)};if(validNavigationFileId(p.file)&&p.line&&p.page&&p.x>=0&&p.y>=0)points.push_back(std::move(p));}catch(...){}}
 if(points.empty())return KError{KErrorCode::NotFound,"navigation.mappingNotFound",false};return points;}
}
class KBasicBackend final:public IKSyncTexBackend{public:
 KResult<KBackendPdf> forward(const KSyncTexArtifact&a,const KBackendSource&s)const override{auto r=parse(a.m_syncTex);if(auto*e=std::get_if<KError>(&r))return *e;const KPoint*b=nullptr;std::size_t dist=static_cast<std::size_t>(-1);for(const auto&p:std::get<std::vector<KPoint>>(r))if(p.file==s.m_fileId){auto x=p.line>s.m_line?p.line-s.m_line:s.m_line-p.line;if(x<dist){dist=x;b=&p;}}if(!b)return KError{KErrorCode::NotFound,"navigation.mappingNotFound",false};return KBackendPdf{b->page,b->x,b->y};}
 KResult<KBackendSource> reverse(const KSyncTexArtifact&a,const KBackendPdf&q)const override{auto r=parse(a.m_syncTex);if(auto*e=std::get_if<KError>(&r))return *e;const KPoint*b=nullptr;double dist=1e300;for(const auto&p:std::get<std::vector<KPoint>>(r))if(p.page==q.m_page){double x=std::hypot(p.x-q.m_x,p.y-q.m_y);if(x<dist){dist=x;b=&p;}}if(!b)return KError{KErrorCode::NotFound,"navigation.mappingNotFound",false};return KBackendSource{b->file,b->line,1};}};
std::shared_ptr<IKSyncTexBackend> createBasicSyncTexBackend(){return std::make_shared<KBasicBackend>();}
}
