add_library(lol_session_adapter_sqlite STATIC src/ksqlitesessionstore.cpp)
target_include_directories(lol_session_adapter_sqlite PUBLIC include PRIVATE "${LIGHTOVERLEAF_THIRDPARTY_ROOT}/vcpkg/installed/x64-windows/include")
target_link_libraries(lol_session_adapter_sqlite PUBLIC lol_session_outbound PRIVATE "${LIGHTOVERLEAF_THIRDPARTY_ROOT}/vcpkg/installed/x64-windows/lib/sqlite3.lib")
lol_register(lol_session_adapter_sqlite adapter session)
