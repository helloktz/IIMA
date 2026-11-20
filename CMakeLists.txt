cmake_minimum_required(VERSION 3.31)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/build/config.json" iima_config)
string(JSON IIMA_VERSION GET "${iima_config}" version)
string(JSON IIMA_MSVC_VERSION GET "${iima_config}" msvc)
string(JSON IIMA_WINDOWS_SDK_VERSION GET "${iima_config}" windowsSdk)

project(IIMA VERSION ${IIMA_VERSION} LANGUAGES CXX RC)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
        OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "IIMA requires x64 MSVC on Windows.")
endif()
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "Use the Ninja generator or a project preset.")
endif()
if(NOT "$ENV{VCToolsVersion}" STREQUAL "${IIMA_MSVC_VERSION}")
    message(FATAL_ERROR "Activate MSVC ${IIMA_MSVC_VERSION} with scripts/build.ps1 or vcvarsall.bat.")
endif()
string(REGEX REPLACE "[/\\\\]+$" "" iima_active_sdk "$ENV{WindowsSDKVersion}")
if(NOT iima_active_sdk STREQUAL IIMA_WINDOWS_SDK_VERSION)
    message(FATAL_ERROR "Activate Windows SDK ${IIMA_WINDOWS_SDK_VERSION} with scripts/build.ps1 or vcvarsall.bat.")
endif()

# Ninja matches /showIncludes as bytes. Preserve cl's native output even when
# the console encoding used by CMake's discovery differs from cl's encoding.
set(show_includes_probe "${CMAKE_BINARY_DIR}/CMakeFiles/iima-showincludes")
file(WRITE "${show_includes_probe}.h" "")
file(WRITE "${show_includes_probe}.cpp" "#include \"iima-showincludes.h\"\n")
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /Zs /showIncludes /utf-8 "${show_includes_probe}.cpp"
    ENCODING NONE
    OUTPUT_VARIABLE show_includes_output
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT show_includes_output MATCHES "(^|\n)([^\r\n]+:[ \t]+)[A-Za-z]:[^\r\n]*iima-showincludes\\.h")
    message(FATAL_ERROR "Could not determine the MSVC /showIncludes prefix for Ninja.")
endif()
set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")

cmake_path(CONVERT "$ENV{WindowsSdkDir}" TO_CMAKE_PATH_LIST IIMA_WINDOWS_SDK NORMALIZE)
cmake_path(CONVERT "$ENV{VCToolsInstallDir}" TO_CMAKE_PATH_LIST IIMA_VC_TOOLS NORMALIZE)
set(IIMA_WORK_DIR "${PROJECT_SOURCE_DIR}/.build")
set(IIMA_PACKAGES_DIR "${IIMA_WORK_DIR}/packages")
set(IIMA_PACKAGE_CONFIG "${PROJECT_SOURCE_DIR}/src/IIMA.App/packages.config")
find_program(IIMA_PWSH pwsh REQUIRED)
execute_process(
    COMMAND "${IIMA_PWSH}" -NoProfile -File "${PROJECT_SOURCE_DIR}/scripts/restore-packages.ps1"
    COMMAND_ERROR_IS_FATAL ANY
)
file(STRINGS "${IIMA_PACKAGE_CONFIG}" iima_package_entries REGEX "<package id=")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${IIMA_PACKAGE_CONFIG}" "${PROJECT_SOURCE_DIR}/build/config.json"
    "${PROJECT_SOURCE_DIR}/scripts/restore-packages.ps1")

function(iima_package_directory output package_id)
    foreach(entry IN LISTS iima_package_entries)
        if(entry MATCHES "id=\"([^\"]+)\" version=\"([^\"]+)\"" AND CMAKE_MATCH_1 STREQUAL package_id)
            set("${output}" "${IIMA_PACKAGES_DIR}/${package_id}.${CMAKE_MATCH_2}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "Missing ${package_id} in packages.config.")
endfunction()

add_library(iima_compile_settings INTERFACE)
target_compile_options(iima_compile_settings INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:/std:c++23;/permissive-;/utf-8;/Zc:__cplusplus;/bigobj;/sdl;/W4>"
)
target_compile_definitions(iima_compile_settings INTERFACE
    UNICODE _UNICODE NOMINMAX "IIMA_VERSION=\"${IIMA_VERSION}\""
)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/link")

add_subdirectory(src/IIMA.Core)
add_subdirectory(src/IIMA.App)
