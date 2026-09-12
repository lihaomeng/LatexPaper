#include <lightoverleaf/rpc/kapplicationrpchandler.h>
#include <lightoverleaf/system/inbound/ikgetcapabilities.h>
#include <lightoverleaf/workspaceworkflow/inbound/ikworkspaceworkflow.h>
#include <iostream>

using namespace lightoverleaf;
using namespace lightoverleaf::rpc;

namespace
{
class KFakeWorkspaces final : public workspace::IKWorkspaces
{
public:
    KResult<workspace::KWorkspaceState> refresh(std::stop_token stop) override
    {
        if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "cancelled", true};
        if (!m_state) return KError{KErrorCode::Unavailable, "workspace.notOpen", false};
        m_state->m_revision = "revision-refreshed";
        return *m_state;
    }
    KResult<workspace::KWorkspaceState> mutate(const workspace::KWorkspaceMutation&, std::stop_token) override
    {
        return KError{KErrorCode::Unavailable, "test.notImplemented", false};
    }
    KResult<workspace::KWorkspaceState> mutateDirectory(
        const workspace::KWorkspaceDirectoryMutation& command, std::stop_token) override
    {
        if (!m_state) return KError{KErrorCode::Unavailable, "workspace.notOpen", false};
        if (command.m_workspaceId != m_state->m_id)
            return KError{KErrorCode::InvalidArgument, "workspace.changed", false};
        m_state->m_revision = "mutation-directory";
        return *m_state;
    }
    KResult<workspace::KWorkspaceState> open(const std::string& selection,
        std::stop_token stop) override
    {
        if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "cancelled", true};
        if (selection != "D:/论文") return KError{KErrorCode::NotFound, "missing", false};
        m_state = workspace::KWorkspaceState{"workspace-1", "论文", "revision-1",
            {{"章节", true, 0}, {"章节/引言.tex", false, 12}}};
        return *m_state;
    }
    std::optional<workspace::KWorkspaceState> state() const override { return m_state; }
    void close() noexcept override { m_state.reset(); }

private:
    std::optional<workspace::KWorkspaceState> m_state;
};

class KFakeDocuments final : public document::IKDocuments
{
public:
    KResult<document::KDocumentSnapshot> open(const std::string& fileId,
        std::stop_token stop) override
    {
        if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "cancelled", true};
        if (fileId != "章节/引言.tex") return KError{KErrorCode::NotFound, "missing", false};
        return document::KDocumentSnapshot{fileId, "测试内容", "document-1", false};
    }
    KResult<document::KDocumentSaved> save(const document::KSaveDocument& command,
        std::stop_token) override
    {
        if (command.m_expectedRevision != "document-1")
            return KError{KErrorCode::Conflict, "conflict", true};
        return document::KDocumentSaved{command.m_fileId, "document-2", command.m_clientSequence};
    }
};

KValue request(const std::string& id, const std::string& method, KValue::KObject params = {},
    std::uint64_t sequence = 0)
{
    return KValue{KValue::KObject{{"version", KValue{2.0}}, {"id", KValue{id}},
        {"clientSequence", KValue{static_cast<double>(sequence)}}, {"method", KValue{method}},
        {"params", KValue{std::move(params)}}}};
}

std::string errorCode(const KValue& response)
{
    const auto& object = std::get<KValue::KObject>(response.m_value);
    const auto& error = std::get<KValue::KObject>(object.at("error").m_value);
    return std::get<std::string>(error.at("code").m_value);
}
}

int main()
{
    int failures = 0;
    const auto check = [&failures](bool valid, const char* label)
    {
        if (!valid) { ++failures; std::cerr << label << '\n'; }
    };
    auto workflow = workspaceworkflow::createWorkspaceWorkflow(
        std::make_unique<KFakeWorkspaces>(),
        [](const std::string& selection) -> KResult<std::unique_ptr<document::IKDocuments>>
        {
            if (selection != "D:/论文") return KError{KErrorCode::NotFound, "missing", false};
            return std::unique_ptr<document::IKDocuments>(std::make_unique<KFakeDocuments>());
        });
    const auto sharedWorkflow = std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow>(std::move(workflow));
    KApplicationRpcHandler handler(createCapabilities({true, false, false, false}), sharedWorkflow);

    KValue response = handler.dispatch(request("before-open", "document.open",
        {{"fileId", KValue{std::string("章节/引言.tex")}}}), std::nullopt, {});
    check(v2::validateErrorResponse(response) && errorCode(response) == "WORKSPACE_NOT_OPEN",
        "document requires active workspace");
    response = handler.dispatch(request("cancel-open", "workspace.open"), std::nullopt, {});
    check(v2::validateErrorResponse(response) && errorCode(response) == "USER_CANCELLED",
        "picker cancellation is explicit");
    response = handler.dispatch(request("open", "workspace.open"), std::string("D:/论文"), {});
    check(v2::validateWorkspaceStateResponse(response), "workspace open response");
    const auto& opened = std::get<KValue::KObject>(response.m_value);
    const auto& state = std::get<KValue::KObject>(opened.at("result").m_value);
    check(std::get<std::string>(state.at("displayName").m_value) == "论文", "unicode workspace projection");

    response = handler.dispatch(request("state", "workspace.getState"), std::nullopt, {});
    check(v2::validateWorkspaceStateResponse(response), "workspace state response");
    response = handler.dispatch(request("refresh", "workspace.refresh"), std::nullopt, {});
    check(v2::validateWorkspaceStateResponse(response) &&
        std::get<std::string>(std::get<KValue::KObject>(response.m_value).at("method").m_value) == "workspace.refresh",
        "workspace refresh response");
    response = handler.dispatch(request("manage-directory", "workspace.manageDirectory",
        {{"workspaceId", KValue{std::string("workspace-1")}},
         {"operation", KValue{std::string("create")}},
         {"directoryId", KValue{std::string("附录")}},
         {"destination", KValue{std::string()}}}), std::nullopt, {});
    check(v2::validateWorkspaceStateResponse(response) &&
        std::get<std::string>(std::get<KValue::KObject>(response.m_value).at("method").m_value) ==
            "workspace.manageDirectory", "workspace directory response");
    response = handler.dispatch(request("open-document", "document.open",
        {{"fileId", KValue{std::string("章节/引言.tex")}}}, 4), std::nullopt, {});
    check(v2::validateDocumentOpenResponse(response), "document open response");
    response = handler.dispatch(request("save-document", "document.save",
        {{"fileId", KValue{std::string("章节/引言.tex")}},
         {"content", KValue{std::string("新内容")}},
         {"expectedRevision", KValue{std::string("document-1")}}}, 5), std::nullopt, {});
    check(v2::validateDocumentSaveResponse(response), "document save response");
    response = handler.dispatch(request("conflict", "document.save",
        {{"fileId", KValue{std::string("章节/引言.tex")}},
         {"content", KValue{std::string("冲突")}},
         {"expectedRevision", KValue{std::string("document-old")}}}, 6), std::nullopt, {});
    check(v2::validateErrorResponse(response) && errorCode(response) == "FILE_CONFLICT",
        "document conflict mapping");
    response = handler.dispatch(request("close", "workspace.close"), std::nullopt, {});
    check(v2::validateWorkspaceClosedResponse(response), "workspace close response");
    response = handler.dispatch(request("after-close", "document.open",
        {{"fileId", KValue{std::string("章节/引言.tex")}}}), std::nullopt, {});
    check(errorCode(response) == "WORKSPACE_NOT_OPEN", "close releases document session");

    check(!workspaceworkflow::createWorkspaceWorkflow({}, [](const std::string&)
        -> KResult<std::unique_ptr<document::IKDocuments>>
        { return KError{KErrorCode::Internal, "unused", false}; }), "workflow requires workspaces");
    check(!workspaceworkflow::createWorkspaceWorkflow(std::make_unique<KFakeWorkspaces>(), {}),
        "workflow requires document factory");
    return failures == 0 ? 0 : 1;
}
