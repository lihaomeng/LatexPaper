#include <lightoverleaf/transport/kjsontransport.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace lightoverleaf
{
namespace
{
using KJson = nlohmann::json;
rpc::KValue decode(const KJson& input, unsigned int depth = 0)
{
    if (depth > 12) throw std::runtime_error("JSON_DEPTH");
    if (input.is_null()) return rpc::KValue{nullptr};
    if (input.is_boolean()) return rpc::KValue{input.get<bool>()};
    if (input.is_number())
    {
        const double number = input.get<double>();
        if (!std::isfinite(number)) throw std::runtime_error("JSON_NUMBER");
        return rpc::KValue{number};
    }
    if (input.is_string()) return rpc::KValue{input.get<std::string>()};
    if (input.is_array())
    {
        if (input.size() > 10000) throw std::runtime_error("JSON_ARRAY");
        rpc::KValue::KArray result;
        for (const auto& value : input) result.push_back(decode(value, depth + 1));
        return rpc::KValue{std::move(result)};
    }
    if (input.size() > 32) throw std::runtime_error("JSON_OBJECT");
    rpc::KValue::KObject result;
    for (const auto& [key, value] : input.items()) result.emplace(key, decode(value, depth + 1));
    return rpc::KValue{std::move(result)};
}
KJson encode(const rpc::KValue& input)
{
    if (const auto* value = std::get_if<bool>(&input.m_value)) return *value;
    if (const auto* value = std::get_if<double>(&input.m_value)) return *value;
    if (const auto* value = std::get_if<std::string>(&input.m_value)) return *value;
    if (const auto* value = std::get_if<rpc::KValue::KArray>(&input.m_value))
    {
        KJson result = KJson::array();
        for (const auto& item : *value) result.push_back(encode(item));
        return result;
    }
    if (const auto* value = std::get_if<rpc::KValue::KObject>(&input.m_value))
    {
        KJson result = KJson::object();
        for (const auto& [key, item] : *value) result[key] = encode(item);
        return result;
    }
    return nullptr;
}
class KJsonEndpoint final : public IKNativeMessageEndpoint
{
public:
    explicit KJsonEndpoint(std::shared_ptr<rpc::IKRpcEndpoint> endpoint) : m_endpoint(std::move(endpoint)) {}
    ~KJsonEndpoint() override { close(); }
    void request(std::int64_t id, const std::string& message, bool persistent, KNativeReplyCallback reply) override
    {
        if (!reply) return;
        auto value = parseJsonValue(message);
        if (!value) { reply({false, "INVALID_ARGUMENT"}); return; }
        m_endpoint->request(id, std::move(*value), persistent, [reply = std::move(reply)](rpc::KEndpointReply result)
        {
            if (!result.m_success)
            {
                const auto* error = std::get_if<std::string>(&result.m_payload.m_value);
                reply({false, error ? *error : "INTERNAL_ERROR"});
                return;
            }
            auto wire = writeJsonValue(result.m_payload);
            reply(wire ? KNativeMessageReply{true, std::move(*wire)} :
                KNativeMessageReply{false, "RESOURCE_EXHAUSTED"});
        });
    }
    void cancel(std::int64_t id) override { m_endpoint->cancel(id); }
    void close() override { m_endpoint->close(); }
private:
    std::shared_ptr<rpc::IKRpcEndpoint> m_endpoint;
};
}
std::optional<rpc::KValue> parseJsonValue(const std::string& wire)
{
    if (wire.empty() || wire.size() > kMaxPipeBytes) return {};
    try
    {
        // Bound nesting before a DOM can be allocated.
        return decode(KJson::parse(wire, [](int depth, KJson::parse_event_t, KJson&)
        {
            if (depth > 12) throw std::runtime_error("JSON_DEPTH");
            return true;
        }));
    }
    catch (...)
    {
        return {};
    }
}
std::optional<std::string> writeJsonValue(const rpc::KValue& value)
{
    try
    {
        std::string wire = encode(value).dump();
        if (wire.size() <= kMaxPipeBytes) return wire;
    }
    catch (...)
    {
    }
    return {};
}
std::shared_ptr<IKNativeMessageEndpoint> createJsonEndpoint(std::shared_ptr<rpc::IKRpcEndpoint> endpoint)
{
    return endpoint ? std::make_shared<KJsonEndpoint>(std::move(endpoint)) : nullptr;
}
}
