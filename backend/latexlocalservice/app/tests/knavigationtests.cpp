#include <lightoverleaf/navigation/inbound/iknavigation.h>
#include <lightoverleaf/navigation/outbound/iknavigationbackends.h>
#include <lightoverleaf/navigation/adapters/kbasicnavigationbackend.h>
#include <iostream>
using namespace lightoverleaf;using namespace lightoverleaf::navigation;
class KSource final:public IKSyncTexDataSource{public:KResult<KSyncTexArtifact>read(const std::string&)const override{
 const std::string v="LOLSYNC1\nF|main.tex|7|2|12|30\nF|main.tex|20|3|4|5\n";return KSyncTexArtifact{{'%','P','D','F','-'},std::vector<std::uint8_t>(v.begin(),v.end())};}};
int main(){int failures=0;auto check=[&](bool v,const char*m){if(!v){++failures;std::cerr<<m<<'\n';}};
 auto app=createNavigation(std::make_shared<KSource>(),createBasicSyncTexBackend());
 auto forward=app->forward("artifact-1",{"main.tex",8,1});check(std::holds_alternative<KPdfLocation>(forward)&&std::get<KPdfLocation>(forward).m_page==2,"forward nearest");
 auto reverse=app->reverse("artifact-1",{3,4,5});check(std::holds_alternative<KSourceLocation>(reverse)&&std::get<KSourceLocation>(reverse).m_line==20,"reverse nearest");
 check(std::holds_alternative<KError>(app->forward("artifact-1",{"../bad.tex",1,1})),"unsafe file rejected");
 check(std::holds_alternative<KError>(app->forward("artifact-1",{"C:/bad.tex",1,1})),"drive path rejected");
 check(std::holds_alternative<KError>(app->forward("artifact-1",{"a\\bad.tex",1,1})),"backslash rejected");
 check(std::holds_alternative<KError>(app->forward("artifact-1",{"bad\n.tex",1,1})),"control rejected");
 return failures?1:0;}
