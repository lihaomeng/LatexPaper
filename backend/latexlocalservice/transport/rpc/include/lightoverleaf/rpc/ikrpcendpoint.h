#pragma once
#include <lightoverleaf/rpc/krpccore.h>
#include <functional>
#include <memory>
#include <stop_token>

namespace lightoverleaf::rpc
{
struct KEndpointReply
{
public:
    KEndpointReply(bool success, KValue payload) : m_success(success), m_payload(std::move(payload)) {}
    KEndpointReply(bool success, const char* error) : m_success(success), m_payload{std::string(error ? error : "INTERNAL_ERROR")} {}

public:
    bool m_success = false;
    KValue m_payload;
};
using KEndpointCallback = std::function<void(KEndpointReply)>;
// Queue on the owning dispatcher; never execute inline.
using KEndpointSchedule = std::function<bool(std::function<void()>)>;
using KEndpointClock = std::function<std::uint64_t()>;
using KEndpointCompletion = std::function<void(KValue)>;
// Dispatch starts on the endpoint owner thread. Asynchronous implementations
// must post completion back to that same owner thread and invoke it at most once.
using KEndpointDispatch = std::function<void(KValue, std::stop_token, KEndpointCompletion)>;
using KEndpointResponseValidator = std::function<bool(const KValue&)>;
class IKRpcEndpoint
{
public:
    virtual ~IKRpcEndpoint() = default;
    virtual void request(std::int64_t queryId, KValue request, bool persistent, KEndpointCallback reply) = 0;
    virtual void cancel(std::int64_t queryId) = 0;
    virtual void close() = 0;
};
std::shared_ptr<IKRpcEndpoint> createRpcEndpoint(KEndpointDispatch dispatch,
    KEndpointResponseValidator responseValidator, KEndpointSchedule schedule,
    const std::string& sessionId, KEndpointClock clock,
    std::shared_ptr<const IKGetCapabilities> legacyCapabilities = {});
std::shared_ptr<IKRpcEndpoint> createRpcEndpoint(std::shared_ptr<const IKGetCapabilities> capabilities,
    KEndpointSchedule schedule, const std::string& sessionId, KEndpointClock clock);
}
