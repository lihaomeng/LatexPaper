#include <lightoverleaf/preferences/domain/kpreferencespolicy.h>
namespace lightoverleaf::preferences {
bool validEngine(std::string_view v){return v=="pdflatex"||v=="xelatex"||v=="lualatex";}
bool validTexRoot(std::string_view v){return v.size()<=4096&&v.find('\0')==std::string_view::npos;}
}
