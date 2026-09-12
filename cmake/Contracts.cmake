set(LOL_GENERATED "${CMAKE_BINARY_DIR}/.generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/contractcodegen/generate.py"
  --cpp-out "${LOL_GENERATED}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/contracts/rpc/v1/envelope.schema.json"
  "${PROJECT_SOURCE_DIR}/tools/contractcodegen/generate.py"
  "${PROJECT_SOURCE_DIR}/tests/fixtures/ping.json"
  "${PROJECT_SOURCE_DIR}/contracts/rpc/v2/system.schema.json"
  "${PROJECT_SOURCE_DIR}/contracts/rpc/v2/workspace.schema.json"
  "${PROJECT_SOURCE_DIR}/contracts/rpc/v2/document.schema.json"
  "${PROJECT_SOURCE_DIR}/tools/contractcodegen/protocol.py"
  "${PROJECT_SOURCE_DIR}/tests/fixtures/system-v2.json"
  "${PROJECT_SOURCE_DIR}/tests/fixtures/lifecycle-v2.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/tests/fixtures/workspace-v2.json"
  "${PROJECT_SOURCE_DIR}/tests/fixtures/document-v2.json")
add_library(lol_rpc_contract_cpp INTERFACE)
target_include_directories(lol_rpc_contract_cpp INTERFACE "${LOL_GENERATED}")
target_link_libraries(lol_rpc_contract_cpp INTERFACE lol_kernel)
lol_register(lol_rpc_contract_cpp contract rpc)
