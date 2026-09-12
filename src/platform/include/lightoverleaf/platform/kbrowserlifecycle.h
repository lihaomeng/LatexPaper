#pragma once
#include <cstdint>

namespace lightoverleaf
{
enum class KBrowserState { Initial, Loading, Ready, Recovering, Closing, Closed, Failed };
enum class KBrowserAction { None, Reload, Close, EmergencyExit };

// Single-threaded lifecycle policy. Time is injected in milliseconds.
class KBrowserLifecycle
{
public:
    bool start(std::int64_t now);
    bool ready();
    KBrowserAction rendererFailed(std::int64_t now);
    KBrowserAction requestClose(std::int64_t now);
    KBrowserAction tick(std::int64_t now);
    void closed();
    KBrowserState state() const { return m_state; }
    unsigned int recoveries() const { return m_recoveries; }

private:
    KBrowserState m_state = KBrowserState::Initial;
    std::int64_t m_startedAt = 0;
    unsigned int m_recoveries = 0;
};
}
