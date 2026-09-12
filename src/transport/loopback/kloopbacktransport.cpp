#include <lightoverleaf/transport/kloopbacktransport.h>

namespace lightoverleaf
{
std::shared_ptr<rpc::IKRpcEndpoint> createLoopbackEndpoint(std::shared_ptr<const IKGetCapabilities> capabilities,
    rpc::KEndpointSchedule schedule, const std::string& sessionId, rpc::KEndpointClock clock)
{
    return rpc::createRpcEndpoint(std::move(capabilities), std::move(schedule), sessionId, std::move(clock));
}
rpc::KValue KLoopbackTransport::exchange(const rpc::KValue& request) const
{
    return rpc::dispatchSystem(request, m_capabilities);
}
}
