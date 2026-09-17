#include <lightoverleaf/transport/kceftransport.h>
#include <lightoverleaf/rpc/krpccore.h>
#include <lightoverleaf/rpc/ikrpcendpoint.h>
#include <chrono>
#ifdef LOL_WITH_CEF
#include "include/cef_parser.h"

namespace lightoverleaf
{
namespace
{
constexpr std::size_t kMaxRpcWireBytes = 32 * 1024 * 1024;
bool convertValue(CefRefPtr<CefValue> input, rpc::KValue& output, unsigned int depth)
{
    if (!input || depth > 8) return false;
    switch (input->GetType())
    {
    case VTYPE_NULL: output.m_value = nullptr; return true;
    case VTYPE_BOOL: output.m_value = input->GetBool(); return true;
    case VTYPE_INT: output.m_value = static_cast<double>(input->GetInt()); return true;
    case VTYPE_DOUBLE: output.m_value = input->GetDouble(); return true;
    case VTYPE_STRING: output.m_value = input->GetString().ToString(); return true;
    case VTYPE_DICTIONARY:
    {
        const auto dictionary = input->GetDictionary();
        CefDictionaryValue::KeyList keys;
        if (!dictionary || dictionary->GetSize() > 32 || !dictionary->GetKeys(keys)) return false;
        rpc::KValue::KObject object;
        for (const auto& key : keys)
        {
            rpc::KValue value;
            if (!convertValue(dictionary->GetValue(key), value, depth + 1)) return false;
            object.emplace(key.ToString(), std::move(value));
        }
        output.m_value = std::move(object);
        return true;
    }
    case VTYPE_LIST:
    {
        const auto list = input->GetList();
        if (!list || list->GetSize() > 10000) return false;
        rpc::KValue::KArray values;
        values.reserve(list->GetSize());
        for (std::size_t index = 0; index < list->GetSize(); ++index)
        {
            rpc::KValue value;
            if (!convertValue(list->GetValue(index), value, depth + 1)) return false;
            values.push_back(std::move(value));
        }
        output.m_value = std::move(values);
        return true;
    }
    default: return false;
    }
}
CefRefPtr<CefValue> encodeValue(const rpc::KValue& input)
{
    CefRefPtr<CefValue> output = CefValue::Create();
    if (const auto* boolean = std::get_if<bool>(&input.m_value)) output->SetBool(*boolean);
    else if (const auto* number = std::get_if<double>(&input.m_value)) output->SetDouble(*number);
    else if (const auto* text = std::get_if<std::string>(&input.m_value)) output->SetString(*text);
    else if (const auto* object = std::get_if<rpc::KValue::KObject>(&input.m_value))
    {
        CefRefPtr<CefDictionaryValue> dictionary = CefDictionaryValue::Create();
        for (const auto& [key, value] : *object) dictionary->SetValue(key, encodeValue(value));
        output->SetDictionary(dictionary);
    }
    else if (const auto* array = std::get_if<rpc::KValue::KArray>(&input.m_value))
    {
        CefRefPtr<CefListValue> list = CefListValue::Create();
        list->SetSize(array->size());
        for (std::size_t index = 0; index < array->size(); ++index)
            list->SetValue(index, encodeValue(array->at(index)));
        output->SetList(list);
    }
    else output->SetNull();
    return output;
}
}
class KCefEndpoint final : public IKNativeMessageEndpoint
{
public:
    explicit KCefEndpoint(std::shared_ptr<rpc::IKRpcEndpoint> endpoint) : m_endpoint(std::move(endpoint)) {}
    ~KCefEndpoint() override { close(); }
    void request(std::int64_t queryId, const std::string& message, bool persistent, KNativeReplyCallback reply) override
    {
        if (!reply) return;
        rpc::KValue request;
        if (message.empty() || message.size() > kMaxRpcWireBytes ||
            !convertValue(CefParseJSON(message, JSON_PARSER_RFC), request, 0))
        {
            reply({false, "INVALID_ARGUMENT"});
            return;
        }
        m_endpoint->request(queryId, std::move(request), persistent, [reply = std::move(reply)](rpc::KEndpointReply result)
        {
            if (!result.m_success)
            {
                const auto* error = std::get_if<std::string>(&result.m_payload.m_value);
                reply({false, error ? *error : "INTERNAL_ERROR"});
                return;
            }
            const std::string wire = CefWriteJSON(encodeValue(result.m_payload), JSON_WRITER_DEFAULT).ToString();
            if (wire.empty() || wire.size() > kMaxRpcWireBytes) reply({false, "RESOURCE_EXHAUSTED"});
            else reply({true, wire});
        });
    }
    void cancel(std::int64_t queryId) override { m_endpoint->cancel(queryId); }
    void close() override { m_endpoint->close(); }

private:
    std::shared_ptr<rpc::IKRpcEndpoint> m_endpoint;
};

std::shared_ptr<IKNativeMessageEndpoint> createNativeEndpoint(std::shared_ptr<rpc::IKRpcEndpoint> endpoint)
{
    if (!endpoint) return nullptr;
    return std::make_shared<KCefEndpoint>(std::move(endpoint));
}

std::shared_ptr<IKNativeMessageEndpoint> createNativeEndpoint(std::shared_ptr<const IKGetCapabilities> capabilities,
    KNativeSchedule schedule, const std::string& sessionId)
{
    auto endpoint = rpc::createRpcEndpoint(std::move(capabilities), std::move(schedule), sessionId, []
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    });
    if (!endpoint) return nullptr;
    return createNativeEndpoint(std::move(endpoint));
}

KNativeMessageReply handleNativeMessage(const std::string& message, const IKGetCapabilities& capabilities)
{
    if (message.empty() || message.size() > 4096) return {false, "INVALID_ARGUMENT"};
    try
    {
        const auto parsed = CefParseJSON(message, JSON_PARSER_RFC);
        rpc::KValue request;
        if (!convertValue(parsed, request, 0)) return {false, "INVALID_ARGUMENT"};
        if (rpc::acceptPing(request)) return {true, CefWriteJSON(parsed, JSON_WRITER_DEFAULT).ToString()};
        const auto* object = std::get_if<rpc::KValue::KObject>(&request.m_value);
        if (!object || !object->contains("version") || !rpc::v2::validateRequestVersion(object->at("version")))
            return {false, "INVALID_ARGUMENT"};
        const rpc::KValue response = rpc::dispatchSystem(request, capabilities);
        if (!rpc::v2::validatePingResponse(response) && !rpc::v2::validateCapabilitiesResponse(response) &&
            !rpc::v2::validateErrorResponse(response)) return {false, "INTERNAL_ERROR"};
        return {true, CefWriteJSON(encodeValue(response), JSON_WRITER_DEFAULT).ToString()};
    }
    catch (...)
    {
        return {false, "INTERNAL_ERROR"};
    }
}
}
#endif
