#include <lightoverleaf/search/domain/ksearchpolicy.h>

namespace lightoverleaf::search
{
namespace
{
bool validUtf8(std::string_view value)
{
    for (std::size_t index = 0; index < value.size();)
    {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        if (first == 0) return false;
        if (first <= 0x7f) { ++index; continue; }
        std::size_t count = first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (count == 0 || index + count > value.size()) return false;
        for (std::size_t offset = 1; offset < count; ++offset)
        {
            const unsigned char next = static_cast<unsigned char>(value[index + offset]);
            if (next < 0x80 || next > 0xbf) return false;
        }
        const unsigned char second = static_cast<unsigned char>(value[index + 1]);
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second > 0x9f) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second > 0x8f)) return false;
        index += count;
    }
    return true;
}
}
bool validSearchText(std::string_view value)
{
    return !value.empty() && value.size() <= kMaxSearchQueryBytes && validUtf8(value);
}
bool validSearchDocumentText(std::string_view value)
{
    return value.size() <= kMaxSearchDocumentBytes && validUtf8(value);
}
}
