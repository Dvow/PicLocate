option(PICLOCATE_BUILD_INSTALLER "Enable the Windows Inno Setup target" ${WIN32})
if(NOT WIN32 OR NOT PICLOCATE_BUILD_INSTALLER)
  return()
endif()
set(PICLOCATE_INSTALLER_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/dist/installers" CACHE PATH "Installer output directory")
get_filename_component(PICLOCATE_INSTALLER_OUTPUT_DIR "${PICLOCATE_INSTALLER_OUTPUT_DIR}" ABSOLUTE)
set(_inno_root "${PICLOCATE_DEPENDENCY_CACHE}/tools/innosetup-7.1.0")
find_program(PICLOCATE_ISCC NAMES ISCC.exe ISCC HINTS
  "${_inno_root}"
  "$ENV{ProgramFiles}/Inno Setup 7"
  "$ENV{ProgramFiles\(x86\)}/Inno Setup 7"
  DOC "Optional Inno Setup 7 compiler override")
if(NOT PICLOCATE_ISCC)
  CPMAddPackage(NAME PicLocateInnoSetup VERSION 7.1.0 DOWNLOAD_ONLY YES DOWNLOAD_NO_EXTRACT YES
    URL "https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-7.1.0-x64.exe"
    URL_HASH SHA256=0362a383ed217d4c4239b5933866dd96d3eb2102737da92f80f6057a4b40df2f)
  set(_inno_bootstrap "${PicLocateInnoSetup_SOURCE_DIR}/innosetup-7.1.0-x64.exe")
  if(NOT EXISTS "${_inno_bootstrap}")
    message(FATAL_ERROR "Inno Setup is not cached. Configure online once or set PICLOCATE_ISCC.")
  endif()
  file(SHA256 "${_inno_bootstrap}" _inno_hash)
  if(NOT _inno_hash STREQUAL "0362a383ed217d4c4239b5933866dd96d3eb2102737da92f80f6057a4b40df2f")
    message(FATAL_ERROR "Inno Setup bootstrap failed SHA-256 verification.")
  endif()
  file(MAKE_DIRECTORY "${PICLOCATE_DEPENDENCY_CACHE}/tools")
  file(LOCK "${PICLOCATE_DEPENDENCY_CACHE}/tools/innosetup-7.1.0.lock" GUARD FILE TIMEOUT 180)
  execute_process(COMMAND "${_inno_bootstrap}"
    /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER /PORTABLE=1 "/DIR=${_inno_root}"
    RESULT_VARIABLE _inno_result TIMEOUT 180)
  if(NOT _inno_result EQUAL 0 OR NOT EXISTS "${_inno_root}/ISCC.exe")
    message(FATAL_ERROR "Inno Setup portable bootstrap failed (${_inno_result}).")
  endif()
  set(PICLOCATE_ISCC "${_inno_root}/ISCC.exe" CACHE FILEPATH "Inno Setup compiler" FORCE)
endif()
execute_process(COMMAND "${PICLOCATE_ISCC}" --version RESULT_VARIABLE _inno_result
  OUTPUT_VARIABLE _inno_version ERROR_VARIABLE _inno_error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _inno_result EQUAL 0 OR NOT _inno_version MATCHES "^7\\." OR _inno_version VERSION_LESS 7.1.0)
  message(FATAL_ERROR "PICLOCATE_ISCC must point to Inno Setup 7.1 or newer (7.x): ${_inno_version}${_inno_error}")
endif()
set(PICLOCATE_SETUP_BASENAME "PicLocate-${PROJECT_VERSION}-Setup-${PICLOCATE_ARCH}")
if(PICLOCATE_ARCH STREQUAL "arm64")
  set(PICLOCATE_INSTALL_ARCH arm64)
  set(PICLOCATE_WINDOWS_MINIMUM 10.0.22000)
else()
  set(PICLOCATE_INSTALL_ARCH x64compatible)
  set(PICLOCATE_WINDOWS_MINIMUM 10.0.19045)
endif()
set(PICLOCATE_INSTALLER_STAGE "${CMAKE_BINARY_DIR}/installer-stage")
configure_file(installer/PicLocate.iss.in "${CMAKE_BINARY_DIR}/PicLocate.iss" @ONLY)
configure_file(cmake/BuildInstaller.cmake.in "${CMAKE_BINARY_DIR}/BuildInstaller.cmake" @ONLY)
add_custom_target(setup
  COMMAND "${CMAKE_COMMAND}" -DPICLOCATE_SETUP_CONFIG=$<CONFIG> -P "${CMAKE_BINARY_DIR}/BuildInstaller.cmake"
  DEPENDS PicLocate "${PICLOCATE_ICON}"
  BYPRODUCTS "${PICLOCATE_INSTALLER_OUTPUT_DIR}/${PICLOCATE_SETUP_BASENAME}.exe"
             "${PICLOCATE_INSTALLER_OUTPUT_DIR}/${PICLOCATE_SETUP_BASENAME}.exe.sha256"
  COMMENT "Staging and compiling the PicLocate installer" USES_TERMINAL VERBATIM)
