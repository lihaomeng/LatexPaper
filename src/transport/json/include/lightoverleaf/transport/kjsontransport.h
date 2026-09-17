#pragma once
#include <lightoverleaf/platform/knativemessage.h>
#include <lightoverleaf/rpc/ikrpcendpoint.h>
#include <optional>

namespace lightoverleaf
{
constexpr std::size_t kMaxPipeBytes = 32 * 1024 * 1024;
std::optional<rpc::KValue> parseJsonValue(const std::string& wire);
std::optional<std::string> writeJsonValue(const rpc::KValue& value);
std::shared_ptr<IKNativeMessageEndpoint> createJsonEndpoint(std::shared_ptr<rpc::IKRpcEndpoint> endpoint);
}
