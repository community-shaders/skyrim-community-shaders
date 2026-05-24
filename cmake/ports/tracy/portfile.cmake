vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO wolfpld/tracy
    REF 14a1a3227e046e7bee3abed406c7faab61de3620
    SHA512 113b2dd35d75edeb1358514bf857bc694c048fb7b2604a22e8fcc7efa63d342a6c7cbf02162073d2ee8690821e0f80f95be6270416777645d9092d5508e76372
    HEAD_REF master
    PATCHES
        build-tools.patch
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        on-demand TRACY_ON_DEMAND
        fibers	  TRACY_FIBERS
        verbose   TRACY_VERBOSE
    INVERTED_FEATURES
        crash-handler TRACY_NO_CRASH_HANDLER
)

vcpkg_check_features(OUT_FEATURE_OPTIONS TOOLS_OPTIONS
    FEATURES
        cli-tools VCPKG_CLI_TOOLS
        gui-tools VCPKG_GUI_TOOLS
)

if("cli-tools" IN_LIST FEATURES OR "gui-tools" IN_LIST FEATURES)
    vcpkg_find_acquire_program(PKGCONFIG)
    list(APPEND TOOLS_OPTIONS "-DPKG_CONFIG_EXECUTABLE=${PKGCONFIG}")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS
        -DDOWNLOAD_CAPSTONE=OFF
        -DLEGACY=ON
        -DTRACY_ENABLE=ON
        ${FEATURE_OPTIONS}
    OPTIONS_RELEASE
        ${TOOLS_OPTIONS}
    MAYBE_UNUSED_VARIABLES
        DOWNLOAD_CAPSTONE
        LEGACY
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME Tracy CONFIG_PATH lib/cmake/Tracy)

function(tracy_copy_tool tool_name tool_dir)
    vcpkg_copy_tools(
        TOOL_NAMES "${tool_name}"
        SEARCH_DIR "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/${tool_dir}"
    )
endfunction()

if("cli-tools" IN_LIST FEATURES)
    tracy_copy_tool(tracy-capture capture)
    tracy_copy_tool(tracy-csvexport csvexport)
    tracy_copy_tool(tracy-import-chrome import)
    tracy_copy_tool(tracy-import-fuchsia import)
    tracy_copy_tool(tracy-update update)
endif()
if("gui-tools" IN_LIST FEATURES)
    tracy_copy_tool(tracy-profiler profiler)
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
