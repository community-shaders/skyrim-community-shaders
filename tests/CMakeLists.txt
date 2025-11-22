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

# Add actual source files that we're testing
# These are pure utility functions with minimal external dependencies
set(TESTED_SOURCES
    "${CMAKE_SOURCE_DIR}/src/Utils/Format.cpp"
    "${CMAKE_SOURCE_DIR}/src/Utils/FileSystem.cpp"
    # Note: FileSystem.cpp needs imgui and nlohmann/json mocks (added to test_stubs.h)
    # Note: Game.cpp has heavy Skyrim dependencies (RE::TES, RE::NiTransform, State.h/Tracy)
    # We test its formatting logic separately with full mocks
)

# Create a static library from the utility code we want to test
# This allows it to compile with its own compilation settings
add_library(UtilsForTesting STATIC ${TESTED_SOURCES})

# Set C++ standard to match main project
set_property(TARGET UtilsForTesting PROPERTY CXX_STANDARD 23)
set_property(TARGET UtilsForTesting PROPERTY CXX_STANDARD_REQUIRED ON)

# Configure the utility library with test-compatible settings
# IMPORTANT: Build directory comes FIRST so our mock Globals.h is used instead of src/Globals.h
target_include_directories(UtilsForTesting
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}  # For test stubs and mock Globals.h (MUST BE FIRST!)
        ${CMAKE_SOURCE_DIR}/src
)

# Link against real nlohmann_json for UtilsForTesting
target_link_libraries(UtilsForTesting
    PRIVATE
        nlohmann_json::nlohmann_json
)

target_compile_definitions(UtilsForTesting
    PRIVATE
        UNIT_TEST_BUILD
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

# Provide the standard library headers AND mocks that Format.cpp needs
# The PCH must include test_stubs.h which provides all the Skyrim/DirectX mocks
target_precompile_headers(UtilsForTesting
    PRIVATE
        <algorithm>
        <chrono>
        <cmath>
        <filesystem>
        <iomanip>
        <sstream>
        <string>
        <utility>
        <vector>
        "${CMAKE_CURRENT_BINARY_DIR}/test_stubs.h"
)

# Create test executable
add_executable(${PROJECT_NAME} ${TEST_SOURCES})

# Link tests against the utility library
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        UtilsForTesting
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
)

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

# Note: target_link_libraries moved to after UtilsForTesting library creation

# We compile actual production code into a test library
# Mocks are provided for Skyrim-specific dependencies

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

        # CRITICAL: Apply coverage flags to BOTH the test executable AND the library being tested
        # Otherwise, we only measure test code, not the production code!
        target_compile_options(UtilsForTesting PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
        target_link_options(UtilsForTesting PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )

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

# Mock external dependencies for tests
# Industry best practice: Mock external dependencies at the minimal interface level
# This allows testing of business logic without pulling in heavy dependencies
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_stubs.h" "
// Test stubs and mocks for external dependencies
#pragma once

// Include ALL standard library headers that our code needs
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>  // for std::pair
#include <vector>

// Mock DirectX types (minimal definitions needed for compilation)
struct D3D_SHADER_MACRO {
    const char* Name;
    const char* Definition;
};

// Mock Windows API types (needed by Format.cpp for QueryPerformanceCounter)
#include <Windows.h>  // Provides LARGE_INTEGER and QueryPerformanceCounter

// Mock DirectX device types (opaque pointers are fine for headers)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

// Mock REL::Version from CommonLibSSE
namespace REL {
    struct Version {
        std::string string([[maybe_unused]] const char* separator = \".\") const {
            return \"1.6.640\";
        }
        constexpr Version([[maybe_unused]] std::size_t a_major = 0, [[maybe_unused]] std::size_t a_minor = 0,
                         [[maybe_unused]] std::size_t a_patch = 0, [[maybe_unused]] std::size_t a_build = 0) noexcept {}
    };
}

// Mock logger from CommonLibSSE
namespace logger {
    template<typename... Args> void warn(const char*, Args&&...) {}
    template<typename... Args> void error(const char*, Args&&...) {}
    template<typename... Args> void info(const char*, Args&&...) {}
    template<typename... Args> void debug(const char*, Args&&...) {}
    template<typename... Args> void trace(const char*, Args&&...) {}
}

// Mock globals namespace from Community Shaders
namespace globals {
    namespace d3d {
        inline ID3D11Device* device = nullptr;
        inline ID3D11DeviceContext* context = nullptr;
        inline IDXGISwapChain* swapChain = nullptr;
    }
}

// Mock GetDllVersion from WinApi.h (used by EnumerateDllVersions)
namespace Util {
    inline std::optional<REL::Version> GetDllVersion(const std::wstring&) {
        return REL::Version(1, 0, 0, 0);
    }
}

// ============================================================================
// Mock ImGui (minimal interface that FileSystem.h needs)
// ============================================================================
// ImGui is a UI library - FileSystem.h only needs a few types, not the full API

// Forward declare ImGui types that might be in headers
struct ImGuiContext;
struct ImGuiIO;

// Minimal ImGui namespace mock
namespace ImGui {
    // FileSystem.h doesn't actually call ImGui functions
    // It just includes the header for type definitions
    // So we provide stub declarations only
}

// Note: nlohmann/json mock is provided as a separate header file
// See nlohmann/json.hpp created below
")

# Create a stub Globals.h that Format.cpp includes
# This just includes our test_stubs.h which has all the mocks
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Globals.h" "
// Stub Globals.h for testing
// This is included by src/Utils/Format.cpp
#include \"test_stubs.h\"
")

# Create mock imgui.h header
# FileSystem.h includes imgui.h but doesn't actually use ImGui functions
# Industry practice: Provide minimal mock that compiles but does nothing
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/imgui.h" "
// Mock imgui.h for testing
// FileSystem.h includes this but doesn't use ImGui functions
// We provide minimal stub types that compile
#pragma once

struct ImGuiContext {};
struct ImGuiIO {};  // Empty struct is fine - not actually used
struct ImVec2 { float x, y; };
struct ImVec4 { float x, y, z, w; };

namespace ImGui {
    // Stub functions - not called by FileSystem
    inline ImGuiIO& GetIO() { static ImGuiIO io; return io; }
}
")

# Create wrapper headers for actual source headers
# These ensure test stubs are included first (provides std:: types and mocks)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Utils/Format.h" "
// Wrapper for Format.h that includes stubs first
#pragma once
#include \"test_stubs.h\"  // Provides std types and mocks
#include \"${CMAKE_SOURCE_DIR}/src/Utils/Format.h\"  // Include actual header
")

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}  # For test_stubs.h and Globals.h
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
