#include <lightoverleaf/desktop/kapplicationcomposition.h>
#include <lightoverleaf/document/adapters/klocaldocumentstore.h>
#include <lightoverleaf/document/inbound/ikdocuments.h>
#include <lightoverleaf/platform/ikworkerexecutor.h>
#include <lightoverleaf/rpc/ikrpcendpoint.h>
#include <lightoverleaf/rpc/kapplicationrpchandler.h>
#include <lightoverleaf/transport/kceftransport.h>
#include <lightoverleaf/workspace/adapters/klocalworkspacestore.h>
#include <lightoverleaf/workspace/inbound/ikworkspaces.h>
#include <lightoverleaf/workspaceworkflow/inbound/ikworkspaceworkflow.h>
#include <lightoverleaf/search/adapters/klocalsearchsource.h>
#include <lightoverleaf/search/inbound/iksearch.h>
#include <lightoverleaf/build/adapters/kwindowsbuildadapters.h>
#include <lightoverleaf/build/inbound/ikbuilds.h>
#include <lightoverleaf/build/outbound/ikbuildartifactpublisher.h>
#include <lightoverleaf/build/outbound/ikcompilerrootsource.h>
#include <lightoverleaf/preview/adapters/klocalartifactstore.h>
#include <lightoverleaf/preview/inbound/ikpreviewartifacts.h>
#include <lightoverleaf/navigation/adapters/kunavailablesynctexbackend.h>
#include <lightoverleaf/navigation/adapters/kwindowssynctexbackend.h>
#include <lightoverleaf/navigation/inbound/iknavigation.h>
#include <lightoverleaf/navigation/outbound/iknavigationbackends.h>
#include <lightoverleaf/preferences/adapters/ksqlitepreferencesstore.h>
#include <lightoverleaf/preferences/inbound/ikpreferences.h>
#include <lightoverleaf/session/adapters/ksqlitesessionstore.h>
#include <lightoverleaf/session/inbound/iksessions.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>

namespace lightoverleaf
{
namespace
{
using KStopCallback = std::stop_callback<std::function<void()>>;
class KBuildArtifactPublisherBridge final : public build::IKBuildArtifactPublisher
{
public:
    explicit KBuildArtifactPublisherBridge(std::shared_ptr<preview::IKPreviewArtifacts> previews)
        : m_previews(std::move(previews)) {}
    KResult<build::KPublishedBuildArtifact> publish(const std::string& jobId,
        std::span<const std::uint8_t> pdf, std::span<const std::uint8_t> syncTex) override
    {
        KResult<preview::KPreviewDescriptor> result = m_previews->publish(jobId, pdf, syncTex);
        if (const KError* error = std::get_if<KError>(&result)) return *error;
        preview::KPreviewDescriptor value = std::get<preview::KPreviewDescriptor>(std::move(result));
        return build::KPublishedBuildArtifact{std::move(value.m_artifactId), value.m_syncTexAvailable};
    }
private: std::shared_ptr<preview::IKPreviewArtifacts> m_previews;
};

class KPreferencesCompilerRootSource final : public build::IKCompilerRootSource
{
public:
    explicit KPreferencesCompilerRootSource(std::shared_ptr<preferences::IKPreferences> preferences)
        : m_preferences(std::move(preferences)) {}
    std::vector<std::string> roots() const override
    {
        if (!m_preferences) return {};
        KResult<preferences::KPreferences> result = m_preferences->get();
        const auto* value = std::get_if<preferences::KPreferences>(&result);
        return value && !value->m_texRoot.empty() ? std::vector<std::string>{value->m_texRoot} :
            std::vector<std::string>{};
    }

private:
    std::shared_ptr<preferences::IKPreferences> m_preferences;
};
class KSyncTexDataSourceBridge final : public navigation::IKSyncTexDataSource
{
public:
    explicit KSyncTexDataSourceBridge(std::shared_ptr<preview::IKPreviewArtifacts> previews)
        : m_previews(std::move(previews)) {}
    KResult<navigation::KSyncTexArtifact> read(const std::string& id) const override
    {
        KResult<std::vector<std::uint8_t>> pdf = m_previews->readPdfAll(id);
        if (const KError* error = std::get_if<KError>(&pdf)) return *error;
        KResult<std::vector<std::uint8_t>> syncTex = m_previews->readSyncTex(id);
        if (const KError* error = std::get_if<KError>(&syncTex)) return *error;
        return navigation::KSyncTexArtifact{
            std::get<std::vector<std::uint8_t>>(std::move(pdf)),
            std::get<std::vector<std::uint8_t>>(std::move(syncTex))};
    }
private: std::shared_ptr<preview::IKPreviewArtifacts> m_previews;
};

struct KCombinedStop
{
public:
    explicit KCombinedStop(std::stop_token endpointStop)
    {
        m_endpoint.emplace(endpointStop, [source = m_source]() mutable { source.request_stop(); });
    }

public:
    std::stop_source m_source;
    std::optional<KStopCallback> m_endpoint;
};

std::string utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) == size ? result : std::string{};
}
std::wstring environment(const wchar_t* name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return {};
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) return {};
    value.resize(written); return value;
}
std::filesystem::path moduleDirectory()
{
    std::vector<wchar_t> value(32768, L'\0');
    const DWORD written = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    if (written == 0 || written >= value.size()) return std::filesystem::current_path();
    value.resize(written);
    return std::filesystem::path(value.data()).parent_path();
}
}

class KApplicationComposition::KImpl final : public std::enable_shared_from_this<KImpl>
{
public:
    KImpl(KWorkspacePicker picker, std::shared_ptr<const IKGetCapabilities> capabilities,
        std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow> workflow,
        std::shared_ptr<rpc::KApplicationRpcHandler> handler,
        std::unique_ptr<IKWorkerExecutor> worker)
        : m_picker(std::move(picker)), m_capabilities(std::move(capabilities)),
          m_workflow(std::move(workflow)), m_handler(std::move(handler)), m_worker(std::move(worker)) {}
    ~KImpl() { close(); }
    std::shared_ptr<IKNativeMessageEndpoint> createEndpoint(KNativeSchedule schedule,
        const std::string& sessionId)
    {
        if (m_closed || !schedule || !m_worker) return nullptr;
        const std::shared_ptr<KImpl> self = shared_from_this();
        rpc::KEndpointDispatch dispatch = [self, schedule](rpc::KValue request,
            std::stop_token endpointStop, rpc::KEndpointCompletion completion) mutable
        {
            if (self->m_closed)
            {
                completion(rpc::makeErrorResponse(request, "RESOURCE_EXHAUSTED"));
                return;
            }
            std::optional<std::string> selection;
            const auto* object = std::get_if<rpc::KValue::KObject>(&request.m_value);
            if (object)
            {
                const auto method = object->find("method");
                if (method != object->end())
                {
                    const std::string* name = std::get_if<std::string>(&method->second.m_value);
                    if (name && *name == "workspace.open")
                    {
                        try
                        {
                            selection = self->m_picker();
                        }
                        catch (...)
                        {
                            completion(rpc::makeErrorResponse(request, "INTERNAL_ERROR"));
                            return;
                        }
                    }
                }
            }
            const auto combined = std::make_shared<KCombinedStop>(endpointStop);
            rpc::KValue rejected = rpc::makeErrorResponse(request, "RESOURCE_EXHAUSTED");
            rpc::KValue internal = rpc::makeErrorResponse(request, "INTERNAL_ERROR");
            const auto sharedCompletion =
                std::make_shared<rpc::KEndpointCompletion>(std::move(completion));
            const auto task = self->m_worker->submit(
                [self, schedule, request = std::move(request), selection = std::move(selection),
                 sharedCompletion, combined,
                 internal = std::move(internal)](std::stop_token workerStop) mutable
                {
                    KStopCallback workerCancellation(workerStop,
                        [source = combined->m_source]() mutable { source.request_stop(); });
                    rpc::KValue response;
                    try
                    {
                        response = self->m_handler->dispatch(std::move(request),
                            std::move(selection), combined->m_source.get_token());
                    }
                    catch (...)
                    {
                        response = std::move(internal);
                    }
                    try
                    {
                        schedule([sharedCompletion, response = std::move(response)]() mutable
                        {
                            (*sharedCompletion)(std::move(response));
                        });
                    }
                    catch (...)
                    {
                    }
                });
            if (!task) (*sharedCompletion)(std::move(rejected));
        };
        const auto endpoint = rpc::createRpcEndpoint(std::move(dispatch),
            [](const rpc::KValue& response) { return rpc::v2::validateResponse(response); },
            schedule, sessionId, []
            {
                return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            }, m_capabilities);
        return createNativeEndpoint(endpoint);
    }
    void close() noexcept
    {
        if (m_closed.exchange(true)) return;
        if (m_worker) m_worker->close();
        m_worker.reset();
        m_handler.reset();
        m_workflow.reset();
        m_capabilities.reset();
    }

private:
    KWorkspacePicker m_picker;
    std::shared_ptr<const IKGetCapabilities> m_capabilities;
    std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow> m_workflow;
    std::shared_ptr<rpc::KApplicationRpcHandler> m_handler;
    std::unique_ptr<IKWorkerExecutor> m_worker;
    std::atomic_bool m_closed = false;
};

KApplicationComposition::KApplicationComposition(std::shared_ptr<KImpl> impl) : m_impl(std::move(impl)) {}
KApplicationComposition::~KApplicationComposition() { close(); }

KNativeEndpointFactory KApplicationComposition::endpointFactory() const
{
    const std::weak_ptr<KImpl> weak = m_impl;
    return [weak](KNativeSchedule schedule, const std::string& sessionId)
    {
        const std::shared_ptr<KImpl> impl = weak.lock();
        return impl ? impl->createEndpoint(std::move(schedule), sessionId) :
            std::shared_ptr<IKNativeMessageEndpoint>{};
    };
}

void KApplicationComposition::close() noexcept
{
    if (m_impl) m_impl->close();
}

std::unique_ptr<KApplicationComposition> createApplicationComposition(KWorkspacePicker picker)
{
    if (!picker) return nullptr;
    const std::shared_ptr<workspace::IKWorkspaceStore> workspaceStore =
        workspace::createLocalWorkspaceStore();
    std::unique_ptr<workspace::IKWorkspaces> workspaces = workspace::createWorkspaces(workspaceStore);
    workspaceworkflow::KDocumentFactory documentFactory = [](const std::string& root)
        -> KResult<std::unique_ptr<document::IKDocuments>>
    {
        document::KLocalDocumentStoreOptions options;
        options.m_rootUtf8 = root;
        KResult<std::shared_ptr<document::IKDocumentStore>> store =
            document::createLocalDocumentStore(options);
        if (const KError* error = std::get_if<KError>(&store)) return *error;
        std::unique_ptr<document::IKDocuments> documents =
            document::createDocuments(std::get<std::shared_ptr<document::IKDocumentStore>>(std::move(store)));
        if (!documents) return KError{KErrorCode::Internal, "document.factoryFailure", false};
        return documents;
    };
    workspaceworkflow::KSearchFactory searchFactory = [](const std::string& root)
        -> KResult<std::unique_ptr<search::IKSearch>>
    {
        KResult<std::shared_ptr<search::IKSearchSource>> source =
            search::createLocalSearchSource({root});
        if (const KError* error = std::get_if<KError>(&source)) return *error;
        std::unique_ptr<search::IKSearch> search = search::createSearch(
            std::get<std::shared_ptr<search::IKSearchSource>>(std::move(source)));
        if (!search) return KError{KErrorCode::Internal, "search.factoryFailure", false};
        return search;
    };
    std::filesystem::path applicationCache = environment(L"LOCALAPPDATA");
    if (applicationCache.empty()) applicationCache = std::filesystem::temp_directory_path();
    applicationCache /= L"LightOverLeaf";
    const std::string statePath = utf8((applicationCache / L"state.sqlite3").wstring());
    KResult<std::shared_ptr<preferences::IKPreferencesStore>> preferenceStore =
        preferences::createSqlitePreferencesStore(statePath);
    KResult<std::shared_ptr<session::IKSessionStore>> sessionStore =
        session::createSqliteSessionStore(statePath);
    if (std::holds_alternative<KError>(preferenceStore) ||
        std::holds_alternative<KError>(sessionStore)) return nullptr;
    std::shared_ptr<preferences::IKPreferences> preferencesService =
        preferences::createPreferences(std::get<std::shared_ptr<preferences::IKPreferencesStore>>(
            std::move(preferenceStore)));
    std::shared_ptr<session::IKSessions> sessionsService =
        session::createSessions(std::get<std::shared_ptr<session::IKSessionStore>>(
            std::move(sessionStore)));
    const std::shared_ptr<const build::IKCompilerRootSource> compilerRootSource =
        std::make_shared<KPreferencesCompilerRootSource>(preferencesService);
    KResult<std::shared_ptr<preview::IKArtifactStore>> artifactStore =
        preview::createLocalArtifactStore(utf8((applicationCache / L"artifacts").wstring()));
    if (const KError* error = std::get_if<KError>(&artifactStore)) return nullptr;
    std::shared_ptr<preview::IKPreviewArtifacts> previews = preview::createPreviewArtifacts(
        std::get<std::shared_ptr<preview::IKArtifactStore>>(std::move(artifactStore)));
    auto publisher = std::make_shared<KBuildArtifactPublisherBridge>(previews);
    auto syncSource = std::make_shared<KSyncTexDataSourceBridge>(previews);
    std::vector<std::string> navigationTexRoots;
    const std::wstring configuredTexRoot = environment(L"LIGHTOVERLEAF_TEX_ROOT");
    if (!configuredTexRoot.empty()) navigationTexRoots.push_back(utf8(configuredTexRoot));
    if (preferencesService)
    {
        KResult<preferences::KPreferences> configuredPreferences = preferencesService->get();
        if (const auto* value = std::get_if<preferences::KPreferences>(&configuredPreferences);
            value && !value->m_texRoot.empty()) navigationTexRoots.push_back(value->m_texRoot);
    }
    navigationTexRoots.push_back(utf8((moduleDirectory() / L"texlive").wstring()));
    navigationTexRoots.push_back(utf8((std::filesystem::current_path() / L"texlive").wstring()));
    navigationTexRoots.push_back(utf8((moduleDirectory() / L"runtime" / L"miktex").wstring()));
    navigationTexRoots.push_back(utf8((std::filesystem::current_path() / L"runtime" / L"miktex").wstring()));
    KResult<std::shared_ptr<navigation::IKSyncTexBackend>> windowsNavigation =
        navigation::createWindowsSyncTexBackend({
            utf8((applicationCache / L"navigation").wstring()),
            std::move(navigationTexRoots), {}});
    const bool syncTexAvailable =
        std::holds_alternative<std::shared_ptr<navigation::IKSyncTexBackend>>(windowsNavigation);
    std::shared_ptr<navigation::IKSyncTexBackend> navigationBackend = syncTexAvailable ?
        std::get<std::shared_ptr<navigation::IKSyncTexBackend>>(std::move(windowsNavigation)) :
        navigation::createUnavailableSyncTexBackend();
    std::shared_ptr<navigation::IKNavigation> navigationService = navigation::createNavigation(
        std::move(syncSource), std::move(navigationBackend));
    workspaceworkflow::KBuildFactory buildFactory = [publisher, applicationCache, compilerRootSource](const std::string& root)
        -> KResult<std::shared_ptr<build::IKBuilds>>
    {
        std::filesystem::path cacheBase = applicationCache / L"build";
        std::vector<std::string> texRoots;
        const std::wstring configured = environment(L"LIGHTOVERLEAF_TEX_ROOT");
        if (!configured.empty()) texRoots.push_back(utf8(configured));
        texRoots.push_back(utf8((moduleDirectory() / L"texlive").wstring()));
        texRoots.push_back(utf8((std::filesystem::current_path() / L"texlive").wstring()));
        texRoots.push_back(utf8((moduleDirectory() / L"runtime" / L"miktex").wstring()));
        texRoots.push_back(utf8((std::filesystem::current_path() / L"runtime" / L"miktex").wstring()));
        KResult<build::KWindowsBuildAdapters> adapters = build::createWindowsBuildAdapters(
            {root, utf8(cacheBase.wstring()), std::move(texRoots), compilerRootSource});
        if (const KError* error = std::get_if<KError>(&adapters)) return *error;
        build::KWindowsBuildAdapters value =
            std::get<build::KWindowsBuildAdapters>(std::move(adapters));
        std::shared_ptr<build::IKBuilds> builds = build::createBuilds(
            std::move(value.m_snapshots), std::move(value.m_compiler), publisher);
        if (!builds) return KError{KErrorCode::Internal, "build.factoryFailure", false};
        return builds;
    };
    std::unique_ptr<workspaceworkflow::IKWorkspaceWorkflow> workflow =
        workspaceworkflow::createWorkspaceWorkflow(std::move(workspaces), std::move(documentFactory),
            std::move(searchFactory), std::move(buildFactory), previews, navigationService);
    std::unique_ptr<IKWorkerExecutor> worker = createWorkerExecutor(64, 2);
    const std::shared_ptr<const IKGetCapabilities> capabilities =
        createCapabilities({true, true, true, syncTexAvailable});
    if (!workspaceStore || !workflow || !worker || !capabilities) return nullptr;
    auto sharedWorkflow = std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow>(std::move(workflow));
    auto handler = std::make_shared<rpc::KApplicationRpcHandler>(capabilities, sharedWorkflow,
        preferencesService, sessionsService);
    auto impl = std::make_shared<KApplicationComposition::KImpl>(std::move(picker), capabilities,
        std::move(sharedWorkflow), std::move(handler), std::move(worker));
    return std::make_unique<KApplicationComposition>(std::move(impl));
}
}
