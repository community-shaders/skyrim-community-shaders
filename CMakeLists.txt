cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0116 NEW)
set(CMAKE_POLICY_WARNING_CMP0116 OFF)

project(
	CommunityShaders
	VERSION 1.3.4
	LANGUAGES CXX
)

# default install path
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE "${CMAKE_CURRENT_BINARY_DIR}/aio")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ########################################################################################################################
# ## Build options
# ########################################################################################################################
message("Options:")
option(AUTO_PLUGIN_DEPLOYMENT "Copy the build output and addons to env:CommunityShadersOutputDir." OFF)
option(TRACY_SUPPORT "Enable support for tracy profiler" OFF)
message("\tAuto plugin deployment: ${AUTO_PLUGIN_DEPLOYMENT}")
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
add_subdirectory(${CMAKE_SOURCE_DIR}/cmake/Streamline)
include(FidelityFX-SDK)

target_compile_definitions(
	${PROJECT_NAME}
	PRIVATE
	"$<$<BOOL:${TRACY_SUPPORT}>:TRACY_SUPPORT>"
)

file(GLOB FEATURE_SHADER_DIRS
    RELATIVE "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/features/*/Shaders"
)

foreach(_dir IN LISTS FEATURE_SHADER_DIRS)
    target_include_directories(
        ${PROJECT_NAME}
        PRIVATE
          "${CMAKE_SOURCE_DIR}/${_dir}"
    )
endforeach()

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
	${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}
	${CLIB_UTIL_INCLUDE_DIRS}
	"${CMAKE_SOURCE_DIR}/package/Shaders"
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	debug ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Debug/detours.lib
	optimized ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Release/detours.lib
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
	file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/Directory.Build.props" CONTENT [==[
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <BuildStlModules>false</BuildStlModules>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
]==] @ONLY)
endif()

# #######################################################################################################################
# # Feature version detection
# #######################################################################################################################
file(GLOB_RECURSE FEATURE_CONFIG_FILES
	LIST_DIRECTORIES false
	CONFIGURE_DEPENDS
	"features/*/Shaders/Features/*.ini"
)

foreach(FEATURE_PATH ${FEATURE_CONFIG_FILES})
	get_filename_component(FEATURE ${FEATURE_PATH} NAME_WE)
	file(READ "${FEATURE_PATH}" CONFIG_VALUE)
	string(STRIP "${CONFIG_VALUE}" CONFIG_VALUE)
	if(CONFIG_VALUE)
		string(REGEX MATCH "Version = ([0-9]+)-([0-9]+)-([0-9]+)" _ "${CONFIG_VALUE}")
		if(DEFINED CMAKE_MATCH_1 AND DEFINED CMAKE_MATCH_2 AND DEFINED CMAKE_MATCH_3)
			set(ver_major ${CMAKE_MATCH_1})
			set(ver_minor ${CMAKE_MATCH_2})
			set(ver_patch ${CMAKE_MATCH_3})
			list(APPEND FEATURE_VERSIONS "\t\t{\"${FEATURE}\"sv, {${ver_major},${ver_minor},${ver_patch}}}")
		else()
			message(WARNING "Feature config file '${FEATURE_PATH}' does not contain a valid version string. Skipping.")
		endif()
	else()
		message(WARNING "Feature config file '${FEATURE_PATH}' is empty or contains only whitespace. Skipping version detection for this feature.")
	endif()
endforeach()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FEATURE_CONFIG_FILES}")

string(REPLACE ";" ",\n" FEATURE_VERSIONS "${FEATURE_VERSIONS}")

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/FeatureVersions.h.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/FeatureVersions.h
	@ONLY
)

target_sources(
	"${PROJECT_NAME}"
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake/FeatureVersions.h
)

# #######################################################################################################################
# # clang-format
# #######################################################################################################################

find_program(CLANG_FORMAT_PATH clang-format)
if(CLANG_FORMAT_PATH)
	add_custom_target(FORMAT_CODE
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
	add_custom_target(generate_shader_configs
		COMMAND ${POWERSHELL_PATH} -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/.github/configs/generate-shader-configs.ps1" -OutputDir "${CMAKE_SOURCE_DIR}/.github/configs"
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		COMMENT "Generating shader validation configuration files from Skyrim log files"
	)
endif()

# #######################################################################################################################
# # Automatic deployment
# #######################################################################################################################
file(GLOB FEATURE_PATHS LIST_DIRECTORIES true ${CMAKE_SOURCE_DIR}/features/*)
string(TIMESTAMP UTC_NOW "%Y-%m-%dT%H-%MZ" UTC)

# Append a '/' to the end of each feature path for installation all its contents but not itself
set(FEATURE_PATHS_SLASH ${FEATURE_PATHS})
list(TRANSFORM FEATURE_PATHS_SLASH APPEND /)

# Install logic for AIO package
# To copy AIO package at a folder do `${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR}`
install(CODE "file(REMOVE_RECURSE \${CMAKE_INSTALL_PREFIX})")
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION SKSE/Plugins COMPONENT SKSE)
install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION SKSE/Plugins COMPONENT SKSE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/package/ ${FEATURE_PATHS_SLASH} DESTINATION . COMPONENT Shaders)
install(CODE "file(REMOVE \${CMAKE_INSTALL_PREFIX}/Core)" COMPONENT Shaders)

# Build and install logic complete here
# One can just build the project and install an AIO folder into anywhere with:
#   `cmake --install ./build/ALL --prefix <AIO_FOLDER>`
# Scripts below defines utility targets for:
# - AIO: install to ./build/ALL/aio
# - Package-AIO: AIO zip package
# - Package-Core: CS-Core zip package
# - Package-<Feature>: Feature zip packages
# - prepare_shaders: Copying shaders only without build the SKSE plugin
# - (AUTO_PLUGIN_DEPLOYMENT): Run cmake install on CommunityShaders.dll POST_BUILD

set(AIO_DIR "${CMAKE_CURRENT_BINARY_DIR}/aio")

add_custom_command(
	OUTPUT copy_shaders.stamp
	COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR} --component Shaders
	COMMAND ${CMAKE_COMMAND} -E touch copy_shaders.stamp
	DEPENDS ${HLSL_FILES}
	COMMENT "Copy shaders"
)

# Standalone target for preparing shaders for CI validation
# This allows shader validation to run without waiting for the full build
add_custom_target(prepare_shaders
	DEPENDS copy_shaders.stamp
	COMMENT "Preparing shaders for validation"
)

# Automatic deployment to CommunityShaders output directory.
if(AUTO_PLUGIN_DEPLOYMENT)
	foreach(DEPLOY_TARGET $ENV{CommunityShadersOutputDir})
		message("Installing to ${DEPLOY_TARGET}")
		add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${DEPLOY_TARGET}
		COMMENT "Installing to ${DEPLOY_TARGET}"
		)
	endforeach()
	if(NOT DEFINED ENV{CommunityShadersOutputDir})
		message("When using AUTO_PLUGIN_DEPLOYMENT option, you need to set environment variable 'CommunityShadersOutputDir'")
	endif()
endif()

set(DIST_PATH "${CMAKE_SOURCE_DIR}/dist")
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

set(CORE_PACKAGE "${DIST_PATH}/${PROJECT_NAME}-${UTC_NOW}.7z")

# CORE_SOURCES = all content copied to the AIO directory + the SKSE plugin dll
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/package/*")
# Add SKSE plugin dll as dependency
list(APPEND CORE_SOURCES ${PROJECT_NAME})

set(CORE_FEATURE_PATHS "${CMAKE_SOURCE_DIR}/package/")

foreach(FEATURE_PATH ${FEATURE_PATHS})
	if (EXISTS "${FEATURE_PATH}/CORE")
		list(APPEND CORE_FEATURE_PATHS "\"${FEATURE_PATH}/\"")
		file(GLOB_RECURSE FEATURE_SOURCES CONFIGURE_DEPENDS "${FEATURE_PATH}/*")
		list(APPEND CORE_SOURCES ${FEATURE_SOURCES})
	endif()
endforeach()

# Core package
set(FEATURE_PATH "${CMAKE_BINARY_DIR}/Core")
file(MAKE_DIRECTORY ${FEATURE_PATH})
add_custom_command(OUTPUT ${CORE_PACKAGE}
	DEPENDS ${CORE_SOURCES}
	COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${FEATURE_PATH} --component SKSE
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CORE_FEATURE_PATHS} ${FEATURE_PATH}
	COMMAND ${CMAKE_COMMAND} -E rm -f -- ${FEATURE_PATH}/Core
	COMMAND ${CMAKE_COMMAND} -E tar cfv ${CORE_PACKAGE} --format=7zip -- .
	WORKING_DIRECTORY ${FEATURE_PATH}
	COMMENT "Creating Core zip package"
)
add_custom_target("Package-Core" DEPENDS ${CORE_PACKAGE})

# Feature packages
foreach(FEATURE_PATH ${FEATURE_PATHS})
	if (EXISTS "${FEATURE_PATH}/CORE")
		continue()
	endif()

	list(APPEND CORE_FEATURE_PATHS "\"${FEATURE_PATH}/\"")
	file(GLOB_RECURSE FEATURE_SOURCES CONFIGURE_DEPENDS "${FEATURE_PATH}/*")
	list(APPEND CORE_SOURCES ${FEATURE_SOURCES})

	get_filename_component(FEATURE ${FEATURE_PATH} NAME)
	set(FEATURE_PACKAGE "${DIST_PATH}/${FEATURE}-${UTC_NOW}.7z")

	add_custom_command(OUTPUT ${FEATURE_PACKAGE}
		COMMAND ${CMAKE_COMMAND} -E tar cfv ${FEATURE_PACKAGE} --format=7zip -- .
		WORKING_DIRECTORY "${FEATURE_PATH}"
		DEPENDS ${FEATURE_SOURCES}
		COMMENT "Creating ${FEATURE} zip package"
	)

	string(REPLACE " " "" FEATURE ${FEATURE})
	string(REPLACE "-" "" FEATURE ${FEATURE})
	add_custom_target("Package-${FEATURE}" DEPENDS ${FEATURE_PACKAGE})
endforeach()

# AIO Folder
add_custom_command(OUTPUT ${AIO_DIR}/SKSE/Plugins/${PROJECT_NAME}.dll
	DEPENDS ${CORE_SOURCES}
	COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR}
	COMMENT "Installing to AIO folder"
)
add_custom_target("AIO" DEPENDS ${AIO_DIR}/SKSE/Plugins/${PROJECT_NAME}.dll)

# AIO package
set(AIO_PACKAGE "${DIST_PATH}/${PROJECT_NAME}_AIO-${UTC_NOW}.7z")
add_custom_command(OUTPUT ${AIO_PACKAGE}
	DEPENDS ${CORE_SOURCES}
	COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${AIO_DIR}
	COMMAND ${CMAKE_COMMAND} -E tar cfv ${AIO_PACKAGE} --format=7zip -- .
	WORKING_DIRECTORY ${AIO_DIR}
	COMMENT "Creating AIO zip package"
)
add_custom_target("Package-AIO" DEPENDS ${AIO_PACKAGE})

