cmake_minimum_required(VERSION 3.21)

message(STATUS "Configuring Community Shaders test suite...")

# Find or fetch Catch2
find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found via find_package, attempting vcpkg...")
    find_package(Catch2 3 REQUIRED)
endif()

# Create test executable for Menu tests
add_executable(MenuTests
    main.cpp
    Menu/AdvancedSettingsRendererTests.cpp
    Menu/BackgroundBlurRemovalTests.cpp
)

target_link_libraries(MenuTests PRIVATE
    Catch2::Catch2WithMain
)

target_include_directories(MenuTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# Enable C++23 for tests
target_compile_features(MenuTests PRIVATE cxx_std_23)

# Create test executable for Shader validation tests
add_executable(ShaderValidationTests
    main.cpp
    Shaders/LightingCommonValidation.cpp
    Shaders/LightingEvalValidation.cpp
    Shaders/HairShaderRefactoringTests.cpp
)

target_link_libraries(ShaderValidationTests PRIVATE
    Catch2::Catch2WithMain
)

target_include_directories(ShaderValidationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/package/Shaders
    ${CMAKE_SOURCE_DIR}/features
)

target_compile_features(ShaderValidationTests PRIVATE cxx_std_23)

# Set working directory for tests to repository root
set_target_properties(MenuTests ShaderValidationTests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Enable CTest integration
enable_testing()

# Discover Catch2 tests
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(MenuTests)
catch_discover_tests(ShaderValidationTests)

# Add custom test target for convenience
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --verbose
    DEPENDS MenuTests ShaderValidationTests
    COMMENT "Running all Community Shaders tests..."
)

message(STATUS "Test suite configured successfully")
message(STATUS "  - MenuTests: C++ menu component tests")
message(STATUS "  - ShaderValidationTests: HLSL shader structure validation")
message(STATUS "Run 'cmake --build . --target run_tests' to execute all tests")