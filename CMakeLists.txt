cmake_minimum_required(VERSION 4.2)
cmake_policy(SET CMP0116 NEW)
set(CMAKE_POLICY_WARNING_CMP0116 OFF)

project(
    # gersemi: ignore
    CommunityShaders
    VERSION 1.5.0
    LANGUAGES CXX
)

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
message("\tAuto plugin deployment: ${AUTO_PLUGIN_DEPLOYMENT}")
message("\tZip to dist: ${ZIP_TO_DIST}")
message("\tAIO Zip to dist: ${AIO_ZIP_TO_DIST}")
message("\tTracy profiler: ${TRACY_SUPPORT}")
message("\tShader tests: ${BUILD_SHADER_TESTS}")

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
find_package(Tracy CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
add_subdirectory(${CMAKE_SOURCE_DIR}/cmake/Streamline)

find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)
include(FidelityFX-SDK)

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
    PRIVATE
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
        Tracy::TracyClient
        Streamline
        d3d12.lib
        Microsoft::DirectX-Headers
        ${DETOURS_LIBRARY}
)

# Some third-party libs (e.g. FFX backend) are built with /GL.
# In RelWithDebInfo, align linker options to avoid LNK4075/LNK1218 when warnings are treated as errors.
if(MSVC)
    target_link_options(
        ${PROJECT_NAME}
        PRIVATE
            "$<$<CONFIG:RelWithDebInfo>:/LTCG>"
            "$<$<CONFIG:RelWithDebInfo>:/INCREMENTAL:NO>"
    )
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
endforeach()

set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${FEATURE_CONFIG_FILES}"
)

string(REPLACE ";" ",\n" FEATURE_VERSIONS "${FEATURE_VERSIONS}")

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
file(GLOB FEATURE_PATHS LIST_DIRECTORIES true ${CMAKE_SOURCE_DIR}/features/*)
string(TIMESTAMP UTC_NOW "%Y-%m-%dT%H-%MZ" UTC)

# Set AIO directory path used by multiple targets below
set(AIO_DIR "${CMAKE_CURRENT_BINARY_DIR}/aio")

# Robocopy wrapper for Windows incremental file copy (used by deployment targets)
if(WIN32)
    set(ROBOCOPY_WRAPPER "${CMAKE_BINARY_DIR}/robocopy_wrapper.cmd")
    file(
        WRITE ${ROBOCOPY_WRAPPER}
        "@echo off\r\nrem Robocopy wrapper: forwards all args to robocopy and normalizes exit codes\r\nrobocopy %*\r\nset rc=%ERRORLEVEL%\r\nif %rc% GEQ 8 exit /b %rc%\r\nexit /b 0\r\n"
    )
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
    file(
        GLOB_RECURSE _AIO_PACKAGE_FILES
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/*"
    )
    list(FILTER _AIO_PACKAGE_FILES EXCLUDE REGEX "/Tests/")
    foreach(_fpath IN LISTS FEATURE_PATHS)
        file(GLOB_RECURSE _tmp LIST_DIRECTORIES FALSE "${_fpath}/*")
        list(APPEND _AIO_PACKAGE_FILES ${_tmp})
    endforeach()

    # Prepare AIO by copying files only when different. This avoids updating
    # timestamps for unchanged files and prevents downstream incremental
    # deploys from copying everything every build.
    set(_prepare_aio_cmds)
    string(REPLACE ";" "\\;" _feature_paths_arg "${FEATURE_PATHS}")

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
        -DFEATURE_PATHS="${_feature_paths_arg}"
        -P
        "${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake"
    )

    # Copy package files (exclude test files from production packages)
    file(
        GLOB_RECURSE _AIO_PACKAGE_SOURCE_FILES
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/*"
    )
    list(FILTER _AIO_PACKAGE_SOURCE_FILES EXCLUDE REGEX "/Tests/")
    append_copy_if_different(_prepare_aio_cmds _AIO_PACKAGE_SOURCE_FILES "${CMAKE_SOURCE_DIR}/package" "${AIO_DIR}")

    # Copy feature folders (only files, preserve existing files in AIO)
    foreach(_fpath IN LISTS FEATURE_PATHS)
        if(EXISTS "${_fpath}")
            file(
                GLOB_RECURSE _feature_files
                LIST_DIRECTORIES FALSE
                "${_fpath}/*"
            )
            append_copy_if_different(_prepare_aio_cmds _feature_files "${_fpath}" "${AIO_DIR}")
        endif()
    endforeach()

    # Remove CORE from AIO if it exists (keep rest intact)
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
            ${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake
    )

    # If AIO was manually (or programmatically) deleted, the stamp is stale —
    # clear it so cmake --build re-runs PREPARE_AIO to recreate the directory.
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

    # Only copy shaders when HLSL files change; copy individually so unchanged
    # files do not get their timestamps updated.
    file(
        GLOB_RECURSE _package_shaders
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/Shaders/*"
    )
    # Exclude test files from production packages
    list(FILTER _package_shaders EXCLUDE REGEX "/Tests/")
    set(_shader_copy_cmds)

    set(_feature_shader_paths)
    foreach(_fpath IN LISTS FEATURE_PATHS)
        if(EXISTS "${_fpath}/Shaders")
            list(APPEND _feature_shader_paths "${_fpath}/Shaders")
        endif()
    endforeach()
    string(
        REPLACE ";"
        "\\;"
        _feature_shader_paths_arg
        "${_feature_shader_paths}"
    )

    list(
        APPEND _shader_copy_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -DMODE=SHADERS
        -DAIO_DIR="${AIO_DIR}"
        -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
        -DFEATURE_SHADER_PATHS="${_feature_shader_paths_arg}"
        -P
        "${CMAKE_SOURCE_DIR}/cmake/CleanupStaleEntries.cmake"
    )

    append_copy_if_different(_shader_copy_cmds _package_shaders "${CMAKE_SOURCE_DIR}/package/Shaders" "${AIO_DIR}/Shaders")

    # feature shader folders
    foreach(_fpath IN LISTS FEATURE_PATHS)
        if(EXISTS "${_fpath}/Shaders")
            file(
                GLOB_RECURSE _feat_shaders
                LIST_DIRECTORIES FALSE
                "${_fpath}/Shaders/*"
            )
            list(FILTER _feat_shaders EXCLUDE REGEX "/Tests/")
            append_copy_if_different(_shader_copy_cmds _feat_shaders "${_fpath}/Shaders" "${AIO_DIR}/Shaders")
        endif()
    endforeach()

    add_custom_command(
        OUTPUT copy_shaders.stamp
        COMMAND
            ${CMAKE_COMMAND} -E make_directory "${AIO_DIR}/Shaders"
            ${_shader_copy_cmds}
        COMMAND ${CMAKE_COMMAND} -E touch copy_shaders.stamp
        DEPENDS
            ${HLSL_FILES}
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
endif()

# Automatic deployment to CommunityShaders output directory.
if(AUTO_PLUGIN_DEPLOYMENT)
    # Detect git HEAD changes (branch switch or new commit) and invalidate stale
    # deploy state. BuildRelease.bat always reconfigures, so this runs on every
    # build invocation. When HEAD changes we:
    #   1. Touch all AIO files so robocopy /XO sees them as newer than any
    #      previously-deployed files (including manual package installs).
    #   2. Delete deploy stamps so CMake re-runs the robocopy commands.
    # Files the user intentionally edited in-game remain protected by /XO on
    # subsequent builds (same HEAD = no touch, same stamp file exists).
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
                "Git HEAD changed (${_prev_git_head} -> ${_git_head}): clearing AIO shaders and deploy stamps for full redeploy"
            )
            # Delete the entire AIO directory so PREPARE_AIO re-copies everything
            # with fresh timestamps. Without this, copy_if_different skips
            # content-identical files (shaders, textures, configs, etc.) and
            # leaves them with old timestamps that deployed files (e.g. from a
            # manual package install) may beat under /XO. The DLL is always
            # rebuilt fresh by the C++ compile step so it doesn't need special
            # handling.
            file(REMOVE_RECURSE "${AIO_DIR}")
            # Also clear the PREPARE_AIO / COPY_SHADERS stamps so cmake --build
            # actually re-runs those targets.
            file(
                GLOB _build_stamps
                "${CMAKE_BINARY_DIR}/*_deploy.stamp"
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
                COMMAND
                    ${ROBOCOPY_WRAPPER} "${AIO_DIR}" "${DEPLOY_TARGET}" "/E"
                    "/XD" "${AIO_DIR}/Shaders" "/COPY:DAT" "/XO" "/R:1" "/W:1"
                    "/NFL" "/NDL" "/NJH" "/NJS"
                COMMAND
                    ${CMAKE_COMMAND} -E touch ${DEPLOY_TARGET_HASH}_deploy.stamp
                DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
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
                DEPENDS
                    ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
                    ${PROJECT_NAME}
                COMMENT "Copy plugin DLL/PDB to ${DEPLOY_TARGET}/SKSE/Plugins"
            )

            list(APPEND DEPLOY_TARGET_HASHES ${DEPLOY_TARGET_HASH}_plugin.stamp)

            # Incremental shader-only copy for fast dev iteration
            # Depends ONLY on copy_shaders.stamp (no DLL, no PREPARE_AIO)
            add_custom_command(
                OUTPUT ${DEPLOY_TARGET_HASH}_shaders_only.stamp
                COMMAND
                    ${CMAKE_COMMAND} -E make_directory
                    "${DEPLOY_TARGET}/Shaders"
                COMMAND
                    ${ROBOCOPY_WRAPPER} "${AIO_DIR}/Shaders"
                    "${DEPLOY_TARGET}/Shaders" "/E" "/COPY:DAT" "/XO" "/R:1"
                    "/W:1" "/NFL" "/NDL" "/NJH" "/NJS"
                COMMAND
                    ${CMAKE_COMMAND} -E touch
                    ${DEPLOY_TARGET_HASH}_shaders_only.stamp
                DEPENDS copy_shaders.stamp
                COMMENT "Fast shader-only deploy to ${DEPLOY_TARGET}/Shaders"
            )

            list(
                APPEND SHADER_ONLY_HASHES
                ${DEPLOY_TARGET_HASH}_shaders_only.stamp
            )

            # Full shader copy for packaging (includes PREPARE_AIO dependency)
            # This ensures DLL is built and all AIO files are ready
            add_custom_command(
                OUTPUT ${DEPLOY_TARGET_HASH}_shaders_full.stamp
                COMMAND
                    ${CMAKE_COMMAND} -E make_directory
                    "${DEPLOY_TARGET}/Shaders"
                COMMAND
                    ${ROBOCOPY_WRAPPER} "${AIO_DIR}/Shaders"
                    "${DEPLOY_TARGET}/Shaders" "/E" "/COPY:DAT" "/XO" "/R:1"
                    "/W:1" "/NFL" "/NDL" "/NJH" "/NJS"
                COMMAND
                    ${CMAKE_COMMAND} -E touch
                    ${DEPLOY_TARGET_HASH}_shaders_full.stamp
                DEPENDS
                    copy_shaders.stamp
                    ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
                COMMENT
                    "Full shader deploy to ${DEPLOY_TARGET}/Shaders (with PREPARE_AIO)"
            )

            list(APPEND DEPLOY_TARGET_HASHES ${DEPLOY_TARGET_HASH}_deploy.stamp)
            list(
                APPEND DEPLOY_TARGET_HASHES
                ${DEPLOY_TARGET_HASH}_shaders_full.stamp
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
    add_custom_target(
        DEPLOY_ALL
        ALL
        DEPENDS copy_shaders.stamp ${DEPLOY_TARGET_HASHES}
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

    set(TARGET_ZIP "${PROJECT_NAME}-${UTC_NOW}.7z")
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
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E tar cf
                ${CMAKE_SOURCE_DIR}/dist/${FEATURE}-${UTC_NOW}.7z --format=7zip
                -- .
            WORKING_DIRECTORY ${FEATURE_PATH}
        )
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

    # Create a stamp-producing custom command for the AIO archive so CMake
    # only rebuilds the archive when its inputs change. The archive filename
    # keeps the UTC timestamp as before, but the command writes a stable
    # stamp file that CMake can track as OUTPUT.
    set(TARGET_AIO_ZIP "${PROJECT_NAME}_AIO-${UTC_NOW}.7z")
    set(AIO_ARCHIVE "${CMAKE_SOURCE_DIR}/dist/${TARGET_AIO_ZIP}")
    set(AIO_ZIP_STAMP "${CMAKE_CURRENT_BINARY_DIR}/aio_package.stamp")

    message("Zipping ${AIO_DIR} to ${AIO_ARCHIVE}")

    add_custom_command(
        OUTPUT ${AIO_ZIP_STAMP}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E tar cf ${AIO_ARCHIVE} --format=7zip -- .
        COMMAND ${CMAKE_COMMAND} -E touch ${AIO_ZIP_STAMP}
        WORKING_DIRECTORY ${AIO_DIR}
        DEPENDS PREPARE_AIO
        COMMENT "Creating AIO archive ${AIO_ARCHIVE}"
    )

    add_custom_target(AIO_ZIP_PACKAGE ALL DEPENDS ${AIO_ZIP_STAMP})
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

set(CORE_PACKAGE "${DIST_PATH}/${PROJECT_NAME}-${UTC_NOW}.7z")

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

    add_custom_command(
        OUTPUT ${FEATURE_PACKAGE}
        COMMAND
            ${CMAKE_COMMAND} -E tar cfv ${FEATURE_PACKAGE} --format=7zip -- .
        WORKING_DIRECTORY "${FEATURE_PATH}"
        DEPENDS ${FEATURE_SOURCES}
        COMMENT "Creating ${FEATURE} zip package"
    )

    string(REPLACE " " "" FEATURE ${FEATURE})
    string(REPLACE "-" "" FEATURE ${FEATURE})
    add_custom_target("Package-${FEATURE}" DEPENDS ${FEATURE_PACKAGE})
endforeach()

# AIO Folder target
add_custom_command(
    OUTPUT ${AIO_DIR}/SKSE/Plugins/${PROJECT_NAME}.dll
    DEPENDS ${CORE_SOURCES}
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR}
    COMMENT "Installing to AIO folder"
)
add_custom_target("AIO" DEPENDS ${AIO_DIR}/SKSE/Plugins/${PROJECT_NAME}.dll)

# Manual AIO package target
set(AIO_PACKAGE "${DIST_PATH}/${PROJECT_NAME}_AIO-${UTC_NOW}.7z")
add_custom_command(
    OUTPUT ${AIO_PACKAGE}
    DEPENDS ${CORE_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${AIO_DIR}
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR}
    COMMAND
        ${CMAKE_COMMAND} -E chdir ${AIO_DIR} ${CMAKE_COMMAND} -E tar cfv
        ${AIO_PACKAGE} --format=7zip -- .
    COMMENT "Creating AIO zip package (manual)"
)
add_custom_target("Package-AIO-Manual" DEPENDS ${AIO_PACKAGE})

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

message("*************************************************************")
message("Community Shaders configuration complete")
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
