#include <lightoverleaf/preferences/domain/kpreferencespolicy.h>
namespace lightoverleaf::preferences {
bool validEngine(std::string_view v){return v=="pdflatex";}
bool validTexRoot(std::string_view v){return v.size()<=4096&&v.find('\0')==std::string_view::npos;}
bool validCompileMode(std::string_view v){return v=="manual"||v=="onSave"||v=="live";}
}
