#include <lightoverleaf/preferences/inbound/ikpreferences.h>
#include <lightoverleaf/preferences/adapters/ksqlitepreferencesstore.h>
#include <lightoverleaf/session/inbound/iksessions.h>
#include <lightoverleaf/session/adapters/ksqlitesessionstore.h>
#include <windows.h>
#include <filesystem>
#include <iostream>
using namespace lightoverleaf;namespace fs=std::filesystem;
int main(){int failures=0;auto check=[&](bool v,const char*m){if(!v){++failures;std::cerr<<m<<'\n';}};
 fs::path db=fs::temp_directory_path()/("LightOverLeaf-state-"+std::to_string(GetCurrentProcessId())+".sqlite3");std::error_code e;fs::remove(db,e);auto path=db.u8string();std::string utf8(reinterpret_cast<const char*>(path.data()),path.size());
 auto ps=preferences::createSqlitePreferencesStore(utf8);check(std::holds_alternative<std::shared_ptr<preferences::IKPreferencesStore>>(ps),"preferences sqlite");
 if(auto*s=std::get_if<std::shared_ptr<preferences::IKPreferencesStore>>(&ps)){auto app=preferences::createPreferences(*s);auto defaults=app->get();check(std::holds_alternative<preferences::KPreferences>(defaults)&&std::get<preferences::KPreferences>(defaults).m_engine=="xelatex","preferences defaults");
  auto saved=app->update({"D:/TeX","lualatex",45000,true});check(std::holds_alternative<preferences::KPreferences>(saved),"preferences save");}
 auto ss=session::createSqliteSessionStore(utf8);check(std::holds_alternative<std::shared_ptr<session::IKSessionStore>>(ss),"session sqlite");
 if(auto*s=std::get_if<std::shared_ptr<session::IKSessionStore>>(&ss)){auto app=session::createSessions(*s);check(std::holds_alternative<std::optional<session::KSessionState>>(app->restore()),"empty restore");
  check(std::holds_alternative<bool>(app->save({"D:/项目",{"main.tex","章节/一.tex"},"章节/一.tex",310,true})),"session save");
  auto restored=app->restore();check(std::holds_alternative<std::optional<session::KSessionState>>(restored)&&std::get<std::optional<session::KSessionState>>(restored)->m_openFiles.size()==2,"session restore");
  auto history=app->history();check(std::holds_alternative<std::vector<std::string>>(history)&&std::get<std::vector<std::string>>(history).size()==1,"history");}
 fs::remove(db,e);return failures?1:0;}
