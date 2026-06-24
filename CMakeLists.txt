cmake_minimum_required(VERSION 3.30)
message("Using toolchain file ${CMAKE_TOOLCHAIN_FILE}.")

set(X_VCPKG_APPLOCAL_DEPS_INSTALL OFF)

project(
  AbsoluteHOTAS
  VERSION 3.1.0
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
  @ONLY
)

find_package(CommonLibSF CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_path(SIMPLEINI_INCLUDE_DIRS SimpleIni.h)

set(shared_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/RuntimePaths.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/RuntimePaths.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/Plugin.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/Plugin.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/DeviceManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/DeviceManager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/ControlMapReader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/ControlMapReader.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/UIHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/UIHook.h
)

set(absolute_hotas_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/Main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/ThrottleController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/ThrottleHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/BindingWizard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/WizardCapture.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/WizardConfig.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/ShipOutput.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/SignalHunter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/AimController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/SettingBeacon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/ThrottleController.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/ThrottleHook.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/BindingWizard.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/WizardDefs.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/WizardCapture.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/WizardConfig.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/ShipOutput.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/SignalHunter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/AimController.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/SettingBeacon.h
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
  ${shared_sources}
)

add_library(AbsoluteHOTAS SHARED ${absolute_hotas_sources})

set(all_targets AbsoluteHOTAS)

foreach(target ${all_targets})
  target_compile_definitions(${target} PRIVATE __CMAKE_COMMONLIBSF_PLUGIN=1)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/include
    ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SIMPLEINI_INCLUDE_DIRS}
  )
  target_link_libraries(${target} PRIVATE dinput8 dxguid d3d12 dxgi)
  target_compile_definitions(${target} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    SPDLOG_FMT_EXTERNAL
    SPDLOG_NO_EXCEPTIONS
    FMT_ENFORCE_COMPILE_STRING=0
    SPDLOG_COMPILED_LIB
    PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
  )
  target_precompile_headers(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/PCH.h
    ${SIMPLEINI_INCLUDE_DIRS}/SimpleIni.h
  )
  target_compile_options(${target} PRIVATE
    /cgthreads8 /diagnostics:caret /MP /W4 /Zc:__cplusplus /Zc:enumTypes /Zc:inline /Zc:templateScope
    /utf-8 /wd5050 /wd4005
  )

  if(CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo)
    target_compile_options(${target} PRIVATE /fp:fast /Ob3 /GL /GR- /Gw /Qpar)
    target_link_options(${target} PRIVATE /CGTHREADS:8 /INCREMENTAL:NO /OPT:REF,ICF=4)
  endif()
endforeach()

target_include_directories(AbsoluteHOTAS SYSTEM PRIVATE
  $<TARGET_PROPERTY:CommonLibSF::CommonLibSF,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(AbsoluteHOTAS PRIVATE spdlog::spdlog imgui::imgui minhook::minhook)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
  set(BUILD_NAME Debug)
else()
  set(BUILD_NAME Release)
endif()

set(PACKAGE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/contrib/Plugin${BUILD_NAME})
set(PACKAGE_PLUGIN_DIR ${PACKAGE_ROOT}/Data/SFSE/Plugins)

add_custom_command(
  TARGET AbsoluteHOTAS
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PACKAGE_PLUGIN_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:AbsoluteHOTAS> ${PACKAGE_PLUGIN_DIR}
)

add_custom_command(
  TARGET AbsoluteHOTAS
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/config/AbsoluteHOTAS.ini
    ${PACKAGE_PLUGIN_DIR}
)

# Optional direct deploy into a local mod-manager folder for quick smoke tests.
# Opt-in: set ABSOLUTEHOTAS_DEPLOY_DIR (e.g. in your gitignored CMakeUserPresets.json)
# to the SFSE/Plugins folder of your test mod. The DLL is refreshed every build;
# the INI is only seeded if absent, so your tuned bindings survive rebuilds
# (delete the deployed INI to re-seed the default).
if(ABSOLUTEHOTAS_DEPLOY_DIR)
  add_custom_command(
    TARGET AbsoluteHOTAS
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ABSOLUTEHOTAS_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:AbsoluteHOTAS> "${ABSOLUTEHOTAS_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/config/AbsoluteHOTAS.ini
      -DDST=${ABSOLUTEHOTAS_DEPLOY_DIR}/AbsoluteHOTAS.ini
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_ini.cmake
    COMMENT "Deploying AbsoluteHOTAS.dll -> ${ABSOLUTEHOTAS_DEPLOY_DIR}"
    VERBATIM
  )
endif()

set(RELEASE_ARCHIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/releases")
set(RELEASE_ARCHIVE_NAME "${PROJECT_NAME}-v${PROJECT_VERSION}-${BUILD_NAME}.zip")

add_custom_target(
  package-mo2
  COMMAND ${CMAKE_COMMAND} -E make_directory ${RELEASE_ARCHIVE_DIR}
  COMMAND ${CMAKE_COMMAND} -E tar "cfv" "${RELEASE_ARCHIVE_DIR}/${RELEASE_ARCHIVE_NAME}" --format=zip "Data"
  WORKING_DIRECTORY ${PACKAGE_ROOT}
  DEPENDS AbsoluteHOTAS
  COMMENT "Creating MO2 deployable archive: ${RELEASE_ARCHIVE_NAME}"
)

# ---- Unit tests (opt-in: -DABSOLUTEHOTAS_BUILD_TESTS=ON) ----
# ControlMapReader is self-contained (std + <windows.h> for MapVirtualKey), so it
# builds into a standalone test exe with the same MSVC toolchain as the plugin.
option(ABSOLUTEHOTAS_BUILD_TESTS "Build unit tests" OFF)
if(ABSOLUTEHOTAS_BUILD_TESTS)
  enable_testing()
  add_executable(control_map_reader_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_map_reader_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/ControlMapReader.cpp
  )
  target_include_directories(control_map_reader_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared/include)
  target_compile_definitions(control_map_reader_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(control_map_reader_test PRIVATE user32)
  add_test(NAME control_map_reader
    COMMAND control_map_reader_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
endif()
