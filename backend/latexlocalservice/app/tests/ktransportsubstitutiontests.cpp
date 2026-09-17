#include <lightoverleaf/transport/kloopbacktransport.h>
#include <lightoverleaf/transport/kceftransport.h>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include <deque>
#include <iostream>
#include <string_view>
#include <vector>

using namespace lightoverleaf;
using namespace lightoverleaf::rpc;

namespace
{
// Test-only codec: fixtures contain bounded primitive objects, never arrays.
CefRefPtr<CefValue> encode(const KValue& value)
{
    const auto result = CefValue::Create();
    if (const auto* text = std::get_if<std::string>(&value.m_value)) result->SetString(*text);
    else if (const auto* number = std::get_if<double>(&value.m_value)) result->SetDouble(*number);
    else if (const auto* flag = std::get_if<bool>(&value.m_value)) result->SetBool(*flag);
    else if (const auto* object = std::get_if<KValue::KObject>(&value.m_value))
    {
        const auto dictionary = CefDictionaryValue::Create();
        for (const auto& [key, field] : *object) dictionary->SetValue(key, encode(field));
        result->SetDictionary(dictionary);
    }
    else result->SetNull();
    return result;
}
KValue decode(CefRefPtr<CefValue> value)
{
    if (!value) return {nullptr};
    if (value->GetType() == VTYPE_STRING) return {value->GetString().ToString()};
    if (value->GetType() == VTYPE_BOOL) return {value->GetBool()};
    if (value->GetType() == VTYPE_INT) return {static_cast<double>(value->GetInt())};
    if (value->GetType() == VTYPE_DOUBLE) return {value->GetDouble()};
    KValue::KObject object;
    if (value->GetType() == VTYPE_DICTIONARY)
    {
        const auto dictionary = value->GetDictionary();
        CefDictionaryValue::KeyList keys;
        dictionary->GetKeys(keys);
        for (const auto& key : keys) object.emplace(key.ToString(), decode(dictionary->GetValue(key)));
    }
    return {std::move(object)};
}
KValue request(const std::string& id, const std::string& method = "system.ping")
{
    return {KValue::KObject{{"version", KValue{2.0}}, {"id", KValue{id}}, {"method", KValue{method}},
        {"clientSequence", KValue{0.0}}, {"params", KValue{KValue::KObject{}}}}};
}
}

int main()
{
    const char* hash = cef_api_hash(CEF_API_VERSION, 0);
    if (!hash || std::string_view(hash) != CEF_API_HASH_PLATFORM) return 2;
    int failures = 0;
    for (bool native : {false, true})
    {
        const auto check = [&](bool valid, const char* label)
        {
            if (!valid) { ++failures; std::cerr << (native ? "CEF: " : "Loopback: ") << label << '\n'; }
        };
        std::deque<std::function<void()>> queue;
        const auto schedule = [&queue](std::function<void()> work) { queue.push_back(std::move(work)); return true; };
        const auto capabilities = createCapabilities({});
        const auto loopback = createLoopbackEndpoint(capabilities, schedule, "shared", [] { return 0; });
        const auto cef = createNativeEndpoint(capabilities, schedule, "shared");
        const auto send = [&](std::int64_t id, KValue value, bool persistent, KEndpointCallback reply)
        {
            if (!native) { loopback->request(id, std::move(value), persistent, std::move(reply)); return; }
            cef->request(id, CefWriteJSON(encode(value), JSON_WRITER_DEFAULT).ToString(), persistent,
                [reply = std::move(reply)](KNativeMessageReply result)
                {
                    reply({result.m_success, result.m_success ?
                        decode(CefParseJSON(result.m_payload, JSON_PARSER_RFC)) : KValue{result.m_payload}});
                });
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
        std::vector<KValue> events;
        send(1, request("subscribe", "rpc.subscribe"), true, [&](KEndpointReply reply) { events.push_back(reply.m_payload); });
        check(events.size() == 1 && v2::validateConnectedEvent(events.front()), "connected");
        std::string error;
        send(2, request("work"), false, [&](KEndpointReply reply)
        {
            if (!reply.m_success) error = std::get<std::string>(reply.m_payload.m_value);
        });
        check(events.size() == 2 && v2::validateAcceptedEvent(events.back()) && error.empty(), "deferred admission");
        const auto ticket = std::get<KValue::KObject>(events.back().m_value);
        KValue cancel{KValue::KObject{{"version", KValue{2.0}}, {"method", KValue{std::string("rpc.cancel")}},
            {"sessionId", ticket.at("sessionId")}, {"id", ticket.at("id")}, {"generation", ticket.at("generation")}}};
        bool acknowledged = false;
        send(3, cancel, false, [&](KEndpointReply reply)
        {
            acknowledged = reply.m_success && v2::validateCancellationResponse(reply.m_payload) &&
                std::get<bool>(std::get<KValue::KObject>(reply.m_payload.m_value).at("accepted").m_value);
        });
        check(acknowledged && events.size() == 2, "cancellation acknowledgement");
        flush();
        check(error == "RPC_CANCELLED" && events.size() == 3 && v2::validateRequestEvent(events.back()), "cancellation terminal");
        check(std::get<std::string>(std::get<KValue::KObject>(events.back().m_value).at("outcome").m_value) == "cancelled", "outcome");
        check(std::get<double>(std::get<KValue::KObject>(events.back().m_value).at("sequence").m_value) == 2, "sequence");
        bool success = false;
        send(4, request("next"), false, [&](KEndpointReply reply) { success = reply.m_success && v2::validatePingResponse(reply.m_payload); });
        flush();
        check(success && events.size() == 5, "success after cancellation");
        send(5, request("late"), false, [&](KEndpointReply) { check(false, "late callback"); });
        loopback->close();
        cef->close();
        const auto count = events.size();
        flush();
        check(events.size() == count, "closed events suppressed");
    }
    return failures == 0 ? 0 : 1;
}
