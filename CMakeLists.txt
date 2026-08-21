cmake_minimum_required(VERSION 4.2)
cmake_policy(SET CMP0116 NEW)
set(CMAKE_POLICY_WARNING_CMP0116 OFF)

# The bridge is optional at build time and inert when the devbench SKSE host is
# absent. Select the manifest feature before project() loads the vcpkg toolchain.
option(
    DEVBENCH_BRIDGE
    "Expose render-scale iteration controls through the devbench MCP host"
    ON
)
if(DEVBENCH_BRIDGE)
    list(APPEND VCPKG_MANIFEST_FEATURES "devbench-bridge")
endif()

set(_CSX_VERSION_DEFAULT "3.18-VR")
if(CMAKE_BINARY_DIR MATCHES [[[/\\]build[/\\]AIO-Release$]])
    set(_CSX_VERSION_DEFAULT "3.15-SE")
elseif(DEFINED ENABLE_SKYRIM_VR AND NOT ENABLE_SKYRIM_VR)
    set(_CSX_VERSION_DEFAULT "3.15-SE")
endif()
set(CSX_VERSION
    "${_CSX_VERSION_DEFAULT}"
    CACHE STRING
    "Display version for Community Shaders Expanded."
)
if(NOT CSX_VERSION MATCHES [[^([0-9]+)\.([0-9]+)-(SE|VR)$]])
    message(
        FATAL_ERROR
        "CSX_VERSION must use the <major>.<minor>-<SE|VR> format."
    )
endif()
set(_CSX_PROJECT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.0")

project(
    # gersemi: ignore
    CommunityShaders
    VERSION "${_CSX_PROJECT_VERSION}"
    DESCRIPTION "Community Shaders Expanded (CSX)"
    LANGUAGES CXX
)

set(CSX_PACKAGE_NAME "CSX")

# default install path
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set_property(
        CACHE CMAKE_INSTALL_PREFIX
        PROPERTY VALUE "${CMAKE_CURRENT_BINARY_DIR}/aio"
    )
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ########################################################################################################################
# ## Build options
# ########################################################################################################################
message("Options:")
option(
    AUTO_PLUGIN_DEPLOYMENT
    "Copy the build output and addons to env:CommunityShadersOutputDir."
    OFF
)
option(
    ZIP_TO_DIST
    "Zip the base mod and addons to their own 7z file in dist."
    ON
)
option(
    AIO_ZIP_TO_DIST
    "Zip the base mod and addons to a AIO 7z file in dist."
    ON
)
option(TRACY_SUPPORT "Enable support for tracy profiler" OFF)
option(
    BUILD_SHADER_TESTS
    "Build shader unit tests (runs automatically before packaging)"
    ON
)
option(
    BUILD_CONTROLLER_TESTS
    "Build dependency-free controller state regression tests"
    OFF
)
option(
    AIO_EXCLUDE_HIDDEN_FEATURE_PACKAGES
    "Exclude feature packages hidden from user-facing feature lists from AIO and package outputs."
    OFF
)
set(AIO_EXCLUDED_FEATURE_PACKAGES
    ""
    CACHE STRING
    "Additional semicolon-separated feature package directories to exclude from AIO and package outputs."
)
set(AIO_NON_SHIPPED_FEATURE_PACKAGES "Wetness Effects")
message("\tAuto plugin deployment: ${AUTO_PLUGIN_DEPLOYMENT}")
message("\tZip to dist: ${ZIP_TO_DIST}")
message("\tAIO Zip to dist: ${AIO_ZIP_TO_DIST}")
message("\tTracy profiler: ${TRACY_SUPPORT}")
message("\tShader tests: ${BUILD_SHADER_TESTS}")
message("\tController tests: ${BUILD_CONTROLLER_TESTS}")
message(
    "\tExclude hidden AIO feature packages: ${AIO_EXCLUDE_HIDDEN_FEATURE_PACKAGES}"
)
message(
    "\tAdditional excluded AIO feature packages: ${AIO_EXCLUDED_FEATURE_PACKAGES}"
)
message(
    "\tNon-shipped AIO feature packages: ${AIO_NON_SHIPPED_FEATURE_PACKAGES}"
)

# #######################################################################################################################
# # Build version info from git
# #######################################################################################################################
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_DESCRIBE "unknown")
endif()
message("\tBuild version: ${GIT_DESCRIBE}")
message("\tCommunity Shaders Expanded version: CSX ${CSX_VERSION}")

# #######################################################################################################################
# # Add CMake features
# #######################################################################################################################
include(XSEPlugin)

# #######################################################################################################################
# # Find dependencies
# #######################################################################################################################
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
find_package(magic_enum CONFIG REQUIRED)
find_package(xbyak CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(EASTL CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_path(CLIB_UTIL_INCLUDE_DIRS "ClibUtil/utils.hpp")
find_package(pystring CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)
find_package(unordered_dense CONFIG REQUIRED)
find_package(efsw CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_path(RENDERDOC_INCLUDE_DIRS "Renderdoc/renderdoc_app.h" REQUIRED)
if(DEVBENCH_BRIDGE)
    find_package(devbench-api CONFIG REQUIRED)
endif()
add_subdirectory(${CMAKE_SOURCE_DIR}/cmake/Streamline)

find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)
include(FidelityFX-SDK)
include(FidelityFX-Runtime)

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE "$<$<BOOL:${TRACY_SUPPORT}>:TRACY_SUPPORT>"
)

file(
    GLOB FEATURE_SHADER_DIRS
    RELATIVE "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/features/*/Shaders"
)

foreach(_dir IN LISTS FEATURE_SHADER_DIRS)
    target_include_directories(
        ${PROJECT_NAME}
        PRIVATE "${CMAKE_SOURCE_DIR}/${_dir}"
    )
endforeach()

target_include_directories(
    ${PROJECT_NAME}
    BEFORE
    PRIVATE
        ${RENDERDOC_INCLUDE_DIRS}
        ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}
        ${CLIB_UTIL_INCLUDE_DIRS}
        "${CMAKE_SOURCE_DIR}/package/Shaders"
        ${DETOURS_INCLUDE_DIRS}
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        Microsoft::CppWinRT
        magic_enum::magic_enum
        xbyak::xbyak
        nlohmann_json::nlohmann_json
        imgui::imgui
        EASTL
        Microsoft::DirectXTK
        Microsoft::DirectXTex
        pystring::pystring
        unordered_dense::unordered_dense
        efsw::efsw
        xxHash::xxhash
        Tracy::TracyClient
        Streamline
        d3d12.lib
        Microsoft::DirectX-Headers
        ${DETOURS_LIBRARY}
)

if(DEVBENCH_BRIDGE)
    target_link_libraries(${PROJECT_NAME} PRIVATE DevBench::API)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEVBENCH_BRIDGE_ENABLED)
endif()

# https://gitlab.kitware.com/cmake/cmake/-/issues/24922#note_1371990
if(MSVC_VERSION GREATER_EQUAL 1936 AND MSVC_IDE) # 17.6+
    # When using /std:c++latest, "Build ISO C++23 Standard Library Modules" defaults to "Yes".
    # Default to "No" instead.
    #
    # As of CMake 3.26.4, there isn't a way to control this property
    # (https://gitlab.kitware.com/cmake/cmake/-/issues/24922),
    # We'll use the MSBuild project system instead
    # (https://learn.microsoft.com/en-us/cpp/build/reference/vcxproj-file-structure)
    file(
        CONFIGURE
        OUTPUT "${CMAKE_BINARY_DIR}/Directory.Build.props"
        CONTENT
            [==[
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <BuildStlModules>false</BuildStlModules>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
]==]
        @ONLY
    )
endif()

# #######################################################################################################################
# # Feature version detection
# #######################################################################################################################
file(
    GLOB_RECURSE FEATURE_CONFIG_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "features/*/Shaders/Features/*.ini"
)

foreach(FEATURE_PATH ${FEATURE_CONFIG_FILES})
    get_filename_component(FEATURE ${FEATURE_PATH} NAME_WE)
    file(READ "${FEATURE_PATH}" CONFIG_VALUE)
    string(STRIP "${CONFIG_VALUE}" CONFIG_VALUE)
    if(CONFIG_VALUE)
        string(
            REGEX MATCH "[Vv]ersion = ([0-9]+)-([0-9]+)-([0-9]+)"
            _
            "${CONFIG_VALUE}"
        )
        if(
            DEFINED CMAKE_MATCH_1
            AND DEFINED CMAKE_MATCH_2
            AND DEFINED CMAKE_MATCH_3
        )
            set(ver_major ${CMAKE_MATCH_1})
            set(ver_minor ${CMAKE_MATCH_2})
            set(ver_patch ${CMAKE_MATCH_3})
            list(
                APPEND FEATURE_VERSIONS
                "\t\t{\"${FEATURE}\"sv, {${ver_major},${ver_minor},${ver_patch}}}"
            )
        else()
            message(
                WARNING
                "Feature config file '${FEATURE_PATH}' does not contain a valid version string. Skipping."
            )
        endif()
    else()
        message(
            WARNING
            "Feature config file '${FEATURE_PATH}' is empty or contains only whitespace. Skipping version detection for this feature."
        )
    endif()

    # Detect core features by checking for the CORE marker file in the
    # feature root (features/<Folder>/CORE). FEATURE_PATH is
    # features/<Folder>/Shaders/Features/<ShortName>.ini, so the feature
    # root is three directories up.
    get_filename_component(_FEATURE_DIR "${FEATURE_PATH}" DIRECTORY)
    get_filename_component(_FEATURE_DIR "${_FEATURE_DIR}" DIRECTORY)
    get_filename_component(_FEATURE_DIR "${_FEATURE_DIR}" DIRECTORY)
    set("FEATURE_DIR_FOR_SHORT_NAME_${FEATURE}" "${_FEATURE_DIR}")
    if(EXISTS "${_FEATURE_DIR}/CORE")
        list(APPEND FEATURE_CORE_NAMES "\t\t\"${FEATURE}\"sv")
    endif()
endforeach()

set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${FEATURE_CONFIG_FILES}"
)

string(REPLACE ";" ",\n" FEATURE_VERSIONS "${FEATURE_VERSIONS}")
string(REPLACE ";" ",\n" FEATURE_CORE_NAMES "${FEATURE_CORE_NAMES}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FeatureVersions.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/FeatureVersions.h
    @ONLY
)

target_sources(
    "${PROJECT_NAME}"
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/cmake/FeatureVersions.h
)

# #######################################################################################################################
# # Theme presets
# #######################################################################################################################
set(THEMES_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/package/SKSE/Plugins/CommunityShaders/Themes"
)
file(GLOB THEME_JSON_FILES CONFIGURE_DEPENDS "${THEMES_DIR}/*.json")
set(THEME_PRESET_NAMES_LIST "")
foreach(f IN LISTS THEME_JSON_FILES)
    get_filename_component(name "${f}" NAME_WE)
    list(APPEND THEME_PRESET_NAMES_LIST "${name}")
endforeach()
list(SORT THEME_PRESET_NAMES_LIST)
list(LENGTH THEME_PRESET_NAMES_LIST THEME_PRESET_COUNT)
set(THEME_PRESET_NAMES "")
foreach(p IN LISTS THEME_PRESET_NAMES_LIST)
    set(THEME_PRESET_NAMES "${THEME_PRESET_NAMES}\t\t\"${p}\",\n")
endforeach()
string(REGEX REPLACE ",\n$" "" THEME_PRESET_NAMES "${THEME_PRESET_NAMES}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThemePresets.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/ThemePresets.h
    @ONLY
)

target_sources(
    "${PROJECT_NAME}"
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/cmake/ThemePresets.h
)

# #######################################################################################################################
# # clang-format
# #######################################################################################################################

find_program(CLANG_FORMAT_PATH clang-format)
if(CLANG_FORMAT_PATH)
    add_custom_target(
        FORMAT_CODE
        COMMAND ${CLANG_FORMAT_PATH} -i -style=file ${CPP_SOURCES};${HLSL_FILES}
        COMMENT "Running clang format for cpp and hlsl files"
    )
endif()

# #######################################################################################################################
# # HLSL additional include directories for VS intellisense
# #######################################################################################################################

set(HLSL_INCLUDE_DIRS ${FEATURE_SHADER_DIRS} "package/Shaders")

set(HLSL_INCLUDE_JSON "")
foreach(dir IN LISTS HLSL_INCLUDE_DIRS)
    if(HLSL_INCLUDE_JSON STREQUAL "")
        set(HLSL_INCLUDE_JSON "    \"${dir}\"")
    else()
        set(HLSL_INCLUDE_JSON "${HLSL_INCLUDE_JSON},\n    \"${dir}\"")
    endif()
endforeach()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shadertoolsconfig.json.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/shadertoolsconfig.json"
    @ONLY
)

# #######################################################################################################################
# # Shader validation config generation
# #######################################################################################################################

# Add target to generate shader validation configuration files
# This requires hlslkit and valid Skyrim installations with recent log files
find_program(POWERSHELL_PATH pwsh powershell)
if(POWERSHELL_PATH)
    add_custom_target(
        generate_shader_configs
        COMMAND
            ${POWERSHELL_PATH} -ExecutionPolicy Bypass -File
            "${CMAKE_SOURCE_DIR}/.github/configs/generate-shader-configs.ps1"
            -OutputDir "${CMAKE_SOURCE_DIR}/.github/configs"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT
            "Generating shader validation configuration files from Skyrim log files"
    )
endif()

# #######################################################################################################################
# # Automatic deployment
# #######################################################################################################################
file(
    GLOB FEATURE_PATHS
    LIST_DIRECTORIES true
    CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/features/*
)

set(_AIO_FEATURE_DIR_NAMES_TO_EXCLUDE
    ${AIO_NON_SHIPPED_FEATURE_PACKAGES}
    ${AIO_EXCLUDED_FEATURE_PACKAGES}
)

if(AIO_EXCLUDE_HIDDEN_FEATURE_PACKAGES)
    file(
        GLOB_RECURSE _AIO_FEATURE_HEADER_FILES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/Features/*.h"
    )

    set(_AIO_HIDDEN_FEATURE_SHORT_NAMES)
    foreach(_feature_header IN LISTS _AIO_FEATURE_HEADER_FILES)
        file(READ "${_feature_header}" _feature_header_contents)
        string(
            REGEX MATCH
                "IsHiddenFromUserView[^{]*\\{[ \t\r\n]*return[ \t\r\n]*true[ \t\r\n]*;"
            _hidden_feature_match
            "${_feature_header_contents}"
        )
        if(_hidden_feature_match)
            set(_hidden_feature_short_name "")
            string(
                REGEX MATCH
                    "GetShortName[^{]*\\{[ \t\r\n]*return[ \t\r\n]*\"([^\"]+)\""
                _short_name_match
                "${_feature_header_contents}"
            )
            if(_short_name_match)
                set(_hidden_feature_short_name "${CMAKE_MATCH_1}")
            else()
                string(
                    REGEX MATCH
                        "kFeatureShortName[ \t\r\n]*=[ \t\r\n]*\"([^\"]+)\""
                    _short_name_constant_match
                    "${_feature_header_contents}"
                )
                if(_short_name_constant_match)
                    set(_hidden_feature_short_name "${CMAKE_MATCH_1}")
                endif()
            endif()
            if(_hidden_feature_short_name)
                list(
                    APPEND _AIO_HIDDEN_FEATURE_SHORT_NAMES
                    "${_hidden_feature_short_name}"
                )
            else()
                message(
                    WARNING
                    "Could not detect short name for hidden feature header: ${_feature_header}"
                )
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _AIO_HIDDEN_FEATURE_SHORT_NAMES)

    foreach(_feature_short_name IN LISTS _AIO_HIDDEN_FEATURE_SHORT_NAMES)
        if(DEFINED FEATURE_DIR_FOR_SHORT_NAME_${_feature_short_name})
            set(_hidden_feature_dir
                "${FEATURE_DIR_FOR_SHORT_NAME_${_feature_short_name}}"
            )
            get_filename_component(
                _feature_dir_name
                "${_hidden_feature_dir}"
                NAME
            )
            list(
                APPEND _AIO_FEATURE_DIR_NAMES_TO_EXCLUDE
                "${_feature_dir_name}"
            )
            message(
                STATUS
                "Detected hidden feature for AIO/package exclusion: ${_feature_short_name} (${_feature_dir_name})"
            )
        else()
            message(
                WARNING
                "Hidden feature short name does not match a packaged feature config: ${_feature_short_name}"
            )
        endif()
    endforeach()
endif()

if(_AIO_FEATURE_DIR_NAMES_TO_EXCLUDE)
    list(REMOVE_DUPLICATES _AIO_FEATURE_DIR_NAMES_TO_EXCLUDE)
    set(_VISIBLE_FEATURE_PATHS)
    set(_UNMATCHED_AIO_FEATURE_DIR_NAMES_TO_EXCLUDE
        ${AIO_NON_SHIPPED_FEATURE_PACKAGES}
        ${AIO_EXCLUDED_FEATURE_PACKAGES}
    )
    foreach(_fpath IN LISTS FEATURE_PATHS)
        get_filename_component(_feature_dir_name "${_fpath}" NAME)
        if(_feature_dir_name IN_LIST _AIO_FEATURE_DIR_NAMES_TO_EXCLUDE)
            list(
                REMOVE_ITEM _UNMATCHED_AIO_FEATURE_DIR_NAMES_TO_EXCLUDE
                "${_feature_dir_name}"
            )
            message(
                STATUS
                "Excluding feature from AIO/package outputs: ${_feature_dir_name}"
            )
        else()
            list(APPEND _VISIBLE_FEATURE_PATHS "${_fpath}")
        endif()
    endforeach()
    set(FEATURE_PATHS "${_VISIBLE_FEATURE_PATHS}")
    foreach(
        _feature_dir_name
        IN
        LISTS _UNMATCHED_AIO_FEATURE_DIR_NAMES_TO_EXCLUDE
    )
        message(
            WARNING
            "Configured AIO feature exclusion does not match a feature package: ${_feature_dir_name}"
        )
    endforeach()
endif()
string(TIMESTAMP UTC_NOW "%Y-%m-%dT%H-%MZ" UTC)

# Set AIO directory path used by multiple targets below
set(AIO_DIR "${CMAKE_CURRENT_BINARY_DIR}/aio")

# Robocopy wrapper for Windows incremental file copy (used by deployment targets).
# Invoke through cmd /c with the absolute path: CMake may strip binary-dir
# command paths in generated MSBuild rules, and Windows will not run a script
# from the current directory without an explicit path prefix.
if(WIN32)
    set(ROBOCOPY_WRAPPER_PATH "${CMAKE_BINARY_DIR}/robocopy_wrapper.cmd")
    file(
        WRITE "${ROBOCOPY_WRAPPER_PATH}"
        "@echo off\r\nrem Robocopy wrapper: forwards all args to robocopy and normalizes exit codes\r\nrobocopy %*\r\nset rc=%ERRORLEVEL%\r\nif %rc% GEQ 8 exit /b %rc%\r\nexit /b 0\r\n"
    )
    set(ROBOCOPY_WRAPPER cmd /c "${ROBOCOPY_WRAPPER_PATH}")
endif()

# #######################################################################################################################
# # CMake install() infrastructure for manual packaging
# #######################################################################################################################

# Append a '/' to the end of each feature path for installation all its contents but not itself
set(FEATURE_PATHS_SLASH ${FEATURE_PATHS})
list(TRANSFORM FEATURE_PATHS_SLASH APPEND /)

# Install logic for AIO package
# To copy AIO package at a folder do `${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR}`
install(CODE "file(REMOVE_RECURSE \${CMAKE_INSTALL_PREFIX})")
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION SKSE/Plugins COMPONENT SKSE)
install(
    FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
    DESTINATION SKSE/Plugins
    COMPONENT SKSE
)
install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/package/ ${FEATURE_PATHS_SLASH}
    DESTINATION .
    COMPONENT Shaders
    PATTERN "Tests" EXCLUDE
)
install(
    FILES ${FFX_RUNTIME_FILES}
    DESTINATION Shaders/Upscaling/FidelityFX
    COMPONENT FidelityFXRuntime
)
install(CODE "file(REMOVE \${CMAKE_INSTALL_PREFIX}/Core)" COMPONENT Shaders)

# #######################################################################################################################
# # Automatic AIO preparation (incremental copy system)
# #######################################################################################################################

if(AUTO_PLUGIN_DEPLOYMENT OR AIO_ZIP_TO_DIST)
    message("Preparing AIO package in ${AIO_DIR}")

    function(append_copy_if_different _cmds _srcs _src_base _dst_base)
        foreach(_src IN LISTS ${_srcs})
            file(RELATIVE_PATH _rel "${_src_base}" "${_src}")
            set(_dst "${_dst_base}/${_rel}")
            get_filename_component(_dst_dir "${_dst}" DIRECTORY)
            list(
                APPEND ${_cmds}
                COMMAND
                ${CMAKE_COMMAND}
                -E
                make_directory
                "${_dst_dir}"
                COMMAND
                ${CMAKE_COMMAND}
                -E
                copy_if_different
                "${_src}"
                "${_dst}"
            )
        endforeach()
        set(${_cmds} ${${_cmds}} PARENT_SCOPE)
    endfunction()

    # Prepare AIO only when sources change. Gather package + feature files as
    # inputs so the prepare step runs only when something actually changed.
    # Shader files are intentionally excluded: copy_shaders.stamp owns those
    # destinations, and parallel copy_if_different calls can race on Windows.
    file(
        GLOB_RECURSE _AIO_PACKAGE_FILES
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/*"
    )
    list(FILTER _AIO_PACKAGE_FILES EXCLUDE REGEX "/Tests/")
    list(FILTER _AIO_PACKAGE_FILES EXCLUDE REGEX "/Shaders/")
    foreach(_fpath IN LISTS FEATURE_PATHS)
        file(GLOB_RECURSE _tmp LIST_DIRECTORIES FALSE "${_fpath}/*")
        list(FILTER _tmp EXCLUDE REGEX "/Shaders/")
        list(APPEND _AIO_PACKAGE_FILES ${_tmp})
    endforeach()

    # Prepare AIO by copying files only when different. This avoids updating
    # timestamps for unchanged files and prevents downstream incremental
    # deploys from copying everything every build.
    set(_prepare_aio_cmds)
    # Write the list to a file instead of passing an escaped-semicolon value
    # through -D. Ninja misquotes list entries containing spaces.
    set(_feature_paths_list_file
        "${CMAKE_CURRENT_BINARY_DIR}/feature_paths.txt"
    )
    set(_aio_source_paths ${FEATURE_PATHS} "${FFX_RUNTIME_FEATURE_ROOT}")
    string(REPLACE ";" "\n" _feature_paths_file_content "${_aio_source_paths}")
    file(WRITE "${_feature_paths_list_file}" "${_feature_paths_file_content}")

    # Ensure SKSE/Plugins dir exists
    # Note: DLL and PDB are copied via POST_BUILD command to avoid race conditions
    list(
        APPEND _prepare_aio_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -E
        make_directory
        "${AIO_DIR}/SKSE/Plugins"
    )
    list(
        APPEND _prepare_aio_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -DMODE=AIO
        -DAIO_DIR="${AIO_DIR}"
        -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
        -DFEATURE_PATHS_FILE="${_feature_paths_list_file}"
        -P
        "${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake"
    )

    # Copy package files (exclude test files and shaders from production
    # non-shader copy; shader copying is handled by copy_shaders.stamp).
    file(
        GLOB_RECURSE _AIO_PACKAGE_SOURCE_FILES
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/*"
    )
    list(FILTER _AIO_PACKAGE_SOURCE_FILES EXCLUDE REGEX "/Tests/")
    list(FILTER _AIO_PACKAGE_SOURCE_FILES EXCLUDE REGEX "/Shaders/")
    append_copy_if_different(_prepare_aio_cmds _AIO_PACKAGE_SOURCE_FILES "${CMAKE_SOURCE_DIR}/package" "${AIO_DIR}")

    # Copy feature folders (only files, preserve existing files in AIO).
    # Shader files are excluded because copy_shaders.stamp owns Shaders/.
    #
    # CORE markers are excluded from the per-feature copy: each marker would
    # write to the same ${AIO_DIR}/CORE path, and the cleanup below removes it
    # anyway. Parallel MSBuild copies can otherwise race on that shared file.
    foreach(_fpath IN LISTS FEATURE_PATHS)
        if(EXISTS "${_fpath}")
            file(
                GLOB_RECURSE _feature_files
                LIST_DIRECTORIES FALSE
                "${_fpath}/*"
            )
            list(FILTER _feature_files EXCLUDE REGEX "/Shaders/")
            list(FILTER _feature_files EXCLUDE REGEX "/CORE$")
            append_copy_if_different(_prepare_aio_cmds _feature_files "${_fpath}" "${AIO_DIR}")
        endif()
    endforeach()

    # Remove stale CORE markers from older builds that predate the exclusion
    # above. Cheap, idempotent.
    list(
        APPEND _prepare_aio_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -E
        remove
        "${AIO_DIR}/CORE"
    )
    list(
        APPEND _prepare_aio_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -E
        touch
        ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
    )

    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
            ${_prepare_aio_cmds}
        DEPENDS
            ${_AIO_PACKAGE_FILES}
            ${PROJECT_NAME}
            ${_feature_paths_list_file}
            ${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake
    )

    # If AIO was manually or programmatically deleted, the stamps are stale.
    # Clear them so the next build recreates the directory.
    if(NOT EXISTS "${AIO_DIR}")
        file(
            REMOVE
            "${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp"
            "${CMAKE_CURRENT_BINARY_DIR}/copy_shaders.stamp"
        )
    endif()

    add_custom_target(
        PREPARE_AIO
        ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
    )

    # Copy DLL and PDB using POST_BUILD to avoid race conditions with file locking
    # This ensures the linker has fully released the files before we attempt to copy them
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AIO_DIR}/SKSE/Plugins"
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${AIO_DIR}/SKSE/Plugins/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
            "${AIO_DIR}/SKSE/Plugins/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
        COMMENT "Copying built DLL and PDB to AIO package"
        VERBATIM
    )

    # Copy shader-package content individually so unchanged files keep their
    # timestamps. Track every staged asset (not only HLSL/HLSLI): feature
    # shader trees also contain runtime configuration, textures, libraries,
    # symbols, and notices that must retrigger staging when changed.
    file(
        GLOB_RECURSE _package_shaders
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/Shaders/*"
    )
    # Exclude test files from production packages
    list(FILTER _package_shaders EXCLUDE REGEX "/Tests/")
    set(_all_shader_sources ${_package_shaders})
    set(_shader_copy_cmds)

    set(_feature_shader_paths "${FFX_RUNTIME_SHADER_ROOT}")
    foreach(_fpath IN LISTS FEATURE_PATHS)
        if(EXISTS "${_fpath}/Shaders")
            list(APPEND _feature_shader_paths "${_fpath}/Shaders")
        endif()
    endforeach()
    # Use the same file transport for feature shader paths so generators do
    # not reinterpret space-containing entries as malformed command arguments.
    set(_feature_shader_paths_list_file
        "${CMAKE_CURRENT_BINARY_DIR}/feature_shader_paths.txt"
    )
    string(
        REPLACE ";"
        "\n"
        _feature_shader_paths_file_content
        "${_feature_shader_paths}"
    )
    file(
        WRITE "${_feature_shader_paths_list_file}"
        "${_feature_shader_paths_file_content}"
    )

    list(
        APPEND _shader_copy_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -DMODE=SHADERS
        -DAIO_DIR="${AIO_DIR}"
        -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
        -DFEATURE_SHADER_PATHS_FILE="${_feature_shader_paths_list_file}"
        -P
        "${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake"
    )

    append_copy_if_different(_shader_copy_cmds _package_shaders "${CMAKE_SOURCE_DIR}/package/Shaders" "${AIO_DIR}/Shaders")
    append_copy_if_different(_shader_copy_cmds FFX_RUNTIME_FILES "${FFX_RUNTIME_SHADER_ROOT}" "${AIO_DIR}/Shaders")

    # feature shader folders
    foreach(_fpath IN LISTS FEATURE_PATHS)
        if(EXISTS "${_fpath}/Shaders")
            file(
                GLOB_RECURSE _feat_shaders
                CONFIGURE_DEPENDS
                LIST_DIRECTORIES FALSE
                "${_fpath}/Shaders/*"
            )
            list(FILTER _feat_shaders EXCLUDE REGEX "/Tests/")
            list(APPEND _all_shader_sources ${_feat_shaders})
            append_copy_if_different(_shader_copy_cmds _feat_shaders "${_fpath}/Shaders" "${AIO_DIR}/Shaders")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _all_shader_sources)

    add_custom_command(
        OUTPUT copy_shaders.stamp
        COMMAND
            ${CMAKE_COMMAND} -E make_directory "${AIO_DIR}/Shaders"
            ${_shader_copy_cmds}
        COMMAND ${CMAKE_COMMAND} -E touch copy_shaders.stamp
        DEPENDS
            ${_all_shader_sources}
            ${FFX_RUNTIME_FILES}
            ${_feature_shader_paths_list_file}
            ${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake
        COMMENT "Copying changed shaders into AIO/Shaders"
    )

    # Standalone target for preparing shaders for CI validation
    # This allows shader validation to run without waiting for the full build
    add_custom_target(
        prepare_shaders
        DEPENDS copy_shaders.stamp
        COMMENT "Preparing shaders for validation"
    )

    # Incremental local validation: compile only the shaders affected by the
    # current working-tree diff (vs HEAD). Mirrors the CI incremental path but
    # derives the change set from git instead of a precomputed list. Override
    # the config with -DVALIDATE_CHANGED_CONFIG=... (defaults to Flatrim).
    set(VALIDATE_CHANGED_CONFIG
        "${CMAKE_SOURCE_DIR}/.github/configs/shader-validation.yaml"
        CACHE FILEPATH
        "hlslkit config used by the validate_changed target"
    )
    add_custom_target(
        validate_changed
        COMMAND
            ${CMAKE_COMMAND} -E env python
            "${CMAKE_SOURCE_DIR}/tools/validate_changed_shaders.py" --repo-root
            "${CMAKE_SOURCE_DIR}" -- hlslkit-compile --shader-dir
            "${AIO_DIR}/Shaders" --output-dir "${CMAKE_BINARY_DIR}/ShaderCache"
            --config "${VALIDATE_CHANGED_CONFIG}" --max-warnings 0
            --suppress-warnings X1519
        DEPENDS prepare_shaders
        USES_TERMINAL
        COMMENT "Validating only shaders affected by working-tree changes"
    )
endif()

# Automatic deployment to CommunityShaders output directory.
if(AUTO_PLUGIN_DEPLOYMENT)
    # Detect git HEAD changes (branch switch or new commit) and invalidate stale
    # deploy state. Preserve AIO/Shaders: the shader deploy hop below compares
    # content, so recreating unchanged staged files would only produce false
    # runtime FileWatcher events. Non-shader content remains timestamp-deployed.
    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _git_head
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_git_head)
        set(_git_head_stamp "${CMAKE_BINARY_DIR}/git-head.stamp")
        set(_prev_git_head "")
        if(EXISTS "${_git_head_stamp}")
            file(READ "${_git_head_stamp}" _prev_git_head)
            string(STRIP "${_prev_git_head}" _prev_git_head)
        endif()
        if(NOT "${_git_head}" STREQUAL "${_prev_git_head}")
            message(
                STATUS
                "Git HEAD changed (${_prev_git_head} -> ${_git_head}): reconciling shaders and clearing non-shader deploy state"
            )
            # Delete everything under AIO except Shaders. The shader staging
            # stamp is still cleared so deletion-only changes are reconciled;
            # copy_if_different preserves the mtimes of unchanged files.
            file(GLOB _aio_top_entries "${AIO_DIR}/*")
            foreach(_entry IN LISTS _aio_top_entries)
                get_filename_component(_entry_name "${_entry}" NAME)
                if(NOT _entry_name STREQUAL "Shaders")
                    file(REMOVE_RECURSE "${_entry}")
                endif()
            endforeach()
            file(
                GLOB _build_stamps
                "${CMAKE_BINARY_DIR}/*_deploy.stamp"
                "${CMAKE_BINARY_DIR}/*_plugin.stamp"
                "${CMAKE_BINARY_DIR}/*_shaders.stamp"
                "${CMAKE_BINARY_DIR}/*_shaders_full.stamp"
                "${CMAKE_BINARY_DIR}/*_shaders_only.stamp"
                "${CMAKE_BINARY_DIR}/prepare_aio.stamp"
                "${CMAKE_BINARY_DIR}/copy_shaders.stamp"
            )
            foreach(_stamp IN LISTS _build_stamps)
                file(REMOVE "${_stamp}")
            endforeach()
        endif()
        file(WRITE "${_git_head_stamp}" "${_git_head}")
    endif()

    set(DEPLOY_TARGET_HASHES)
    if(WIN32)
        foreach(DEPLOY_TARGET $ENV{CommunityShadersOutputDir})
            message("Deploying AIO to ${DEPLOY_TARGET} (incremental)")

            # Ensure destination root exists
            add_custom_command(
                TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_TARGET}"
            )

            string(MD5 DEPLOY_TARGET_HASH ${DEPLOY_TARGET})

            # Incremental copy (non-shaders) - produce a stamp so the COPY_SHADERS
            # target can depend on both shader and non-shader deploy steps.
            add_custom_command(
                OUTPUT ${DEPLOY_TARGET_HASH}_deploy.stamp
                COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_TARGET}"
                # No /XO: a previous manual or external deploy can leave dest
                # files with mtime > source. /XO then skips them even when
                # source content changed, so build output silently diverges from
                # what runs in-game. Without /XO, robocopy's default
                # name+size+timestamp delta catches newer-source and
                # size-mismatch cases.
                COMMAND
                    ${ROBOCOPY_WRAPPER} "${AIO_DIR}" "${DEPLOY_TARGET}" "/E"
                    "/XD" "${AIO_DIR}/Shaders" "/COPY:DAT" "/R:1" "/W:1" "/NFL"
                    "/NDL" "/NJH" "/NJS"
                COMMAND
                    ${CMAKE_COMMAND} -E touch ${DEPLOY_TARGET_HASH}_deploy.stamp
                # Depend on the PREPARE_AIO target AND its stamp file: the
                # target-level dependency stops the VS generator duplicating
                # PREPARE_AIO's producing rule into DEPLOY_ALL.vcxproj (two
                # copies race writing the same AIO tree); the stamp file adds
                # the mtime edge so this rule re-runs when staging changes
                # (target-only DEPENDS is order-only and never re-triggers).
                DEPENDS
                    PREPARE_AIO
                    ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
                COMMENT
                    "Incremental deploy (excluding Shaders) to ${DEPLOY_TARGET} (robocopy-wrapper)"
            )

            # Ensure plugin DLL/PDB are copied directly to the target SKSE/Plugins
            # folder in case robocopy rules do not copy them as expected.
            add_custom_command(
                OUTPUT ${DEPLOY_TARGET_HASH}_plugin.stamp
                COMMAND
                    ${CMAKE_COMMAND} -E make_directory
                    "${DEPLOY_TARGET}/SKSE/Plugins"
                COMMAND
                    ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${PROJECT_NAME}>
                    "${DEPLOY_TARGET}/SKSE/Plugins/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
                COMMAND
                    ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_PDB_FILE:${PROJECT_NAME}>
                    "${DEPLOY_TARGET}/SKSE/Plugins/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
                COMMAND
                    ${CMAKE_COMMAND} -E touch ${DEPLOY_TARGET_HASH}_plugin.stamp
                # Target-only dependencies are order-only, so also depend on
                # the built DLL file to retrigger deployment when it changes.
                DEPENDS
                    PREPARE_AIO
                    ${PROJECT_NAME}
                    $<TARGET_FILE:${PROJECT_NAME}>
                COMMENT "Copy plugin DLL/PDB to ${DEPLOY_TARGET}/SKSE/Plugins"
            )

            list(APPEND DEPLOY_TARGET_HASHES ${DEPLOY_TARGET_HASH}_plugin.stamp)

            # One canonical shader-sync rule serves shader-only and full
            # deploys. This avoids concurrent writers to the same ownership
            # manifest while preserving the fast path's DLL-free dependency
            # graph. Files are copied only when their content differs.
            add_custom_command(
                OUTPUT ${DEPLOY_TARGET_HASH}_shaders.stamp
                COMMAND
                    ${CMAKE_COMMAND} -DSRC_DIR="${AIO_DIR}/Shaders"
                    -DDST_DIR="${DEPLOY_TARGET}/Shaders"
                    -DMANIFEST_FILE="${CMAKE_CURRENT_BINARY_DIR}/${DEPLOY_TARGET_HASH}_shaders.manifest"
                    -P "${CMAKE_SOURCE_DIR}/cmake/SyncShaderDeploy.cmake"
                COMMAND
                    ${CMAKE_COMMAND} -E touch
                    ${DEPLOY_TARGET_HASH}_shaders.stamp
                # See the PREPARE_AIO target+stamp rationale above.
                DEPENDS
                    prepare_shaders
                    ${CMAKE_CURRENT_BINARY_DIR}/copy_shaders.stamp
                    ${CMAKE_SOURCE_DIR}/cmake/SyncShaderDeploy.cmake
                COMMENT
                    "Content-based shader deploy to ${DEPLOY_TARGET}/Shaders"
                VERBATIM
            )

            list(APPEND SHADER_ONLY_HASHES ${DEPLOY_TARGET_HASH}_shaders.stamp)

            list(APPEND DEPLOY_TARGET_HASHES ${DEPLOY_TARGET_HASH}_deploy.stamp)
            list(
                APPEND DEPLOY_TARGET_HASHES
                ${DEPLOY_TARGET_HASH}_shaders.stamp
            )
        endforeach()
    else()
        # AUTO_PLUGIN_DEPLOYMENT is enabled but the host is not Windows. Do
        # not attempt to deploy to local Skyrim directories on non-Windows
        # systems; instead provide a minimal COPY_SHADERS target so CI jobs
        # that only prepare shaders still work.
        message(
            WARNING
            "AUTO_PLUGIN_DEPLOYMENT is enabled but not supported on this platform; skipping deployment to CommunityShadersOutputDir"
        )
    endif()

    # Lightweight target for fast shader dev iteration
    # Deploys ONLY shaders to game directory (no DLL build, no tests)
    add_custom_target(
        COPY_SHADERS
        DEPENDS ${SHADER_ONLY_HASHES}
        COMMENT "Fast shader-only deploy to game directory (no DLL, no tests)"
    )

    # Full deployment target for packaging/CI
    # Builds DLL, prepares AIO, deploys everything, runs tests
    # Depend on the prepare_shaders target (not its copy_shaders.stamp
    # output file): the per-deploy-target stamps above already order
    # themselves after prepare_shaders/PREPARE_AIO by target name, and
    # listing the raw file here too would make the VS generator duplicate
    # the shader-copy rule into DEPLOY_ALL.vcxproj, letting that duplicate
    # race the real one in prepare_shaders.vcxproj for the same AIO tree.
    add_custom_target(
        DEPLOY_ALL
        ALL
        DEPENDS prepare_shaders ${DEPLOY_TARGET_HASHES}
        COMMENT "Full deployment: DLL + shaders + all files to game directory"
    )
endif()

if(NOT DEFINED ENV{CommunityShadersOutputDir})
    message(
        "When using AUTO_PLUGIN_DEPLOYMENT option, you need to set environment variable 'CommunityShadersOutputDir'"
    )
endif()

# Zip base CommunityShaders and all addons as their own 7z in dist folder
if(ZIP_TO_DIST)
    set(ZIP_DIR "${CMAKE_CURRENT_BINARY_DIR}/zip")
    message("Copying base CommunityShader into ${ZIP_DIR}.")
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E remove_directory "${ZIP_DIR}"
            ${CMAKE_SOURCE_DIR}/dist
        COMMAND
            ${CMAKE_COMMAND} -E make_directory "${ZIP_DIR}/SKSE/Plugins"
            ${CMAKE_SOURCE_DIR}/dist
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/package
            "${ZIP_DIR}"
        COMMAND
            ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}>
            "${ZIP_DIR}/SKSE/Plugins/"
        COMMAND
            ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:${PROJECT_NAME}>
            "${ZIP_DIR}/SKSE/Plugins/"
    )
    foreach(FEATURE_PATH ${FEATURE_PATHS})
        if(EXISTS "${FEATURE_PATH}/CORE")
            add_custom_command(
                TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND
                    ${CMAKE_COMMAND} -E copy_directory ${FEATURE_PATH}
                    "${ZIP_DIR}"
            )
        endif()
    endforeach()
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove "${ZIP_DIR}/CORE"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${ZIP_DIR}/Shaders/Tests"
    )

    set(TARGET_ZIP "${CSX_PACKAGE_NAME}-${UTC_NOW}.7z")
    message("Zipping ${ZIP_DIR} to ${CMAKE_SOURCE_DIR}/dist/${TARGET_ZIP}")
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E tar cf ${CMAKE_SOURCE_DIR}/dist/${TARGET_ZIP}
            --format=7zip -- .
        WORKING_DIRECTORY ${ZIP_DIR}
    )

    foreach(FEATURE_PATH ${FEATURE_PATHS})
        if(EXISTS "${FEATURE_PATH}/CORE")
            continue()
        endif()
        get_filename_component(FEATURE ${FEATURE_PATH} NAME)
        message(
            "Zipping ${FEATURE_PATH} to ${CMAKE_SOURCE_DIR}/dist/${FEATURE}-${UTC_NOW}.7z"
        )
        if(FEATURE STREQUAL "Upscaling")
            set(
                FEATURE_ZIP_STAGE
                "${CMAKE_CURRENT_BINARY_DIR}/feature_zip/${FEATURE}"
            )
            add_custom_command(
                TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND
                    ${CMAKE_COMMAND} -E remove_directory "${FEATURE_ZIP_STAGE}"
                COMMAND
                    ${CMAKE_COMMAND} -E copy_directory "${FEATURE_PATH}"
                    "${FEATURE_ZIP_STAGE}"
                COMMAND
                    ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix
                    "${FEATURE_ZIP_STAGE}" --component FidelityFXRuntime
                COMMAND
                    ${CMAKE_COMMAND} -E chdir "${FEATURE_ZIP_STAGE}"
                    ${CMAKE_COMMAND} -E tar cf
                    ${CMAKE_SOURCE_DIR}/dist/${FEATURE}-${UTC_NOW}.7z
                    --format=7zip -- .
            )
        else()
            add_custom_command(
                TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND
                    ${CMAKE_COMMAND} -E tar cf
                    ${CMAKE_SOURCE_DIR}/dist/${FEATURE}-${UTC_NOW}.7z
                    --format=7zip -- .
                WORKING_DIRECTORY ${FEATURE_PATH}
            )
        endif()
    endforeach()
endif()

# Create a AIO zip for easier testing
if(AIO_ZIP_TO_DIST)
    if(NOT ZIP_TO_DIST)
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/dist
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist
        )
    endif()
endif()

if(NOT DEFINED ENV{CommunityShadersOutputDir})
    message(
        "When using AUTO_PLUGIN_DEPLOYMENT option, you need to set environment variable 'CommunityShadersOutputDir'"
    )
endif()

# #######################################################################################################################
# # Manual packaging targets (Package-XXX)
# #######################################################################################################################

set(DIST_PATH "${CMAKE_SOURCE_DIR}/dist")
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

set(CORE_PACKAGE "${DIST_PATH}/${CSX_PACKAGE_NAME}-${UTC_NOW}.7z")

# CORE_SOURCES = all content copied to the AIO directory + the SKSE plugin dll
file(
    GLOB_RECURSE CORE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/package/*"
)
# Add SKSE plugin dll as dependency (use target-file generator expression so CMake
# knows the actual output path of the target at build time)
list(APPEND CORE_SOURCES "$<TARGET_FILE:${PROJECT_NAME}>")

set(CORE_FEATURE_PATHS "${CMAKE_SOURCE_DIR}/package/")

foreach(FEATURE_PATH ${FEATURE_PATHS})
    if(EXISTS "${FEATURE_PATH}/CORE")
        list(APPEND CORE_FEATURE_PATHS "${FEATURE_PATH}/")
        file(GLOB_RECURSE FEATURE_SOURCES CONFIGURE_DEPENDS "${FEATURE_PATH}/*")
        list(APPEND CORE_SOURCES ${FEATURE_SOURCES})
    endif()
endforeach()

# Core package
set(FEATURE_PATH "${CMAKE_BINARY_DIR}/Core")
file(MAKE_DIRECTORY ${FEATURE_PATH})
add_custom_command(
    OUTPUT ${CORE_PACKAGE}
    DEPENDS ${CORE_SOURCES}
    COMMAND
        ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${FEATURE_PATH}
        --component SKSE
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory ${CORE_FEATURE_PATHS} ${FEATURE_PATH}
    COMMAND ${CMAKE_COMMAND} -E rm -f -- ${FEATURE_PATH}/Core
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${FEATURE_PATH}/Shaders/Tests"
    COMMAND ${CMAKE_COMMAND} -E tar cfv ${CORE_PACKAGE} --format=7zip -- .
    WORKING_DIRECTORY ${FEATURE_PATH}
    COMMENT "Creating Core zip package"
)
add_custom_target("Package-Core" DEPENDS ${CORE_PACKAGE})

# Feature packages
foreach(FEATURE_PATH ${FEATURE_PATHS})
    if(EXISTS "${FEATURE_PATH}/CORE")
        continue()
    endif()

    list(APPEND CORE_FEATURE_PATHS "${FEATURE_PATH}/")
    file(GLOB_RECURSE FEATURE_SOURCES CONFIGURE_DEPENDS "${FEATURE_PATH}/*")
    list(APPEND CORE_SOURCES ${FEATURE_SOURCES})

    get_filename_component(FEATURE ${FEATURE_PATH} NAME)
    set(FEATURE_PACKAGE "${DIST_PATH}/${FEATURE}-${UTC_NOW}.7z")

    if(FEATURE STREQUAL "Upscaling")
        set(
            FEATURE_PACKAGE_STAGE
            "${CMAKE_CURRENT_BINARY_DIR}/feature_package/${FEATURE}"
        )
        add_custom_command(
            OUTPUT ${FEATURE_PACKAGE}
            COMMAND
                ${CMAKE_COMMAND} -E remove_directory "${FEATURE_PACKAGE_STAGE}"
            COMMAND
                ${CMAKE_COMMAND} -E copy_directory "${FEATURE_PATH}"
                "${FEATURE_PACKAGE_STAGE}"
            COMMAND
                ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix
                "${FEATURE_PACKAGE_STAGE}" --component FidelityFXRuntime
            COMMAND
                ${CMAKE_COMMAND} -E chdir "${FEATURE_PACKAGE_STAGE}"
                ${CMAKE_COMMAND} -E tar cfv ${FEATURE_PACKAGE}
                --format=7zip -- .
            DEPENDS ${FEATURE_SOURCES} ${FFX_RUNTIME_FILES}
            COMMENT "Creating ${FEATURE} zip package"
        )
    else()
        add_custom_command(
            OUTPUT ${FEATURE_PACKAGE}
            COMMAND
                ${CMAKE_COMMAND} -E tar cfv ${FEATURE_PACKAGE}
                --format=7zip -- .
            WORKING_DIRECTORY "${FEATURE_PATH}"
            DEPENDS ${FEATURE_SOURCES}
            COMMENT "Creating ${FEATURE} zip package"
        )
    endif()

    string(REPLACE " " "" FEATURE ${FEATURE})
    string(REPLACE "-" "" FEATURE ${FEATURE})
    add_custom_target("Package-${FEATURE}" DEPENDS ${FEATURE_PACKAGE})
endforeach()

# AIO Folder target (always refresh)
#
# Keep this as a command-style custom target (no OUTPUT) so invoking target AIO
# always performs a full install step. The install script already clears the
# prefix first, which guarantees a fresh AIO folder each run.
add_custom_target(
    "AIO"
    COMMAND
        ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR}
        --config $<CONFIG>
    DEPENDS ${PROJECT_NAME} ${FFX_RUNTIME_FILES}
    COMMENT "Installing fresh AIO folder"
    VERBATIM
)

# Manual AIO package target
set(AIO_PACKAGE "${DIST_PATH}/${CSX_PACKAGE_NAME}_AIO-${UTC_NOW}.7z")
add_custom_command(
    OUTPUT ${AIO_PACKAGE}
    DEPENDS ${CORE_SOURCES} ${FFX_RUNTIME_FILES}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${AIO_DIR}
    COMMAND
        ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR}
        --config $<CONFIG>
    COMMAND
        ${CMAKE_COMMAND} -E chdir ${AIO_DIR} ${CMAKE_COMMAND} -E tar cfv
        ${AIO_PACKAGE} --format=7zip -- .
    COMMENT "Creating AIO zip package (manual)"
)
add_custom_target("Package-AIO-Manual" DEPENDS ${AIO_PACKAGE})

if(AIO_ZIP_TO_DIST)
    add_custom_target(AIO_ZIP_PACKAGE ALL)
    add_dependencies(AIO_ZIP_PACKAGE Package-AIO-Manual)
endif()

# #######################################################################################################################
# # Shader Unit Tests
# #######################################################################################################################
if(BUILD_SHADER_TESTS)
    message(STATUS "Adding shader tests subdirectory")
    enable_testing() # Enable CTest integration for shader tests
    add_subdirectory(tests/shaders)

    # Add a custom target that runs the shader tests
    # Users can run this manually with: cmake --build <build-dir> --target run_shader_tests
    # Runs the test executable directly (not via CTest) to show discovery count
    add_custom_target(
        run_shader_tests
        COMMAND $<TARGET_FILE:shader_tests> --reporter compact
        DEPENDS shader_tests
        WORKING_DIRECTORY $<TARGET_FILE_DIR:shader_tests>
        COMMENT "Running shader unit tests..."
        VERBATIM
    )

    # Make all package targets depend on shader tests passing
    add_dependencies("Package-Core" run_shader_tests)
    add_dependencies("Package-AIO-Manual" run_shader_tests)
    foreach(FEATURE_PATH ${FEATURE_PATHS})
        get_filename_component(FEATURE ${FEATURE_PATH} NAME)
        string(REPLACE " " "" FEATURE ${FEATURE})
        string(REPLACE "-" "" FEATURE ${FEATURE})
        if(TARGET "Package-${FEATURE}")
            add_dependencies("Package-${FEATURE}" run_shader_tests)
        endif()
    endforeach()

    # Make shader deployment targets depend on tests passing
    if(TARGET prepare_shaders)
        add_dependencies(prepare_shaders run_shader_tests)
    endif()
    if(TARGET DEPLOY_ALL)
        add_dependencies(DEPLOY_ALL run_shader_tests)
    endif()

    message(
        STATUS
        "Package and shader deployment targets will automatically run shader tests before deploying"
    )
endif()

if(BUILD_CONTROLLER_TESTS)
    enable_testing()

    function(add_controller_test TARGET_NAME TEST_NAME SOURCE_FILE)
        add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL ${SOURCE_FILE})
        target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)
        target_include_directories(
            ${TARGET_NAME}
            PRIVATE "${PROJECT_SOURCE_DIR}/src"
        )
        add_test(NAME ${TEST_NAME} COMMAND ${TARGET_NAME})
    endfunction()

    add_controller_test(
        perf_mode_restart_state_test
        PerfModeRestartState
        tests/perf_mode_restart_state_test.cpp
    )
    add_controller_test(
        vr_vendor_relatch_policy_test
        VRVendorRelatchPolicy
        tests/vr_vendor_relatch_policy_test.cpp
    )
    add_controller_test(
        fsr_host_lifecycle_policy_test
        FSRHostLifecyclePolicy
        tests/fsr_host_lifecycle_policy_test.cpp
    )
    add_controller_test(
        overlay_policy_test
        OverlayPolicy
        tests/overlay_policy_test.cpp
    )
    add_controller_test(
        vr_geometry_policy_test
        VRGeometryPolicy
        tests/vr_geometry_policy_test.cpp
    )
    add_controller_test(
        vr_geometry_spaces_test
        VRGeometrySpaces
        tests/vr_geometry_spaces_test.cpp
    )
    add_controller_test(
        vr_pipeline_contract_test
        VRPipelineContract
        tests/vr_pipeline_contract_test.cpp
    )

    add_custom_target(
        controller_tests
        DEPENDS
            perf_mode_restart_state_test
            vr_vendor_relatch_policy_test
            fsr_host_lifecycle_policy_test
            overlay_policy_test
            vr_geometry_policy_test
            vr_geometry_spaces_test
            vr_pipeline_contract_test
    )
endif()

message("*************************************************************")
message("Community Shaders Expanded configuration complete")
message("To prepare a ZIP package of AIO, Core, or Features")
message("  Build cmake targets:")
message("    - Package-Core: Core package")
message("    - Package-AIO-Manual: AIO package (manual)")
message("    - Package-<Feature>: Individual feature packages")
message("  Or use cmake --install for custom deployment:")
message("    cmake --install ./build/ALL --prefix <TARGET_DIR>")
if(BUILD_SHADER_TESTS)
    message("To run shader tests manually:")
    message(
        "  cmake --build build/<preset> --config <Debug|Release> --target run_shader_tests"
    )
endif()
message("Try switching to build preset 'Dev' for faster iteration time")
message("*************************************************************")
