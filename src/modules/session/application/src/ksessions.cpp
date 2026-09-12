#include <lightoverleaf/session/domain/ksessionpolicy.h>
#include <lightoverleaf/session/inbound/iksessions.h>
#include <lightoverleaf/session/outbound/iksessionstore.h>

#include <sstream>

namespace lightoverleaf::session
{
namespace
{
std::string join(const std::vector<std::string>& values)
{
    std::string result;
    for (const std::string& value : values)
    {
        if (!result.empty()) result.push_back('\n');
        result += value;
    }
    return result;
}

std::vector<std::string> split(const std::string& value)
{
    std::istringstream input(value);
    std::string item;
    std::vector<std::string> result;
    while (std::getline(input, item))
        if (!item.empty()) result.push_back(item);
    return result;
}
}

class KSessions final : public IKSessions
{
public:
    explicit KSessions(std::shared_ptr<IKSessionStore> store) : m_store(std::move(store)) {}

    KResult<std::optional<KSessionState>> restore() const override
    {
        KResult<std::optional<KStoredSession>> loaded = m_store->load();
        if (const KError* error = std::get_if<KError>(&loaded)) return *error;
        std::optional<KStoredSession> stored =
            std::get<std::optional<KStoredSession>>(std::move(loaded));
        if (!stored) return std::optional<KSessionState>{};
        KSessionState state;
        state.m_workspaceRoot = std::move(stored->m_workspaceRoot);
        state.m_openFiles = split(stored->m_openFiles);
        state.m_activeFile = std::move(stored->m_activeFile);
        state.m_sidebarWidth = stored->m_sidebarWidth;
        state.m_editorWidth = stored->m_editorWidth;
        state.m_previewOpen = stored->m_previewOpen;
        state.m_activeLine = stored->m_activeLine;
        state.m_activeColumn = stored->m_activeColumn;
        state.m_previewZoom = stored->m_previewZoom;
        return std::optional<KSessionState>{std::move(state)};
    }

    KResult<bool> save(const KSessionState& value) override
    {
        if (!validSessionText(value.m_workspaceRoot, 4096) || value.m_openFiles.size() > 64 ||
            !validSessionText(value.m_activeFile, 4096) || value.m_sidebarWidth < 180 ||
            value.m_sidebarWidth > 600 || value.m_editorWidth < 300 || value.m_editorWidth > 4000 ||
            value.m_activeLine == 0 || value.m_activeLine > 9007199254740991ULL ||
            value.m_activeColumn == 0 || value.m_activeColumn > 9007199254740991ULL ||
            value.m_previewZoom < 50 || value.m_previewZoom > 300)
            return KError{KErrorCode::InvalidArgument, "session.invalid", false};
        for (const std::string& file : value.m_openFiles)
            if (!validSessionText(file, 4096) || file.find('\n') != std::string::npos)
                return KError{KErrorCode::InvalidArgument, "session.invalid", false};
        return m_store->save({value.m_workspaceRoot, join(value.m_openFiles), value.m_activeFile,
            value.m_sidebarWidth, value.m_editorWidth, value.m_previewOpen, value.m_activeLine,
            value.m_activeColumn, value.m_previewZoom});
    }

    KResult<std::vector<std::string>> history() const override
    {
        return m_store->history();
    }

private:
    std::shared_ptr<IKSessionStore> m_store;
};

std::shared_ptr<IKSessions> createSessions(std::shared_ptr<IKSessionStore> store)
{
    return store ? std::make_shared<KSessions>(std::move(store)) : nullptr;
}
}
