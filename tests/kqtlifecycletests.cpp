#include <lightoverleaf/platform/kqtruntime.h>
#include <lightoverleaf/platform/ikbrowsersurface.h>
#include <iostream>
#include <string>
#include <utility>

namespace lightoverleaf
{
class KFakeSurface final : public IKBrowserSurface
{
public:
    explicit KFakeSurface(std::string mode) : m_mode(std::move(mode)) {}
    bool start(std::uintptr_t, int, int, KBrowserCallbacks callbacks) override
    {
        m_callbacks = std::move(callbacks);
        if (m_mode == "start-failed") return false;
        m_closed = false;
        if (m_mode == "recover" || m_mode == "crash-loop") m_callbacks.m_rendererTerminated();
        else m_callbacks.m_ready();
        return true;
    }
    void resize(int, int) override {}
    void pump() override {}
    bool isClosed() const override { return m_closed; }
    void reload() override
    {
        ++m_reloadCount;
        if (m_mode == "crash-loop") m_callbacks.m_rendererTerminated();
        else m_callbacks.m_ready();
    }
    void close() override
    {
        ++m_closeCount;
        // Deliberately deliver stale callbacks during shutdown.
        m_callbacks.m_rendererTerminated();
        m_callbacks.m_ready();
        m_closed = true;
        m_callbacks.m_closed();
    }
    int reloadCount() const { return m_reloadCount; }
    int closeCount() const { return m_closeCount; }

private:
    std::string m_mode;
    KBrowserCallbacks m_callbacks;
    bool m_closed = true;
    int m_reloadCount = 0;
    int m_closeCount = 0;
};
}
int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    const std::string mode = argv[1];
    lightoverleaf::KQtRuntime runtime;
    lightoverleaf::KFakeSurface surface(mode);
    const int result = runtime.run(surface, true);
    const int expectedCode = mode == "start-failed" || mode == "crash-loop" ? 2 : 0;
    const int expectedReloads = mode == "recover" ? 1 : mode == "crash-loop" ? 2 : 0;
    const int expectedCloses = mode == "start-failed" ? 0 : 1;
    if (result != expectedCode || !surface.isClosed() || surface.reloadCount() != expectedReloads ||
        surface.closeCount() != expectedCloses)
    {
        std::cerr << mode << ": code=" << result << " reloads=" << surface.reloadCount()
                  << " closes=" << surface.closeCount() << '\n';
        return 1;
    }
    return 0;
}
