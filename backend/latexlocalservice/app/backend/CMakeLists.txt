add_executable(LightOverLeafBackend main.cpp)
target_link_libraries(LightOverLeafBackend PRIVATE lol_composition lol_transport_json)
target_compile_definitions(LightOverLeafBackend PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(LightOverLeafBackend PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
lol_register(LightOverLeafBackend composition backend)
add_custom_command(TARGET LightOverLeafBackend POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${LIGHTOVERLEAF_THIRDPARTY_ROOT}/vcpkg/installed/x64-windows/$<$<CONFIG:Debug>:debug/>bin/sqlite3.dll"
    "$<TARGET_FILE_DIR:LightOverLeafBackend>")
