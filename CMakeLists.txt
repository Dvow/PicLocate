cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()
project(PicLocate VERSION 1.7.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
include(CTest)
include(cmake/Dependencies.cmake)
include(cmake/Models.cmake)
include(cmake/Gpu.cmake)
include(cmake/Updates.cmake)
add_library(piclocate_core STATIC
  src/core/core.cpp
  src/core/core.h
  src/core/types.h
  src/core/paths.cpp
  src/core/digest.cpp
  src/library/database.cpp
  src/library/database.h
  src/library/indexer.cpp
  src/library/indexer.h
  src/library/search.cpp
  src/library/search.h
  src/models/inference.cpp
  src/models/inference.h
  src/models/appearance.cpp
  src/models/appearance.h
  src/models/gpu.cpp
  src/models/gpu.h
  src/models/model_download.cpp
  src/models/model_download.h
  src/models/model_files.cpp
  src/models/model_files.h
)
if(PICLOCATE_ARCH STREQUAL "x64")
  target_sources(piclocate_core PRIVATE src/core/dot_avx2.cpp)
  if(MSVC)
    set_source_files_properties(src/core/dot_avx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
  else()
    set_source_files_properties(src/core/dot_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mno-fma")
  endif()
  target_compile_definitions(piclocate_core PRIVATE PICLOCATE_AVX2_DISPATCH)
endif()
target_include_directories(piclocate_core PUBLIC src)
target_link_libraries(piclocate_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Concurrent ONNXRuntime::ONNXRuntime)
target_compile_definitions(piclocate_core PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
if(MSVC)
  target_compile_options(piclocate_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
  target_compile_definitions(piclocate_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(piclocate_core PUBLIC windowsapp dxgi d3d12 bcrypt)
endif()
add_library(piclocate_ui STATIC
  src/ui/window.cpp
  src/ui/window.h
  src/ui/window_layout.cpp
  src/ui/settings.cpp
  src/ui/theme.cpp
  src/ui/ui_helpers.h
  src/ui/gallery.cpp
  src/ui/gallery.h
  src/ui/viewer.cpp
  src/ui/viewer.h
)
target_link_libraries(piclocate_ui PUBLIC piclocate_core piclocate_updates Qt6::Widgets)
qt_add_resources(piclocate_ui ui_icons PREFIX "/" FILES
  resources/chevron.svg
  resources/check.svg
)
target_compile_definitions(piclocate_ui PRIVATE PICLOCATE_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(piclocate_ui PRIVATE /W4 /permissive- /utf-8)
endif()
qt_add_executable(PicLocate WIN32 src/app/main.cpp src/app/headless.cpp src/app/headless.h)
if(APPLE)
  set_target_properties(PicLocate PROPERTIES MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.dvow.piclocate"
    MACOSX_BUNDLE_BUNDLE_NAME "PicLocate"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    INSTALL_RPATH "@executable_path/../Frameworks")
elseif(UNIX)
  set_target_properties(PicLocate PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
if(WIN32)
  enable_language(RC)
  find_program(PICLOCATE_POWERSHELL NAMES powershell.exe REQUIRED)
  set(PICLOCATE_ICON "${CMAKE_BINARY_DIR}/piclocate.ico")
  add_custom_command(OUTPUT "${PICLOCATE_ICON}"
    COMMAND "${PICLOCATE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
      -File "${PROJECT_SOURCE_DIR}/scripts/make-icon.ps1" -OutputFile "${PICLOCATE_ICON}"
    DEPENDS scripts/make-icon.ps1 VERBATIM)
  configure_file(installer/PicLocate.rc.in "${CMAKE_BINARY_DIR}/PicLocate.rc" @ONLY)
  set_source_files_properties("${CMAKE_BINARY_DIR}/PicLocate.rc" PROPERTIES OBJECT_DEPENDS "${PICLOCATE_ICON}")
  target_sources(PicLocate PRIVATE "${CMAKE_BINARY_DIR}/PicLocate.rc" "${PICLOCATE_ICON}")
endif()
target_link_libraries(PicLocate PRIVATE piclocate_ui)
target_compile_definitions(PicLocate PRIVATE PICLOCATE_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(PicLocate PRIVATE /W4 /permissive- /utf-8)
endif()
if(WIN32)
  add_custom_command(TARGET PicLocate POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>" "$<TARGET_FILE_DIR:PicLocate>")
endif()
if(BUILD_TESTING)
  qt_add_executable(piclocate_tests tests/test_core.cpp)
  target_link_libraries(piclocate_tests PRIVATE piclocate_core Qt6::Test)
  qt_add_executable(piclocate_ui_tests tests/test_ui.cpp)
  target_link_libraries(piclocate_ui_tests PRIVATE piclocate_ui Qt6::Test)
  target_compile_definitions(piclocate_ui_tests PRIVATE PICLOCATE_VERSION="${PROJECT_VERSION}")
  qt_add_executable(piclocate_update_tests tests/test_update.cpp)
  target_link_libraries(piclocate_update_tests PRIVATE piclocate_updates Qt6::Test)
  if(MSVC)
    target_compile_options(piclocate_tests PRIVATE /utf-8)
    target_compile_options(piclocate_ui_tests PRIVATE /utf-8)
    target_compile_options(piclocate_update_tests PRIVATE /utf-8)
  endif()
  add_test(NAME core COMMAND piclocate_tests -o "${CMAKE_BINARY_DIR}/core-test-results.txt,txt")
  add_test(NAME desktop_ui COMMAND piclocate_ui_tests -o "${CMAKE_BINARY_DIR}/ui-test-results.txt,txt")
  add_test(NAME updates COMMAND piclocate_update_tests -o "${CMAKE_BINARY_DIR}/update-test-results.txt,txt")
  add_test(NAME scalar_core COMMAND piclocate_tests vectorBackends exactTopK focusedProjectionPreservesCosine -o "${CMAKE_BINARY_DIR}/scalar-test-results.txt,txt")
  set_tests_properties(scalar_core PROPERTIES ENVIRONMENT "PICLOCATE_DISABLE_AVX2=1")
  set_tests_properties(core desktop_ui updates PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${PICLOCATE_QT_ROOT}/bin"
    ENVIRONMENT "PICLOCATE_MODEL_DIR=${PICLOCATE_MODEL_DIR}")
  set_property(TEST scalar_core PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${PICLOCATE_QT_ROOT}/bin")
  # Exercise real widgets without opening fixture windows on the user's desktop.
  set_property(TEST desktop_ui APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  set_property(TEST desktop_ui APPEND PROPERTY ENVIRONMENT "PICLOCATE_DISABLE_UPDATE_CHECK=1")
  if(WIN32)
    set_property(TEST desktop_ui APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
  endif()
endif()
install(TARGETS PicLocate RUNTIME DESTINATION ${PICLOCATE_BINARY_DEST} BUNDLE DESTINATION .)
if(WIN32)
  install(FILES "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>" DESTINATION .)
elseif(APPLE)
  install(DIRECTORY "${ONNXRUNTIME_ROOT}/lib/" DESTINATION PicLocate.app/Contents/Frameworks
    FILES_MATCHING PATTERN "*.dylib" PATTERN "*.dSYM" EXCLUDE)
else()
  install(DIRECTORY "${ONNXRUNTIME_ROOT}/lib/" DESTINATION lib FILES_MATCHING PATTERN "*.so*")
endif()
install(FILES LICENSE README.md CONTRIBUTING.md PRIVACY.md THIRD_PARTY.md CHANGELOG.md DESTINATION ${PICLOCATE_RESOURCE_DEST})
install(FILES docs/architecture.md docs/compatibility.md docs/installer.md docs/preview.png
  DESTINATION ${PICLOCATE_RESOURCE_DEST}/docs)
install(FILES "${ONNXRUNTIME_ROOT}/LICENSE" DESTINATION ${PICLOCATE_RESOURCE_DEST}/licenses RENAME ONNX-Runtime-LICENSE.txt)
install(FILES "${ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt" DESTINATION ${PICLOCATE_RESOURCE_DEST}/licenses RENAME ONNX-Runtime-ThirdPartyNotices.txt)
install(FILES licenses/CLIP-MIT.txt DESTINATION ${PICLOCATE_RESOURCE_DEST}/licenses)
include(cmake/Packaging.cmake)
include(cmake/Installer.cmake)
