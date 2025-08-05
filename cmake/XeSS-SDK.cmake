# XeSS SDK Configuration
# This file configures the Intel XeSS SDK integration for the project

# XeSS is dynamically loaded at runtime, so we don't need to link against static libraries
# The XeSS DLL (libxess.dll) should be placed in the Data/SKSE/Plugins/XeSS directory

# Create interface library for XeSS headers (if SDK headers are available)
if(EXISTS "${CMAKE_SOURCE_DIR}/extern/XeSS-SDK")
    add_library(xess_interface INTERFACE)
    target_include_directories(xess_interface INTERFACE "${CMAKE_SOURCE_DIR}/extern/XeSS-SDK/inc")
    
    # Link the interface to the main project
    target_link_libraries(
        ${PROJECT_NAME}
        PRIVATE
        xess_interface
    )
    
    message(STATUS "XeSS SDK headers found and configured")
else()
    message(STATUS "XeSS SDK not found - using forward declarations in XeSS.h")
endif()

# Add preprocessor definition to enable XeSS support
target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
    XESS_SUPPORT=1
)