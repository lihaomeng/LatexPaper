#include <lightoverleaf/workspaceworkflow/inbound/ikworkspaceworkflow.h>

namespace lightoverleaf::workspaceworkflow
{
namespace
{
KError notOpen()
{
    return {KErrorCode::Unavailable, "workspace.notOpen", false};
}
}

class KWorkspaceWorkflow final : public IKWorkspaceWorkflow
{
public:
    KWorkspaceWorkflow(std::unique_ptr<workspace::IKWorkspaces> workspaces,
        KDocumentFactory documentFactory)
        : m_workspaces(std::move(workspaces)), m_documentFactory(std::move(documentFactory)) {}
    ~KWorkspaceWorkflow() override { close(); }
    KResult<workspace::KWorkspaceState> open(const std::string& nativeSelection,
        std::stop_token stop) override
    {
        KResult<workspace::KWorkspaceState> opened = m_workspaces->open(nativeSelection, stop);
        if (const KError* failure = std::get_if<KError>(&opened)) return *failure;
        m_documents.reset();
        KResult<std::unique_ptr<document::IKDocuments>> documents = m_documentFactory(nativeSelection);
        if (const KError* failure = std::get_if<KError>(&documents))
        {
            m_workspaces->close();
            return *failure;
        }
        m_documents = std::move(std::get<std::unique_ptr<document::IKDocuments>>(documents));
        if (!m_documents)
        {
            m_workspaces->close();
            return KError{KErrorCode::Internal, "document.factoryFailure", false};
        }
        return std::get<workspace::KWorkspaceState>(std::move(opened));
    }
    std::optional<workspace::KWorkspaceState> state() const override
    {
        return m_workspaces->state();
    }
    KResult<workspace::KWorkspaceState> refresh(std::stop_token stop) override
    {
        if (!m_documents) return notOpen();
        return m_workspaces->refresh(stop);
    }
    void close() noexcept override
    {
        m_documents.reset();
        m_workspaces->close();
    }
    KResult<workspace::KWorkspaceState> mutate(const workspace::KWorkspaceMutation& command,
        std::stop_token stop) override
    {
        return m_workspaces->mutate(command, stop);
    }
    KResult<workspace::KWorkspaceState> mutateDirectory(
        const workspace::KWorkspaceDirectoryMutation& command, std::stop_token stop) override
    {
        return m_workspaces->mutateDirectory(command, stop);
    }
    KResult<document::KDocumentSnapshot> openDocument(const std::string& fileId,
        std::stop_token stop) override
    {
        if (!m_documents) return notOpen();
        return m_documents->open(fileId, stop);
    }
    KResult<document::KDocumentSaved> saveDocument(const document::KSaveDocument& command,
        std::stop_token stop) override
    {
        if (!m_documents) return notOpen();
        return m_documents->save(command, stop);
    }

private:
    std::unique_ptr<workspace::IKWorkspaces> m_workspaces;
    KDocumentFactory m_documentFactory;
    std::unique_ptr<document::IKDocuments> m_documents;
};

std::unique_ptr<IKWorkspaceWorkflow> createWorkspaceWorkflow(
    std::unique_ptr<workspace::IKWorkspaces> workspaces, KDocumentFactory documentFactory)
{
    if (!workspaces || !documentFactory) return nullptr;
    return std::make_unique<KWorkspaceWorkflow>(std::move(workspaces), std::move(documentFactory));
}
}
