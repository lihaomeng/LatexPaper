#include <lightoverleaf/transport/kceftransport.h>
#include <lightoverleaf/system/inbound/ikgetcapabilities.h>
#include <iostream>
#include <string_view>
#include <deque>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"

int main()
{
    // Standalone test entry has no CefExecuteProcess/Bootstrap to select the ABI.
    const char* apiHash = cef_api_hash(CEF_API_VERSION, 0);
    if (!apiHash || std::string_view(apiHash) != CEF_API_HASH_PLATFORM) return 2;
    int failures = 0;
    const auto capabilities = lightoverleaf::createCapabilities({});
    const auto check = [&failures](bool valid, const char* name)
    {
        if (!valid) { std::cerr << name << '\n'; ++failures; }
    };
    const auto exchange = [&capabilities](const std::string& wire)
    {
        return lightoverleaf::handleNativeMessage(wire, *capabilities);
    };
    const auto ping = exchange(R"({"version":2,"id":"cef-test","method":"system.ping","params":{},"clientSequence":4})");
    check(ping.m_success && ping.m_payload.find("\"ok\":true") != std::string::npos, "CEF V2 ping");
    const auto available = exchange(R"({"version":2,"id":"cef-caps","method":"system.getCapabilities","params":{},"clientSequence":9007199254740991})");
    const auto decoded = CefParseJSON(available.m_payload, JSON_PARSER_RFC);
    check(available.m_success && available.m_payload.find("\"nativeFiles\":false") != std::string::npos &&
        decoded && decoded->GetDictionary() &&
        decoded->GetDictionary()->GetDouble("clientSequence") == 9007199254740991.0, "CEF capabilities and safe sequence");
    const auto unknown = exchange(R"({"version":2,"id":"unknown","method":"unknown.method","params":{},"clientSequence":0})");
    check(unknown.m_success && unknown.m_payload.find("METHOD_NOT_FOUND") != std::string::npos, "CEF structured method error");
    const auto extra = exchange(R"({"version":2,"id":"extra","method":"system.ping","params":{},"clientSequence":0,"extra":true})");
    check(extra.m_success && extra.m_payload.find("INVALID_ARGUMENT") != std::string::npos, "CEF structured argument error");
    const auto legacy = exchange(R"({"version":1,"id":"legacy","method":"system.ping","params":{},"clientSequence":0})");
    check(legacy.m_success && legacy.m_payload.find("\"version\":1") != std::string::npos, "V1 compatibility");
    check(!exchange("not-json").m_success, "invalid JSON");
    check(!exchange("null").m_success, "null JSON");
    check(!exchange(std::string(4097, ' ')).m_success, "byte bound");
    check(!exchange(R"({"version":3})").m_success, "unknown version");
    std::deque<std::function<void()>> tasks;
    const auto schedule = [&tasks](std::function<void()> task) { tasks.push_back(std::move(task)); return true; };
    auto endpoint = lightoverleaf::createNativeEndpoint(capabilities, schedule, "session-a");
    std::vector<std::string> events;
    int replies = 0;
    const auto subscribe = R"({"version":2,"id":"subscription","method":"rpc.subscribe","params":{},"clientSequence":0})";
    const auto wire = R"({"version":2,"id":"deferred","method":"system.ping","params":{},"clientSequence":0})";
    endpoint->request(1, subscribe, true, [&events](auto reply) {
        if (reply.m_payload.find("rpc.accepted") == std::string::npos) events.push_back(reply.m_payload);
    });
    check(events.size() == 1 && events[0].find("rpc.connected") != std::string::npos, "subscription handshake");
    endpoint->request(2, wire, false, [&replies](auto) { ++replies; });
    check(replies == 0 && tasks.size() == 1, "request actually deferred");
    endpoint->cancel(2);
    tasks.front()(); tasks.pop_front();
    check(replies == 0 && events.size() == 2 && events.back().find("cancelled") != std::string::npos,
        "cancel skips queued request and emits terminal");
    endpoint->request(3, wire, false, [&check](auto reply) { check(!reply.m_success && reply.m_payload == "DUPLICATE_REQUEST", "recent ID rejected"); });
    endpoint->request(4, R"({"version":2,"id":"success","method":"system.ping","params":{},"clientSequence":0})",
        false, [&replies, &check](auto reply) { ++replies; check(reply.m_success, "deferred success"); });
    tasks.front()(); tasks.pop_front();
    check(replies == 1 && events.size() == 3 && events.back().find("succeeded") != std::string::npos, "success terminal");
    endpoint->request(5, R"({"version":2,"id":"closed","method":"system.ping","params":{},"clientSequence":0})",
        false, [&replies](auto) { ++replies; });
    endpoint->close();
    endpoint.reset();
    tasks.front()(); tasks.pop_front();
    check(replies == 1 && events.size() == 3, "close destroys callbacks and late work is inert");
    auto rejected = lightoverleaf::createNativeEndpoint(capabilities, [](auto) { return false; }, "session-b");
    rejected->request(1, wire, false, [&check](auto reply)
        { check(!reply.m_success && reply.m_payload == "TRANSPORT_UNAVAILABLE", "post failure reported"); });
    auto bounded = lightoverleaf::createNativeEndpoint(capabilities, schedule, "session-c");
    for (int index = 0; index < 64; ++index)
    {
        bounded->request(index + 1, "{\"version\":2,\"id\":\"bounded-" + std::to_string(index) +
            "\",\"method\":\"system.ping\",\"params\":{},\"clientSequence\":0}", false, [](auto) {});
    }
    bounded->request(100, wire, false, [&check](auto reply)
        { check(!reply.m_success && reply.m_payload == "RESOURCE_EXHAUSTED", "64 pending bound"); });
    while (!tasks.empty()) { tasks.front()(); tasks.pop_front(); }
    bounded->request(101, wire, false, [&check](auto reply)
        { check(!reply.m_success && reply.m_payload == "RESOURCE_EXHAUSTED", "undrained events apply backpressure"); });
    bounded->request(102, subscribe, true, [](auto) {});
    bounded->request(103, wire, false, [&check](auto reply) { check(reply.m_success, "drain restores capacity"); });
    while (!tasks.empty()) { tasks.front()(); tasks.pop_front(); }
    auto cancellable = lightoverleaf::createNativeEndpoint(capabilities, schedule, "wire-session");
    std::string ticket;
    std::string terminal;
    cancellable->request(1, subscribe, true, [&ticket, &terminal](auto reply) {
        if (reply.m_payload.find("rpc.accepted") != std::string::npos) ticket = reply.m_payload;
        if (reply.m_payload.find("rpc.completed") != std::string::npos) terminal = reply.m_payload;
    });
    int cancelledReplies = 0;
    cancellable->request(2, wire, false, [&check, &cancelledReplies](auto reply) {
        ++cancelledReplies;
        check(!reply.m_success && reply.m_payload == "RPC_CANCELLED", "wire cancellation settles original query");
    });
    const auto admitted = CefParseJSON(ticket, JSON_PARSER_RFC)->GetDictionary();
    check(admitted->GetDouble("sequence") == 1, "accepted event has first outward sequence");
    check(admitted->GetString("id") == "deferred" && admitted->GetDouble("generation") == 1, "accepted ticket correlation");
    const auto cancellation = R"({"version":2,"method":"rpc.cancel","sessionId":"wire-session","id":"deferred","generation":1})";
    cancellable->request(3, R"({"version":2,"method":"rpc.cancel","sessionId":"wrong-session","id":"deferred","generation":1})",
        false, [&check](auto reply) { check(reply.m_payload.find("\"accepted\":false") != std::string::npos, "foreign session denied"); });
    cancellable->request(4, R"({"version":2,"method":"rpc.cancel","sessionId":"wire-session","id":"deferred","generation":2})",
        false, [&check](auto reply) { check(reply.m_payload.find("\"accepted\":false") != std::string::npos, "stale generation denied"); });
    cancellable->request(5, cancellation, false, [&check](auto reply) {
        check(reply.m_success && reply.m_payload.find("\"accepted\":true") != std::string::npos, "wire cancellation accepted");
    });
    check(terminal.empty() && cancelledReplies == 0, "acknowledgement is not a terminal");
    tasks.front()(); tasks.pop_front();
    check(cancelledReplies == 1 && terminal.find("cancelled") != std::string::npos, "wire cancellation terminal delivered");
    check(CefParseJSON(terminal, JSON_PARSER_RFC)->GetDictionary()->GetDouble("sequence") == 2,
        "terminal and accepted share an outward sequence");
    cancellable->request(6, cancellation, false, [&check](auto reply) {
        check(reply.m_payload.find("\"accepted\":false") != std::string::npos, "late cancellation rejected");
    });
    check(!lightoverleaf::createNativeEndpoint(capabilities, schedule, "../invalid"), "trusted session validation");
    return failures == 0 ? 0 : 1;
}
