#include <lightoverleaf/preview/domain/kpreviewpolicy.h>

namespace lightoverleaf::preview
{
bool validArtifactToken(std::string_view value)
{
    if (value.empty() || value.size() > 96) return false;
    for (const unsigned char c : value)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '-' && c != '.') return false;
    return true;
}
bool validPdf(std::span<const std::uint8_t> value)
{
    return value.size() >= 5 && value[0] == '%' && value[1] == 'P' &&
        value[2] == 'D' && value[3] == 'F' && value[4] == '-';
}
}
