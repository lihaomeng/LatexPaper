#include <lightoverleaf/rpc/ikrpcendpoint.h>
#include <lightoverleaf/rpc/krpcsession.h>
#include <atomic>

namespace lightoverleaf::rpc
{
class KRpcEndpoint final : public IKRpcEndpoint, public std::enable_shared_from_this<KRpcEndpoint>
{
public:
    KRpcEndpoint(KEndpointDispatch dispatch, KEndpointResponseValidator responseValidator,
        KEndpointSchedule schedule, std::string sessionId, KEndpointClock clock,
        std::shared_ptr<const IKGetCapabilities> legacyCapabilities = {})
        : m_dispatch(std::move(dispatch)), m_responseValidator(std::move(responseValidator)),
          m_schedule(std::move(schedule)), m_sessionId(std::move(sessionId)),
          m_clock(std::move(clock)), m_legacyCapabilities(std::move(legacyCapabilities)) {}
    ~KRpcEndpoint() override { close(); }
    void request(std::int64_t queryId, KValue request, bool persistent, KEndpointCallback reply) override
    {
        if (!reply) return;
        if (m_closed) { reply({false, "TRANSPORT_UNAVAILABLE"}); return; }
        if (m_pending.contains(queryId) || (m_subscription && queryId == m_subscriptionId))
        {
            reply({false, "DUPLICATE_REQUEST"});
            return;
        }
        if (v2::validateCancellation(request))
        {
            if (persistent) { reply({false, "REQUEST_DENIED"}); return; }
            const bool accepted = m_session.cancelMessage(request, m_sessionId);
            const auto& fields = std::get<KValue::KObject>(request.m_value);
            auto response = fields;
            response.emplace("accepted", KValue{accepted});
            const KValue acknowledgement{std::move(response)};
            if (!v2::validateCancellationResponse(acknowledgement)) { reply({false, "INTERNAL_ERROR"}); return; }
            reply({true, acknowledgement});
            return;
        }
        if (!v2::validateRpcRequest(request))
        {
            if (persistent) reply({false, "REQUEST_DENIED"});
            else if (m_legacyCapabilities) reply(dispatchImmediate(request));
            else reply({false, "INVALID_ARGUMENT"});
            return;
        }
        const auto& object = std::get<KValue::KObject>(request.m_value);
        const auto& method = std::get<std::string>(object.at("method").m_value);
        if (persistent)
        {
            const auto& params = std::get<KValue::KObject>(object.at("params").m_value);
            if (method != "rpc.subscribe" || !params.empty() || m_subscription)
            {
                reply({false, "REQUEST_DENIED"});
                return;
            }
            const KValue connected{KValue::KObject{
                {"version", KValue{2.0}}, {"event", KValue{std::string("rpc.connected")}},
                {"sessionId", KValue{m_sessionId}}}};
            m_subscriptionId = queryId;
            m_subscription = reply;
            reply({true, connected});
            drain();
            return;
        }
        // Leave room for all 64 outstanding terminal events and the next admission.
        if (m_eventSequence > 9007199254740991ULL - 129) { reply({false, "RESOURCE_EXHAUSTED"}); return; }
        // Validated build requests need preparation/publication time as well as
        // the compiler budget. Ordinary RPCs retain the short deadline.
        std::uint64_t requestTimeoutMs = 5000;
        if (method == "build.start")
        {
            const auto& params = std::get<KValue::KObject>(object.at("params").m_value);
            const double budget = std::get<double>(params.at("timeoutMs").m_value);
            if (!(budget >= 1000 && budget <= 300000))
            {
                reply({false, "INVALID_ARGUMENT"});
                return;
            }
            requestTimeoutMs = static_cast<std::uint64_t>(budget) + 60000;
        }
        const auto started = m_session.begin(std::get<std::string>(object.at("id").m_value), m_clock(), requestTimeoutMs);
        if (!started.m_ticket)
        {
            reply({false, started.m_status == KRequestAdmission::Duplicate ? "DUPLICATE_REQUEST" : "RESOURCE_EXHAUSTED"});
            return;
        }
        m_pending.emplace(queryId, KPending{*started.m_ticket, std::move(request), std::move(reply)});
        if (m_subscription)
        {
            const KValue accepted{KValue::KObject{
                {"version", KValue{2.0}}, {"event", KValue{std::string("rpc.accepted")}},
                {"sessionId", KValue{m_sessionId}}, {"id", KValue{started.m_ticket->m_id}},
                {"generation", KValue{static_cast<double>(started.m_ticket->m_generation)}},
                {"sequence", KValue{static_cast<double>(++m_eventSequence)}}}};
            const auto subscriber = m_subscription;
            subscriber({true, accepted});
        }
        if (m_closed) return;
        const std::weak_ptr<KRpcEndpoint> weak = weak_from_this();
        bool scheduled = false;
        try
        {
            scheduled = m_schedule([weak, queryId] { if (const auto endpoint = weak.lock()) endpoint->run(queryId); });
        }
        catch (...)
        {
            scheduled = false;
        }
        if (!scheduled)
        {
            auto pending = std::move(m_pending.at(queryId));
            m_pending.erase(queryId);
            m_session.complete(pending.m_ticket, KRequestOutcome::Failed);
            drain();
            if (!m_closed && pending.m_reply) pending.m_reply({false, "TRANSPORT_UNAVAILABLE"});
        }
    }
    void cancel(std::int64_t queryId) override
    {
        if (m_subscription && queryId == m_subscriptionId) m_subscription = {};
        const auto found = m_pending.find(queryId);
        if (found == m_pending.end()) return;
        found->second.m_reply = {};
        m_session.cancel(found->second.m_ticket);
    }
    void close() override
    {
        if (m_closed) return;
        m_closed = true;
        m_subscription = {};
        m_session.close();
        // Only deferred dispatcher work exists here, not running worker threads.
        m_pending.clear();
    }

private:
    KEndpointReply dispatchImmediate(const KValue& request)
    {
        if (acceptPing(request)) return {true, request};
        const auto* object = std::get_if<KValue::KObject>(&request.m_value);
        if (!object || !object->contains("version") || !v2::validateRequestVersion(object->at("version")))
            return {false, "INVALID_ARGUMENT"};
        return {true, dispatchSystem(request, *m_legacyCapabilities)};
    }
    void drain()
    {
        while (!m_closed && m_subscription)
        {
            auto event = m_session.nextEventMessage(m_sessionId);
            if (!event) return;
            // One outward sequence across accepted and terminal notifications.
            std::get<KValue::KObject>(event->m_value).at("sequence") =
                KValue{static_cast<double>(++m_eventSequence)};
            const auto callback = m_subscription;
            callback({true, *event});
        }
    }
    void run(std::int64_t queryId)
    {
        const auto found = m_pending.find(queryId);
        if (m_closed || found == m_pending.end()) return;
        m_session.expire(m_clock());
        if (found->second.m_ticket.m_stop.stop_requested())
        {
            finishStopped(queryId);
            return;
        }
        if (found->second.m_started) return;
        found->second.m_started = true;
        KValue request = std::move(found->second.m_request);
        const KRequestTicket ticket = found->second.m_ticket;
        const std::weak_ptr<KRpcEndpoint> weak = weak_from_this();
        const auto completed = std::make_shared<std::atomic_bool>(false);
        const KEndpointCompletion completion = [weak, queryId, completed](KValue response) mutable
        {
            if (completed->exchange(true)) return;
            if (const auto endpoint = weak.lock()) endpoint->finish(queryId, std::move(response));
        };
        try
        {
            m_dispatch(std::move(request), ticket.m_stop, completion);
        }
        catch (...)
        {
            if (!completed->exchange(true)) finishFailed(queryId);
        }
    }
    void finish(std::int64_t queryId, KValue response)
    {
        const auto found = m_pending.find(queryId);
        if (m_closed || found == m_pending.end()) return;
        m_session.expire(m_clock());
        if (found->second.m_ticket.m_stop.stop_requested())
        {
            finishStopped(queryId);
            return;
        }
        auto pending = std::move(found->second);
        m_pending.erase(found);
        const bool valid = m_responseValidator(response);
        const KRequestOutcome outcome = valid ? responseOutcome(response) : KRequestOutcome::Failed;
        m_session.complete(pending.m_ticket, outcome);
        drain();
        if (!m_closed && pending.m_reply)
            pending.m_reply(valid ? KEndpointReply{true, std::move(response)} : KEndpointReply{false, "INTERNAL_ERROR"});
    }
    void finishFailed(std::int64_t queryId)
    {
        const auto found = m_pending.find(queryId);
        if (m_closed || found == m_pending.end()) return;
        auto pending = std::move(found->second);
        m_pending.erase(found);
        m_session.complete(pending.m_ticket, KRequestOutcome::Failed);
        drain();
        if (!m_closed && pending.m_reply) pending.m_reply({false, "INTERNAL_ERROR"});
    }
    void finishStopped(std::int64_t queryId)
    {
        const auto found = m_pending.find(queryId);
        if (m_closed || found == m_pending.end()) return;
        auto pending = std::move(found->second);
        m_pending.erase(found);
        const auto reason = m_session.stopReason(pending.m_ticket);
        m_session.complete(pending.m_ticket, KRequestOutcome::Cancelled);
        drain();
        if (!m_closed && pending.m_reply)
            pending.m_reply({false, reason == KRequestOutcome::TimedOut ? "RPC_TIMEOUT" : "RPC_CANCELLED"});
    }
    static KRequestOutcome responseOutcome(const KValue& response)
    {
        const auto* object = std::get_if<KValue::KObject>(&response.m_value);
        if (!object) return KRequestOutcome::Failed;
        const auto ok = object->find("ok");
        if (ok == object->end()) return KRequestOutcome::Failed;
        const bool* success = std::get_if<bool>(&ok->second.m_value);
        return success && *success ? KRequestOutcome::Succeeded : KRequestOutcome::Failed;
    }

private:
    struct KPending
    {
        KRequestTicket m_ticket;
        KValue m_request;
        KEndpointCallback m_reply;
        bool m_started = false;
    };
    KEndpointDispatch m_dispatch;
    KEndpointResponseValidator m_responseValidator;
    KEndpointSchedule m_schedule;
    const std::string m_sessionId;
    KEndpointClock m_clock;
    std::shared_ptr<const IKGetCapabilities> m_legacyCapabilities;
    KRpcSession m_session;
    std::map<std::int64_t, KPending> m_pending;
    KEndpointCallback m_subscription;
    std::int64_t m_subscriptionId = 0;
    std::uint64_t m_eventSequence = 0;
    bool m_closed = false;
};

std::shared_ptr<IKRpcEndpoint> createRpcEndpoint(KEndpointDispatch dispatch,
    KEndpointResponseValidator responseValidator, KEndpointSchedule schedule,
    const std::string& sessionId, KEndpointClock clock,
    std::shared_ptr<const IKGetCapabilities> legacyCapabilities)
{
    if (!dispatch || !responseValidator || !schedule || !clock ||
        !v2::validateCancellationSessionId(KValue{sessionId})) return nullptr;
    return std::make_shared<KRpcEndpoint>(std::move(dispatch), std::move(responseValidator),
        std::move(schedule), sessionId, std::move(clock), std::move(legacyCapabilities));
}

std::shared_ptr<IKRpcEndpoint> createRpcEndpoint(std::shared_ptr<const IKGetCapabilities> capabilities,
    KEndpointSchedule schedule, const std::string& sessionId, KEndpointClock clock)
{
    if (!capabilities || !schedule || !clock || !v2::validateCancellationSessionId(KValue{sessionId})) return nullptr;
    const auto retained = capabilities;
    KEndpointDispatch dispatch = [retained](KValue request, std::stop_token, KEndpointCompletion completion)
    {
        completion(dispatchSystem(request, *retained));
    };
    KEndpointResponseValidator validator = [](const KValue& response)
    {
        return v2::validatePingResponse(response) || v2::validateCapabilitiesResponse(response) ||
            v2::validateErrorResponse(response);
    };
    return std::make_shared<KRpcEndpoint>(std::move(dispatch), std::move(validator),
        std::move(schedule), sessionId, std::move(clock), std::move(capabilities));
}
}
