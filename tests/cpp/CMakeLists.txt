# C++ unit tests for plugin utility code.
#
# Catch2-based. Disjoint from tests/shaders (which tests HLSL via
# ShaderTestFramework). Add test sources here for any plugin code that
# doesn't require a live DirectX device or ImGui context.

cmake_minimum_required(VERSION 4.2)

option(BUILD_CPP_TESTS "Build C++ unit tests for plugin utilities" ON)

if(NOT BUILD_CPP_TESTS)
    message(STATUS "C++ unit tests disabled")
    return()
endif()

message(STATUS "Configuring C++ unit tests...")

include(FetchContent)

# Reused by tests/shaders if that target is also enabled. FetchContent dedupes
# by name, so the same Catch2 source tree serves both.
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.11.0
)
FetchContent_MakeAvailable(Catch2)

# MSVC 14.50+ (VS 2026) ICE workaround copied from tests/shaders.
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1950)
    if(TARGET Catch2)
        target_compile_options(Catch2 PRIVATE /Od)
    endif()
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(cpp_tests
    test_main.cpp
    test_subrect.cpp
    # Compile the unit-under-test directly into the test binary so we don't
    # depend on the plugin DLL build (which pulls in FFX/Streamline/etc.).
    "${CMAKE_SOURCE_DIR}/src/Utils/Subrect.cpp"
)

set_property(TARGET cpp_tests PROPERTY CXX_STANDARD 23)
set_property(TARGET cpp_tests PROPERTY CXX_STANDARD_REQUIRED ON)

target_include_directories(cpp_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(cpp_tests PRIVATE
    Catch2::Catch2
    nlohmann_json::nlohmann_json
    imgui::imgui
)

# windows.h (pulled in via d3d11.h) defines min/max macros that break the
# std::min<T>/std::max<T> calls in Subrect.cpp. The plugin's PCH suppresses
# this implicitly; the test target needs it explicitly.
target_compile_definitions(cpp_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

enable_testing()
add_test(
    NAME CppUtilTests
    COMMAND cpp_tests --reporter compact
    WORKING_DIRECTORY $<TARGET_FILE_DIR:cpp_tests>
)
set_tests_properties(CppUtilTests PROPERTIES
    TIMEOUT 60
    LABELS "Cpp;UnitTests"
)

message(STATUS "C++ unit tests configured successfully")
