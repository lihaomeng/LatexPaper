add_library(lol_preferences_adapter_sqlite STATIC src/ksqlitepreferencesstore.cpp)
target_include_directories(lol_preferences_adapter_sqlite PUBLIC include PRIVATE "${LIGHTOVERLEAF_THIRDPARTY_ROOT}/vcpkg/installed/x64-windows/include")
target_link_libraries(lol_preferences_adapter_sqlite PUBLIC lol_preferences_outbound PRIVATE "${LIGHTOVERLEAF_THIRDPARTY_ROOT}/vcpkg/installed/x64-windows/lib/sqlite3.lib")
lol_register(lol_preferences_adapter_sqlite adapter preferences)
