#include <lightoverleaf/rpc/krpcsession.h>
#include <algorithm>
#include <limits>

namespace lightoverleaf::rpc
{
namespace
{
constexpr std::size_t kCapacity = 64;
constexpr std::size_t kReplayWindow = 256;
constexpr std::uint64_t kSafeSequence = 9007199254740991ULL;
}
KRpcSession::~KRpcSession() { close(); }

KRequestStart KRpcSession::begin(const std::string& id, std::uint64_t nowMs, std::uint64_t timeoutMs)
{
    if (m_closed) return {KRequestAdmission::Closed, std::nullopt};
    if (!v2::validateRequestId(KValue{id}) || nowMs < m_nowMs || timeoutMs == 0 || timeoutMs > 360000 ||
        nowMs > std::numeric_limits<std::uint64_t>::max() - timeoutMs)
        return {KRequestAdmission::Invalid, std::nullopt};
    m_nowMs = nowMs;
    if (m_pending.contains(id) || std::find(m_recent.begin(), m_recent.end(), id) != m_recent.end())
        return {KRequestAdmission::Duplicate, std::nullopt};
    // Reserve a terminal event slot before accepting work. No completion is dropped.
    if (m_pending.size() + m_events.size() >= kCapacity || m_generation == kSafeSequence)
        return {KRequestAdmission::Exhausted, std::nullopt};
    KPending pending;
    pending.m_generation = ++m_generation;
    pending.m_deadline = nowMs + timeoutMs;
    const KRequestTicket ticket{id, pending.m_generation, pending.m_source.get_token()};
    m_pending.emplace(id, std::move(pending));
    return {KRequestAdmission::Accepted, ticket};
}

bool KRpcSession::stop(const KRequestTicket& ticket, KRequestOutcome reason)
{
    const auto found = m_pending.find(ticket.m_id);
    if (found == m_pending.end() || found->second.m_generation != ticket.m_generation ||
        found->second.m_source.get_token() != ticket.m_stop) return false;
    if (found->second.m_stopReason) return true;
    found->second.m_stopReason = reason;
    std::stop_source source = found->second.m_source;
    // request_stop can synchronously invoke a callback that completes/erases this request.
    source.request_stop();
    return true;
}
bool KRpcSession::cancel(const KRequestTicket& ticket) { return stop(ticket, KRequestOutcome::Cancelled); }

std::optional<KRequestOutcome> KRpcSession::stopReason(const KRequestTicket& ticket) const
{
    const auto found = m_pending.find(ticket.m_id);
    if (found == m_pending.end() || found->second.m_generation != ticket.m_generation ||
        found->second.m_source.get_token() != ticket.m_stop) return std::nullopt;
    return found->second.m_stopReason;
}

bool KRpcSession::cancelMessage(const KValue& message, const std::string& sessionId)
{
    if (!v2::validateCancellation(message)) return false;
    const auto& fields = std::get<KValue::KObject>(message.m_value);
    if (std::get<std::string>(fields.at("sessionId").m_value) != sessionId) return false;
    const auto found = m_pending.find(std::get<std::string>(fields.at("id").m_value));
    const auto generation = static_cast<std::uint64_t>(std::get<double>(fields.at("generation").m_value));
    if (found == m_pending.end() || found->second.m_generation != generation) return false;
    return cancel({found->first, generation, found->second.m_source.get_token()});
}

std::optional<KValue> KRpcSession::nextEventMessage(const std::string& sessionId)
{
    if (!v2::validateCancellationSessionId(KValue{sessionId}) || m_events.empty()) return std::nullopt;
    const KRequestEvent& event = m_events.front();
    const char* outcome = "failed";
    switch (event.m_outcome)
    {
    case KRequestOutcome::Succeeded: outcome = "succeeded"; break;
    case KRequestOutcome::Failed: break;
    case KRequestOutcome::Cancelled: outcome = "cancelled"; break;
    case KRequestOutcome::TimedOut: outcome = "timed-out"; break;
    }
    KValue message{KValue::KObject{{"version", KValue{2.0}}, {"event", KValue{std::string{"rpc.completed"}}},
        {"sessionId", KValue{sessionId}}, {"id", KValue{event.m_id}},
        {"generation", KValue{static_cast<double>(event.m_generation)}},
        {"sequence", KValue{static_cast<double>(event.m_sequence)}}, {"outcome", KValue{std::string{outcome}}}}};
    if (!v2::validateRequestEvent(message)) return std::nullopt;
    m_events.pop_front();
    return message;
}

bool KRpcSession::complete(const KRequestTicket& ticket, KRequestOutcome outcome)
{
    if (outcome != KRequestOutcome::Succeeded && outcome != KRequestOutcome::Failed &&
        outcome != KRequestOutcome::Cancelled && outcome != KRequestOutcome::TimedOut) return false;
    const auto found = m_pending.find(ticket.m_id);
    if (found == m_pending.end() || found->second.m_generation != ticket.m_generation ||
        found->second.m_source.get_token() != ticket.m_stop) return false;
    const KRequestOutcome terminal = found->second.m_stopReason.value_or(outcome);
    m_events.push_back({ticket.m_id, ticket.m_generation, ++m_sequence, terminal});
    m_recent.push_back(ticket.m_id);
    if (m_recent.size() > kReplayWindow) m_recent.pop_front();
    m_pending.erase(found);
    return true;
}
bool KRpcSession::expire(std::uint64_t nowMs)
{
    if (nowMs < m_nowMs) return false;
    m_nowMs = nowMs;
    // Restart search after stop callbacks: callbacks may remove other entries as well.
    for (;;)
    {
        const auto found = std::find_if(m_pending.begin(), m_pending.end(), [nowMs](const auto& entry)
        {
            return !entry.second.m_stopReason && entry.second.m_deadline <= nowMs;
        });
        if (found == m_pending.end()) return true;
        stop({found->first, found->second.m_generation, found->second.m_source.get_token()}, KRequestOutcome::TimedOut);
    }
}
std::optional<KRequestEvent> KRpcSession::nextEvent()
{
    if (m_events.empty()) return std::nullopt;
    KRequestEvent event = std::move(m_events.front());
    m_events.pop_front();
    return event;
}
void KRpcSession::close()
{
    m_closed = true;
    for (;;)
    {
        const auto found = std::find_if(m_pending.begin(), m_pending.end(), [](const auto& entry)
        {
            return !entry.second.m_stopReason;
        });
        if (found == m_pending.end()) return;
        stop({found->first, found->second.m_generation, found->second.m_source.get_token()}, KRequestOutcome::Cancelled);
    }
}
std::size_t KRpcSession::pendingCount() const { return m_pending.size(); }
}
