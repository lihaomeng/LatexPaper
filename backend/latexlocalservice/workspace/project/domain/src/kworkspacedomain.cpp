#include <lightoverleaf/workspace/domain/kworkspacepolicy.h>
#include <string>

namespace lightoverleaf::workspace
{
bool validWorkspaceText(std::string_view text)
{
    std::size_t index = 0;
    while (index < text.size())
    {
        const unsigned char first = static_cast<unsigned char>(text[index++]);
        if (first == 0) return false;
        if (first < 0x80) continue;
        unsigned int remaining = 0;
        unsigned int scalar = 0;
        unsigned int minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { remaining = 1; scalar = first & 0x1F; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { remaining = 2; scalar = first & 0x0F; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { remaining = 3; scalar = first & 0x07; minimum = 0x10000; }
        else return false;
        if (text.size() - index < remaining) return false;
        for (unsigned int offset = 0; offset < remaining; ++offset)
        {
            const unsigned char next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xC0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3F);
        }
        if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) return false;
    }
    return true;
}

bool validWorkspaceId(std::string_view id)
{
    if (id.empty() || id.size() > 128) return false;
    for (const unsigned char byte : id)
        if (!(byte >= 'a' && byte <= 'z') && !(byte >= 'A' && byte <= 'Z') &&
            !(byte >= '0' && byte <= '9') && byte != '-') return false;
    return true;
}

bool validWorkspaceFileId(std::string_view fileId)
{
    if (fileId.empty() || fileId.size() > 1024 || !validWorkspaceText(fileId)) return false;
    std::size_t start = 0;
    while (start < fileId.size())
    {
        const std::size_t separator = fileId.find('/', start);
        const std::size_t end = separator == std::string_view::npos ? fileId.size() : separator;
        const std::string_view segment = fileId.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == ".." || segment.back() == '.' || segment.back() == ' ')
            return false;
        for (const unsigned char byte : segment)
            if (byte < 32 || byte == 127 || byte == ':' || byte == '\\' || byte == '*' || byte == '?' ||
                byte == '"' || byte == '<' || byte == '>' || byte == '|') return false;
        std::string stem(segment.substr(0, segment.find('.')));
        for (char& byte : stem) if (byte >= 'a' && byte <= 'z') byte = static_cast<char>(byte - 'a' + 'A');
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CLOCK$" ||
            stem == "CONIN$" || stem == "CONOUT$") return false;
        if ((stem.starts_with("COM") || stem.starts_with("LPT")) &&
            ((stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9') ||
             stem.substr(3) == "\xc2\xb9" || stem.substr(3) == "\xc2\xb2" || stem.substr(3) == "\xc2\xb3")) return false;
        if (separator == std::string_view::npos) return true;
        start = separator + 1;
    }
    return false;
}
}
