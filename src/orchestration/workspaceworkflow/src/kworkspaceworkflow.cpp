#include <lightoverleaf/workspaceworkflow/inbound/ikworkspaceworkflow.h>
#include <mutex>

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
        KDocumentFactory documentFactory, KSearchFactory searchFactory, KBuildFactory buildFactory,
        std::shared_ptr<preview::IKPreviewArtifacts> previews,
        std::shared_ptr<navigation::IKNavigation> navigation)
        : m_workspaces(std::move(workspaces)), m_documentFactory(std::move(documentFactory)),
          m_searchFactory(std::move(searchFactory)), m_buildFactory(std::move(buildFactory)),
          m_previews(std::move(previews)), m_navigation(std::move(navigation)) {}
    ~KWorkspaceWorkflow() override { close(); }
    KResult<workspace::KWorkspaceState> open(const std::string& nativeSelection,
        std::stop_token stop) override
    {
        // Prepare every dependent service before committing the active workspace.
        auto documents = m_documentFactory(nativeSelection);
        if (const KError* failure = std::get_if<KError>(&documents)) return *failure;
        auto nextDocuments = std::get<std::unique_ptr<document::IKDocuments>>(std::move(documents));
        if (!nextDocuments) return KError{KErrorCode::Internal, "document.factoryFailure", false};
        std::unique_ptr<search::IKSearch> nextSearch;
        if (m_searchFactory)
        {
            auto search = m_searchFactory(nativeSelection);
            if (const KError* failure = std::get_if<KError>(&search)) return *failure;
            nextSearch = std::get<std::unique_ptr<search::IKSearch>>(std::move(search));
            if (!nextSearch) return KError{KErrorCode::Internal, "search.factoryFailure", false};
        }
        std::shared_ptr<build::IKBuilds> nextBuilds;
        if (m_buildFactory)
        {
            auto builds = m_buildFactory(nativeSelection);
            if (const KError* failure = std::get_if<KError>(&builds)) return *failure;
            nextBuilds = std::get<std::shared_ptr<build::IKBuilds>>(std::move(builds));
            if (!nextBuilds) return KError{KErrorCode::Internal, "build.factoryFailure", false};
        }
        KResult<workspace::KWorkspaceState> opened = m_workspaces->open(nativeSelection, stop);
        if (const KError* failure = std::get_if<KError>(&opened)) return *failure;
        m_documents = std::move(nextDocuments);
        m_search = std::move(nextSearch);
        {
            std::scoped_lock lock(m_buildMutex);
            m_builds = std::move(nextBuilds);
            m_nativeSelection = nativeSelection;
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
        {
            std::scoped_lock lock(m_buildMutex);
            m_builds.reset();
            m_nativeSelection.clear();
        }
        m_search.reset();
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
    KResult<std::vector<workspace::KWorkspaceTrashEntry>> listTrash(
        const std::string& workspaceId, std::stop_token stop) override
    {
        return m_workspaces->listTrash(workspaceId, stop);
    }
    KResult<workspace::KWorkspaceState> restoreTrash(
        const workspace::KRestoreWorkspaceEntry& command, std::stop_token stop) override
    {
        return m_workspaces->restoreTrash(command, stop);
    }
    KResult<bool> pollChanges(const std::string& workspaceId, std::stop_token stop) override
    {
        return m_workspaces->pollChanges(workspaceId, stop);
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
    KResult<document::KDocumentSaved> saveDocumentAs(
        const document::KSaveDocumentAs& command, std::stop_token stop) override
    {
        if (!m_documents) return notOpen();
        return m_documents->saveAs(command, stop);
    }
    KResult<search::KSearchResult> search(const search::KSearchCommand& command,
        std::stop_token stop) override
    {
        if (!m_workspaces->state()) return notOpen();
        if (!m_search) return KError{KErrorCode::Unavailable, "search.notAvailable", false};
        return m_search->run(command, stop);
    }
    KResult<std::vector<build::KCompilerCapability>> detectCompilers(std::stop_token stop) override
    {
        KResult<std::shared_ptr<build::IKBuilds>> resolved = resolveBuilds();
        if (const KError* failure = std::get_if<KError>(&resolved)) return *failure;
        return std::get<std::shared_ptr<build::IKBuilds>>(std::move(resolved))->detect(stop);
    }
    KResult<build::KBuildResult> startBuild(const build::KBuildCommand& command,
        std::stop_token stop) override
    {
        KResult<std::shared_ptr<build::IKBuilds>> resolved = resolveBuilds();
        if (const KError* failure = std::get_if<KError>(&resolved)) return *failure;
        return std::get<std::shared_ptr<build::IKBuilds>>(std::move(resolved))->start(command, stop);
    }
    KResult<bool> cancelBuild(const std::string& jobId) override
    {
        std::shared_ptr<build::IKBuilds> builds;
        { std::scoped_lock lock(m_buildMutex); builds = m_builds; }
        if (!builds) return KError{KErrorCode::Unavailable, "build.notAvailable", false};
        return builds->cancel(jobId);
    }
    KResult<build::KBuildStatus> buildStatus(const std::string& jobId) const override
    {
        std::shared_ptr<build::IKBuilds> builds;
        { std::scoped_lock lock(m_buildMutex); builds = m_builds; }
        if (!builds) return KError{KErrorCode::Unavailable, "build.notAvailable", false};
        return builds->status(jobId);
    }
    KResult<preview::KPreviewChunk> readPreview(const std::string& id,
        std::size_t offset, std::size_t count) const override
    {
        if (!m_previews) return KError{KErrorCode::Unavailable, "preview.notAvailable", false};
        return m_previews->readPdf(id, offset, count);
    }
    KResult<navigation::KPdfLocation> forwardSync(const std::string& id,
        const navigation::KSourceLocation& source) const override
    {
        if (!m_navigation) return KError{KErrorCode::Unavailable, "navigation.notAvailable", false};
        return m_navigation->forward(id, source);
    }
    KResult<navigation::KSourceLocation> reverseSync(const std::string& id,
        const navigation::KPdfLocation& pdf) const override
    {
        if (!m_navigation) return KError{KErrorCode::Unavailable, "navigation.notAvailable", false};
        return m_navigation->reverse(id, pdf);
    }

private:
    KResult<std::shared_ptr<build::IKBuilds>> resolveBuilds()
    {
        std::string nativeSelection;
        {
            std::scoped_lock lock(m_buildMutex);
            if (m_builds) return m_builds;
            nativeSelection = m_nativeSelection;
        }
        if (!m_buildFactory)
            return KError{KErrorCode::Unavailable, "build.notAvailable", false};
        KResult<std::shared_ptr<build::IKBuilds>> created = m_buildFactory(nativeSelection);
        if (const KError* failure = std::get_if<KError>(&created)) return *failure;
        std::shared_ptr<build::IKBuilds> candidate =
            std::get<std::shared_ptr<build::IKBuilds>>(std::move(created));
        if (!candidate) return KError{KErrorCode::Internal, "build.factoryFailure", false};
        std::scoped_lock lock(m_buildMutex);
        if (!m_builds) m_builds = std::move(candidate);
        return m_builds;
    }

private:
    std::unique_ptr<workspace::IKWorkspaces> m_workspaces;
    KDocumentFactory m_documentFactory;
    KSearchFactory m_searchFactory;
    KBuildFactory m_buildFactory;
    std::unique_ptr<document::IKDocuments> m_documents;
    std::unique_ptr<search::IKSearch> m_search;
    std::shared_ptr<build::IKBuilds> m_builds;
    std::string m_nativeSelection;
    mutable std::mutex m_buildMutex;
    std::shared_ptr<preview::IKPreviewArtifacts> m_previews;
    std::shared_ptr<navigation::IKNavigation> m_navigation;
};

std::unique_ptr<IKWorkspaceWorkflow> createWorkspaceWorkflow(
    std::unique_ptr<workspace::IKWorkspaces> workspaces, KDocumentFactory documentFactory,
    KSearchFactory searchFactory, KBuildFactory buildFactory,
    std::shared_ptr<preview::IKPreviewArtifacts> previews,
    std::shared_ptr<navigation::IKNavigation> navigation)
{
    if (!workspaces || !documentFactory) return nullptr;
    return std::make_unique<KWorkspaceWorkflow>(std::move(workspaces), std::move(documentFactory),
        std::move(searchFactory), std::move(buildFactory), std::move(previews), std::move(navigation));
}
}
