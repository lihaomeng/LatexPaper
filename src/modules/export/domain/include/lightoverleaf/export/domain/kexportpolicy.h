#pragma once
#include <cstddef>
#include <string_view>
namespace lightoverleaf::exporting {
inline constexpr std::size_t kMaxExportFiles=32;
inline constexpr std::size_t kMaxExportFileBytes=4U*1024U*1024U;
inline constexpr std::size_t kMaxExportBytes=16U*1024U*1024U;
bool validExportToken(std::string_view value);
bool validExportFileId(std::string_view value);
}
