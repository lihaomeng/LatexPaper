#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace lightoverleaf::session {struct KStoredSession{std::string m_workspaceRoot,m_openFiles,m_activeFile;unsigned int m_sidebarWidth=280;bool m_previewOpen=true;};
class IKSessionStore{public:virtual~IKSessionStore()=default;virtual KResult<std::optional<KStoredSession>>load()const=0;virtual KResult<bool>save(const KStoredSession&)=0;virtual KResult<std::vector<std::string>>history()const=0;};}
