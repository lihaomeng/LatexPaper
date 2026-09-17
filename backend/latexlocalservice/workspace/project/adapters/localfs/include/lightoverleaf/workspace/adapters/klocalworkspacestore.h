#pragma once
#include <lightoverleaf/workspace/outbound/ikworkspacestore.h>
#include <cstddef>
#include <memory>

namespace lightoverleaf::workspace
{
struct KLocalWorkspaceStoreOptions
{
    std::size_t m_maxEntries = 10000;
    std::size_t m_maxDepth = 32;
};
std::shared_ptr<IKWorkspaceStore> createLocalWorkspaceStore(const KLocalWorkspaceStoreOptions& options = {});
}
