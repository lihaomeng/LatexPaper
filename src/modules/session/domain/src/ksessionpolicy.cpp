#include <lightoverleaf/session/domain/ksessionpolicy.h>
namespace lightoverleaf::session { bool validSessionText(std::string_view v,std::size_t n){return v.size()<=n&&v.find('\0')==std::string_view::npos;} }
