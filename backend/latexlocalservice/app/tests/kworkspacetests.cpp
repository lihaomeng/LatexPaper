#include <lightoverleaf/workspace/inbound/ikworkspaces.h>
#include <lightoverleaf/workspace/outbound/ikworkspacestore.h>
#include <lightoverleaf/workspace/domain/kworkspacepolicy.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace lightoverleaf;
using namespace lightoverleaf::workspace;

namespace
{
class KFakeWorkspaceStore final : public IKWorkspaceStore
{
public:
    KResult<bool> mutate(const std::string&, const KStoredMutation&, std::stop_token) override { return true; }
    KResult<bool> mutateDirectory(const std::string&, const KStoredDirectoryMutation&,
        std::stop_token) override { return true; }
    KResult<std::vector<KStoredTrashEntry>> listTrash(const std::string&, const std::string&,
        std::stop_token) override { return m_trash; }
    KResult<bool> restoreTrash(const std::string&, const std::string&,
        const std::string&, std::stop_token) override { return true; }
    KResult<bool> pollChanges(const std::string&, const std::string&,
        std::stop_token) override { return m_changed; }
    KResult<KStoredWorkspace> refresh(const std::string&, const std::string&, std::stop_token stop) override
    {
        ++m_refreshes;
        if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "cancelled", true};
        if (m_refreshFailure) return KError{KErrorCode::Unavailable, "refresh", true};
        return m_value;
    }
    KResult<KStoredWorkspace> open(const std::string&, std::stop_token) override
    {
        ++m_opens;
        if (m_throw) throw std::runtime_error("fake");
        if (m_afterOpen) m_afterOpen->request_stop();
        return m_value;
    }
    void close(const std::string& workspaceId) noexcept override
    {
        m_closed.push_back(workspaceId);
    }

public:
    KStoredWorkspace m_value{"workspace-1", "中文项目", "revision-1",
        {{"z.tex", false, 4}, {"figures", true, 0}, {"a.tex", false, 2}}};
    std::vector<std::string> m_closed;
    unsigned int m_opens = 0;
    unsigned int m_refreshes = 0;
    bool m_throw = false;
    bool m_refreshFailure = false;
    std::vector<KStoredTrashEntry> m_trash;
    bool m_changed = false;
    std::stop_source* m_afterOpen = nullptr;
};
template<class T> bool failed(const KResult<T>& result, KErrorCode code)
{
    const KError* error = std::get_if<KError>(&result);
    return error && error->m_code == code;
}
}

int main()
{
    int failures = 0;
    const auto check = [&](bool valid, const char* label)
    {
        if (!valid) { ++failures; std::cerr << label << '\n'; }
    };
    const auto store = std::make_shared<KFakeWorkspaceStore>();
    {
        const auto workspaces = createWorkspaces(store);
        check(store->m_opens == 0, "construction has no I/O");
        const KResult<KWorkspaceState> opened = workspaces->open("native-picker-selection");
        check(std::holds_alternative<KWorkspaceState>(opened), "open");
        const KWorkspaceState& state = std::get<KWorkspaceState>(opened);
        check(state.m_entries.size() == 3 && state.m_entries[0].m_directory &&
            state.m_entries[1].m_fileId == "a.tex", "deterministic projection");
        store->m_trash = {{"0123456789abcdef0123456789abcdef", "旧稿.tex", false, 100}};
        const KResult<std::vector<KWorkspaceTrashEntry>> trash = workspaces->listTrash("workspace-1");
        check(std::holds_alternative<std::vector<KWorkspaceTrashEntry>>(trash) &&
            std::get<std::vector<KWorkspaceTrashEntry>>(trash)[0].m_originalFileId == "旧稿.tex",
            "trash projection through fake store");
        check(workspaces->state() && workspaces->state()->m_displayName == "中文项目", "state snapshot");
        store->m_value = {"workspace-2", "next", "revision-2", {}};
        check(std::holds_alternative<KWorkspaceState>(workspaces->open("next-selection")), "replace active workspace");
        check(store->m_closed.size() == 1 && store->m_closed.front() == "workspace-1", "old workspace closed after success");
        store->m_value = {"workspace-3", "bad", "revision-3", {{"A.tex", false, 1}, {"a.tex", false, 1}}};
        check(failed(workspaces->open("bad-selection"), KErrorCode::Internal), "case-fold duplicate rejected");
        check(workspaces->state() && workspaces->state()->m_id == "workspace-2", "failed open preserves active state");
        check(store->m_closed.back() == "workspace-3", "invalid adapter result released");
        std::stop_source stop;
        stop.request_stop();
        const unsigned int opens = store->m_opens;
        check(failed(workspaces->open("cancelled", stop.get_token()), KErrorCode::Cancelled), "early cancellation");
        check(store->m_opens == opens, "early cancellation avoids store");
        std::stop_source late;
        store->m_afterOpen = &late;
        store->m_value = {"workspace-4", "late", "revision-4", {}};
        check(failed(workspaces->open("late", late.get_token()), KErrorCode::Cancelled), "late cancellation");
        check(store->m_closed.back() == "workspace-4", "late cancellation releases new handle");
        store->m_afterOpen = nullptr;
        store->m_throw = true;
        check(failed(workspaces->open("throw", {}), KErrorCode::Internal), "adapter exception mapped");
        workspaces->close();
        workspaces->close();
        check(store->m_closed.back() == "workspace-2", "idempotent close");
    }
    check(failed(createWorkspaces(store)->open(std::string(kMaxWorkspaceSelectionBytes + 1, 'x')),
        KErrorCode::InvalidArgument), "selection bound");
    check(!createWorkspaces(nullptr), "missing store rejected");
    const auto mutationStore = std::make_shared<KFakeWorkspaceStore>();
    const auto managed = createWorkspaces(mutationStore);
    check(failed(managed->refresh(), KErrorCode::Unavailable), "refresh requires open workspace");
    check(std::holds_alternative<KWorkspaceState>(managed->open("fake-selection")), "mutation fake open");
    mutationStore->m_value.m_revision = "revision-refreshed";
    mutationStore->m_value.m_entries.push_back({"external.tex", false, 3});
    check(std::holds_alternative<KWorkspaceState>(managed->refresh()) &&
        managed->state()->m_revision == "revision-refreshed" && mutationStore->m_refreshes == 1,
        "refresh replaces tree through fake store");
    mutationStore->m_refreshFailure = true;
    check(failed(managed->refresh(), KErrorCode::Unavailable) &&
        managed->state()->m_revision == "revision-refreshed", "failed refresh preserves state");
    mutationStore->m_refreshFailure = false;
    mutationStore->m_value.m_id = "workspace-other";
    check(failed(managed->refresh(), KErrorCode::Internal) &&
        managed->state()->m_id == "workspace-1", "refresh rejects changed root identity");
    mutationStore->m_value.m_id = "workspace-1";
    std::stop_source stoppedRefresh;
    stoppedRefresh.request_stop();
    const unsigned int refreshes = mutationStore->m_refreshes;
    check(failed(managed->refresh(stoppedRefresh.get_token()), KErrorCode::Cancelled) &&
        mutationStore->m_refreshes == refreshes, "early refresh cancellation avoids store");
    check(std::holds_alternative<KWorkspaceState>(managed->mutate({KWorkspaceMutationKind::CreateEmpty,
        "workspace-1", "new.tex", ""})), "fake store substitutes create");
    check(std::holds_alternative<KWorkspaceState>(managed->mutate({KWorkspaceMutationKind::RenameFile,
        "workspace-1", "new.tex", "renamed.tex"})), "fake store substitutes rename");
    check(std::holds_alternative<KWorkspaceState>(managed->mutate({KWorkspaceMutationKind::RemoveFile,
        "workspace-1", "renamed.tex", ""})), "fake store substitutes remove");
    check(managed->state()->m_entries.size() == 4, "mutation state projected consistently");
    check(std::holds_alternative<KWorkspaceState>(managed->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Create, "workspace-1", "parts", ""})),
        "fake store substitutes directory create");
    check(std::holds_alternative<KWorkspaceState>(managed->mutate(
        {KWorkspaceMutationKind::CreateEmpty, "workspace-1", "parts/note.tex", ""})),
        "file can be projected below directory");
    const KResult<KWorkspaceState> renamedDirectory = managed->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Rename, "workspace-1", "parts", "chapters"});
    const std::optional<KWorkspaceState> renamedState = managed->state();
    check(std::holds_alternative<KWorkspaceState>(renamedDirectory) && renamedState &&
        std::any_of(renamedState->m_entries.begin(), renamedState->m_entries.end(),
            [](const KWorkspaceEntry& entry) { return entry.m_fileId == "chapters/note.tex"; }),
        "directory rename projects descendants");
    check(failed(managed->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Rename, "workspace-1", "chapters", "chapters/nested"}),
        KErrorCode::InvalidArgument), "directory cannot move into itself");
    check(std::holds_alternative<KWorkspaceState>(managed->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Remove, "workspace-1", "chapters", ""})) &&
        managed->state()->m_entries.size() == 4, "directory remove projects descendants");
    mutationStore->m_trash = {{"0123456789abcdef0123456789abcdef", "restored.tex", false, 100}};
    mutationStore->m_refreshFailure = true;
    const KResult<KWorkspaceState> restoredWithoutRefresh = managed->restoreTrash(
        {"workspace-1", "0123456789abcdef0123456789abcdef"});
    const std::optional<KWorkspaceState> restoredState = managed->state();
    check(std::holds_alternative<KWorkspaceState>(restoredWithoutRefresh) && restoredState &&
        std::any_of(restoredState->m_entries.begin(), restoredState->m_entries.end(),
            [](const KWorkspaceEntry& entry) { return entry.m_fileId == "restored.tex"; }),
        "committed restore is not reported as failed when post-commit refresh fails");
    mutationStore->m_refreshFailure = false;
    for (const std::string& invalidName : {"CON.tex", "aux.txt", "LPT1", "COM¹.tex", "wild*.tex"})
        check(!validWorkspaceFileId(invalidName), "reserved device or wildcard rejected");
    return failures == 0 ? 0 : 1;
}
