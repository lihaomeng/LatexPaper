#include <lightoverleaf/rpc/krpccore.h>

namespace lightoverleaf::rpc
{
bool acceptPing(const KValue& request)
{
    return validatePing(request);
}

KValue makeErrorResponse(const KValue& request, const std::string& code)
{
    KValue::KObject response{{"version", KValue{2.0}}, {"id", KValue{std::string{"invalid-request"}}},
        {"clientSequence", KValue{0.0}}, {"ok", KValue{false}}};
    const auto* object = std::get_if<KValue::KObject>(&request.m_value);
    if (object)
    {
        const auto id = object->find("id");
        const auto sequence = object->find("clientSequence");
        if (id != object->end() && v2::validateRequestId(id->second)) response["id"] = id->second;
        if (sequence != object->end() && v2::validateRequestClientSequence(sequence->second))
            response["clientSequence"] = sequence->second;
    }
    response.emplace("error", KValue{KValue::KObject{{"code", KValue{code}},
        {"messageKey", KValue{std::string{"rpc.error"}}}}});
    KValue result{std::move(response)};
    if (v2::validateErrorResponse(result)) return result;
    return makeErrorResponse(request, "INTERNAL_ERROR");
}

KValue dispatchSystem(const KValue& request, const IKGetCapabilities& capabilities)
{
    KValue::KObject response{{"version", KValue{2.0}}, {"id", KValue{std::string{"invalid-request"}}},
        {"clientSequence", KValue{0.0}}, {"ok", KValue{false}}};
    const auto* object = std::get_if<KValue::KObject>(&request.m_value);
    if (object)
    {
        const auto id = object->find("id");
        const auto sequence = object->find("clientSequence");
        if (id != object->end() && v2::validateRequestId(id->second)) response["id"] = id->second;
        if (sequence != object->end() && v2::validateRequestClientSequence(sequence->second))
            response["clientSequence"] = sequence->second;
    }
    if (!v2::validateRequest(request)) return makeErrorResponse(request, "INVALID_ARGUMENT");
    const std::string& method = std::get<std::string>(object->at("method").m_value);
    const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
    if ((method == "system.getCapabilities" || method == "system.ping") && !params.empty())
        return makeErrorResponse(request, "INVALID_ARGUMENT");
    KValue::KObject result;
    if (method == "system.getCapabilities")
    {
        try
        {
            const KRuntimeCapabilities available = capabilities.get();
            result = {{"nativeFiles", KValue{available.m_nativeFiles}}, {"build", KValue{available.m_build}},
                {"pdf", KValue{available.m_pdf}}, {"syncTex", KValue{available.m_syncTex}}};
        }
        catch (...)
        {
            return makeErrorResponse(request, "INTERNAL_ERROR");
        }
    }
    else if (method != "system.ping") return makeErrorResponse(request, "METHOD_NOT_FOUND");
    response["ok"] = KValue{true};
    response["method"] = KValue{method};
    response["result"] = KValue{std::move(result)};
    return KValue{std::move(response)};
}
}
