#include <lightoverleaf/navigation/adapters/kunavailablesynctexbackend.h>

namespace lightoverleaf::navigation
{
namespace
{
class KUnavailableSyncTexBackend final : public IKSyncTexBackend
{
public:
    KResult<KBackendPdf> forward(const KSyncTexArtifact&,
        const KBackendSource&) const override
    {
        return KError{KErrorCode::Unavailable, "navigation.syncTexUnavailable", false};
    }

    KResult<KBackendSource> reverse(const KSyncTexArtifact&,
        const KBackendPdf&) const override
    {
        return KError{KErrorCode::Unavailable, "navigation.syncTexUnavailable", false};
    }
};
}

std::shared_ptr<IKSyncTexBackend> createUnavailableSyncTexBackend()
{
    return std::make_shared<KUnavailableSyncTexBackend>();
}
}
