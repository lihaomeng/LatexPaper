#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <krpcprotocol.h>

namespace lightoverleaf::rpc
{
enum class KRequestAdmission { Accepted, Invalid, Duplicate, Exhausted, Closed };
enum class KRequestOutcome { Succeeded, Failed, Cancelled, TimedOut };
struct KRequestTicket
{
    std::string m_id;
    std::uint64_t m_generation = 0;
    std::stop_token m_stop;
};
struct KRequestStart
{
    KRequestAdmission m_status = KRequestAdmission::Invalid;
    std::optional<KRequestTicket> m_ticket;
};
struct KRequestEvent
{
    std::string m_id;
    std::uint64_t m_generation = 0;
    std::uint64_t m_sequence = 0;
    KRequestOutcome m_outcome = KRequestOutcome::Failed;
};

// Owned and called by one dispatcher thread. Workers receive only stop_token;
// they must post completion to the dispatcher, never capture this object's address.
class KRpcSession
{
public:
    KRpcSession() = default;
    KRpcSession(const KRpcSession&) = delete;
    KRpcSession& operator=(const KRpcSession&) = delete;
    ~KRpcSession();
    KRequestStart begin(const std::string& id, std::uint64_t nowMs, std::uint64_t timeoutMs);
    bool cancel(const KRequestTicket& ticket);
    bool cancelMessage(const KValue& message, const std::string& sessionId);
    std::optional<KRequestOutcome> stopReason(const KRequestTicket& ticket) const;
    bool complete(const KRequestTicket& ticket, KRequestOutcome outcome);
    bool expire(std::uint64_t nowMs);
    std::optional<KRequestEvent> nextEvent();
    std::optional<KValue> nextEventMessage(const std::string& sessionId);
    void close();
    std::size_t pendingCount() const;

private:
    struct KPending
    {
        std::uint64_t m_generation = 0;
        std::uint64_t m_deadline = 0;
        std::stop_source m_source;
        std::optional<KRequestOutcome> m_stopReason;
    };
    bool stop(const KRequestTicket& ticket, KRequestOutcome reason);

private:
    std::map<std::string, KPending> m_pending;
    std::deque<std::string> m_recent;
    std::deque<KRequestEvent> m_events;
    std::uint64_t m_generation = 0;
    std::uint64_t m_sequence = 0;
    std::uint64_t m_nowMs = 0;
    bool m_closed = false;
};
}
