#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <stop_token>
#include <string>
#include <vector>

namespace lightoverleaf::build
{
struct KBuildSnapshotOverlayFile
{
    std::string m_fileId;
    std::string m_content;
};
class IKBuildSnapshotStore
{
public:
    virtual ~IKBuildSnapshotStore() = default;
    virtual KResult<bool> prepare(const std::string& snapshotId,
        const std::vector<KBuildSnapshotOverlayFile>& overlayFiles,
        std::stop_token stop) = 0;
    virtual void release(const std::string& snapshotId) noexcept = 0;
};
}
