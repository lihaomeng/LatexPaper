#include <lightoverleaf/navigation/domain/knavigationpolicy.h>
namespace lightoverleaf::navigation {
bool validNavigationFileId(std::string_view value) {
 if(value.empty() || value.size() > 4096 || value.front() == '/' || value.back() == '/' ||
  value.find("..") != std::string_view::npos || value.find("//") != std::string_view::npos ||
  value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos) return false;
 for(const unsigned char character:value) if(character<0x20 || character==0x7f) return false;
 return true;
}}
