# XeSS SDK Configuration
# This file configures the Intel XeSS SDK integration for the project

# XeSS is dynamically loaded at runtime, so we don't need to link against static libraries
# The XeSS DLL (libxess.dll) should be placed in the Data/SKSE/Plugins/XeSS directory

# XeSS headers are available in the include/xess directory
if(EXISTS "${CMAKE_SOURCE_DIR}/include/xess")
    message(STATUS "XeSS SDK headers found in include/xess")
    # Headers are already available through the main include path
else()
    message(WARNING "XeSS SDK headers not found in include/xess - XeSS compilation may fail")
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