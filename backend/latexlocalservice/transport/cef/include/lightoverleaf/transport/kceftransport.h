#pragma once
#include <lightoverleaf/platform/knativemessage.h>

namespace lightoverleaf
{
class IKGetCapabilities;
namespace rpc
{
class IKRpcEndpoint;
}
std::shared_ptr<IKNativeMessageEndpoint> createNativeEndpoint(std::shared_ptr<rpc::IKRpcEndpoint> endpoint);
std::shared_ptr<IKNativeMessageEndpoint> createNativeEndpoint(std::shared_ptr<const IKGetCapabilities> capabilities,
    KNativeSchedule schedule, const std::string& sessionId);
KNativeMessageReply handleNativeMessage(const std::string& message, const IKGetCapabilities& capabilities);
}
