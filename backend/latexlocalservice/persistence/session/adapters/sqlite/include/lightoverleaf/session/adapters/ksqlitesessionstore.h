#pragma once
#include <lightoverleaf/session/outbound/iksessionstore.h>
namespace lightoverleaf::session {KResult<std::shared_ptr<IKSessionStore>>createSqliteSessionStore(const std::string&databaseUtf8);}
