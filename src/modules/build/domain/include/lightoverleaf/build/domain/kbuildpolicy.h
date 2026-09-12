#pragma once
#include <cstddef>
#include <string_view>

namespace lightoverleaf::build
{
inline constexpr std::size_t kMaxBuildLogBytes = 1024 * 1024;
inline constexpr std::size_t kMaxBuildSnapshotBytes = 64 * 1024 * 1024;
inline constexpr std::size_t kMaxPdfArtifactBytes = 64 * 1024 * 1024;
inline constexpr std::size_t kMaxSyncTexArtifactBytes = 16 * 1024 * 1024;
inline constexpr unsigned int kMinBuildTimeoutMs = 1000;
inline constexpr unsigned int kMaxBuildTimeoutMs = 300000;
bool validBuildToken(std::string_view value);
bool validBuildFileId(std::string_view value);
}
