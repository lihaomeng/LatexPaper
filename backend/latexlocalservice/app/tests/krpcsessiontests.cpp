#include <lightoverleaf/rpc/krpcsession.h>
#include <iostream>
#include <limits>
#include <vector>

using namespace lightoverleaf::rpc;

int main()
{
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name)
    {
        if (!condition) { std::cerr << name << '\n'; ++failures; }
    };
    KRpcSession session;
    check(session.begin("bad/id", 0, 10).m_status == KRequestAdmission::Invalid, "invalid id");
    check(session.begin("id", 0, 0).m_status == KRequestAdmission::Invalid, "zero timeout");
    check(session.begin("id", std::numeric_limits<std::uint64_t>::max(), 1).m_status == KRequestAdmission::Invalid, "overflow");
    const auto first = session.begin("id", 0, 10).m_ticket.value();
    KRpcSession otherSession;
    const auto other = otherSession.begin("id", 0, 10).m_ticket.value();
    check(other.m_generation == first.m_generation, "same numeric ticket across sessions");
    check(!session.cancel(other) && !session.complete(other, KRequestOutcome::Succeeded), "foreign ticket rejected");
    check(!first.m_stop.stop_requested(), "foreign cancellation has no effect");
    check(session.begin("id", 0, 10).m_status == KRequestAdmission::Duplicate, "in-flight duplicate");
    check(session.cancel(first) && first.m_stop.stop_requested(), "cooperative stop");
    check(session.pendingCount() == 1 && !session.nextEvent(), "cancellation is not worker acknowledgement");
    check(session.complete(first, KRequestOutcome::Succeeded), "acknowledge after cancellation");
    const auto cancelled = session.nextEvent().value();
    check(cancelled.m_sequence == 1 && cancelled.m_outcome == KRequestOutcome::Cancelled, "cancel wins linearization");
    check(!session.complete(first, KRequestOutcome::Succeeded), "late duplicate completion ignored");
    check(session.begin("id", 0, 10).m_status == KRequestAdmission::Duplicate, "completed replay rejected");
    const auto second = session.begin("second", 2, 10).m_ticket.value();
    check(!session.expire(1), "clock regression rejected");
    check(session.expire(11) && !second.m_stop.stop_requested(), "before deadline");
    check(session.expire(12) && second.m_stop.stop_requested(), "deadline stop");
    session.complete(second, KRequestOutcome::Failed);
    check(session.nextEvent()->m_outcome == KRequestOutcome::TimedOut, "timeout outcome");

    const auto reentrant = session.begin("reentrant", 12, 10).m_ticket.value();
    std::stop_callback callback(reentrant.m_stop, [&session, reentrant]
    {
        session.complete(reentrant, KRequestOutcome::Cancelled);
    });
    check(session.cancel(reentrant) && session.pendingCount() == 0, "synchronous callback erasure safe");
    session.nextEvent();

    std::vector<KRequestTicket> tickets;
    for (int index = 0; index < 64; ++index)
        tickets.push_back(session.begin("bounded-" + std::to_string(index), 12, 100).m_ticket.value());
    check(session.begin("overflow", 12, 10).m_status == KRequestAdmission::Exhausted, "capacity bounded");
    for (const auto& ticket : tickets) session.complete(ticket, KRequestOutcome::Succeeded);
    check(session.begin("overflow", 12, 10).m_status == KRequestAdmission::Exhausted, "undrained events backpressure");
    std::uint64_t sequence = 3;
    while (const auto event = session.nextEvent()) check(event->m_sequence == ++sequence, "ordered event sequence");
    check(sequence == 67, "no terminal events dropped");
    for (int index = 0; index < 260; ++index)
    {
        const auto ticket = session.begin("rotate-" + std::to_string(index), 12, 10).m_ticket.value();
        session.complete(ticket, KRequestOutcome::Succeeded);
        session.nextEvent();
    }
    const auto reused = session.begin("id", 12, 10).m_ticket.value();
    check(reused.m_generation != first.m_generation && !session.cancel(first), "old ticket cannot cancel reused id");
    check(!session.complete(first, KRequestOutcome::Succeeded), "stale completion rejected");
    session.close();
    check(reused.m_stop.stop_requested(), "close requests stop");
    check(session.begin("closed", 12, 10).m_status == KRequestAdmission::Closed, "closed rejects admission");
    session.complete(reused, KRequestOutcome::Succeeded);
    check(session.nextEvent()->m_outcome == KRequestOutcome::Cancelled, "close acknowledgement");
    std::stop_token orphan;
    {
        KRpcSession temporary;
        orphan = temporary.begin("destruction", 0, 10).m_ticket->m_stop;
    }
    check(orphan.stop_requested(), "destructor requests cooperative stop");
    KRpcSession wireSession;
    const auto wireTicket = wireSession.begin("wire", 0, 10).m_ticket.value();
    KValue cancellation{KValue::KObject{{"version", KValue{2.0}}, {"method", KValue{std::string{"rpc.cancel"}}},
        {"sessionId", KValue{std::string{"live"}}}, {"id", KValue{std::string{"wire"}}}, {"generation", KValue{1.0}}}};
    check(!wireSession.cancelMessage(cancellation, "different"), "wire cancellation session isolation");
    check(!wireSession.cancelMessage(KValue{nullptr}, "live"), "invalid cancellation rejected");
    check(wireSession.cancelMessage(cancellation, "live") && wireTicket.m_stop.stop_requested(), "wire cancellation requests stop");
    wireSession.complete(wireTicket, KRequestOutcome::Succeeded);
    check(!wireSession.nextEventMessage("bad/session"), "invalid subscriber cannot consume event");
    const auto wireEvent = wireSession.nextEventMessage("live");
    check(wireEvent && v2::validateRequestEvent(*wireEvent), "terminal event matches wire schema");
    check(!wireSession.nextEventMessage("live"), "terminal event consumed exactly once");
    KRpcSession reasons;
    KRpcSession otherReasons;
    const auto timedTicket = reasons.begin("reason", 0, 10).m_ticket.value();
    const auto otherTicket = otherReasons.begin("reason", 0, 10).m_ticket.value();
    check(!reasons.stopReason(timedTicket), "no stop reason before stopping");
    reasons.expire(10);
    reasons.cancel(timedTicket);
    check(reasons.stopReason(timedTicket) == KRequestOutcome::TimedOut, "timeout is not overwritten by later cancel");
    check(!reasons.stopReason(otherTicket), "foreign ticket cannot read stop reason");
    reasons.complete(timedTicket, KRequestOutcome::Succeeded);
    check(!reasons.stopReason(timedTicket), "completed ticket has no live stop reason");
    const auto cancelledTicket = reasons.begin("cancel-first", 10, 10).m_ticket.value();
    reasons.cancel(cancelledTicket);
    reasons.expire(20);
    check(reasons.stopReason(cancelledTicket) == KRequestOutcome::Cancelled, "cancel is not overwritten by later timeout");
    return failures == 0 ? 0 : 1;
}
