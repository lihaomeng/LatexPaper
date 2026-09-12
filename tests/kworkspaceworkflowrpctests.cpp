#include <lightoverleaf/rpc/kapplicationrpchandler.h>
#include <lightoverleaf/preferences/inbound/ikpreferences.h>
#include <lightoverleaf/session/inbound/iksessions.h>
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
    KResult<std::vector<workspace::KWorkspaceTrashEntry>> listTrash(const std::string&,
        std::stop_token) override
    {
        return std::vector<workspace::KWorkspaceTrashEntry>{{
            "0123456789abcdef0123456789abcdef", "旧稿.tex", false, 1770000000000ULL}};
    }
    KResult<workspace::KWorkspaceState> restoreTrash(const workspace::KRestoreWorkspaceEntry& command,
        std::stop_token) override
    {
        if (!m_state || command.m_workspaceId != m_state->m_id ||
            command.m_trashId != "0123456789abcdef0123456789abcdef")
            return KError{KErrorCode::InvalidArgument, "test.invalid", false};
        m_state->m_revision = "trash-restored";
        return *m_state;
    }
    KResult<bool> pollChanges(const std::string&, std::stop_token) override { return false; }
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
    KResult<document::KDocumentSaved> saveAs(const document::KSaveDocumentAs& command,
        std::stop_token stop) override
    {
        if (stop.stop_requested())
            return KError{KErrorCode::Cancelled, "cancelled", true};
        return document::KDocumentSaved{command.m_fileId, "document-copy",
            command.m_clientSequence};
    }
};

class KFakeSearch final : public search::IKSearch
{
public:
    KResult<search::KSearchResult> run(const search::KSearchCommand& command,
        std::stop_token) override
    {
        return search::KSearchResult{{{"章节/引言.tex", 2, 3, command.m_query}}, false};
    }
};

class KFakeBuilds final : public build::IKBuilds
{
public:
    KResult<std::vector<build::KCompilerCapability>> detect(std::stop_token) override
    {
        return std::vector<build::KCompilerCapability>{{"fake-tex", "Fake TeX", {build::KBuildEngine::XeLatex}}};
    }
    KResult<build::KBuildResult> start(const build::KBuildCommand& command,
        std::stop_token) override
    {
        return build::KBuildResult{command.m_jobId, build::KBuildTerminal::Succeeded, 0,
            "ok", false, {}};
    }
    KResult<bool> cancel(const std::string&) override { return true; }
};

class KFakePreferences final : public preferences::IKPreferences
{
public:
    KResult<preferences::KPreferences> get() const override { return m_value; }
    KResult<preferences::KPreferences> update(const preferences::KPreferences& value) override
    { m_value = value; return m_value; }
private:
    preferences::KPreferences m_value;
};

class KFakeSessions final : public session::IKSessions
{
public:
    KResult<std::optional<session::KSessionState>> restore() const override { return m_value; }
    KResult<bool> save(const session::KSessionState& value) override
    { m_value = value; return true; }
    KResult<std::vector<std::string>> history() const override
    { return std::vector<std::string>{"D:/论文"}; }
private:
    std::optional<session::KSessionState> m_value;
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
        },
        [](const std::string&) -> KResult<std::unique_ptr<search::IKSearch>>
        { return std::unique_ptr<search::IKSearch>(std::make_unique<KFakeSearch>()); },
        [](const std::string&) -> KResult<std::shared_ptr<build::IKBuilds>>
        { return std::shared_ptr<build::IKBuilds>(std::make_shared<KFakeBuilds>()); });
    const auto sharedWorkflow = std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow>(std::move(workflow));
    KApplicationRpcHandler handler(createCapabilities({true, false, false, false}), sharedWorkflow,
        std::make_shared<KFakePreferences>(), std::make_shared<KFakeSessions>());

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
    response = handler.dispatch(request("trash-list", "workspace.listTrash",
        {{"workspaceId", KValue{std::string("workspace-1")}}}), std::nullopt, {});
    check(v2::validateWorkspaceTrashListResponse(response), "workspace trash list response");
    response = handler.dispatch(request("trash-restore", "workspace.restoreTrash",
        {{"workspaceId", KValue{std::string("workspace-1")}},
         {"trashId", KValue{std::string("0123456789abcdef0123456789abcdef")}}}),
        std::nullopt, {});
    check(v2::validateWorkspaceStateResponse(response) &&
        std::get<std::string>(std::get<KValue::KObject>(response.m_value).at("method").m_value) ==
            "workspace.restoreTrash", "workspace trash restore response");
    response = handler.dispatch(request("change-poll", "workspace.pollChanges",
        {{"workspaceId", KValue{std::string("workspace-1")}}}), std::nullopt, {});
    check(v2::validateWorkspacePollChangesResponse(response), "workspace native change response");
    response = handler.dispatch(request("open-document", "document.open",
        {{"fileId", KValue{std::string("章节/引言.tex")}}}, 4), std::nullopt, {});
    check(v2::validateDocumentOpenResponse(response), "document open response");
    response = handler.dispatch(request("save-document", "document.save",
        {{"fileId", KValue{std::string("章节/引言.tex")}},
         {"content", KValue{std::string("新内容")}},
         {"expectedRevision", KValue{std::string("document-1")}}}, 5), std::nullopt, {});
    check(v2::validateDocumentSaveResponse(response), "document save response");
    response = handler.dispatch(request("save-document-as", "document.saveAs",
        {{"fileId", KValue{std::string("章节/副本.tex")}},
         {"content", KValue{std::string("保留编辑")}},
         {"utf8Bom", KValue{true}}}, 7), std::nullopt, {});
    check(v2::validateDocumentSaveResponse(response) &&
        std::get<std::string>(std::get<KValue::KObject>(response.m_value).at("method").m_value) ==
            "document.saveAs", "document save as response");
    response = handler.dispatch(request("search", "search.start",
        {{"query", KValue{std::string("中文")}}, {"caseSensitive", KValue{false}},
         {"maxResults", KValue{20.0}}}), std::nullopt, {});
    check(v2::validateSearchStartResponse(response), "search response");
    response = handler.dispatch(request("detect", "build.detect"), std::nullopt, {});
    check(v2::validateBuildDetectResponse(response), "build detection response");
    response = handler.dispatch(request("build", "build.start",
        {{"jobId", KValue{std::string("job-1")}}, {"snapshotId", KValue{std::string("snapshot-1")}},
         {"mainFileId", KValue{std::string("main.tex")}}, {"engine", KValue{std::string("xelatex")}},
         {"timeoutMs", KValue{3000.0}}}), std::nullopt, {});
    check(v2::validateBuildStartResponse(response), "build start response");
    response = handler.dispatch(request("cancel", "build.cancel",
        {{"jobId", KValue{std::string("job-1")}}}), std::nullopt, {});
    check(v2::validateBuildCancelResponse(response), "build cancel response");
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

    response = handler.dispatch(request("preferences-get", "preferences.get"), std::nullopt, {});
    check(v2::validatePreferencesGetResponse(response), "preferences get response");
    response = handler.dispatch(request("preferences-update", "preferences.update",
        {{"texRoot", KValue{std::string("D:/texlive")}},
         {"engine", KValue{std::string("lualatex")}}, {"timeoutMs", KValue{45000.0}},
         {"autoCompile", KValue{true}}}), std::nullopt, {});
    check(v2::validatePreferencesGetResponse(response), "preferences update response");
    response = handler.dispatch(request("session-save", "session.save",
        {{"workspaceRoot", KValue{std::string("D:/论文")}},
         {"openFiles", KValue{KValue::KArray{KValue{std::string("main.tex")}}}},
         {"activeFile", KValue{std::string("main.tex")}},
         {"sidebarWidth", KValue{300.0}}, {"previewOpen", KValue{true}}}), std::nullopt, {});
    check(v2::validateSessionSaveResponse(response), "session save response");
    response = handler.dispatch(request("session-restore", "session.restore"), std::nullopt, {});
    check(v2::validateSessionRestoreResponse(response), "session restore response");
    response = handler.dispatch(request("session-history", "session.history"), std::nullopt, {});
    check(v2::validateSessionHistoryResponse(response), "session history response");

    check(!workspaceworkflow::createWorkspaceWorkflow({}, [](const std::string&)
        -> KResult<std::unique_ptr<document::IKDocuments>>
        { return KError{KErrorCode::Internal, "unused", false}; }), "workflow requires workspaces");
    check(!workspaceworkflow::createWorkspaceWorkflow(std::make_unique<KFakeWorkspaces>(), {}),
        "workflow requires document factory");
    return failures == 0 ? 0 : 1;
}
