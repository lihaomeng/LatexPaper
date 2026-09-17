#pragma once
#include <functional>
#include <cstdint>
#include <memory>
#include <string>

namespace lightoverleaf
{
struct KNativeMessageReply
{
    bool m_success = false;
    std::string m_payload;
};
using KNativeMessageHandler = std::function<KNativeMessageReply(const std::string&)>;
using KNativeReplyCallback = std::function<void(KNativeMessageReply)>;
// The dispatcher queues work on its owning thread; it must not run inline.
using KNativeSchedule = std::function<bool(std::function<void()>)>;
class IKNativeMessageEndpoint
{
public:
    virtual ~IKNativeMessageEndpoint() = default;
    virtual void request(std::int64_t queryId, const std::string& message, bool persistent,
        KNativeReplyCallback reply) = 0;
    virtual void cancel(std::int64_t queryId) = 0;
    virtual void close() = 0;
};
using KNativeEndpointFactory = std::function<std::shared_ptr<IKNativeMessageEndpoint>(KNativeSchedule, const std::string&)>;
}
