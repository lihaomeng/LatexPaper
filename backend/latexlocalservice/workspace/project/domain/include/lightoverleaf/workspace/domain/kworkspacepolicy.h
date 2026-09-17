#pragma once
#include <cstddef>
#include <string_view>

namespace lightoverleaf::workspace
{
constexpr std::size_t kMaxWorkspaceEntries = 10000;
constexpr std::size_t kMaxWorkspaceSelectionBytes = 32768;
bool validWorkspaceText(std::string_view text);
bool validWorkspaceId(std::string_view id);
bool validWorkspaceFileId(std::string_view fileId);
}
