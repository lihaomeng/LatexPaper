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
#include <atomic>
#include <chrono>

namespace lightoverleaf
{
namespace
{
using KStopCallback = std::stop_callback<std::function<void()>>;

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
    std::unique_ptr<workspaceworkflow::IKWorkspaceWorkflow> workflow =
        workspaceworkflow::createWorkspaceWorkflow(std::move(workspaces), std::move(documentFactory));
    std::unique_ptr<IKWorkerExecutor> worker = createWorkerExecutor(64, 1);
    const std::shared_ptr<const IKGetCapabilities> capabilities =
        createCapabilities({true, false, false, false});
    if (!workspaceStore || !workflow || !worker || !capabilities) return nullptr;
    auto sharedWorkflow = std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow>(std::move(workflow));
    auto handler = std::make_shared<rpc::KApplicationRpcHandler>(capabilities, sharedWorkflow);
    auto impl = std::make_shared<KApplicationComposition::KImpl>(std::move(picker), capabilities,
        std::move(sharedWorkflow), std::move(handler), std::move(worker));
    return std::make_unique<KApplicationComposition>(std::move(impl));
}
}
