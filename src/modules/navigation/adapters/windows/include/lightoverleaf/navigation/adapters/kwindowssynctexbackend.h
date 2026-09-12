#pragma once

#include <lightoverleaf/navigation/outbound/iknavigationbackends.h>
#include <memory>
#include <string>
#include <vector>

namespace lightoverleaf::navigation
{
struct KWindowsSyncTexOptions
{
    std::string m_cacheRootUtf8;
    std::vector<std::string> m_texRootsUtf8;
    std::string m_executableUtf8;
};

KResult<std::shared_ptr<IKSyncTexBackend>> createWindowsSyncTexBackend(
    const KWindowsSyncTexOptions& options);
}
