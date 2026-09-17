#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <memory>
#include <string>
namespace lightoverleaf::preferences {
struct KStoredPreferences { std::string m_texRoot; std::string m_engine; unsigned int m_timeoutMs=120000; std::string m_compileMode="live"; };
class IKPreferencesStore { public: virtual ~IKPreferencesStore()=default; virtual KResult<KStoredPreferences> load()const=0; virtual KResult<bool> save(const KStoredPreferences&)=0; };
}
