# Run after linking: resolve actual Qt/plugin imports instead of copying an
# entire vcpkg bin directory (which can mix unrelated runtimes and licenses).
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "dumpbin")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${LOL_DUMPBIN}")
file(GET_RUNTIME_DEPENDENCIES
  LIBRARIES "${LOL_QT_CORE}" "${LOL_QT_GUI}" "${LOL_QT_WIDGETS}" "${LOL_QT_PLUGIN}"
  DIRECTORIES "${LOL_QT_BIN}"
  PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
  POST_EXCLUDE_REGEXES ".*[/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\].*"
  RESOLVED_DEPENDENCIES_VAR dependencies
  UNRESOLVED_DEPENDENCIES_VAR unresolved)
if(unresolved)
  message(FATAL_ERROR "Missing Qt runtime dependencies: ${unresolved}")
endif()
foreach(dependency IN LISTS dependencies)
  file(COPY "${dependency}" DESTINATION "${LOL_DEST}")
endforeach()
list(JOIN dependencies "\n" manifest)
file(WRITE "${LOL_DEST}/qt-runtime-dependencies.txt" "${manifest}\n")
