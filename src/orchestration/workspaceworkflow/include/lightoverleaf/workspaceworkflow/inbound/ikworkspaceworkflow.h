#pragma once
#include <lightoverleaf/document/inbound/ikdocuments.h>
#include <lightoverleaf/workspace/inbound/ikworkspaces.h>
#include <lightoverleaf/search/inbound/iksearch.h>
#include <lightoverleaf/build/inbound/ikbuilds.h>
#include <lightoverleaf/preview/inbound/ikpreviewartifacts.h>
#include <lightoverleaf/navigation/inbound/iknavigation.h>
#include <functional>
#include <memory>
#include <optional>

namespace lightoverleaf::workspaceworkflow
{
using KDocumentFactory = std::function<KResult<std::unique_ptr<document::IKDocuments>>(const std::string&)>;
using KSearchFactory = std::function<KResult<std::unique_ptr<search::IKSearch>>(const std::string&)>;
using KBuildFactory = std::function<KResult<std::shared_ptr<build::IKBuilds>>(const std::string&)>;

// Workspace/Document/Search calls are serialized by the RPC mapper. Build start/cancel
// may overlap; the workflow snapshots their shared service under an internal lock.
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
    virtual KResult<std::vector<workspace::KWorkspaceTrashEntry>> listTrash(
        const std::string& workspaceId, std::stop_token stop = {}) = 0;
    virtual KResult<workspace::KWorkspaceState> restoreTrash(
        const workspace::KRestoreWorkspaceEntry& command, std::stop_token stop = {}) = 0;
    virtual KResult<bool> pollChanges(const std::string& workspaceId,
        std::stop_token stop = {}) = 0;
    virtual void close() noexcept = 0;
    virtual KResult<document::KDocumentSnapshot> openDocument(const std::string& fileId,
        std::stop_token stop = {}) = 0;
    virtual KResult<document::KDocumentSaved> saveDocument(const document::KSaveDocument& command,
        std::stop_token stop = {}) = 0;
    virtual KResult<document::KDocumentSaved> saveDocumentAs(
        const document::KSaveDocumentAs& command, std::stop_token stop = {}) = 0;
    virtual KResult<search::KSearchResult> search(const search::KSearchCommand& command,
        std::stop_token stop = {}) = 0;
    virtual KResult<std::vector<build::KCompilerCapability>> detectCompilers(
        std::stop_token stop = {}) = 0;
    virtual KResult<build::KBuildResult> startBuild(const build::KBuildCommand& command,
        std::stop_token stop = {}) = 0;
    virtual KResult<bool> cancelBuild(const std::string& jobId) = 0;
    virtual KResult<preview::KPreviewChunk> readPreview(const std::string& artifactId,
        std::size_t offset, std::size_t count) const = 0;
    virtual KResult<navigation::KPdfLocation> forwardSync(const std::string& artifactId,
        const navigation::KSourceLocation& source) const = 0;
    virtual KResult<navigation::KSourceLocation> reverseSync(const std::string& artifactId,
        const navigation::KPdfLocation& pdf) const = 0;
};

std::unique_ptr<IKWorkspaceWorkflow> createWorkspaceWorkflow(
    std::unique_ptr<workspace::IKWorkspaces> workspaces, KDocumentFactory documentFactory,
    KSearchFactory searchFactory = {}, KBuildFactory buildFactory = {},
    std::shared_ptr<preview::IKPreviewArtifacts> previews = {},
    std::shared_ptr<navigation::IKNavigation> navigation = {});
}
