function(lol_deploy_cef target)
  set(destination "$<TARGET_FILE_DIR:${target}>")
  # POST_BUILD does not run when only the always-built frontend changes and the DLL
  # does not relink. Keep web deployment as an explicit dependency of the app target.
  add_custom_target(${target}_web_runtime
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${destination}/web"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}/web"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/frontend/web/dist" "${destination}/web"
    DEPENDS lol_frontend
    VERBATIM)
  add_dependencies(${target} ${target}_web_runtime)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CEF_ROOT}/$<CONFIG>/bootstrap.exe" "${destination}/LightOverLeaf.exe"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CEF_ROOT}/Resources" "${destination}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${destination}/web"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/frontend/web/dist" "${destination}/web"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CEF_ROOT}/LICENSE.txt" "${destination}/licenses/CEF.txt"
    VERBATIM)
  foreach(file IN LISTS CEF_BINARY_FILES)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CEF_ROOT}/$<CONFIG>/${file}" "${destination}/${file}" VERBATIM)
  endforeach()
  foreach(component Core Gui Widgets)
    add_custom_command(TARGET ${target} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:Qt5::${component}>" "${destination}" VERBATIM)
  endforeach()
  get_target_property(qtRelease Qt5::Core IMPORTED_LOCATION_RELEASE)
  get_target_property(qtDebug Qt5::Core IMPORTED_LOCATION_DEBUG)
  get_filename_component(releaseBin "${qtRelease}" DIRECTORY)
  get_filename_component(debugBin "${qtDebug}" DIRECTORY)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}/platforms"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<IF:$<CONFIG:Debug>,${debugBin}/../plugins/platforms/qwindowsd.dll,${releaseBin}/../plugins/platforms/qwindows.dll>"
      "${destination}/platforms" VERBATIM)
  get_filename_component(msvcBin "${CMAKE_LINKER}" DIRECTORY)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
      "-DLOL_DUMPBIN=${msvcBin}/dumpbin.exe"
      "-DLOL_QT_CORE=$<TARGET_FILE:Qt5::Core>"
      "-DLOL_QT_GUI=$<TARGET_FILE:Qt5::Gui>"
      "-DLOL_QT_WIDGETS=$<TARGET_FILE:Qt5::Widgets>"
      "-DLOL_QT_PLUGIN=$<IF:$<CONFIG:Debug>,${debugBin}/../plugins/platforms/qwindowsd.dll,${releaseBin}/../plugins/platforms/qwindows.dll>"
      "-DLOL_QT_BIN=$<IF:$<CONFIG:Debug>,${debugBin},${releaseBin}>"
      "-DLOL_DEST=${destination}"
      -P "${PROJECT_SOURCE_DIR}/cmake/DeployQt.cmake"
    VERBATIM)
  set(CEF_TARGET_OUT_DIR "${destination}")
  SET_LPAC_ACLS(${target})
endfunction()

