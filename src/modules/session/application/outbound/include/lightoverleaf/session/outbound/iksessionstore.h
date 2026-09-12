#pragma once

#include <lightoverleaf/kernel/kresult.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lightoverleaf::session
{
struct KStoredSession
{
    std::string m_workspaceRoot;
    std::string m_openFiles;
    std::string m_activeFile;
    unsigned int m_sidebarWidth = 280;
    unsigned int m_editorWidth = 720;
    bool m_previewOpen = true;
    std::size_t m_activeLine = 1;
    std::size_t m_activeColumn = 1;
    unsigned int m_previewZoom = 125;
};

class IKSessionStore
{
public:
    virtual ~IKSessionStore() = default;
    virtual KResult<std::optional<KStoredSession>> load() const = 0;
    virtual KResult<bool> save(const KStoredSession& value) = 0;
    virtual KResult<std::vector<std::string>> history() const = 0;
};
}
