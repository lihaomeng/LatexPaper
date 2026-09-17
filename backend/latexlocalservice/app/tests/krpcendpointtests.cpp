#include <lightoverleaf/transport/kloopbacktransport.h>
#include <deque>
#include <iostream>
#include <vector>

using namespace lightoverleaf;
using namespace lightoverleaf::rpc;

namespace
{
KValue request(const std::string& id, const std::string& method = "system.ping")
{
    return KValue{KValue::KObject{{"version", KValue{2.0}}, {"id", KValue{id}},
        {"method", KValue{method}}, {"params", KValue{KValue::KObject{}}}, {"clientSequence", KValue{0.0}}}};
}
}

int main()
{
    int failures = 0;
    const auto check = [&failures](bool valid, const char* label)
    {
        if (!valid) { ++failures; std::cerr << label << '\n'; }
    };
    std::deque<std::function<void()>> queue;
    std::uint64_t now = 0;
    const auto schedule = [&queue](std::function<void()> work)
    {
        queue.push_back(std::move(work));
        return true;
    };
    const auto flush = [&queue]
    {
        while (!queue.empty())
        {
            auto work = std::move(queue.front());
            queue.pop_front();
            work();
        }
    };
    const auto endpoint = createLoopbackEndpoint(createCapabilities({}), schedule, "test-session", [&now] { return now; });
    std::vector<KValue> events;
    endpoint->request(1, request("subscription", "rpc.subscribe"), true, [&events](KEndpointReply reply)
    {
        if (reply.m_success) events.push_back(std::move(reply.m_payload));
    });
    check(events.size() == 1 && v2::validateConnectedEvent(events.front()), "connected");
    std::string error;
    endpoint->request(2, request("cancelled"), false, [&error](KEndpointReply reply)
    {
        if (!reply.m_success) error = std::get<std::string>(reply.m_payload.m_value);
    });
    check(events.size() == 2 && v2::validateAcceptedEvent(events.back()), "admission before execution");
    const auto accepted = std::get<KValue::KObject>(events.back().m_value);
    KValue cancellation{KValue::KObject{{"version", KValue{2.0}}, {"method", KValue{std::string("rpc.cancel")}},
        {"sessionId", accepted.at("sessionId")}, {"id", accepted.at("id")}, {"generation", accepted.at("generation")}}};
    bool acknowledged = false;
    endpoint->request(3, cancellation, false, [&acknowledged](KEndpointReply reply)
    {
        acknowledged = reply.m_success && v2::validateCancellationResponse(reply.m_payload) &&
            std::get<bool>(std::get<KValue::KObject>(reply.m_payload.m_value).at("accepted").m_value);
    });
    check(acknowledged && events.size() == 2 && error.empty(), "acknowledgement is not terminal");
    flush();
    check(error == "RPC_CANCELLED" && events.size() == 3 && v2::validateRequestEvent(events.back()), "cancelled terminal");
    check(std::get<double>(std::get<KValue::KObject>(events.back().m_value).at("sequence").m_value) == 2, "shared sequence");
    endpoint->request(4, request("timeout"), false, [&error](KEndpointReply reply)
    {
        if (!reply.m_success) error = std::get<std::string>(reply.m_payload.m_value);
    });
    now = 5000;
    flush();
    check(error == "RPC_TIMEOUT", "injected clock expires queued work");
    bool replied = false;
    endpoint->request(5, request("closed"), false, [&replied](KEndpointReply) { replied = true; });
    endpoint->close();
    const auto count = events.size();
    flush();
    check(!replied && events.size() == count, "close suppresses queued work and callbacks");

    const auto asyncCapabilities = createCapabilities({});
    std::vector<KEndpointCompletion> completions;
    std::vector<KValue> deferredResponses;
    std::vector<std::stop_token> stops;
    const auto asyncEndpoint = createRpcEndpoint(
        [&completions, &deferredResponses, &stops, asyncCapabilities](KValue input,
            std::stop_token stop, KEndpointCompletion completion)
        {
            stops.push_back(stop);
            deferredResponses.push_back(dispatchSystem(input, *asyncCapabilities));
            completions.push_back(std::move(completion));
        },
        [](const KValue& response)
        {
            return v2::validatePingResponse(response) || v2::validateCapabilitiesResponse(response) ||
                v2::validateErrorResponse(response);
        }, schedule, "async-session", [&now] { return now; });
    check(asyncEndpoint != nullptr, "injected dispatcher accepted");
    std::vector<KValue> asyncEvents;
    asyncEndpoint->request(10, request("async-subscription", "rpc.subscribe"), true,
        [&asyncEvents](KEndpointReply reply)
        {
            if (reply.m_success) asyncEvents.push_back(std::move(reply.m_payload));
        });
    bool asyncReplied = false;
    asyncEndpoint->request(11, request("async-success"), false,
        [&asyncReplied](KEndpointReply reply)
        {
            asyncReplied = reply.m_success && v2::validatePingResponse(reply.m_payload);
        });
    flush();
    check(!asyncReplied && completions.size() == 1 && !stops[0].stop_requested(),
        "injected dispatcher may complete later");
    KEndpointCompletion firstCompletion = completions[0];
    firstCompletion(deferredResponses[0]);
    check(asyncReplied && asyncEvents.size() == 3, "asynchronous completion settles on owner");
    firstCompletion(deferredResponses[0]);
    check(asyncEvents.size() == 3, "duplicate completion ignored");

    std::string asyncError;
    asyncEndpoint->request(12, request("async-cancel"), false,
        [&asyncError](KEndpointReply reply)
        {
            if (!reply.m_success) asyncError = std::get<std::string>(reply.m_payload.m_value);
        });
    flush();
    const auto acceptedAsync = std::get<KValue::KObject>(asyncEvents.back().m_value);
    KValue asyncCancellation{KValue::KObject{{"version", KValue{2.0}},
        {"method", KValue{std::string("rpc.cancel")}}, {"sessionId", acceptedAsync.at("sessionId")},
        {"id", acceptedAsync.at("id")}, {"generation", acceptedAsync.at("generation")}}};
    asyncEndpoint->request(13, asyncCancellation, false, [](KEndpointReply) {});
    check(stops.size() == 2 && stops[1].stop_requested(), "running dispatcher receives cancellation");
    completions[1](deferredResponses[1]);
    check(asyncError == "RPC_CANCELLED" && asyncEvents.size() == 5,
        "late business result cannot override cancellation");

    asyncEndpoint->request(14, request("async-timeout"), false,
        [&asyncError](KEndpointReply reply)
        {
            if (!reply.m_success) asyncError = std::get<std::string>(reply.m_payload.m_value);
        });
    flush();
    now += 5000;
    completions[2](deferredResponses[2]);
    check(asyncError == "RPC_TIMEOUT" && stops[2].stop_requested(),
        "late completion observes endpoint timeout");

    bool closeReply = false;
    asyncEndpoint->request(15, request("async-close"), false,
        [&closeReply](KEndpointReply) { closeReply = true; });
    flush();
    asyncEndpoint->close();
    check(stops.size() == 4 && stops[3].stop_requested(), "close requests business stop");
    completions[3](deferredResponses[3]);
    check(!closeReply, "completion after close is inert");
    check(!createRpcEndpoint({}, [](const KValue&) { return true; }, schedule,
        "valid", [&now] { return now; }), "missing dispatcher rejected");
    check(!createRpcEndpoint([](KValue, std::stop_token, KEndpointCompletion) {}, {}, schedule,
        "valid", [&now] { return now; }), "missing response validator rejected");
    check(!createLoopbackEndpoint(createCapabilities({}), schedule, "", [&now] { return now; }), "invalid session rejected");
    check(!createLoopbackEndpoint(createCapabilities({}), schedule, "valid", {}), "missing clock rejected");
    return failures == 0 ? 0 : 1;
}
