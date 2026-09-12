#pragma once
#include <lightoverleaf/platform/ikbrowsersurface.h>
#include <lightoverleaf/platform/knativemessage.h>

namespace lightoverleaf
{
int executeCefProcess(std::uintptr_t instance, void* sandbox);
std::unique_ptr<IKBrowserSurface> createCefSurface(std::uintptr_t instance, void* sandbox,
    const std::string& cachePath, const std::string& resourcePath,
    KNativeEndpointFactory endpointFactory, bool crashSmokeTest = false, bool rpcSmokeTest = false,
    bool payloadSmokeTest = false);
}
