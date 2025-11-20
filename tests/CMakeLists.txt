# Community Shaders Unit Tests
cmake_minimum_required(VERSION 3.21)

project(CommunityShaders_Tests LANGUAGES CXX)

# Find Catch2
find_package(Catch2 3 CONFIG REQUIRED)

# Collect all test source files
file(GLOB_RECURSE TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Create test executable
add_executable(${PROJECT_NAME} ${TEST_SOURCES})

# Set C++ standard to match main project
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)

# Include directories - we need access to the main source files
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}
        ${CLIB_UTIL_INCLUDE_DIRS}
)

# Link against Catch2 and necessary dependencies
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
        # Note: We avoid linking the full plugin to prevent Skyrim dependencies
        # Instead, we compile only the utility files we need for testing
)

# We don't compile any source files - tests are self-contained
# This avoids Skyrim dependencies entirely

# Add preprocessor definitions
target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        UNIT_TEST_BUILD  # Signal to source files that this is a test build
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WINDOWS
)

# Code Coverage Support (Clang only)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(STATUS "Enabling Clang source-based code coverage")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
        target_link_options(${PROJECT_NAME} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
    else()
        message(WARNING "Coverage is only supported with Clang compiler. Current compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

# Mock REL::Version for tests - add a simple stub header
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_stubs.h" "
// Test stubs for Skyrim-specific types
#include <string>
#include <vector>
#include <filesystem>
#include <d3dcommon.h>

namespace REL {
    struct Version {
        std::string string(const char*) const { return \"1.0.0\"; }
    };
}

namespace logger {
    template<typename... Args> void warn(const char*, Args&&...) {}
    template<typename... Args> void error(const char*, Args&&...) {}
    template<typename... Args> void info(const char*, Args&&...) {}
}

namespace globals {
    namespace d3d {
        inline void* device = nullptr;
        inline void* context = nullptr;
    }
}
")

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}  # For test_stubs.h
)

# Enable Catch2 test discovery
include(CTest)
include(Catch)
catch_discover_tests(${PROJECT_NAME})

# Add custom test target that can be run with "cmake --build . --target run_tests"
add_custom_target(run_tests
    COMMAND ${PROJECT_NAME}
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests..."
)

message(STATUS "Unit tests configured. Run with: cmake --build . --target test")
