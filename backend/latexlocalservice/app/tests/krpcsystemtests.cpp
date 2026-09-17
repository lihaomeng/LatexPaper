#include <lightoverleaf/transport/kloopbacktransport.h>
#include <iostream>
#include <stdexcept>

using namespace lightoverleaf;
using namespace lightoverleaf::rpc;

namespace
{
class KFakeCapabilities final : public IKGetCapabilities
{
public:
    KRuntimeCapabilities get() const override { return {true, false, true, false}; }
};
class KFailingCapabilities final : public IKGetCapabilities
{
public:
    KRuntimeCapabilities get() const override { throw std::runtime_error("adapter failed"); }
};
KValue request(const char* method)
{
    return KValue{KValue::KObject{{"version", KValue{2.0}}, {"id", KValue{std::string{"test-id"}}},
        {"method", KValue{std::string{method}}}, {"params", KValue{KValue::KObject{}}}, {"clientSequence", KValue{7.0}}}};
}
std::string errorCode(const KValue& response)
{
    return std::get<std::string>(std::get<KValue::KObject>(
        std::get<KValue::KObject>(response.m_value).at("error").m_value).at("code").m_value);
}
}

int main()
{
    int failures = 0;
    const auto check = [&failures](bool valid, const char* name)
    {
        if (!valid) { std::cerr << name << '\n'; ++failures; }
    };
    const KFakeCapabilities fake;
    const KLoopbackTransport loopback(fake);
    check(v2::validatePingResponse(loopback.exchange(request("system.ping"))), "ping");
    const KValue result = loopback.exchange(request("system.getCapabilities"));
    check(v2::validateCapabilitiesResponse(result), "capability schema");
    const auto& response = std::get<KValue::KObject>(result.m_value);
    const auto& capabilities = std::get<KValue::KObject>(response.at("result").m_value);
    check(std::get<bool>(capabilities.at("nativeFiles").m_value), "fake substituted");
    check(!std::get<bool>(capabilities.at("build").m_value), "unavailable remains false");
    check(std::get<std::string>(response.at("id").m_value) == "test-id", "correlation id");
    check(std::get<double>(response.at("clientSequence").m_value) == 7.0, "correlation sequence");
    check(v2::validateCapabilitiesResponse(loopback.exchange(request("system.getCapabilities"))), "read-only repeat");
    check(errorCode(loopback.exchange(request("unknown.method"))) == "METHOD_NOT_FOUND", "unknown method");
    KValue invalid = request("system.ping");
    std::get<KValue::KObject>(invalid.m_value)["extra"] = KValue{true};
    check(errorCode(loopback.exchange(invalid)) == "INVALID_ARGUMENT", "unknown field");
    invalid = request("system.ping");
    std::get<KValue::KObject>(std::get<KValue::KObject>(invalid.m_value).at("params").m_value)
        .emplace("unexpected", KValue{true});
    check(errorCode(loopback.exchange(invalid)) == "INVALID_ARGUMENT", "system params remain closed");
    check(v2::validateErrorResponse(loopback.exchange(KValue{nullptr})), "uncorrelated invalid input");
    const KFailingCapabilities failing;
    check(errorCode(KLoopbackTransport(failing).exchange(request("system.getCapabilities"))) == "INTERNAL_ERROR", "provider failure");
    const auto application = createCapabilities({});
    const KValue applicationReply = KLoopbackTransport(*application).exchange(request("system.getCapabilities"));
    const auto& applicationResult = std::get<KValue::KObject>(std::get<KValue::KObject>(applicationReply.m_value).at("result").m_value);
    for (const auto& [key, value] : applicationResult) check(!std::get<bool>(value.m_value), key.c_str());
    return failures == 0 ? 0 : 1;
}
