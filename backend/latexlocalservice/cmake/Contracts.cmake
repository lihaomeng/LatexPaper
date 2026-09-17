set(LOL_GENERATED "${CMAKE_BINARY_DIR}/.generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${LOL_REPO_ROOT}/tools/contractcodegen/generate.py"
  --cpp-out "${LOL_GENERATED}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${LOL_REPO_ROOT}/support/contracts/rpc/v1/envelope.schema.json"
  "${LOL_REPO_ROOT}/tools/contractcodegen/generate.py"
  "${LOL_REPO_ROOT}/tests/fixtures/ping.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/system.schema.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/workspace.schema.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/document.schema.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/search.schema.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/build.schema.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/preview.schema.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/navigation.schema.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/preferences.schema.json"
  "${LOL_REPO_ROOT}/support/contracts/rpc/v2/session.schema.json"
  "${LOL_REPO_ROOT}/tools/contractcodegen/protocol.py"
  "${LOL_REPO_ROOT}/tests/fixtures/system-v2.json"
  "${LOL_REPO_ROOT}/tests/fixtures/lifecycle-v2.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${LOL_REPO_ROOT}/tests/fixtures/workspace-v2.json"
  "${LOL_REPO_ROOT}/tests/fixtures/document-v2.json"
  "${LOL_REPO_ROOT}/tests/fixtures/search-v2.json"
  "${LOL_REPO_ROOT}/tests/fixtures/build-v2.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${LOL_REPO_ROOT}/tests/fixtures/preview-v2.json"
  "${LOL_REPO_ROOT}/tests/fixtures/navigation-v2.json")
add_library(lol_rpc_contract_cpp INTERFACE)
target_include_directories(lol_rpc_contract_cpp INTERFACE "${LOL_GENERATED}")
target_link_libraries(lol_rpc_contract_cpp INTERFACE lol_kernel)
lol_register(lol_rpc_contract_cpp contract rpc)
