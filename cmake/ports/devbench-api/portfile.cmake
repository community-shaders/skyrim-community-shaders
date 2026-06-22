# devbench-api — header-only cross-plugin API for SKSE consumers.
# Installs the MIT API header + its companion .cpp (compiled into the consumer via the
# config's INTERFACE_SOURCES).
#
# Pinned to a published commit. devbench-api isn't in the official vcpkg registry, so
# consumers add this directory to VCPKG_OVERLAY_PORTS (see README). To ship a newer API
# revision, bump REF to the new commit and SHA512 to its archive hash.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/devbench
    REF c75e7d2ea51f1de5c7469124955bff8d39694d78
    SHA512 1dfcdb838a67b181ab21dd7cbcc818459563364a519e9ab6b3bce0a2c1bd29b46ee160f10dbfc8ff488fb5ec74c22723defd692d620f0df0fe788b1244bfb7a4
    HEAD_REF main
)

# MIT API glue → header to include/, source to share/ (referenced by the config target).
file(INSTALL "${SOURCE_PATH}/include/DevBenchAPI.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/include/DevBenchAPI.cpp"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/src")

# CMake package config — defines DevBench::API.
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/devbench-api-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

# The API glue is MIT (not the GPL-3.0 plugin) — ship that as the port copyright.
file(INSTALL "${SOURCE_PATH}/include/DevBenchAPI.LICENSE.txt"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
