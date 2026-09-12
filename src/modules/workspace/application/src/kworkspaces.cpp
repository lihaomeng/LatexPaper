#include <lightoverleaf/workspace/inbound/ikworkspaces.h>
#include <lightoverleaf/workspace/outbound/ikworkspacestore.h>
#include <lightoverleaf/workspace/domain/kworkspacepolicy.h>
#include <algorithm>
#include <set>

namespace lightoverleaf::workspace
{
namespace
{
KError invalid()
{
    return {KErrorCode::InvalidArgument, "workspace.invalidArgument", false};
}

KError cancelled()
{
    return {KErrorCode::Cancelled, "workspace.cancelled", true};
}

KError internal()
{
    return {KErrorCode::Internal, "workspace.storeFailure", false};
}

bool validStored(const KStoredWorkspace& value)
{
    if (!validWorkspaceId(value.m_id) || !validWorkspaceId(value.m_revision) ||
        value.m_displayName.empty() || value.m_displayName.size() > 255 ||
        !validWorkspaceText(value.m_displayName) || value.m_entries.size() > kMaxWorkspaceEntries)
        return false;
    std::set<std::string> identities;
    for (const KStoredWorkspaceEntry& entry : value.m_entries)
    {
        if (!validWorkspaceFileId(entry.m_fileId) || (entry.m_directory && entry.m_sizeBytes != 0))
            return false;
        std::string identity = entry.m_fileId;
        std::transform(identity.begin(), identity.end(), identity.begin(), [](unsigned char byte)
        {
            return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a') : static_cast<char>(byte);
        });
        if (!identities.emplace(std::move(identity)).second) return false;
    }
    return true;
}

bool inDirectory(const std::string& fileId, const std::string& directoryId)
{
    return fileId == directoryId || (fileId.size() > directoryId.size() &&
        fileId.compare(0, directoryId.size(), directoryId) == 0 && fileId[directoryId.size()] == '/');
}

bool validTrashId(const std::string& value)
{
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](const unsigned char byte)
    {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    });
}
}

class KWorkspaces final : public IKWorkspaces
{
public:
    explicit KWorkspaces(std::shared_ptr<IKWorkspaceStore> store) : m_store(std::move(store)) {}
    ~KWorkspaces() override { close(); }
    KResult<KWorkspaceState> open(const std::string& selection, std::stop_token stop) override
    {
        if (selection.empty() || selection.size() > kMaxWorkspaceSelectionBytes || !validWorkspaceText(selection))
            return invalid();
        if (stop.stop_requested()) return cancelled();
        try
        {
            KResult<KStoredWorkspace> result = m_store->open(selection, stop);
            if (const KError* error = std::get_if<KError>(&result)) return *error;
            KStoredWorkspace stored = std::move(std::get<KStoredWorkspace>(result));
            if (!validStored(stored))
            {
                if (validWorkspaceId(stored.m_id)) m_store->close(stored.m_id);
                return internal();
            }
            if (stop.stop_requested())
            {
                m_store->close(stored.m_id);
                return cancelled();
            }
            std::sort(stored.m_entries.begin(), stored.m_entries.end(), [](const auto& left, const auto& right)
            {
                if (left.m_directory != right.m_directory) return left.m_directory > right.m_directory;
                return left.m_fileId < right.m_fileId;
            });
            KWorkspaceState next;
            next.m_id = stored.m_id;
            next.m_displayName = std::move(stored.m_displayName);
            next.m_revision = std::move(stored.m_revision);
            next.m_entries.reserve(stored.m_entries.size());
            for (KStoredWorkspaceEntry& entry : stored.m_entries)
                next.m_entries.push_back({std::move(entry.m_fileId), entry.m_directory, entry.m_sizeBytes});
            if (m_state) m_store->close(m_state->m_id);
            m_state = std::move(next);
            m_selection = selection;
            return *m_state;
        }
        catch (...)
        {
            return internal();
        }
    }
    std::optional<KWorkspaceState> state() const override
    {
        return m_state;
    }
    KResult<KWorkspaceState> refresh(std::stop_token stop) override
    {
        if (!m_state) return KError{KErrorCode::Unavailable, "workspace.notOpen", false};
        if (stop.stop_requested()) return cancelled();
        try
        {
            KResult<KStoredWorkspace> result = m_store->refresh(m_selection, m_state->m_id, stop);
            if (const KError* failure = std::get_if<KError>(&result)) return *failure;
            KStoredWorkspace stored = std::move(std::get<KStoredWorkspace>(result));
            if (!validStored(stored) || stored.m_id != m_state->m_id) return internal();
            if (stop.stop_requested()) return cancelled();
            std::sort(stored.m_entries.begin(), stored.m_entries.end(), [](const auto& left, const auto& right)
            {
                if (left.m_directory != right.m_directory) return left.m_directory > right.m_directory;
                return left.m_fileId < right.m_fileId;
            });
            KWorkspaceState next;
            next.m_id = stored.m_id;
            next.m_displayName = std::move(stored.m_displayName);
            next.m_revision = std::move(stored.m_revision);
            next.m_entries.reserve(stored.m_entries.size());
            for (KStoredWorkspaceEntry& entry : stored.m_entries)
                next.m_entries.push_back({std::move(entry.m_fileId), entry.m_directory, entry.m_sizeBytes});
            m_state = std::move(next);
            return *m_state;
        }
        catch (...)
        {
            return internal();
        }
    }
    KResult<KWorkspaceState> mutate(const KWorkspaceMutation& command, std::stop_token stop) override
    {
        if (!m_state) return KError{KErrorCode::Unavailable, "workspace.notOpen", false};
        if (command.m_workspaceId != m_state->m_id) return invalid();
        if (!validWorkspaceFileId(command.m_fileId) ||
            (command.m_kind == KWorkspaceMutationKind::RenameFile && !validWorkspaceFileId(command.m_destination)) ||
            (command.m_kind != KWorkspaceMutationKind::RenameFile && !command.m_destination.empty())) return invalid();
        if (stop.stop_requested()) return cancelled();
        try
        {
            KWorkspaceState next = *m_state;
            auto source = std::find_if(next.m_entries.begin(), next.m_entries.end(), [&](const KWorkspaceEntry& entry)
            {
                return entry.m_fileId == command.m_fileId;
            });
            if (command.m_kind == KWorkspaceMutationKind::CreateEmpty)
            {
                if (next.m_entries.size() >= kMaxWorkspaceEntries)
                    return KError{KErrorCode::ResourceExhausted, "workspace.tooManyEntries", false};
                if (source != next.m_entries.end()) return KError{KErrorCode::Conflict, "workspace.exists", false};
                next.m_entries.push_back({command.m_fileId, false, 0});
            }
            else if (command.m_kind == KWorkspaceMutationKind::RenameFile || command.m_kind == KWorkspaceMutationKind::RemoveFile)
            {
                if (source == next.m_entries.end()) return KError{KErrorCode::NotFound, "workspace.notFound", false};
                if (source->m_directory) return invalid();
                if (command.m_kind == KWorkspaceMutationKind::RenameFile)
                {
                    if (std::any_of(next.m_entries.begin(), next.m_entries.end(), [&](const KWorkspaceEntry& entry)
                        { return entry.m_fileId == command.m_destination; }))
                        return KError{KErrorCode::Conflict, "workspace.exists", false};
                    source->m_fileId = command.m_destination;
                }
                else next.m_entries.erase(source);
            }
            else return invalid();
            next.m_revision = "mutation-" + std::to_string(++m_mutationSequence);
            std::sort(next.m_entries.begin(), next.m_entries.end(), [](const auto& left, const auto& right)
            {
                if (left.m_directory != right.m_directory) return left.m_directory > right.m_directory;
                return left.m_fileId < right.m_fileId;
            });
            const KStoredMutation stored{command.m_kind == KWorkspaceMutationKind::CreateEmpty ? KStoredMutationKind::CreateEmpty :
                command.m_kind == KWorkspaceMutationKind::RenameFile ? KStoredMutationKind::RenameFile : KStoredMutationKind::RemoveFile,
                command.m_workspaceId, command.m_fileId, command.m_destination};
            KResult<bool> result = m_store->mutate(m_selection, stored, stop);
            if (const KError* failure = std::get_if<KError>(&result)) return *failure;
            if (!std::get<bool>(result)) return internal();
            // Commit succeeded: late cancellation must not report an unperformed mutation.
            m_state = std::move(next);
            return *m_state;
        }
        catch (...)
        {
            return internal();
        }
    }
    KResult<KWorkspaceState> mutateDirectory(const KWorkspaceDirectoryMutation& command,
        std::stop_token stop) override
    {
        if (!m_state) return KError{KErrorCode::Unavailable, "workspace.notOpen", false};
        if (command.m_workspaceId != m_state->m_id) return invalid();
        if (!validWorkspaceFileId(command.m_directoryId) ||
            (command.m_kind == KWorkspaceDirectoryMutationKind::Rename &&
                !validWorkspaceFileId(command.m_destination)) ||
            (command.m_kind != KWorkspaceDirectoryMutationKind::Rename && !command.m_destination.empty()) ||
            (command.m_kind == KWorkspaceDirectoryMutationKind::Rename &&
                inDirectory(command.m_destination, command.m_directoryId)))
            return invalid();
        if (stop.stop_requested()) return cancelled();
        try
        {
            KWorkspaceState next = *m_state;
            auto source = std::find_if(next.m_entries.begin(), next.m_entries.end(),
                [&](const KWorkspaceEntry& entry) { return entry.m_fileId == command.m_directoryId; });
            if (command.m_kind == KWorkspaceDirectoryMutationKind::Create)
            {
                if (next.m_entries.size() >= kMaxWorkspaceEntries)
                    return KError{KErrorCode::ResourceExhausted, "workspace.tooManyEntries", false};
                if (source != next.m_entries.end()) return KError{KErrorCode::Conflict, "workspace.exists", false};
                next.m_entries.push_back({command.m_directoryId, true, 0});
            }
            else
            {
                if (source == next.m_entries.end()) return KError{KErrorCode::NotFound, "workspace.notFound", false};
                if (!source->m_directory) return invalid();
                if (command.m_kind == KWorkspaceDirectoryMutationKind::Rename)
                {
                    if (std::any_of(next.m_entries.begin(), next.m_entries.end(), [&](const KWorkspaceEntry& entry)
                        { return entry.m_fileId == command.m_destination; }))
                        return KError{KErrorCode::Conflict, "workspace.exists", false};
                    for (KWorkspaceEntry& entry : next.m_entries)
                    {
                        if (!inDirectory(entry.m_fileId, command.m_directoryId)) continue;
                        entry.m_fileId = command.m_destination +
                            entry.m_fileId.substr(command.m_directoryId.size());
                    }
                }
                else if (command.m_kind == KWorkspaceDirectoryMutationKind::Remove)
                {
                    std::erase_if(next.m_entries, [&](const KWorkspaceEntry& entry)
                        { return inDirectory(entry.m_fileId, command.m_directoryId); });
                }
                else return invalid();
            }
            next.m_revision = "mutation-" + std::to_string(++m_mutationSequence);
            std::sort(next.m_entries.begin(), next.m_entries.end(), [](const auto& left, const auto& right)
            {
                if (left.m_directory != right.m_directory) return left.m_directory > right.m_directory;
                return left.m_fileId < right.m_fileId;
            });
            const KStoredDirectoryMutation stored{
                command.m_kind == KWorkspaceDirectoryMutationKind::Create ? KStoredDirectoryMutationKind::Create :
                command.m_kind == KWorkspaceDirectoryMutationKind::Rename ? KStoredDirectoryMutationKind::Rename :
                KStoredDirectoryMutationKind::Remove,
                command.m_workspaceId, command.m_directoryId, command.m_destination};
            KResult<bool> result = m_store->mutateDirectory(m_selection, stored, stop);
            if (const KError* failure = std::get_if<KError>(&result)) return *failure;
            if (!std::get<bool>(result)) return internal();
            m_state = std::move(next);
            return *m_state;
        }
        catch (...)
        {
            return internal();
        }
    }
    KResult<std::vector<KWorkspaceTrashEntry>> listTrash(const std::string& workspaceId,
        std::stop_token stop) override
    {
        if (!m_state) return KError{KErrorCode::Unavailable, "workspace.notOpen", false};
        if (workspaceId != m_state->m_id) return invalid();
        if (stop.stop_requested()) return cancelled();
        try
        {
            KResult<std::vector<KStoredTrashEntry>> result =
                m_store->listTrash(m_selection, workspaceId, stop);
            if (const KError* failure = std::get_if<KError>(&result)) return *failure;
            std::vector<KWorkspaceTrashEntry> entries;
            for (KStoredTrashEntry& stored : std::get<std::vector<KStoredTrashEntry>>(result))
            {
                if (!validTrashId(stored.m_trashId) || !validWorkspaceFileId(stored.m_originalFileId) ||
                    stored.m_deletedAtUnixMs == 0)
                    return internal();
                entries.push_back({std::move(stored.m_trashId), std::move(stored.m_originalFileId),
                    stored.m_directory, stored.m_deletedAtUnixMs});
            }
            std::sort(entries.begin(), entries.end(), [](const KWorkspaceTrashEntry& left,
                const KWorkspaceTrashEntry& right)
            {
                if (left.m_deletedAtUnixMs != right.m_deletedAtUnixMs)
                    return left.m_deletedAtUnixMs > right.m_deletedAtUnixMs;
                return left.m_trashId < right.m_trashId;
            });
            return entries;
        }
        catch (...)
        {
            return internal();
        }
    }
    KResult<KWorkspaceState> restoreTrash(const KRestoreWorkspaceEntry& command,
        std::stop_token stop) override
    {
        if (!m_state) return KError{KErrorCode::Unavailable, "workspace.notOpen", false};
        if (command.m_workspaceId != m_state->m_id || !validTrashId(command.m_trashId))
            return invalid();
        if (stop.stop_requested()) return cancelled();
        try
        {
            KResult<std::vector<KWorkspaceTrashEntry>> indexed = listTrash(command.m_workspaceId, stop);
            if (const KError* failure = std::get_if<KError>(&indexed)) return *failure;
            const auto& entries = std::get<std::vector<KWorkspaceTrashEntry>>(indexed);
            const auto indexedEntry = std::find_if(entries.begin(), entries.end(),
                [&](const KWorkspaceTrashEntry& entry) { return entry.m_trashId == command.m_trashId; });
            if (indexedEntry == entries.end())
                return KError{KErrorCode::NotFound, "workspace.trashNotFound", false};
            KWorkspaceState committed = *m_state;
            if (std::any_of(committed.m_entries.begin(), committed.m_entries.end(),
                [&](const KWorkspaceEntry& entry) { return entry.m_fileId == indexedEntry->m_originalFileId; }))
                return KError{KErrorCode::Conflict, "workspace.exists", false};
            KResult<bool> restored = m_store->restoreTrash(m_selection, command.m_workspaceId,
                command.m_trashId, stop);
            if (const KError* failure = std::get_if<KError>(&restored)) return *failure;
            if (!std::get<bool>(restored)) return internal();
            // The adapter commit is now authoritative. A post-commit refresh is best effort:
            // it must never turn a successful restore into an error that invites a duplicate retry.
            committed.m_entries.push_back({indexedEntry->m_originalFileId, indexedEntry->m_directory, 0});
            committed.m_revision = "mutation-" + std::to_string(++m_mutationSequence);
            std::sort(committed.m_entries.begin(), committed.m_entries.end(), [](const auto& left, const auto& right)
            {
                if (left.m_directory != right.m_directory) return left.m_directory > right.m_directory;
                return left.m_fileId < right.m_fileId;
            });
            m_state = std::move(committed);
            KResult<KWorkspaceState> rescanned = refresh({});
            if (const KWorkspaceState* refreshed = std::get_if<KWorkspaceState>(&rescanned)) return *refreshed;
            return *m_state;
        }
        catch (...)
        {
            return internal();
        }
    }
    KResult<bool> pollChanges(const std::string& workspaceId, std::stop_token stop) override
    {
        if (!m_state) return KError{KErrorCode::Unavailable, "workspace.notOpen", false};
        if (workspaceId != m_state->m_id) return invalid();
        if (stop.stop_requested()) return cancelled();
        try { return m_store->pollChanges(m_selection, workspaceId, stop); }
        catch (...) { return internal(); }
    }
    void close() noexcept override
    {
        if (!m_state) return;
        m_store->close(m_state->m_id);
        m_state.reset();
        m_selection.clear();
    }

private:
    std::shared_ptr<IKWorkspaceStore> m_store;
    std::optional<KWorkspaceState> m_state;
    std::string m_selection;
    std::uint64_t m_mutationSequence = 0;
};

std::unique_ptr<IKWorkspaces> createWorkspaces(std::shared_ptr<IKWorkspaceStore> store)
{
    if (!store) return nullptr;
    return std::make_unique<KWorkspaces>(std::move(store));
}
}
