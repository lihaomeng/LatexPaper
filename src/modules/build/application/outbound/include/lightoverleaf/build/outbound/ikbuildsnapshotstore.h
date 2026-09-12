#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <stop_token>
#include <string>

namespace lightoverleaf::build
{
class IKBuildSnapshotStore
{
public:
    virtual ~IKBuildSnapshotStore() = default;
    virtual KResult<bool> prepare(const std::string& snapshotId, std::stop_token stop) = 0;
    virtual void release(const std::string& snapshotId) noexcept = 0;
};
}
