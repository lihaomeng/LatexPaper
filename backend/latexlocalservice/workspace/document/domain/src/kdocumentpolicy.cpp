#include <lightoverleaf/document/domain/kdocumentpolicy.h>
#include <string>

namespace lightoverleaf::document
{
bool validUtf8(std::string_view text)
{
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto first = static_cast<unsigned char>(text[index++]);
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
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xC0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3F);
        }
        if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) return false;
    }
    return true;
}

bool validFileId(std::string_view fileId)
{
    if (fileId.empty() || fileId.size() > 1024 || !validUtf8(fileId)) return false;
    std::size_t start = 0;
    while (start < fileId.size())
    {
        const auto separator = fileId.find('/', start);
        const auto end = separator == std::string_view::npos ? fileId.size() : separator;
        const auto segment = fileId.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == ".." || segment.back() == '.' || segment.back() == ' ') return false;
        if (segment.size() > 255) return false;
        std::string stem(segment.substr(0, segment.find('.')));
        for (char& letter : stem)
            if (letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            stem == "CONIN$" || stem == "CONOUT$" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9'))
            return false;
        for (const unsigned char byte : segment)
            if (byte < 32 || byte == 127 || byte == ':' || byte == '\\' || byte == '*' || byte == '?' ||
                byte == '"' || byte == '<' || byte == '>' || byte == '|') return false;
        if (separator == std::string_view::npos) return true;
        start = separator + 1;
    }
    return false;
}

bool validRevision(std::string_view revision)
{
    if (revision.empty() || revision.size() > 128) return false;
    for (const unsigned char byte : revision)
        if (!(byte >= 'a' && byte <= 'z') && !(byte >= 'A' && byte <= 'Z') &&
            !(byte >= '0' && byte <= '9') && byte != '-') return false;
    return true;
}
}
