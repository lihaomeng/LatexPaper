#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace lightoverleaf
{
struct KBrowserCallbacks
{
    std::function<void()> m_ready;
    std::function<void()> m_closed;
    std::function<void()> m_failed;
    std::function<void()> m_rendererTerminated;
};
class IKBrowserSurface
{
public:
    virtual ~IKBrowserSurface() = default;
    virtual bool start(std::uintptr_t parent, int width, int height, KBrowserCallbacks callbacks) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void close() = 0;
    virtual void pump() = 0;
    virtual bool isClosed() const = 0;
    virtual void reload() = 0;
};
}
