#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace lightoverleaf::session { class IKSessionStore;
struct KSessionState{std::string m_workspaceRoot;std::vector<std::string>m_openFiles;std::string m_activeFile;unsigned int m_sidebarWidth=280;bool m_previewOpen=true;};
class IKSessions{public:virtual~IKSessions()=default;virtual KResult<std::optional<KSessionState>>restore()const=0;virtual KResult<bool>save(const KSessionState&)=0;virtual KResult<std::vector<std::string>>history()const=0;};
std::shared_ptr<IKSessions>createSessions(std::shared_ptr<IKSessionStore>);
}
