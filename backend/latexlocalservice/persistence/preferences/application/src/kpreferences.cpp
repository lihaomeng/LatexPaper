#include <lightoverleaf/preferences/inbound/ikpreferences.h>
#include <lightoverleaf/preferences/outbound/ikpreferencesstore.h>
#include <lightoverleaf/preferences/domain/kpreferencespolicy.h>
namespace lightoverleaf::preferences {
class KPreferencesApp final:public IKPreferences{public:explicit KPreferencesApp(std::shared_ptr<IKPreferencesStore>s):m_store(std::move(s)){}
 KResult<KPreferences>get()const override{auto r=m_store->load();if(auto*e=std::get_if<KError>(&r))return *e;auto v=std::get<KStoredPreferences>(r);return KPreferences{std::move(v.m_texRoot),std::move(v.m_engine),v.m_timeoutMs,std::move(v.m_compileMode)};}
 KResult<KPreferences>update(const KPreferences&v)override{if(!validTexRoot(v.m_texRoot)||!validEngine(v.m_engine)||!validCompileMode(v.m_compileMode)||v.m_timeoutMs<1000||v.m_timeoutMs>300000)return KError{KErrorCode::InvalidArgument,"preferences.invalid",false};
  auto saved=m_store->save({v.m_texRoot,v.m_engine,v.m_timeoutMs,v.m_compileMode});if(auto*e=std::get_if<KError>(&saved))return *e;return std::get<bool>(saved)?KResult<KPreferences>{v}:KResult<KPreferences>{KError{KErrorCode::Internal,"preferences.notSaved",false}};}
 private:std::shared_ptr<IKPreferencesStore>m_store;};
std::shared_ptr<IKPreferences>createPreferences(std::shared_ptr<IKPreferencesStore>s){return s?std::make_shared<KPreferencesApp>(std::move(s)):nullptr;}
}
