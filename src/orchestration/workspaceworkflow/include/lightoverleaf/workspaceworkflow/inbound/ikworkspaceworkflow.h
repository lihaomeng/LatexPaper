#pragma once
#include <lightoverleaf/document/inbound/ikdocuments.h>
#include <lightoverleaf/workspace/inbound/ikworkspaces.h>
#include <functional>
#include <memory>
#include <optional>

namespace lightoverleaf::workspaceworkflow
{
using KDocumentFactory = std::function<KResult<std::unique_ptr<document::IKDocuments>>(const std::string&)>;

// Thread-confined workflow: invoke every method from the same worker lane.
class IKWorkspaceWorkflow
{
public:
    virtual ~IKWorkspaceWorkflow() = default;
    virtual KResult<workspace::KWorkspaceState> open(const std::string& nativeSelection,
        std::stop_token stop = {}) = 0;
    virtual std::optional<workspace::KWorkspaceState> state() const = 0;
    virtual KResult<workspace::KWorkspaceState> refresh(std::stop_token stop = {}) = 0;
    virtual KResult<workspace::KWorkspaceState> mutate(const workspace::KWorkspaceMutation& command,
        std::stop_token stop = {}) = 0;
    virtual KResult<workspace::KWorkspaceState> mutateDirectory(
        const workspace::KWorkspaceDirectoryMutation& command, std::stop_token stop = {}) = 0;
    virtual void close() noexcept = 0;
    virtual KResult<document::KDocumentSnapshot> openDocument(const std::string& fileId,
        std::stop_token stop = {}) = 0;
    virtual KResult<document::KDocumentSaved> saveDocument(const document::KSaveDocument& command,
        std::stop_token stop = {}) = 0;
};

std::unique_ptr<IKWorkspaceWorkflow> createWorkspaceWorkflow(
    std::unique_ptr<workspace::IKWorkspaces> workspaces, KDocumentFactory documentFactory);
}
