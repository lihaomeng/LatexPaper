#include <lightoverleaf/platform/kbrowserlifecycle.h>

namespace lightoverleaf
{
bool KBrowserLifecycle::start(std::int64_t now)
{
    if (m_state != KBrowserState::Initial || now < 0) return false;
    m_state = KBrowserState::Loading;
    m_startedAt = now;
    return true;
}
bool KBrowserLifecycle::ready()
{
    if (m_state != KBrowserState::Loading && m_state != KBrowserState::Recovering) return false;
    m_state = KBrowserState::Ready;
    return true;
}
KBrowserAction KBrowserLifecycle::rendererFailed(std::int64_t now)
{
    if (m_state != KBrowserState::Ready && m_state != KBrowserState::Loading &&
        m_state != KBrowserState::Recovering) return KBrowserAction::None;
    if (m_recoveries >= 2) return requestClose(now);
    ++m_recoveries;
    m_state = KBrowserState::Recovering;
    m_startedAt = now;
    return KBrowserAction::Reload;
}
KBrowserAction KBrowserLifecycle::requestClose(std::int64_t now)
{
    if (m_state == KBrowserState::Closing || m_state == KBrowserState::Closed ||
        m_state == KBrowserState::Failed) return KBrowserAction::None;
    m_state = KBrowserState::Closing;
    m_startedAt = now;
    return KBrowserAction::Close;
}
KBrowserAction KBrowserLifecycle::tick(std::int64_t now)
{
    if (now < m_startedAt) return KBrowserAction::None;
    const auto elapsed = now - m_startedAt;
    if ((m_state == KBrowserState::Loading || m_state == KBrowserState::Recovering) && elapsed >= 20000)
        return requestClose(now);
    if (m_state == KBrowserState::Closing && elapsed >= 10000)
    {
        m_state = KBrowserState::Failed;
        return KBrowserAction::EmergencyExit;
    }
    return KBrowserAction::None;
}
void KBrowserLifecycle::closed()
{
    m_state = KBrowserState::Closed;
}
}
