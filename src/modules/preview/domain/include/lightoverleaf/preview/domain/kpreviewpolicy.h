#pragma once
#include <cstddef>
#include <span>
#include <string_view>
#include <cstdint>

namespace lightoverleaf::preview
{
inline constexpr std::size_t kMaxPreviewPdfBytes = 64 * 1024 * 1024;
inline constexpr std::size_t kMaxPreviewSyncTexBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kMaxPreviewChunkBytes = 512 * 1024;
bool validArtifactToken(std::string_view value);
bool validPdf(std::span<const std::uint8_t> value);
}
