#include <lightoverleaf/preview/inbound/ikpreviewartifacts.h>
#include <lightoverleaf/preview/adapters/klocalartifactstore.h>
#include <windows.h>
#include <filesystem>
#include <iostream>
using namespace lightoverleaf; using namespace lightoverleaf::preview; namespace fs=std::filesystem;
namespace { std::string utf8(const fs::path&p){auto v=p.u8string();return {reinterpret_cast<const char*>(v.data()),v.size()};} }
int main(){
 int failures=0;auto check=[&](bool v,const char*m){if(!v){++failures;std::cerr<<m<<'\n';}};
 fs::path root=fs::temp_directory_path()/("LightOverLeaf-preview-"+std::to_string(GetCurrentProcessId()));std::error_code e;fs::remove_all(root,e);
 auto made=createLocalArtifactStore(utf8(root));check(std::holds_alternative<std::shared_ptr<IKArtifactStore>>(made),"store factory");
 if(auto*store=std::get_if<std::shared_ptr<IKArtifactStore>>(&made)){auto app=createPreviewArtifacts(*store);
  const std::string pdf="%PDF-1.4\n0123456789",sync="LOLSYNC1\nF|main.tex|7|2|12|30\n";
  auto published=app->publish("job-1",{reinterpret_cast<const std::uint8_t*>(pdf.data()),pdf.size()},{reinterpret_cast<const std::uint8_t*>(sync.data()),sync.size()});
  check(std::holds_alternative<KPreviewDescriptor>(published),"publish");
  if(auto*d=std::get_if<KPreviewDescriptor>(&published)){auto first=app->readPdf(d->m_artifactId,0,7);auto second=app->readPdf(d->m_artifactId,7,100);
   check(std::holds_alternative<KPreviewChunk>(first)&&std::get<KPreviewChunk>(first).m_totalBytes==pdf.size(),"bounded first chunk");
   check(std::holds_alternative<KPreviewChunk>(second)&&std::get<KPreviewChunk>(second).m_bytes.size()==pdf.size()-7,"bounded final chunk");
   check(std::holds_alternative<std::vector<std::uint8_t>>(app->readSyncTex(d->m_artifactId)),"synctex read");}
  const std::string bad="not pdf";check(std::holds_alternative<KError>(app->publish("job-2",{reinterpret_cast<const std::uint8_t*>(bad.data()),bad.size()},{})),"invalid pdf rejected");
  check(std::holds_alternative<KError>((*store)->readPdf("../escape")),"unsafe artifact id rejected");
  for(int index=0;index<25;++index){const std::string job="retention-"+std::to_string(index);
   check(std::holds_alternative<KPreviewDescriptor>(app->publish(job,{reinterpret_cast<const std::uint8_t*>(pdf.data()),pdf.size()},{})),"retention publish");}
  std::size_t directories=0;for(const auto&entry:fs::directory_iterator(root))if(entry.is_directory())++directories;
  check(directories<=20,"artifact directory retention");
 }
 fs::remove_all(root,e);return failures?1:0;
}
