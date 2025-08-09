# XeSS SDK Configuration
# This file configures the Intel XeSS SDK integration for the project

# XeSS is dynamically loaded at runtime, so we don't need to link against static libraries
# The XeSS DLL (libxess.dll) should be placed in the Data/SKSE/Plugins/XeSS directory

# Find XeSS headers installed by vcpkg port
find_path(INTEL_XESS_INCLUDE_DIRS "xess/xess.h")

if(INTEL_XESS_INCLUDE_DIRS)
    message(STATUS "XeSS SDK headers found via vcpkg at ${INTEL_XESS_INCLUDE_DIRS}")
    target_include_directories(
        ${PROJECT_NAME}
        PRIVATE
        ${INTEL_XESS_INCLUDE_DIRS}
    )
else()
    message(WARNING "XeSS SDK headers not found - XeSS compilation may fail")
    message(STATUS "Make sure intel-xess is installed via vcpkg")
endif()

# Link required D3D12 libraries for interop
target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    d3d12.lib
    dxgi.lib
)

# Add preprocessor definition to enable XeSS support
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
    XESS_SUPPORT=1
)