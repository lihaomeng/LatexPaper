#pragma once
#include <lightoverleaf/build/outbound/ikbuildsnapshotstore.h>
#include <lightoverleaf/build/outbound/ikcompilerbackend.h>
#include <lightoverleaf/build/outbound/ikcompilerrootsource.h>
#include <memory>
#include <string>
#include <vector>

namespace lightoverleaf::build
{
struct KWindowsBuildOptions
{
    std::string m_workspaceRootUtf8;
    std::string m_cacheRootUtf8;
    std::vector<std::string> m_texRootsUtf8;
    std::shared_ptr<const IKCompilerRootSource> m_texRootSource;
};
struct KWindowsBuildAdapters
{
    std::shared_ptr<IKBuildSnapshotStore> m_snapshots;
    std::shared_ptr<IKCompilerBackend> m_compiler;
};
KResult<KWindowsBuildAdapters> createWindowsBuildAdapters(const KWindowsBuildOptions& options);
}
