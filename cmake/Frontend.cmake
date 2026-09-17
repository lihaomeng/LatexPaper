find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
option(LIGHTOVERLEAF_DEV_BUILD "Build application assets without running frontend tests" OFF)
if(LIGHTOVERLEAF_DEV_BUILD)
  set(_lol_frontend_command dev)
else()
  set(_lol_frontend_command release)
endif()
add_custom_target(lol_frontend
  COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
    -File "${PROJECT_SOURCE_DIR}/scripts/internal/build-frontend.ps1" -Mode ${_lol_frontend_command}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/frontend/web"
  COMMENT "Frontend: ${_lol_frontend_command} dependency preparation and build"
  VERBATIM)

