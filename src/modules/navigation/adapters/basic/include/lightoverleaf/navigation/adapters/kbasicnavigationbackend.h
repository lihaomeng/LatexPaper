#pragma once
#include <lightoverleaf/navigation/outbound/iknavigationbackends.h>
#include <memory>
namespace lightoverleaf::navigation { std::shared_ptr<IKSyncTexBackend> createBasicSyncTexBackend(); }
