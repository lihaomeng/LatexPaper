#include <lightoverleaf/preferences/inbound/ikpreferences.h>
#include <lightoverleaf/preferences/adapters/ksqlitepreferencesstore.h>
#include <lightoverleaf/session/inbound/iksessions.h>
#include <lightoverleaf/session/adapters/ksqlitesessionstore.h>
#include <windows.h>
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
using namespace lightoverleaf;namespace fs=std::filesystem;
int main(){int failures=0;auto check=[&](bool v,const char*m){if(!v){++failures;std::cerr<<m<<'\n';}};
 fs::path db=fs::temp_directory_path()/("LightOverLeaf-state-"+std::to_string(GetCurrentProcessId())+".sqlite3");std::error_code e;fs::remove(db,e);auto path=db.u8string();std::string utf8(reinterpret_cast<const char*>(path.data()),path.size());
 fs::path legacy=db;legacy+=L".legacy";fs::remove(legacy,e);auto legacyPath=legacy.u8string();std::string legacyUtf8(reinterpret_cast<const char*>(legacyPath.data()),legacyPath.size());
 sqlite3* legacyDb=nullptr;check(sqlite3_open_v2(legacyUtf8.c_str(),&legacyDb,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)==SQLITE_OK,"legacy sqlite open");
 if(legacyDb){const char* legacySql="CREATE TABLE preferences(id INTEGER PRIMARY KEY CHECK(id=1),tex_root TEXT NOT NULL,engine TEXT NOT NULL,timeout_ms INTEGER NOT NULL,auto_compile INTEGER NOT NULL DEFAULT 1);INSERT INTO preferences(id,tex_root,engine,timeout_ms,auto_compile) VALUES(1,'','xelatex',120000,0)";check(sqlite3_exec(legacyDb,legacySql,nullptr,nullptr,nullptr)==SQLITE_OK,"legacy schema fixture");sqlite3_close(legacyDb);}
 {auto migrated=preferences::createSqlitePreferencesStore(legacyUtf8);check(std::holds_alternative<std::shared_ptr<preferences::IKPreferencesStore>>(migrated),"legacy store migration");if(auto*s=std::get_if<std::shared_ptr<preferences::IKPreferencesStore>>(&migrated)){auto app=preferences::createPreferences(*s);auto loaded=app->get();check(std::holds_alternative<preferences::KPreferences>(loaded)&&std::get<preferences::KPreferences>(loaded).m_compileMode=="manual","legacy auto_compile migration");}}
 fs::remove(legacy,e); auto ps=preferences::createSqlitePreferencesStore(utf8);check(std::holds_alternative<std::shared_ptr<preferences::IKPreferencesStore>>(ps),"preferences sqlite");
 if(auto*s=std::get_if<std::shared_ptr<preferences::IKPreferencesStore>>(&ps)){auto app=preferences::createPreferences(*s);auto defaults=app->get();check(std::holds_alternative<preferences::KPreferences>(defaults)&&std::get<preferences::KPreferences>(defaults).m_engine=="xelatex"&&std::get<preferences::KPreferences>(defaults).m_compileMode=="live","preferences defaults");
  auto saved=app->update({"D:/TeX","lualatex",45000,"onSave"});check(std::holds_alternative<preferences::KPreferences>(saved)&&std::get<preferences::KPreferences>(saved).m_compileMode=="onSave","preferences save");}
 auto ss=session::createSqliteSessionStore(utf8);check(std::holds_alternative<std::shared_ptr<session::IKSessionStore>>(ss),"session sqlite");
 if(auto*s=std::get_if<std::shared_ptr<session::IKSessionStore>>(&ss)){auto app=session::createSessions(*s);check(std::holds_alternative<std::optional<session::KSessionState>>(app->restore()),"empty restore");
  session::KSessionState state;state.m_workspaceRoot="D:/项目";state.m_openFiles={"main.tex","章节/一.tex"};
  state.m_activeFile="章节/一.tex";state.m_sidebarWidth=310;state.m_editorWidth=840;state.m_previewOpen=true;
  state.m_activeLine=27;state.m_activeColumn=9;state.m_previewZoom=150;
  check(std::holds_alternative<bool>(app->save(state)),"session save");
  auto restored=app->restore();check(std::holds_alternative<std::optional<session::KSessionState>>(restored)&&
   std::get<std::optional<session::KSessionState>>(restored)->m_openFiles.size()==2&&
   std::get<std::optional<session::KSessionState>>(restored)->m_editorWidth==840&&
   std::get<std::optional<session::KSessionState>>(restored)->m_activeLine==27&&
   std::get<std::optional<session::KSessionState>>(restored)->m_activeColumn==9&&
   std::get<std::optional<session::KSessionState>>(restored)->m_previewZoom==150,"session restore");
  auto history=app->history();check(std::holds_alternative<std::vector<std::string>>(history)&&std::get<std::vector<std::string>>(history).size()==1,"history");}
 fs::remove(db,e);return failures?1:0;}
