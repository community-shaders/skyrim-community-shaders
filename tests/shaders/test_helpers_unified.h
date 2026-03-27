// Unified Helper Macros for HLSL Shader Tests
// This file provides a single, consistent macro interface for creating shader tests
#pragma once

#include "test_common.h"
#include <catch2/catch_test_macros.hpp>

// ============================================================================
// UNIFIED SHADER TEST MACRO
// ============================================================================
//
// This macro provides a consistent interface for all shader tests.
// It automatically handles the ShaderTestFramework boilerplate.
//
// Usage:
//   SHADER_TEST("Test Name", "[tag1][tag2]", "/Shaders/Tests/TestFile.hlsl", "HLSLFunctionName", 1, 1, 1)
//
// Parameters:
//   test_name     - Human-readable test name (e.g., "Math - PI Constant")
//   tags          - Catch2 tags (e.g., "[math][constants]")
//   shader_path   - Virtual path to HLSL file (e.g., "/Shaders/Tests/TestMath.hlsl")
//   hlsl_function - Name of HLSL test function (e.g., "TestMathConstants")
//   x, y, z       - Thread group count (use 1,1,1 for most tests; higher for parallel tests)
//
// Example:
//   SHADER_TEST("Math - Constants", "[math][constants]", "/Shaders/Tests/TestMath.hlsl", "TestMathConstants", 1, 1, 1)
//
#define SHADER_TEST(test_name, tags, shader_path, hlsl_function, x, y, z)              \
	TEST_CASE(test_name, tags)                                                         \
	{                                                                                  \
		stf::ShaderTestFixture fixture(                                                \
			ShaderTest::GetFixtureDesc(stf::GPUDevice::EDeviceType::Hardware));        \
		auto shaderDir = (ShaderTest::GetExecutableDirectory() / "Shaders").wstring(); \
		auto result = fixture.RunTest(stf::ShaderTestFixture::RuntimeTestDesc{         \
			.CompilationEnv{ .Source = std::filesystem::path(shader_path),             \
				.CompilationFlags = { L"-I", shaderDir } },                            \
			.TestName = hlsl_function,                                                 \
			.ThreadGroupCount{ x, y, z } });                                           \
		REQUIRE(result);                                                               \
	}

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

// For tests that don't need parallelization (most common case)
// Automatically uses thread group count of (1, 1, 1)
#define SHADER_TEST_SIMPLE(test_name, tags, shader_path, hlsl_function) \
	SHADER_TEST(test_name, tags, shader_path, hlsl_function, 1, 1, 1)

// For parallel tests that benefit from GPU threading
// Uses a default thread group count of (8, 8, 1) which is common for 2D workloads
#define SHADER_TEST_PARALLEL(test_name, tags, shader_path, hlsl_function) \
	SHADER_TEST(test_name, tags, shader_path, hlsl_function, 8, 8, 1)

// ============================================================================
// BATCH TEST GENERATION
// ============================================================================
//
// For when you have multiple test functions in a single HLSL file and want
// to avoid repetitive boilerplate.
//
// Usage:
//   SHADER_TEST_BATCH("/Shaders/Tests/TestMath.hlsl",
//       TEST_ENTRY("Math - Constants", "[math][constants]", "TestMathConstants"),
//       TEST_ENTRY("Math - Epsilon", "[math][epsilon]", "TestEpsilonConstants"))
//

struct ShaderTestEntry
{
	const char* testName;
	const char* tags;
	const char* hlslFunction;
	uint32_t threadX = 1;
	uint32_t threadY = 1;
	uint32_t threadZ = 1;
};

#define TEST_ENTRY(name, tags, function) ShaderTestEntry{ name, tags, function, 1, 1, 1 }
#define TEST_ENTRY_PARALLEL(name, tags, function, x, y, z) \
	ShaderTestEntry { name, tags, function, x, y, z }

// Generate multiple tests from a single shader file
template <size_t N>
inline void GenerateShaderTests(const char* shaderPath, const ShaderTestEntry (&entries)[N])
{
	auto shaderDir = (ShaderTest::GetExecutableDirectory() / "Shaders").wstring();
	for (const auto& entry : entries) {
		DYNAMIC_SECTION(entry.testName)
		{
			stf::ShaderTestFixture fixture(ShaderTest::GetFixtureDesc(stf::GPUDevice::EDeviceType::Hardware));
			auto result = fixture.RunTest(stf::ShaderTestFixture::RuntimeTestDesc{
				.CompilationEnv{ .Source = std::filesystem::path(shaderPath),
					.CompilationFlags = { L"-I", shaderDir } },
				.TestName = entry.hlslFunction,
				.ThreadGroupCount{ entry.threadX, entry.threadY, entry.threadZ } });
			REQUIRE(result);
		}
	}
}

// Macro wrapper for batch test generation
#define SHADER_TEST_BATCH(shader_path, ...)                \
	TEST_CASE("Batch tests for " shader_path, "[batch]")   \
	{                                                      \
		const ShaderTestEntry entries[] = { __VA_ARGS__ }; \
		GenerateShaderTests(shader_path, entries);         \
	}
