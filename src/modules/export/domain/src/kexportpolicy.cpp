#include <lightoverleaf/export/domain/kexportpolicy.h>
namespace lightoverleaf::exporting {
bool validExportToken(std::string_view value){if(value.empty()||value.size()>96)return false;for(unsigned char c:value)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.'))return false;return true;}
bool validExportFileId(std::string_view value){if(value.empty()||value.size()>4096||value.front()=='/'||value.back()=='/'||value.find('\\')!=value.npos||value.find(':')!=value.npos||value.find('\0')!=value.npos)return false;std::size_t start=0;while(start<value.size()){const auto end=value.find('/',start);const auto part=value.substr(start,end==value.npos?value.size()-start:end-start);if(part.empty()||part=="."||part==".."||part.back()==' '||part.back()=='.')return false;start=end==value.npos?value.size():end+1;}return true;}
}
