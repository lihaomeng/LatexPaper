#include <windows.h>
#include <cstdlib>
#include <string_view>
#include <lightoverleaf/desktop/kapplicationcomposition.h>
#include <lightoverleaf/platform/kcefruntime.h>
#include <lightoverleaf/platform/kqtruntime.h>
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPTSTR commandLine,
    int, void* sandbox, cef_version_info_t*)
{
    const std::uintptr_t nativeInstance = reinterpret_cast<std::uintptr_t>(instance);
    const int processResult = lightoverleaf::executeCefProcess(nativeInstance, sandbox);
    if (processResult >= 0) return processResult;
    lightoverleaf::KQtRuntime qt;
    const bool smoke = commandLine && std::wstring_view(commandLine).find(L"--smoke-test") != std::wstring_view::npos;
    const bool crashSmoke = commandLine && std::wstring_view(commandLine).find(L"--smoke-test-renderer-crash") != std::wstring_view::npos;
    const bool rpcSmoke = commandLine && std::wstring_view(commandLine).find(L"--smoke-test-rpc") != std::wstring_view::npos;
    auto composition = lightoverleaf::createApplicationComposition([&qt]
    {
        return qt.selectWorkspace();
    });
    if (!composition) return 2;
    auto surface = lightoverleaf::createCefSurface(nativeInstance, sandbox, qt.cachePath() + (smoke ? "-smoke" : ""), qt.resourcePath(),
        composition->endpointFactory(), crashSmoke, rpcSmoke);
    if (!surface) return 2;
    const int result = qt.run(*surface, smoke);
    // Entry-point emergency path: never destroy CEF while a browser is alive.
    // Qt already reported the bounded shutdown timeout. OS reclaims processes.
    if (!surface->isClosed()) std::_Exit(result != 0 ? result : 3);
    composition->close();
    return result;
}
