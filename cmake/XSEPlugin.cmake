add_compile_definitions(SKYRIM)
set(CommonLibPath "extern/CommonLibSSE-NG")
set(CommonLibName "CommonLibSSE")

add_library("${PROJECT_NAME}" SHARED)

target_compile_features(
	"${PROJECT_NAME}"
	PRIVATE
	cxx_std_23
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(AddCXXFiles)
add_cxx_files("${PROJECT_NAME}")

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
	@ONLY
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc
	@ONLY
)

target_sources(
	"${PROJECT_NAME}"
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
	${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc
)

target_precompile_headers(
	"${PROJECT_NAME}"
	PRIVATE
	include/PCH.h
)

# LTO defaults ON; presets can override via cache variable to skip the
# expensive LTCG link pass during development iteration.
if(NOT DEFINED CACHE{CMAKE_INTERPROCEDURAL_OPTIMIZATION})
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(COMMONLIB_PREBUILT ON CACHE BOOL "Use prebuilt CommonLibSSE" FORCE)
set(COMMONLIB_PREBUILT_MULTICONFIG ON CACHE BOOL "Use prebuilt CommonLibSSE in multi-config generators" FORCE)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

set(BUILD_TESTS OFF)

# Define _WINDOWS for all Windows builds (required by FidelityFX API loader)
if(WIN32)
	add_compile_definitions(_WINDOWS)
endif()

if(MSVC)
	add_compile_definitions(
		$<$<CONFIG:DEBUG>:_ITERATOR_DEBUG_LEVEL=0>
		$<$<CONFIG:DEBUG>:_HAS_ITERATOR_DEBUGGING=0>
	)
endif()

# Build flavors (Release config), selected by presets:
#   shipping (ALL):      IPO=ON (default) -> /O2 /Ob3 /Zi /GL + /LTCG, full PDB
#   PR / CI (PR preset): IPO=OFF, SC_COMPILE_PDB=OFF -> /O2 without LTO,
#                        public-symbols PDB only (~2x faster than LTCG)
#   dev (Dev-Fast):      IPO=OFF, SC_DEVFAST_OPTS=ON -> /Od /Z7 + incremental
#                        link, full PDB; must never ship
option(SC_DEVFAST_OPTS "Use minimal optimization (/Od) and incremental linking for fast dev iteration" OFF)
option(SC_COMPILE_PDB "Generate compile-time debug info (full PDB). OFF leaves only linker public symbols" ON)

if(MSVC)
	# The VS generator implies UNICODE via the project CharacterSet; Ninja
	# does not, and TCHAR APIs would resolve to their ANSI variants.
	add_compile_definitions(_UNICODE UNICODE)

	target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:DEBUG>")

	set(SC_DEBUG_OPTS "/fp:strict;/ZI;/Od;/Gy")

	# Dev path uses /Od (the recompiled TU's optimizer time dominates the edit
	# loop) and /Gy to pair with the incremental linker below.
	# /Ob3 comes from the preset's CMAKE_CXX_FLAGS_RELEASE (single source,
	# applies to externs too); repeating an /Ob here would emit D9025.
	if(SC_DEVFAST_OPTS)
		set(SC_RELEASE_OPTS "/fp:fast;/Gy;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/Od;/Ob1;/fp:except-")
	else()
		set(SC_RELEASE_OPTS "/fp:fast;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Oi;/Ot;/Oy;/fp:except-")
	endif()

	# Shipping: /Zi + /GL. Dev: /Z7 (no mspdbsrv PDB-lock contention across
	# parallel compiles). PR/CI: no compile-time debug info; the linker's
	# /DEBUG below still emits a public-symbols-only PDB.
	if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
		string(PREPEND SC_RELEASE_OPTS "/Zi;")
		string(APPEND SC_RELEASE_OPTS ";/GL")
	elseif(SC_COMPILE_PDB)
		string(PREPEND SC_RELEASE_OPTS "/Z7;")
	endif()

	target_compile_options(
		"${PROJECT_NAME}"
		PRIVATE
		/W4
		/WX
		/permissive-
		/Zc:alignedNew
		/Zc:auto
		/Zc:__cplusplus
		/Zc:externC
		/Zc:externConstexpr
		/Zc:forScope
		/Zc:hiddenFriend
		/Zc:implicitNoexcept
		/Zc:lambda
		/Zc:noexceptTypes
		/Zc:preprocessor
		/Zc:referenceBinding
		/Zc:rvalueCast
		/Zc:sizedDealloc
		/Zc:strictStrings
		/Zc:ternary
		/Zc:threadSafeInit
		/Zc:trigraphs
		/Zc:wchar_t
		/wd4200 # nonstandard extension used : zero-sized array in struct/union
	)

	# /MP (multi-process compilation) only for MSBuild; Ninja handles parallelism itself
	if(CMAKE_GENERATOR MATCHES "Visual Studio")
		target_compile_options("${PROJECT_NAME}" PRIVATE /MP)
	endif()

	target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:${SC_DEBUG_OPTS}>")
	target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:RELEASE>:${SC_RELEASE_OPTS}>")

	if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
		target_link_options(
			${PROJECT_NAME}
			PRIVATE
			/WX
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
		)
	elseif(SC_DEVFAST_OPTS)
		# /OPT:REF and /OPT:ICF are mutually exclusive with /INCREMENTAL
		# (LNK4075 + full-link fallback), so the dev path disables them.
		# /DEBUG:FULL keeps the PDB self-contained; /DEBUG:FASTLINK no longer
		# exists in VS2026 (LNK4315, fatal under /WX).
		target_link_options(
			${PROJECT_NAME}
			PRIVATE
			/WX
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF;/DEBUG:FULL>"
		)
	else()
		# PR / CI path: one-shot link, shipping-sized DLL. /DEBUG is still
		# required: the $<TARGET_PDB_FILE> packaging rules need a PDB to exist.
		target_link_options(
			${PROJECT_NAME}
			PRIVATE
			/WX
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
		)
	endif()
endif()

add_subdirectory(${CommonLibPath} ${CommonLibName} EXCLUDE_FROM_ALL)

# Map Debug to Release imported location for the prebuilt CommonLibSSE target.
# This ensures that Debug builds of the plugin link against the prebuilt Release
# CommonLibSSE.lib instead of compiling it from source.
if(TARGET CommonLibSSE)
	get_target_property(_imported CommonLibSSE IMPORTED)
	if(_imported)
		get_target_property(_loc_release CommonLibSSE IMPORTED_LOCATION_RELEASE)
		set_target_properties(
			CommonLibSSE PROPERTIES
			IMPORTED_LOCATION_DEBUG "${_loc_release}"
		)
	endif()
endif()

# CommonLibSSE-NG forces IPO ON internally, so its lib carries /GL objects.
# A single /GL object drags LTCG into the plugin link, which is incompatible
# with the incremental linker (LNK4075, fatal under /WX) — so on the no-LTO
# paths force CommonLib's IPO off too. Shipping leaves it untouched.
if(MSVC AND NOT CMAKE_INTERPROCEDURAL_OPTIMIZATION)
	set_target_properties(
		${CommonLibName}
		PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION OFF
		INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
	)
endif()

set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
find_package(spdlog CONFIG REQUIRED)

target_include_directories(
	${PROJECT_NAME}
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PUBLIC
	CommonLibSSE::CommonLibSSE
)