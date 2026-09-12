#pragma once
#include <memory>

namespace lightoverleaf
{
struct KRuntimeCapabilities
{
    bool m_nativeFiles = false;
    bool m_build = false;
    bool m_pdf = false;
    bool m_syncTex = false;
};

class IKGetCapabilities
{
public:
    virtual ~IKGetCapabilities() = default;
    virtual KRuntimeCapabilities get() const = 0;
};

// Reports wired application services, not the presence of installed executables.
std::shared_ptr<const IKGetCapabilities> createCapabilities(KRuntimeCapabilities capabilities);
}
