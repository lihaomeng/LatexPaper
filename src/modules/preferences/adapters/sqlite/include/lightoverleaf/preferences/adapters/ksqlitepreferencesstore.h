#pragma once
#include <lightoverleaf/preferences/outbound/ikpreferencesstore.h>
namespace lightoverleaf::preferences { KResult<std::shared_ptr<IKPreferencesStore>> createSqlitePreferencesStore(const std::string& databaseUtf8); }
