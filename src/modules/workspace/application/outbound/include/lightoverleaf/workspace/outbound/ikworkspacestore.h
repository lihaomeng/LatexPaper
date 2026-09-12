#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <stop_token>
#include <vector>

namespace lightoverleaf::workspace
{
enum class KStoredMutationKind { CreateEmpty, RenameFile, RemoveFile };
struct KStoredMutation
{
    KStoredMutationKind m_kind = KStoredMutationKind::CreateEmpty;
    std::string m_workspaceId;
    std::string m_fileId;
    std::string m_destination;
};
enum class KStoredDirectoryMutationKind { Create, Rename, Remove };
struct KStoredDirectoryMutation
{
    KStoredDirectoryMutationKind m_kind = KStoredDirectoryMutationKind::Create;
    std::string m_workspaceId;
    std::string m_directoryId;
    std::string m_destination;
};
struct KStoredWorkspaceEntry
{
    std::string m_fileId;
    bool m_directory = false;
    std::uint64_t m_sizeBytes = 0;
};
struct KStoredWorkspace
{
    std::string m_id;
    std::string m_displayName;
    std::string m_revision;
    std::vector<KStoredWorkspaceEntry> m_entries;
};
struct KStoredTrashEntry
{
    std::string m_trashId;
    std::string m_originalFileId;
    bool m_directory = false;
    std::uint64_t m_deletedAtUnixMs = 0;
};
class IKWorkspaceStore
{
public:
    virtual ~IKWorkspaceStore() = default;
    // Worker-only. selection is created by a trusted native picker and is never an RPC field.
    virtual KResult<KStoredWorkspace> open(const std::string& selection, std::stop_token stop) = 0;
    virtual KResult<KStoredWorkspace> refresh(const std::string& selection,
        const std::string& workspaceId, std::stop_token stop) = 0;
    virtual void close(const std::string& workspaceId) noexcept = 0;
    virtual KResult<bool> mutate(const std::string& selection, const KStoredMutation& command,
        std::stop_token stop) = 0;
    virtual KResult<bool> mutateDirectory(const std::string& selection,
        const KStoredDirectoryMutation& command, std::stop_token stop) = 0;
    virtual KResult<std::vector<KStoredTrashEntry>> listTrash(const std::string& selection,
        const std::string& workspaceId, std::stop_token stop) = 0;
    virtual KResult<bool> restoreTrash(const std::string& selection, const std::string& workspaceId,
        const std::string& trashId, std::stop_token stop) = 0;
    virtual KResult<bool> pollChanges(const std::string& selection, const std::string& workspaceId,
        std::stop_token stop) = 0;
};
}
