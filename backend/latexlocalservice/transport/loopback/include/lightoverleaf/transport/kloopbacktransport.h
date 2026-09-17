#pragma once
#include <lightoverleaf/rpc/krpccore.h>
#include <lightoverleaf/rpc/ikrpcendpoint.h>

namespace lightoverleaf
{
std::shared_ptr<rpc::IKRpcEndpoint> createLoopbackEndpoint(std::shared_ptr<const IKGetCapabilities> capabilities,
    rpc::KEndpointSchedule schedule, const std::string& sessionId, rpc::KEndpointClock clock);
class KLoopbackTransport
{
public:
    explicit KLoopbackTransport(const IKGetCapabilities& capabilities) : m_capabilities(capabilities) {}
    rpc::KValue exchange(const rpc::KValue& request) const;

private:
    // The injected provider must outlive this synchronous transport.
    const IKGetCapabilities& m_capabilities;
};
}
