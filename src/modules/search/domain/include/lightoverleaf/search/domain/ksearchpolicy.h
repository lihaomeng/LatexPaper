#pragma once
#include <cstddef>
#include <string_view>

namespace lightoverleaf::search
{
inline constexpr std::size_t kMaxSearchQueryBytes = 512;
inline constexpr std::size_t kMaxSearchResults = 500;
inline constexpr std::size_t kMaxSearchDocuments = 10000;
inline constexpr std::size_t kMaxSearchDocumentBytes = 4 * 1024 * 1024;
bool validSearchText(std::string_view value);
bool validSearchDocumentText(std::string_view value);
}
