cmake_minimum_required(VERSION 3.30)
message("Using toolchain file ${CMAKE_TOOLCHAIN_FILE}.")

set(X_VCPKG_APPLOCAL_DEPS_INSTALL OFF)

project(
  AbsoluteHOTAS
  VERSION 2.2.2
  LANGUAGES CXX
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
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/AbsoluteGlobals.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/AbsoluteGlobals.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/RuntimePaths.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/RuntimePaths.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/Plugin.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/Plugin.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/DeviceManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/DeviceManager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/src/UIHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include/UIHook.h
)

set(absolute_hotas_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/Main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/ThrottleController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/ThrottleHook.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/vJoyFeeder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/src/BindingWizard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/ThrottleController.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/ThrottleHook.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/vJoyFeeder.h
  ${CMAKE_CURRENT_SOURCE_DIR}/projects/AbsoluteHOTAS/include/BindingWizard.h
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
