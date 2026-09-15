find_program(NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
option(LIGHTOVERLEAF_DEV_BUILD "Build application assets without running frontend tests" OFF)
if(LIGHTOVERLEAF_DEV_BUILD)
  set(_lol_frontend_command build)
else()
  set(_lol_frontend_command check)
endif()
add_custom_target(lol_frontend
  COMMAND "${NPM_EXECUTABLE}" ci --no-audit --no-fund
  COMMAND "${NPM_EXECUTABLE}" run ${_lol_frontend_command}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/web"
  COMMENT "Frontend: npm ci and npm run ${_lol_frontend_command}"
  VERBATIM)

