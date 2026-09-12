#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace lightoverleaf::workspace
{
enum class KWorkspaceMutationKind { CreateEmpty, RenameFile, RemoveFile };
struct KWorkspaceMutation
{
    KWorkspaceMutationKind m_kind = KWorkspaceMutationKind::CreateEmpty;
    std::string m_workspaceId;
    std::string m_fileId;
    std::string m_destination;
};
enum class KWorkspaceDirectoryMutationKind { Create, Rename, Remove };
struct KWorkspaceDirectoryMutation
{
    KWorkspaceDirectoryMutationKind m_kind = KWorkspaceDirectoryMutationKind::Create;
    std::string m_workspaceId;
    std::string m_directoryId;
    std::string m_destination;
};
class IKWorkspaceStore;
struct KWorkspaceEntry
{
    std::string m_fileId;
    bool m_directory = false;
    std::uint64_t m_sizeBytes = 0;
};
struct KWorkspaceState
{
    std::string m_id;
    std::string m_displayName;
    std::string m_revision;
    std::vector<KWorkspaceEntry> m_entries;
};
class IKWorkspaces
{
public:
    virtual ~IKWorkspaces() = default;
    virtual KResult<KWorkspaceState> open(const std::string& nativeSelection, std::stop_token stop = {}) = 0;
    virtual std::optional<KWorkspaceState> state() const = 0;
    virtual KResult<KWorkspaceState> refresh(std::stop_token stop = {}) = 0;
    virtual KResult<KWorkspaceState> mutate(const KWorkspaceMutation& command, std::stop_token stop = {}) = 0;
    virtual KResult<KWorkspaceState> mutateDirectory(const KWorkspaceDirectoryMutation& command,
        std::stop_token stop = {}) = 0;
    virtual void close() noexcept = 0;
};
std::unique_ptr<IKWorkspaces> createWorkspaces(std::shared_ptr<IKWorkspaceStore> store);
}
