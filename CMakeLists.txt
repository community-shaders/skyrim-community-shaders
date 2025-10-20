cmake_minimum_required(VERSION 3.21)

project(
    # gersemi: ignore
    CommunityShaders
    VERSION 1.4.6
    LANGUAGES CXX
)

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
message("\tAuto plugin deployment: ${AUTO_PLUGIN_DEPLOYMENT}")
message("\tZip to dist: ${ZIP_TO_DIST}")
message("\tAIO Zip to dist: ${AIO_ZIP_TO_DIST}")
message("\tTracy profiler: ${TRACY_SUPPORT}")

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
include(XeSS-SDK)

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
            REGEX MATCH
            "Version = ([0-9]+)-([0-9]+)-([0-9]+)"
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
                APPEND
                FEATURE_VERSIONS
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

if(AUTO_PLUGIN_DEPLOYMENT OR AIO_ZIP_TO_DIST)
    set(AIO_DIR "${CMAKE_CURRENT_BINARY_DIR}/aio")
    message("Preparing AIO package in ${AIO_DIR}")

    # Prepare AIO only when sources change. Gather package + feature files as
    # inputs so the prepare step runs only when something actually changed.
    file(
        GLOB_RECURSE _AIO_PACKAGE_FILES
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/*"
    )
    foreach(_fpath IN LISTS FEATURE_PATHS)
        file(GLOB_RECURSE _tmp LIST_DIRECTORIES FALSE "${_fpath}/*")
        list(APPEND _AIO_PACKAGE_FILES ${_tmp})
    endforeach()

    # Prepare AIO by copying files only when different. This avoids updating
    # timestamps for unchanged files and prevents downstream incremental
    # deploys from copying everything every build.
    set(_prepare_aio_cmds)

    # Ensure SKSE/Plugins dir exists and copy built plugin files
    list(
        APPEND
        _prepare_aio_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -E
        make_directory
        "${AIO_DIR}/SKSE/Plugins"
    )
    list(
        APPEND
        _prepare_aio_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -E
        copy_if_different
        $<TARGET_FILE:${PROJECT_NAME}>
        "${AIO_DIR}/SKSE/Plugins/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    )
    list(
        APPEND
        _prepare_aio_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -E
        copy_if_different
        $<TARGET_PDB_FILE:${PROJECT_NAME}>
        "${AIO_DIR}/SKSE/Plugins/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
    )

    # Copy package files
    file(
        GLOB_RECURSE _AIO_PACKAGE_SOURCE_FILES
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/*"
    )
    foreach(_src IN LISTS _AIO_PACKAGE_SOURCE_FILES)
        file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}/package" "${_src}")
        set(_dst "${AIO_DIR}/${_rel}")
        get_filename_component(_dst_dir "${_dst}" DIRECTORY)
        list(
            APPEND
            _prepare_aio_cmds
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

    # Copy feature folders (only files, preserve existing files in AIO)
    foreach(_fpath IN LISTS FEATURE_PATHS)
        if(EXISTS "${_fpath}")
            file(
                GLOB_RECURSE _feature_files
                LIST_DIRECTORIES FALSE
                "${_fpath}/*"
            )
            foreach(_src IN LISTS _feature_files)
                file(RELATIVE_PATH _rel "${_fpath}" "${_src}")
                set(_dst "${AIO_DIR}/${_rel}")
                get_filename_component(_dst_dir "${_dst}" DIRECTORY)
                list(
                    APPEND
                    _prepare_aio_cmds
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
        endif()
    endforeach()

    # Remove CORE from AIO if it exists (keep rest intact)
    list(
        APPEND
        _prepare_aio_cmds
        COMMAND
        ${CMAKE_COMMAND}
        -E
        remove
        "${AIO_DIR}/CORE"
    )
    list(
        APPEND
        _prepare_aio_cmds
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
        DEPENDS ${_AIO_PACKAGE_FILES} ${PROJECT_NAME}
    )

    add_custom_target(
        PREPARE_AIO
        ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
    )

    # Only copy shaders when HLSL files change; copy individually so unchanged
    # files do not get their timestamps updated.
    file(
        GLOB_RECURSE _package_shaders
        LIST_DIRECTORIES FALSE
        "${CMAKE_SOURCE_DIR}/package/Shaders/*"
    )
    set(_shader_copy_cmds)
    foreach(_src IN LISTS _package_shaders)
        file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}/package/Shaders" "${_src}")
        set(_dst "${AIO_DIR}/Shaders/${_rel}")
        get_filename_component(_dst_dir "${_dst}" DIRECTORY)
        list(
            APPEND
            _shader_copy_cmds
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
    # feature shader folders
    foreach(_fpath IN LISTS FEATURE_PATHS)
        if(EXISTS "${_fpath}/Shaders")
            file(
                GLOB_RECURSE _feat_shaders
                LIST_DIRECTORIES FALSE
                "${_fpath}/Shaders/*"
            )
            get_filename_component(_feat_name "${_fpath}" NAME)
            foreach(_src IN LISTS _feat_shaders)
                file(RELATIVE_PATH _rel "${_fpath}/Shaders" "${_src}")
                set(_dst "${AIO_DIR}/Shaders/${_feat_name}/${_rel}")
                get_filename_component(_dst_dir "${_dst}" DIRECTORY)
                list(
                    APPEND
                    _shader_copy_cmds
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
        endif()
    endforeach()

    add_custom_command(
        OUTPUT copy_shaders.stamp
        COMMAND
            ${CMAKE_COMMAND} -E make_directory "${AIO_DIR}/Shaders"
            ${_shader_copy_cmds}
        COMMAND ${CMAKE_COMMAND} -E touch copy_shaders.stamp
        DEPENDS ${HLSL_FILES}
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
    set(DEPLOY_TARGET_HASHES)
    if(WIN32)
        # Write a small wrapper once at configure time to normalize robocopy exit codes.
        set(ROBOCOPY_WRAPPER "${CMAKE_BINARY_DIR}/robocopy_wrapper.cmd")
        file(
            WRITE
            ${ROBOCOPY_WRAPPER}
            "@echo off\r\nrem Robocopy wrapper: forwards all args to robocopy and normalizes exit codes\r\nrobocopy %*\r\nset rc=%ERRORLEVEL%\r\nif %rc% GEQ 8 exit /b %rc%\r\nexit /b 0\r\n"
        )

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

            # Incremental shader copy (only changed/new shader files are copied)
            add_custom_command(
                OUTPUT ${DEPLOY_TARGET_HASH}.stamp
                COMMAND
                    ${CMAKE_COMMAND} -E make_directory
                    "${DEPLOY_TARGET}/Shaders"
                COMMAND
                    ${ROBOCOPY_WRAPPER} "${AIO_DIR}/Shaders"
                    "${DEPLOY_TARGET}/Shaders" "/E" "/COPY:DAT" "/XO" "/R:1"
                    "/W:1" "/NFL" "/NDL" "/NJH" "/NJS"
                COMMAND ${CMAKE_COMMAND} -E touch ${DEPLOY_TARGET_HASH}.stamp
                DEPENDS
                    copy_shaders.stamp
                    ${CMAKE_CURRENT_BINARY_DIR}/prepare_aio.stamp
                COMMENT
                    "Incremental shader copy to ${DEPLOY_TARGET}/Shaders (robocopy-wrapper)"
            )

            list(APPEND DEPLOY_TARGET_HASHES ${DEPLOY_TARGET_HASH}_deploy.stamp)
            list(APPEND DEPLOY_TARGET_HASHES ${DEPLOY_TARGET_HASH}.stamp)
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

    add_custom_target(
        COPY_SHADERS
        ALL
        DEPENDS copy_shaders.stamp ${DEPLOY_TARGET_HASHES}
        COMMENT "Prepare and copy AIO/shaders to CommunityShadersOutputDir"
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

    set(TARGET_AIO_ZIP "${PROJECT_NAME}_AIO-${UTC_NOW}.7z")
    message("Zipping ${AIO_DIR} to ${CMAKE_SOURCE_DIR}/dist/${TARGET_AIO_ZIP}")
    add_custom_target(
        AIO_ZIP_PACKAGE
        ALL
        COMMAND
            ${CMAKE_COMMAND} -E tar cf
            ${CMAKE_SOURCE_DIR}/dist/${TARGET_AIO_ZIP} --format=7zip -- .
        WORKING_DIRECTORY ${AIO_DIR}
        DEPENDS PREPARE_AIO
    )
endif()
