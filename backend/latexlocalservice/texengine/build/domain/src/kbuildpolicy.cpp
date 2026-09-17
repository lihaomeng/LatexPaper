#include <lightoverleaf/build/domain/kbuildpolicy.h>

namespace lightoverleaf::build
{
bool validBuildToken(std::string_view value)
{
    if (value.empty() || value.size() > 64) return false;
    for (const unsigned char byte : value)
        if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '.')) return false;
    return true;
}
bool validBuildFileId(std::string_view value)
{
    if (value.empty() || value.size() > 4096 || value.front() == '/' || value.front() == '\\' ||
        value.find(':') != std::string_view::npos || value.find('\\') != std::string_view::npos ||
        value.find('\0') != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start <= value.size())
    {
        const std::size_t end = value.find('/', start);
        const std::string_view part = value.substr(start,
            (end == std::string_view::npos ? value.size() : end) - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}
}
