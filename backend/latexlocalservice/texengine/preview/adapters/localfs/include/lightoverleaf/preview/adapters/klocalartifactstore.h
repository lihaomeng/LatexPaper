#pragma once
#include <lightoverleaf/preview/outbound/ikartifactstore.h>

namespace lightoverleaf::preview
{
KResult<std::shared_ptr<IKArtifactStore>> createLocalArtifactStore(const std::string& cacheRootUtf8);
}
