find_program(NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
add_custom_target(lol_frontend
  COMMAND "${NPM_EXECUTABLE}" ci --no-audit --no-fund
  COMMAND "${NPM_EXECUTABLE}" run check
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/web"
  COMMENT "Locked frontend install, contracts, boundaries, tests and release assets"
  VERBATIM)

