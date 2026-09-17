#include <lightoverleaf/system/inbound/ikgetcapabilities.h>

namespace lightoverleaf
{
namespace
{
class KGetCapabilities final : public IKGetCapabilities
{
public:
    explicit KGetCapabilities(KRuntimeCapabilities capabilities) : m_capabilities(capabilities) {}
    KRuntimeCapabilities get() const override { return m_capabilities; }

private:
    const KRuntimeCapabilities m_capabilities;
};
}

std::shared_ptr<const IKGetCapabilities> createCapabilities(KRuntimeCapabilities capabilities)
{
    return std::make_shared<KGetCapabilities>(capabilities);
}
}
