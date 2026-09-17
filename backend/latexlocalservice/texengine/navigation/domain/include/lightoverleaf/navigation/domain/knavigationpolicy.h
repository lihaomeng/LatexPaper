#pragma once
#include <cstddef>
#include <string_view>
namespace lightoverleaf::navigation {
bool validNavigationFileId(std::string_view value);
inline constexpr std::size_t kMaxSyncPoints = 200000;
}
