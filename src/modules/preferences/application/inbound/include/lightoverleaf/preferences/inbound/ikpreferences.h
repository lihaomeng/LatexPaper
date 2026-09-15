#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <memory>
#include <string>
namespace lightoverleaf::preferences {
class IKPreferencesStore;
struct KPreferences { std::string m_texRoot; std::string m_engine="pdflatex"; unsigned int m_timeoutMs=120000; std::string m_compileMode="live"; };
class IKPreferences { public: virtual ~IKPreferences()=default; virtual KResult<KPreferences> get()const=0; virtual KResult<KPreferences> update(const KPreferences&)=0; };
std::shared_ptr<IKPreferences> createPreferences(std::shared_ptr<IKPreferencesStore>);
}
