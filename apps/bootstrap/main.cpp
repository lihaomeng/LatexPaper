#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <lightoverleaf/desktop/kapplicationcomposition.h>
#include <lightoverleaf/platform/kcefruntime.h>
#include <lightoverleaf/platform/kqtruntime.h>
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"

namespace
{
class KPayloadSmokeWorkspace
{
public:
    explicit KPayloadSmokeWorkspace(bool enabled)
    {
        if (!enabled) return;
        m_path = std::filesystem::temp_directory_path() /
            (L"LightOverLeaf-payload-smoke-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        error.clear();
        std::filesystem::create_directories(m_path, error);
        m_valid = !error;
    }
    ~KPayloadSmokeWorkspace()
    {
        if (m_path.empty()) return;
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    bool valid() const { return m_valid; }
    std::optional<std::string> selection() const
    {
        if (!m_valid) return std::nullopt;
        const std::u8string encoded = m_path.u8string();
        return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    }

private:
    std::filesystem::path m_path;
    bool m_valid = false;
};
}

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
    const bool payloadSmoke = commandLine && std::wstring_view(commandLine).find(L"--smoke-test-payload") != std::wstring_view::npos;
    KPayloadSmokeWorkspace payloadWorkspace(payloadSmoke);
    if (payloadSmoke && !payloadWorkspace.valid()) return 2;
    auto composition = lightoverleaf::createApplicationComposition([&qt, &payloadWorkspace, payloadSmoke]
    {
        if (payloadSmoke) return payloadWorkspace.selection();
        return qt.selectWorkspace();
    });
    if (!composition) return 2;
    auto surface = lightoverleaf::createCefSurface(nativeInstance, sandbox, qt.cachePath() + (smoke ? "-smoke" : ""), qt.resourcePath(),
        composition->endpointFactory(), crashSmoke, rpcSmoke, payloadSmoke);
    if (!surface) return 2;
    const int result = qt.run(*surface, smoke);
    // Entry-point emergency path: never destroy CEF while a browser is alive.
    // Qt already reported the bounded shutdown timeout. OS reclaims processes.
    if (!surface->isClosed()) std::_Exit(result != 0 ? result : 3);
    composition->close();
    return result;
}
