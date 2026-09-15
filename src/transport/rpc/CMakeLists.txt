add_library(lol_rpc_core STATIC core/krpccore.cpp core/krpcsession.cpp core/krpcendpoint.cpp
  core/kapplicationrpchandler.cpp)
target_include_directories(lol_rpc_core PUBLIC include)
target_link_libraries(lol_rpc_core PUBLIC lol_rpc_contract_cpp lol_system_inbound
  PRIVATE lol_workspaceworkflow_inbound lol_preferences_inbound lol_session_inbound lol_export_inbound)
lol_register(lol_rpc_core rpc rpc)
