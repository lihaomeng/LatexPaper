#pragma once
#include <string_view>
#include <cstddef>

namespace lightoverleaf::document
{
constexpr std::size_t kMaxDocumentBytes = 4 * 1024 * 1024;
bool validFileId(std::string_view fileId);
bool validUtf8(std::string_view content);
bool validRevision(std::string_view revision);
}
