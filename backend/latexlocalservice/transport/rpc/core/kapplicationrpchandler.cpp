#include <lightoverleaf/rpc/kapplicationrpchandler.h>
#include <lightoverleaf/preferences/inbound/ikpreferences.h>
#include <lightoverleaf/session/inbound/iksessions.h>
#include <lightoverleaf/export/inbound/ikexports.h>
#include <chrono>
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
    case KErrorCode::Conflict:
        if (error.m_messageKey == "build.staleGeneration") return "BUILD_STALE_GENERATION";
        if (error.m_messageKey == "build.jobExists") return "BUILD_JOB_EXISTS";
        if (error.m_messageKey == "build.snapshotExists") return "BUILD_SNAPSHOT_EXISTS";
        if (error.m_messageKey == "build.overlayConflict") return "BUILD_OVERLAY_CONFLICT";
        if (error.m_messageKey == "build.duplicateOverlay") return "BUILD_DUPLICATE_OVERLAY";
        return "FILE_CONFLICT";
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

KValue workspaceTrash(const KValue::KObject& request,
    std::vector<workspace::KWorkspaceTrashEntry> entries)
{
    KValue::KArray projection;
    projection.reserve(entries.size());
    for (workspace::KWorkspaceTrashEntry& entry : entries)
    {
        projection.emplace_back(KValue::KObject{
            {"trashId", KValue{std::move(entry.m_trashId)}},
            {"originalFileId", KValue{std::move(entry.m_originalFileId)}},
            {"directory", KValue{entry.m_directory}},
            {"deletedAtUnixMs", KValue{static_cast<double>(entry.m_deletedAtUnixMs)}}});
    }
    KValue::KObject response = responseBase(request);
    response.emplace("ok", KValue{true});
    response.emplace("method", KValue{"workspace.listTrash"});
    response.emplace("result", KValue{KValue::KObject{{"entries", KValue{std::move(projection)}}}});
    return KValue{std::move(response)};
}

const char* buildEngine(build::KBuildEngine engine)
{
    return engine == build::KBuildEngine::PdfLatex ? "pdflatex" :
        engine == build::KBuildEngine::XeLatex ? "xelatex" : "lualatex";
}

const char* buildTerminal(build::KBuildTerminal terminal)
{
    switch (terminal)
    {
    case build::KBuildTerminal::Succeeded: return "succeeded";
    case build::KBuildTerminal::Cancelled: return "cancelled";
    case build::KBuildTerminal::TimedOut: return "timedOut";
    case build::KBuildTerminal::CompilerUnavailable: return "compilerUnavailable";
    case build::KBuildTerminal::Failed: return "failed";
    }
    return "failed";
}

const char* buildState(build::KBuildState state)
{
    switch (state)
    {
    case build::KBuildState::Running: return "running";
    case build::KBuildState::Succeeded: return "succeeded";
    case build::KBuildState::Cancelled: return "cancelled";
    case build::KBuildState::TimedOut: return "timedOut";
    case build::KBuildState::CompilerUnavailable: return "compilerUnavailable";
    case build::KBuildState::Failed: return "failed";
    }
    return "failed";
}

const char* buildPhase(build::KBuildPhase phase)
{
    switch (phase)
    {
    case build::KBuildPhase::Snapshot: return "snapshot";
    case build::KBuildPhase::Detect: return "detect";
    case build::KBuildPhase::Compile: return "compile";
    case build::KBuildPhase::Artifact: return "artifact";
    case build::KBuildPhase::Render: return "render";
    case build::KBuildPhase::Complete: return "complete";
    }
    return "compile";
}

const char* diagnosticSeverity(build::KDiagnosticSeverity severity)
{
    return severity == build::KDiagnosticSeverity::Info ? "info" :
        severity == build::KDiagnosticSeverity::Warning ? "warning" : "error";
}
std::string hex(std::span<const std::uint8_t> bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.resize(bytes.size() * 2);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        result[index * 2] = digits[bytes[index] >> 4];
        result[index * 2 + 1] = digits[bytes[index] & 15];
    }
    return result;
}
}

KApplicationRpcHandler::KApplicationRpcHandler(std::shared_ptr<const IKGetCapabilities> capabilities,
    std::shared_ptr<workspaceworkflow::IKWorkspaceWorkflow> workflow,
    std::shared_ptr<preferences::IKPreferences> preferences,
    std::shared_ptr<session::IKSessions> sessions,
    std::shared_ptr<exporting::IKExports> exports)
    : m_capabilities(std::move(capabilities)), m_workflow(std::move(workflow)),
      m_preferences(std::move(preferences)), m_sessions(std::move(sessions)),
      m_exports(std::move(exports)) {}

KValue KApplicationRpcHandler::dispatch(KValue request,
    std::optional<std::string> nativeSelection, std::stop_token stop,
    const std::string& sessionId) const
{
    const auto* object = std::get_if<KValue::KObject>(&request.m_value);
    if (!object || !m_capabilities || !m_workflow) return KValue{nullptr};
    const std::string& method = std::get<std::string>(object->at("method").m_value);
    // Workspace/Document/Search remain on one logical lane. Build start and cancel
    // intentionally run concurrently so cancellation never waits behind TeX.
    std::unique_lock<std::mutex> serialized(m_dispatchMutex, std::defer_lock);
    if (method != "build.start" && method != "build.cancel") serialized.lock();
    if (method == "system.ping" || method == "system.getCapabilities")
        return dispatchSystem(request, *m_capabilities);
    if (method == "workspace.open" || method == "workspace.reopen")
    {
        if (method == "workspace.reopen")
        {
            if (!v2::validateWorkspaceReopenRequest(request)) return failure(*object, "INVALID_ARGUMENT");
            if (!m_sessions) return failure(*object, "UNAVAILABLE");
            const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
            auto location = m_sessions->workspaceLocation(std::get<std::string>(params.at("workspaceId").m_value));
            if (const KError* error = std::get_if<KError>(&location)) return failure(*object, errorCode(*error));
            nativeSelection = std::get<std::optional<std::string>>(std::move(location));
            if (!nativeSelection) return failure(*object, "NOT_FOUND");
        }
        else if (!v2::validateWorkspaceRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        if (!nativeSelection) return failure(*object, "USER_CANCELLED");
        if (stop.stop_requested()) return failure(*object, "CANCELLED");
        KResult<workspace::KWorkspaceState> result = m_workflow->open(*nativeSelection, stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        if (m_sessions)
        {
            auto remembered = m_sessions->rememberWorkspace(std::get<workspace::KWorkspaceState>(result).m_id, *nativeSelection);
            if (const KError* error = std::get_if<KError>(&remembered))
            {
                m_workflow->close();
                return failure(*object, errorCode(*error));
            }
        }
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
    if (method == "workspace.pollChanges")
    {
        if (!v2::validateWorkspacePollChangesRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        KResult<bool> result = m_workflow->pollChanges(
            std::get<std::string>(params.at("workspaceId").m_value), stop);
        if (const KError* error = std::get_if<KError>(&result))
            return failure(*object, errorCode(*error));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"changed", KValue{std::get<bool>(result)}}}});
        return KValue{std::move(response)};
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
    if (method == "workspace.listTrash")
    {
        if (!v2::validateWorkspaceTrashListRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        KResult<std::vector<workspace::KWorkspaceTrashEntry>> result = m_workflow->listTrash(
            std::get<std::string>(params.at("workspaceId").m_value), stop);
        if (const KError* error = std::get_if<KError>(&result))
            return failure(*object, errorCode(*error));
        return workspaceTrash(*object,
            std::get<std::vector<workspace::KWorkspaceTrashEntry>>(std::move(result)));
    }
    if (method == "workspace.restoreTrash")
    {
        if (!v2::validateWorkspaceTrashRestoreRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        workspace::KRestoreWorkspaceEntry command;
        command.m_workspaceId = std::get<std::string>(params.at("workspaceId").m_value);
        command.m_trashId = std::get<std::string>(params.at("trashId").m_value);
        KResult<workspace::KWorkspaceState> result = m_workflow->restoreTrash(command, stop);
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
    if (method == "document.saveAs")
    {
        if (!v2::validateDocumentSaveAsRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        document::KSaveDocumentAs command;
        command.m_fileId = std::get<std::string>(params.at("fileId").m_value);
        command.m_content = std::get<std::string>(params.at("content").m_value);
        command.m_utf8Bom = std::get<bool>(params.at("utf8Bom").m_value);
        command.m_clientSequence = static_cast<std::uint64_t>(
            std::get<double>(object->at("clientSequence").m_value));
        KResult<document::KDocumentSaved> result = m_workflow->saveDocumentAs(command, stop);
        if (const KError* error = std::get_if<KError>(&result))
            return failure(*object, errorCode(*error));
        document::KDocumentSaved saved = std::get<document::KDocumentSaved>(std::move(result));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{
            {"fileId", KValue{std::move(saved.m_fileId)}},
            {"revision", KValue{std::move(saved.m_revision)}}}});
        return KValue{std::move(response)};
    }
    if (method == "search.start")
    {
        if (!v2::validateSearchStartRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        search::KSearchCommand command;
        command.m_query = std::get<std::string>(params.at("query").m_value);
        command.m_caseSensitive = std::get<bool>(params.at("caseSensitive").m_value);
        command.m_maxResults = static_cast<std::size_t>(std::get<double>(params.at("maxResults").m_value));
        KResult<search::KSearchResult> result = m_workflow->search(command, stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        search::KSearchResult value = std::get<search::KSearchResult>(std::move(result));
        KValue::KArray hits;
        for (search::KSearchHit& hit : value.m_hits)
            hits.emplace_back(KValue::KObject{{"fileId", KValue{std::move(hit.m_fileId)}},
                {"line", KValue{static_cast<double>(hit.m_line)}},
                {"column", KValue{static_cast<double>(hit.m_column)}},
                {"preview", KValue{std::move(hit.m_preview)}}});
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"hits", KValue{std::move(hits)}},
            {"truncated", KValue{value.m_truncated}}}});
        return KValue{std::move(response)};
    }
    if (method == "build.detect")
    {
        if (!v2::validateBuildDetectRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        KResult<std::vector<build::KCompilerCapability>> result = m_workflow->detectCompilers(stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        KValue::KArray toolchains;
        for (build::KCompilerCapability& compiler :
            std::get<std::vector<build::KCompilerCapability>>(std::move(result)))
        {
            KValue::KArray engines;
            for (const build::KBuildEngine engine : compiler.m_engines)
                engines.emplace_back(std::string(buildEngine(engine)));
            toolchains.emplace_back(KValue::KObject{
                {"toolchainId", KValue{std::move(compiler.m_toolchainId)}},
                {"displayName", KValue{std::move(compiler.m_displayName)}},
                {"engines", KValue{std::move(engines)}}});
        }
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"toolchains", KValue{std::move(toolchains)}}}});
        return KValue{std::move(response)};
    }
    if (method == "build.start")
    {
        if (!v2::validateBuildStartRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        const std::string& engine = std::get<std::string>(params.at("engine").m_value);
        build::KBuildCommand command;
        command.m_jobId = std::get<std::string>(params.at("jobId").m_value);
        command.m_snapshotId = std::get<std::string>(params.at("snapshotId").m_value);
        command.m_mainFileId = std::get<std::string>(params.at("mainFileId").m_value);
        command.m_engine = engine == "pdflatex" ? build::KBuildEngine::PdfLatex :
            engine == "xelatex" ? build::KBuildEngine::XeLatex : build::KBuildEngine::LuaLatex;
        command.m_timeoutMs = static_cast<unsigned int>(std::get<double>(params.at("timeoutMs").m_value));
        command.m_scopeId = std::get<std::string>(params.at("scopeId").m_value);
        command.m_generation = static_cast<std::uint64_t>(std::get<double>(params.at("generation").m_value));
        for (const KValue& overlayValue :
            std::get<KValue::KArray>(params.at("overlayFiles").m_value))
        {
            const auto& overlay = std::get<KValue::KObject>(overlayValue.m_value);
            command.m_overlayFiles.push_back({
                std::get<std::string>(overlay.at("fileId").m_value),
                std::get<std::string>(overlay.at("content").m_value)});
        }
        KResult<build::KBuildResult> result = m_workflow->startBuild(command, stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        build::KBuildResult value = std::get<build::KBuildResult>(std::move(result));
        KValue::KArray diagnosticValues;
        for (build::KBuildDiagnostic& diagnostic : value.m_diagnostics)
            diagnosticValues.emplace_back(KValue::KObject{
                {"severity", KValue{std::string(diagnosticSeverity(diagnostic.m_severity))}},
                {"fileId", KValue{std::move(diagnostic.m_fileId)}},
                {"line", KValue{static_cast<double>(diagnostic.m_line)}},
                {"message", KValue{std::move(diagnostic.m_message)}}});
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{
            {"jobId", KValue{std::move(value.m_jobId)}},
            {"terminal", KValue{std::string(buildTerminal(value.m_terminal))}},
            {"exitCode", KValue{static_cast<double>(value.m_exitCode)}},
            {"output", KValue{std::move(value.m_output)}},
            {"outputTruncated", KValue{value.m_outputTruncated}},
            {"diagnostics", KValue{std::move(diagnosticValues)}},
            {"artifactId", KValue{std::move(value.m_artifactId)}},
            {"syncTexAvailable", KValue{value.m_syncTexAvailable}},
            {"generation", KValue{static_cast<double>(value.m_generation)}},
            {"phase", KValue{std::string(buildPhase(value.m_phase))}}}});
        return KValue{std::move(response)};
    }
    if (method == "build.status")
    {
        if (!v2::validateBuildStatusRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        KResult<build::KBuildStatus> result = m_workflow->buildStatus(
            std::get<std::string>(params.at("jobId").m_value));
        if (const KError* error = std::get_if<KError>(&result))
            return failure(*object, errorCode(*error));
        build::KBuildStatus value = std::get<build::KBuildStatus>(std::move(result));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{
            {"jobId", KValue{std::move(value.m_jobId)}},
            {"state", KValue{std::string(buildState(value.m_state))}},
            {"output", KValue{std::move(value.m_output)}},
            {"outputTruncated", KValue{value.m_outputTruncated}},
            {"generation", KValue{static_cast<double>(value.m_generation)}},
            {"phase", KValue{std::string(buildPhase(value.m_phase))}}}});
        return KValue{std::move(response)};
    }
    if (method == "build.cancel")
    {
        if (!v2::validateBuildCancelRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        KResult<bool> result = m_workflow->cancelBuild(
            std::get<std::string>(params.at("jobId").m_value));
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"accepted", KValue{std::get<bool>(result)}}}});
        return KValue{std::move(response)};
    }
    if (method == "preview.read")
    {
        if (!v2::validatePreviewReadRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        KResult<preview::KPreviewChunk> result = m_workflow->readPreview(
            std::get<std::string>(params.at("artifactId").m_value),
            static_cast<std::size_t>(std::get<double>(params.at("offset").m_value)),
            static_cast<std::size_t>(std::get<double>(params.at("count").m_value)));
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        preview::KPreviewChunk value = std::get<preview::KPreviewChunk>(std::move(result));
        KValue::KObject response = responseBase(*object); response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{
            {"offset", KValue{static_cast<double>(value.m_offset)}},
            {"totalBytes", KValue{static_cast<double>(value.m_totalBytes)}},
            {"hex", KValue{hex(value.m_bytes)}}}});
        return KValue{std::move(response)};
    }
    if (method == "navigation.forward")
    {
        if (!v2::validateNavigationForwardRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& p = std::get<KValue::KObject>(object->at("params").m_value);
        KResult<navigation::KPdfLocation> result = m_workflow->forwardSync(
            std::get<std::string>(p.at("artifactId").m_value),
            {std::get<std::string>(p.at("fileId").m_value),
             static_cast<std::size_t>(std::get<double>(p.at("line").m_value)),
             static_cast<std::size_t>(std::get<double>(p.at("column").m_value))});
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        navigation::KPdfLocation value = std::get<navigation::KPdfLocation>(result);
        KValue::KObject response = responseBase(*object); response.emplace("ok", KValue{true}); response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"page",KValue{static_cast<double>(value.m_page)}},
            {"x",KValue{static_cast<double>(value.m_x)}},{"y",KValue{static_cast<double>(value.m_y)}}}});
        return KValue{std::move(response)};
    }
    if (method == "navigation.reverse")
    {
        if (!v2::validateNavigationReverseRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& p = std::get<KValue::KObject>(object->at("params").m_value);
        KResult<navigation::KSourceLocation> result = m_workflow->reverseSync(
            std::get<std::string>(p.at("artifactId").m_value),
            {static_cast<std::size_t>(std::get<double>(p.at("page").m_value)),
             std::get<double>(p.at("x").m_value), std::get<double>(p.at("y").m_value)});
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        navigation::KSourceLocation value = std::get<navigation::KSourceLocation>(std::move(result));
        KValue::KObject response = responseBase(*object); response.emplace("ok", KValue{true}); response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"fileId",KValue{std::move(value.m_fileId)}},
            {"line",KValue{static_cast<double>(value.m_line)}},{"column",KValue{static_cast<double>(value.m_column)}}}});
        return KValue{std::move(response)};
    }
    if (method == "export.selectDestination")
    {
        if (!m_exports) return failure(*object, "FILE_OPERATION_FAILED");
        if (!v2::validateExportSelectDestinationRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        if (!nativeSelection) return failure(*object, "USER_CANCELLED");
        const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        KResult<exporting::KExportDestination> result = m_exports->authorize(sessionId, *nativeSelection, now);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        exporting::KExportDestination value = std::get<exporting::KExportDestination>(std::move(result));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true}); response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{
            {"destinationToken", KValue{std::move(value.m_token)}},
            {"displayName", KValue{std::move(value.m_displayName)}},
            {"expiresAtUnixMs", KValue{static_cast<double>(value.m_expiresAtUnixMs)}}}});
        return KValue{std::move(response)};
    }
    if (method == "export.project")
    {
        if (!m_exports) return failure(*object, "FILE_OPERATION_FAILED");
        if (!v2::validateExportProjectRequest(request)) return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        exporting::KExportCommand command;
        command.m_sessionId = sessionId;
        command.m_destinationToken = std::get<std::string>(params.at("destinationToken").m_value);
        command.m_generation = static_cast<std::uint64_t>(std::get<double>(params.at("generation").m_value));
        for (const KValue& item : std::get<KValue::KArray>(params.at("files").m_value))
        {
            const auto& file = std::get<KValue::KObject>(item.m_value);
            command.m_files.push_back({std::get<std::string>(file.at("fileId").m_value),
                std::get<std::string>(file.at("content").m_value)});
        }
        const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        KResult<exporting::KExportResult> result = m_exports->exportProject(command, now, stop);
        if (const KError* error = std::get_if<KError>(&result)) return failure(*object, errorCode(*error));
        exporting::KExportResult value = std::get<exporting::KExportResult>(std::move(result));
        KValue::KArray writtenFiles;
        for (std::string& file : value.m_writtenFiles) writtenFiles.emplace_back(std::move(file));
        KValue::KArray failures;
        for (exporting::KExportFailure& item : value.m_failures)
            failures.emplace_back(KValue::KObject{{"fileId", KValue{std::move(item.m_fileId)}},
                {"code", KValue{std::move(item.m_code)}}});
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true}); response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{
            {"generation", KValue{static_cast<double>(value.m_generation)}},
            {"complete", KValue{value.m_complete}},
            {"writtenFiles", KValue{std::move(writtenFiles)}},
            {"failures", KValue{std::move(failures)}}}});
        return KValue{std::move(response)};
    }    if (method == "preferences.get" || method == "preferences.update")
    {
        if (!m_preferences) return failure(*object, "FILE_OPERATION_FAILED");
        preferences::KPreferences value;
        if (method == "preferences.get")
        {
            if (!v2::validatePreferencesGetRequest(request))
                return failure(*object, "INVALID_ARGUMENT");
            KResult<preferences::KPreferences> result = m_preferences->get();
            if (const KError* error = std::get_if<KError>(&result))
                return failure(*object, errorCode(*error));
            value = std::get<preferences::KPreferences>(std::move(result));
        }
        else
        {
            if (!v2::validatePreferencesUpdateRequest(request))
                return failure(*object, "INVALID_ARGUMENT");
            const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
            preferences::KPreferences update;
            update.m_texRoot = std::get<std::string>(params.at("texRoot").m_value);
            update.m_engine = std::get<std::string>(params.at("engine").m_value);
            update.m_timeoutMs = static_cast<unsigned int>(
                std::get<double>(params.at("timeoutMs").m_value));
            update.m_compileMode = std::get<std::string>(params.at("compileMode").m_value);
            KResult<preferences::KPreferences> result = m_preferences->update(update);
            if (const KError* error = std::get_if<KError>(&result))
                return failure(*object, errorCode(*error));
            value = std::get<preferences::KPreferences>(std::move(result));
        }
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true});
        response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{
            {"texRoot", KValue{std::move(value.m_texRoot)}},
            {"engine", KValue{std::move(value.m_engine)}},
            {"timeoutMs", KValue{static_cast<double>(value.m_timeoutMs)}},
            {"compileMode", KValue{std::move(value.m_compileMode)}}}});
        return KValue{std::move(response)};
    }
    if (method == "session.restore")
    {
        if (!m_sessions) return failure(*object, "FILE_OPERATION_FAILED");
        if (!v2::validateSessionRestoreRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        KResult<std::optional<session::KSessionState>> result = m_sessions->restore();
        if (const KError* error = std::get_if<KError>(&result))
            return failure(*object, errorCode(*error));
        std::optional<session::KSessionState> state =
            std::get<std::optional<session::KSessionState>>(std::move(result));
        session::KSessionState value = state.value_or(session::KSessionState{});
        KValue::KArray openFiles;
        for (std::string& file : value.m_openFiles) openFiles.emplace_back(std::move(file));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true}); response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{
            {"found", KValue{state.has_value()}},
            {"state", KValue{KValue::KObject{
                {"workspaceRoot", KValue{std::move(value.m_workspaceRoot)}},
                {"openFiles", KValue{std::move(openFiles)}},
                {"activeFile", KValue{std::move(value.m_activeFile)}},
                {"sidebarWidth", KValue{static_cast<double>(value.m_sidebarWidth)}},
                {"editorWidth", KValue{static_cast<double>(value.m_editorWidth)}},
                {"previewOpen", KValue{value.m_previewOpen}},
                {"activeLine", KValue{static_cast<double>(value.m_activeLine)}},
                {"activeColumn", KValue{static_cast<double>(value.m_activeColumn)}},
                {"previewZoom", KValue{static_cast<double>(value.m_previewZoom)}}}}}}});
        return KValue{std::move(response)};
    }
    if (method == "session.save")
    {
        if (!m_sessions) return failure(*object, "FILE_OPERATION_FAILED");
        if (!v2::validateSessionSaveRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        const auto& params = std::get<KValue::KObject>(object->at("params").m_value);
        session::KSessionState state;
        state.m_workspaceRoot = std::get<std::string>(params.at("workspaceRoot").m_value);
        for (const KValue& file : std::get<KValue::KArray>(params.at("openFiles").m_value))
            state.m_openFiles.push_back(std::get<std::string>(file.m_value));
        state.m_activeFile = std::get<std::string>(params.at("activeFile").m_value);
        state.m_sidebarWidth = static_cast<unsigned int>(
            std::get<double>(params.at("sidebarWidth").m_value));
        state.m_editorWidth = static_cast<unsigned int>(
            std::get<double>(params.at("editorWidth").m_value));
        state.m_previewOpen = std::get<bool>(params.at("previewOpen").m_value);
        state.m_activeLine = static_cast<std::size_t>(
            std::get<double>(params.at("activeLine").m_value));
        state.m_activeColumn = static_cast<std::size_t>(
            std::get<double>(params.at("activeColumn").m_value));
        state.m_previewZoom = static_cast<unsigned int>(
            std::get<double>(params.at("previewZoom").m_value));
        KResult<bool> result = m_sessions->save(state);
        if (const KError* error = std::get_if<KError>(&result))
            return failure(*object, errorCode(*error));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true}); response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"saved", KValue{std::get<bool>(result)}}}});
        return KValue{std::move(response)};
    }
    if (method == "session.history")
    {
        if (!m_sessions) return failure(*object, "FILE_OPERATION_FAILED");
        if (!v2::validateSessionHistoryRequest(request))
            return failure(*object, "INVALID_ARGUMENT");
        KResult<std::vector<std::string>> result = m_sessions->history();
        if (const KError* error = std::get_if<KError>(&result))
            return failure(*object, errorCode(*error));
        KValue::KArray roots;
        for (std::string& root : std::get<std::vector<std::string>>(result))
            roots.emplace_back(std::move(root));
        KValue::KObject response = responseBase(*object);
        response.emplace("ok", KValue{true}); response.emplace("method", KValue{method});
        response.emplace("result", KValue{KValue::KObject{{"roots", KValue{std::move(roots)}}}});
        return KValue{std::move(response)};
    }
    return failure(*object, "METHOD_NOT_FOUND");
}
}
