#include <lightoverleaf/rpc/kapplicationrpchandler.h>
#include <lightoverleaf/workspaceworkflow/inbound/ikworkspaceworkflow.h>

namespace lightoverleaf::rpc
{
namespace
{
KValue::KObject responseBase(const KValue::KObject& request)
{
    return {{"version", KValue{2.0}}, {"id", request.at("id")},
        {"clientSequence", request.at("clientSequence")}};
}

const char* errorCode(const KError& error)
{
    switch (error.m_code)
    {
    case KErrorCode::InvalidArgument:
    case KErrorCode::InvalidEncoding: return "INVALID_ARGUMENT";
    case KErrorCode::NotFound: return "FILE_NOT_FOUND";
    case KErrorCode::Conflict: return "FILE_CONFLICT";
    case KErrorCode::Unavailable: return error.m_messageKey == "workspace.notOpen" ? "WORKSPACE_NOT_OPEN" : "FILE_OPERATION_FAILED";
    case KErrorCode::ResourceExhausted: return "RESOURCE_EXHAUSTED";
    case KErrorCode::Cancelled: return "USER_CANCELLED";
    case KErrorCode::Internal: return "INTERNAL_ERROR";
    }
    return "INTERNAL_ERROR";
}

KValue failure(const KValue::KObject& request, const char* code)
{
    return makeErrorResponse(KValue{request}, code);
}

KValue workspaceState(const KValue::KObject& request, const std::string& method,
    workspace::KWorkspaceState state)
{
    KValue::KArray entries;
    entries.reserve(state.m_entries.size());
    for (workspace::KWorkspaceEntry& entry : state.m_entries)
    {
        entries.emplace_back(KValue::KObject{{"fileId", KValue{std::move(entry.m_fileId)}},
            {"directory", KValue{entry.m_directory}},
            {"sizeBytes", KValue{static_cast<double>(entry.m_sizeBytes)}}});
    }
    KValue::KObject response = responseBase(request);
    response.emplace("ok", KValue{true});
    response.emplace("method", KValue{method});
    response.emplace("result", KValue{KValue::KObject{
        {"workspaceId", KValue{std::move(state.m_id)}},
        {"displayName", KValue{std::move(state.m_displayName)}},
        {"revision", KValue{std::move(state.m_revision)}},
        {"entries", KValue{std::move(entries)}}}});
    return KValue{std::move(response)};
}
}

KApplicationRpcHandler::KApplicationRpcHandler(std::shared_ptr<const IKGetCapabilities> capabilities,
    std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow> workflow)
    : m_capabilities(std::move(capabilities)), m_workflow(std::move(workflow)) {}

KValue KApplicationRpcHandler::dispatch(KValue request,
    std::optional<std::string> nativeSelection, std::stop_token stop) const
{
    const auto* object = std::get_if<KValue::KObject>(&request.m_value);
    if (!object || !m_capabilities || !m_workflow) return KValue{nullptr};
    const std::string& method = std::get<std::string>(object->at("method").m_value);
    if (method == "system.ping" || method == "system.getCapabilities")
        return dispatchSystem(request, *m_capabilities);
    if (method == "workspace.open")
    {
        if (!v2::validateWorkspaceRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        if (!nativeSelection) return failure(*object, "USER_CANCELLED");
        KResult<workspace::KWorkspaceState> result = m_workflow->open(*nativeSelection, stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        return workspaceState(*object, method, std::get<workspace::KWorkspaceState>(std::move(result)));
    }
    if (method == "workspace.getState")
    {
        if (!v2::validateWorkspaceRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        std::optional<workspace::KWorkspaceState> state = m_workflow->state();
        if (!state) return failure(*object, "WORKSPACE_NOT_OPEN");
        return workspaceState(*object, method, std::move(*state));
    }
    if (method == "workspace.refresh")
    {
        if (!v2::validateWorkspaceRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        KResult<workspace::KWorkspaceState> result = m_workflow->refresh(stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        return workspaceState(*object, method, std::get<workspace::KWorkspaceState>(std::move(result)));
    }
    if (method == "workspace.manageFile")
    {
        if (!v2::validateWorkspaceManageRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        const std::string& operation = std::get<std::string>(params.at("operation").m_value);
        workspace::KWorkspaceMutation command;
        command.m_kind = operation == "create" ? workspace::KWorkspaceMutationKind::CreateEmpty :
            operation == "rename" ? workspace::KWorkspaceMutationKind::RenameFile : workspace::KWorkspaceMutationKind::RemoveFile;
        command.m_workspaceId = std::get<std::string>(params.at("workspaceId").m_value);
        command.m_fileId = std::get<std::string>(params.at("fileId").m_value);
        command.m_destination = std::get<std::string>(params.at("destination").m_value);
        KResult<workspace::KWorkspaceState> result = m_workflow->mutate(command, stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        return workspaceState(*object, method, std::get<workspace::KWorkspaceState>(std::move(result)));
    }
    if (method == "workspace.manageDirectory")
    {
        if (!v2::validateWorkspaceManageDirectoryRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        const std::string& operation = std::get<std::string>(params.at("operation").m_value);
        workspace::KWorkspaceDirectoryMutation command;
        command.m_kind = operation == "create" ? workspace::KWorkspaceDirectoryMutationKind::Create :
            operation == "rename" ? workspace::KWorkspaceDirectoryMutationKind::Rename :
                workspace::KWorkspaceDirectoryMutationKind::Remove;
        command.m_workspaceId = std::get<std::string>(params.at("workspaceId").m_value);
        command.m_directoryId = std::get<std::string>(params.at("directoryId").m_value);
        command.m_destination = std::get<std::string>(params.at("destination").m_value);
        KResult<workspace::KWorkspaceState> result = m_workflow->mutateDirectory(command, stop);
        if (const KError* error = std::get_if<KError>(&result))
            return failure(*object, errorCode(*error));
        return workspaceState(*object, method,
            std::get<workspace::KWorkspaceState>(std::move(result)));
    }
    if (method == "workspace.close")
    {
        if (!v2::validateWorkspaceRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        m_workflow->close();
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{}});
        return KValue{std::move(response)};
    }
    if (method == "document.open")
    {
        if (!v2::validateDocumentOpenRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        const std::string& fileId = std::get<std::string>(params.at("fileId").m_value);
        KResult<document::KDocumentSnapshot> result = m_workflow->openDocument(fileId, stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        document::KDocumentSnapshot opened = std::get<document::KDocumentSnapshot>(std::move(result));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"fileId", KValue{std::move(opened.m_fileId)}},
            {"content", KValue{std::move(opened.m_content)}}, {"revision", KValue{std::move(opened.m_revision)}},
            {"utf8Bom", KValue{opened.m_utf8Bom}}}});
        return KValue{std::move(response)};
    }
    if (method == "document.save")
    {
        if (!v2::validateDocumentSaveRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        document::KSaveDocument command;
        command.m_fileId = std::get<std::string>(params.at("fileId").m_value);
        command.m_content = std::get<std::string>(params.at("content").m_value);
        command.m_expectedRevision = std::get<std::string>(params.at("expectedRevision").m_value);
        command.m_clientSequence = static_cast<std::uint64_t>(
            std::get<double>(object->at("clientSequence").m_value));
        KResult<document::KDocumentSaved> result = m_workflow->saveDocument(command, stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        document::KDocumentSaved saved = std::get<document::KDocumentSaved>(std::move(result));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"fileId", KValue{std::move(saved.m_fileId)}},
            {"revision", KValue{std::move(saved.m_revision)}}}});
        return KValue{std::move(response)};
    }
    return failure(*object, "METHOD_NOT_FOUND");
}
}
