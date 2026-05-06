# Shader Tests using ShaderTestFramework
cmake_minimum_required(VERSION 4.2)

# Option to enable/disable shader tests
option(BUILD_SHADER_TESTS "Build shader unit tests (runs automatically before packaging)" ON)

if(NOT BUILD_SHADER_TESTS)
    message(STATUS "Shader tests disabled")
    return()
endif()

message(STATUS "Configuring shader unit tests...")

# Fetch ShaderTestFramework (which will fetch Catch2)
include(FetchContent)

FetchContent_Declare(
    ShaderTestFramework
    GIT_REPOSITORY https://github.com/KStocky/ShaderTestFramework.git
    # Pinned to specific commit for build reproducibility (Dec 14, 2025)
    GIT_TAG dcbe56c46d474948c27134f570c50ff7110648f6
    GIT_CONFIG core.longpaths=true  # Enable long path support for Windows
)

FetchContent_MakeAvailable(ShaderTestFramework)

# Get the ShaderTestFramework source directory to access its CMake utilities
FetchContent_GetProperties(ShaderTestFramework SOURCE_DIR STF_SOURCE_DIR)

# Add STF's cmake library and our project's cmake to CMAKE_MODULE_PATH
list(APPEND CMAKE_MODULE_PATH ${STF_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Include STF's helper modules
include(AssetDependencyManagement)
include(STF)

# Check for Windows Graphics Tools (required for D3D12 shader tests)
include(DetectGraphicsTools)

# Set shader source paths
set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/package/Shaders)
set(SHADER_SOURCE_REL_DIR "Shaders")

# Set feature shader source paths (for Hair Specular, etc.)
set(FEATURES_DIR ${CMAKE_SOURCE_DIR}/features)

# ============================================================================
# RUNTIME DISCOVERY: No code generation needed!
# ============================================================================
# Tests are discovered by scanning HLSL files at runtime.
# No build-time generation, no external tools, just pure C++ reflection.

# Create shader test executable
add_executable(shader_tests
    minimal_test.cpp                   # Provides main()
    runtime_discovered_tests.cpp       # Single test that discovers & runs all HLSL tests

    # Headers (listed for IDE integration and dependency tracking)
    test_common.h                      # Common test utilities
    test_helpers_unified.h             # Unified test helper macros
    runtime_test_discovery.h           # Runtime HLSL test discovery
)

# Set C++23 standard (required by ShaderTestFramework)
set_property(TARGET shader_tests PROPERTY CXX_STANDARD 23)
set_property(TARGET shader_tests PROPERTY CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Suppress C4864 in tuplet (via ShaderTestFramework) for VS 2026 migration
    if(TARGET ShaderTestFramework)
        target_compile_options(ShaderTestFramework PUBLIC /wd4864)
    endif()
endif()

# Set VS debugger working directory
set_target_properties(shader_tests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:shader_tests>"
)

# Initialize asset dependency management for shader files
asset_dependency_init(shader_tests)

# Add the shader source directory to be copied relative to the exe
target_add_asset_directory(shader_tests ${SHADER_SOURCE_DIR} "/${SHADER_SOURCE_REL_DIR}")

# Add feature shader directories (Hair Specular, etc.) to virtual shader paths
# These are mapped so that #include "/Shaders/Hair/Hair.hlsli" works in tests
file(GLOB FEATURE_SHADER_DIRS
    LIST_DIRECTORIES true
    "${FEATURES_DIR}/*/Shaders"
)
foreach(FEATURE_SHADER_DIR ${FEATURE_SHADER_DIRS})
    if(IS_DIRECTORY ${FEATURE_SHADER_DIR})
        target_add_asset_directory(shader_tests ${FEATURE_SHADER_DIR} "/${SHADER_SOURCE_REL_DIR}")
    endif()
endforeach()

# Optional: Make shader tests depend on HLSL test files for automatic rebuild triggers
# This ensures that changing HLSL test files triggers a relink (to update copied assets)
file(GLOB_RECURSE HLSL_TEST_FILES "${SHADER_SOURCE_DIR}/Tests/Test*.hlsl")
list(LENGTH HLSL_TEST_FILES HLSL_TEST_COUNT)
target_sources(shader_tests PRIVATE ${HLSL_TEST_FILES})
set_source_files_properties(${HLSL_TEST_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

message(STATUS "Found ${HLSL_TEST_COUNT} HLSL test files:")
foreach(TEST_FILE ${HLSL_TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME)
    message(STATUS "  - ${TEST_NAME}")
endforeach()

# Link ShaderTestFramework BEFORE copying assets (so dependencies are resolved)
target_link_libraries(shader_tests PRIVATE ShaderTestFramework)

# Copy all dependent assets (DLLs, shaders, etc.) - must be AFTER target_link_libraries
copy_all_dependent_assets(shader_tests)

# Add Catch2 for testing (but not Catch2WithMain - we provide our own main() due to CMake 4.0 linking issues)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.11.0
)
FetchContent_MakeAvailable(Catch2)
target_link_libraries(shader_tests PRIVATE Catch2::Catch2)

# Workaround: MSVC 14.50+ (VS 2026) has an Internal Compiler Error (ICE C1001)
# in c2.dll when compiling Catch2 v3.11.0 with default optimizations.
# Disabling optimizations for Catch2 avoids the ICE; Catch2 is a test harness
# so the performance impact does not matter.
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1950)
    if(TARGET Catch2)
        target_compile_options(Catch2 PRIVATE /Od)
    endif()
    if(TARGET Catch2WithMain)
        target_compile_options(Catch2WithMain PRIVATE /Od)
    endif()
endif()

# Define USE_PIX for PIX capture support
target_compile_definitions(shader_tests PRIVATE USE_PIX)

# ============================================================================
# CTest Integration
# ============================================================================
# Register with CTest so CI can discover and run tests
enable_testing()
add_test(
    NAME ShaderTests
    COMMAND shader_tests --reporter compact
    WORKING_DIRECTORY $<TARGET_FILE_DIR:shader_tests>
)

# Set test properties for better CI output
set_tests_properties(ShaderTests PROPERTIES
    TIMEOUT 300  # 5 minute timeout for all shader tests
    LABELS "Shaders;UnitTests"
)

message(STATUS "Shader tests configured successfully")
message(STATUS "  Tests will run automatically before packaging and deployment")
