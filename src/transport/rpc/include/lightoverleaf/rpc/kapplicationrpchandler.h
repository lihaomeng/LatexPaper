#pragma once
#include <lightoverleaf/rpc/krpccore.h>
#include <memory>
#include <optional>
#include <stop_token>

namespace lightoverleaf::workspaceworkflow
{
class IKWorkspaceWorkflow;
}

namespace lightoverleaf::rpc
{
// Thread-confined mapper. Invoke from the worker lane, never a CEF/Qt UI thread.
class KApplicationRpcHandler
{
public:
    KApplicationRpcHandler(std::shared_ptr<const IKGetCapabilities> capabilities,
        std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow> workflow);
    KValue dispatch(KValue request, std::optional<std::string> nativeSelection,
        std::stop_token stop) const;

private:
    std::shared_ptr<const IKGetCapabilities> m_capabilities;
    std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow> m_workflow;
};
}
