/**
 * Simple standalone utility tests
 * These test pure C++ logic without any Skyrim dependencies
 */

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;
using Catch::Matchers::WithinAbs;

// Standalone implementations of utility functions for testing
// These replicate the logic from the actual codebase

namespace TestUtils
{
	/**
	 * Normalize a file path (from Format.h logic)
	 */
	std::string FixFilePath(const std::string& a_path)
	{
		std::string lowerFilePath = a_path;

		// Replace all backslashes with forward slashes
		std::replace(lowerFilePath.begin(), lowerFilePath.end(), '\\', '/');

		// Remove consecutive forward slashes
		std::string::iterator newEnd = std::unique(lowerFilePath.begin(), lowerFilePath.end(),
			[](char a, char b) { return a == '/' && b == '/'; });
		lowerFilePath.erase(newEnd, lowerFilePath.end());

		// Convert all characters to lowercase
		std::transform(lowerFilePath.begin(), lowerFilePath.end(), lowerFilePath.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		return lowerFilePath;
	}

	/**
	 * Calculate percentage (from Format.h logic)
	 */
	float CalculatePercentage(float part, float total, float defaultValue = 0.0f)
	{
		return (total > 0.0f) ? (part / total * 100.0f) : defaultValue;
	}

	/**
	 * Calculate cost per call (from Format.h logic)
	 */
	float CalculateCostPerCall(float frameTime, float drawCalls)
	{
		return (drawCalls > 0.0f) ? (frameTime / drawCalls) : 0.0f;
	}

	/**
	 * Format file size (from Format.h logic)
	 */
	std::string FormatFileSize(uint64_t bytes)
	{
		if (bytes >= 1024 * 1024) {
			char buffer[32];
			sprintf_s(buffer, "%.1f MB", static_cast<float>(bytes) / (1024 * 1024));
			return buffer;
		} else {
			char buffer[32];
			sprintf_s(buffer, "%.1f KB", static_cast<float>(bytes) / 1024);
			return buffer;
		}
	}

	/**
	 * Format milliseconds (from Format.h logic)
	 */
	std::string FormatMilliseconds(float ms)
	{
		if (std::abs(ms) < 1e-4f)
			return "0 ms";
		std::ostringstream oss;
		if (ms < 0.1f)
			oss << std::fixed << std::setprecision(3) << ms << " ms";
		else
			oss << std::fixed << std::setprecision(2) << ms << " ms";
		return oss.str();
	}
}

TEST_CASE("FixFilePath normalizes paths correctly", "[Utilities][Path]")
{
	SECTION("Converts backslashes to forward slashes")
	{
		REQUIRE(TestUtils::FixFilePath("path\\to\\file.txt") == "path/to/file.txt");
	}

	SECTION("Removes consecutive slashes")
	{
		REQUIRE(TestUtils::FixFilePath("path//to///file.txt") == "path/to/file.txt");
	}

	SECTION("Converts to lowercase")
	{
		REQUIRE(TestUtils::FixFilePath("Path/To/FILE.TXT") == "path/to/file.txt");
	}

	SECTION("Handles mixed backslashes and forward slashes")
	{
		REQUIRE(TestUtils::FixFilePath("Path\\To/Mixed\\Slashes") == "path/to/mixed/slashes");
	}

	SECTION("Handles empty path")
	{
		REQUIRE(TestUtils::FixFilePath("") == "");
	}

	SECTION("Handles Skyrim-style paths")
	{
		REQUIRE(TestUtils::FixFilePath("Data\\SKSE\\Plugins\\CommunityShaders") == "data/skse/plugins/communityshaders");
		REQUIRE(TestUtils::FixFilePath("Shaders\\Features\\TerrainBlending.hlsl") == "shaders/features/terrainblending.hlsl");
	}
}

TEST_CASE("CalculatePercentage computes percentages correctly", "[Utilities][Math]")
{
	SECTION("Calculates normal percentages")
	{
		REQUIRE_THAT(TestUtils::CalculatePercentage(50.0f, 100.0f), WithinAbs(50.0f, 0.001f));
		REQUIRE_THAT(TestUtils::CalculatePercentage(25.0f, 200.0f), WithinAbs(12.5f, 0.001f));
		REQUIRE_THAT(TestUtils::CalculatePercentage(1.0f, 3.0f), WithinAbs(33.333f, 0.01f));
	}

	SECTION("Returns default value when total is zero")
	{
		REQUIRE_THAT(TestUtils::CalculatePercentage(10.0f, 0.0f, 0.0f), WithinAbs(0.0f, 0.001f));
		REQUIRE_THAT(TestUtils::CalculatePercentage(10.0f, 0.0f, 100.0f), WithinAbs(100.0f, 0.001f));
	}

	SECTION("Returns default value when total is negative")
	{
		REQUIRE_THAT(TestUtils::CalculatePercentage(10.0f, -5.0f, 0.0f), WithinAbs(0.0f, 0.001f));
	}

	SECTION("Handles zero part")
	{
		REQUIRE_THAT(TestUtils::CalculatePercentage(0.0f, 100.0f), WithinAbs(0.0f, 0.001f));
	}

	SECTION("Handles values over 100%")
	{
		REQUIRE_THAT(TestUtils::CalculatePercentage(150.0f, 100.0f), WithinAbs(150.0f, 0.001f));
	}
}

TEST_CASE("CalculateCostPerCall computes per-call cost correctly", "[Utilities][Math]")
{
	SECTION("Calculates normal cost per call")
	{
		REQUIRE_THAT(TestUtils::CalculateCostPerCall(100.0f, 10.0f), WithinAbs(10.0f, 0.001f));
		REQUIRE_THAT(TestUtils::CalculateCostPerCall(16.67f, 1000.0f), WithinAbs(0.01667f, 0.00001f));
	}

	SECTION("Returns zero when draw calls is zero")
	{
		REQUIRE_THAT(TestUtils::CalculateCostPerCall(100.0f, 0.0f), WithinAbs(0.0f, 0.001f));
	}

	SECTION("Returns zero when draw calls is negative")
	{
		REQUIRE_THAT(TestUtils::CalculateCostPerCall(100.0f, -5.0f), WithinAbs(0.0f, 0.001f));
	}
}

TEST_CASE("FormatFileSize formats bytes correctly", "[Utilities][Format]")
{
	SECTION("Formats kilobytes")
	{
		REQUIRE_THAT(TestUtils::FormatFileSize(512), Equals("0.5 KB"));
		REQUIRE_THAT(TestUtils::FormatFileSize(1024), Equals("1.0 KB"));
		REQUIRE_THAT(TestUtils::FormatFileSize(2048), Equals("2.0 KB"));
	}

	SECTION("Formats megabytes")
	{
		REQUIRE_THAT(TestUtils::FormatFileSize(1024 * 1024), Equals("1.0 MB"));
		REQUIRE_THAT(TestUtils::FormatFileSize(5 * 1024 * 1024), Equals("5.0 MB"));
		REQUIRE_THAT(TestUtils::FormatFileSize(1536 * 1024), Equals("1.5 MB"));
	}

	SECTION("Handles very small sizes")
	{
		REQUIRE_THAT(TestUtils::FormatFileSize(0), Equals("0.0 KB"));
		REQUIRE_THAT(TestUtils::FormatFileSize(100), Equals("0.1 KB"));
	}

	SECTION("Handles large sizes")
	{
		REQUIRE_THAT(TestUtils::FormatFileSize(1024ULL * 1024ULL * 1024ULL), Equals("1024.0 MB"));
	}
}

TEST_CASE("FormatMilliseconds formats time correctly", "[Utilities][Format]")
{
	SECTION("Formats zero as '0 ms'")
	{
		REQUIRE_THAT(TestUtils::FormatMilliseconds(0.0f), Equals("0 ms"));
	}

	SECTION("Formats small values with 3 decimal places")
	{
		REQUIRE_THAT(TestUtils::FormatMilliseconds(0.001f), Equals("0.001 ms"));
		REQUIRE_THAT(TestUtils::FormatMilliseconds(0.099f), Equals("0.099 ms"));
	}

	SECTION("Formats larger values with 2 decimal places")
	{
		REQUIRE_THAT(TestUtils::FormatMilliseconds(1.234f), Equals("1.23 ms"));
		REQUIRE_THAT(TestUtils::FormatMilliseconds(10.567f), Equals("10.57 ms"));
	}

	SECTION("Handles negative values")
	{
		// Note: actual implementation behavior may vary
		auto result = TestUtils::FormatMilliseconds(-1.5f);
		REQUIRE_THAT(result, ContainsSubstring("-1.5"));
		REQUIRE_THAT(result, ContainsSubstring("ms"));
	}
}

TEST_CASE("Path construction patterns", "[Utilities][Path]")
{
	SECTION("Feature INI paths follow consistent pattern")
	{
		auto basePath = std::filesystem::path("Data/Shaders/Features");
		auto featureName = std::string("TerrainShadows");
		auto iniPath = basePath / (featureName + ".ini");

		REQUIRE(iniPath.string().find("TerrainShadows.ini") != std::string::npos);
		REQUIRE(iniPath.extension().string() == ".ini");
	}

	SECTION("Feature shader directory paths")
	{
		auto basePath = std::filesystem::path("Data/Shaders");
		auto featureName = std::string("ScreenSpaceGI");
		auto shaderDir = basePath / featureName;

		REQUIRE(shaderDir.string().find("ScreenSpaceGI") != std::string::npos);
	}

	SECTION("Path concatenation works correctly")
	{
		auto base = std::filesystem::path("Data");
		auto full = base / "SKSE" / "Plugins" / "CommunityShaders";
		REQUIRE(full.string().find("CommunityShaders") != std::string::npos);
	}

	SECTION("Path parent and filename extraction")
	{
		auto path = std::filesystem::path("Data/Shaders/Features/CloudShadows.ini");
		REQUIRE(path.filename().string() == "CloudShadows.ini");
		REQUIRE(path.parent_path().string().find("Features") != std::string::npos);
		REQUIRE(path.extension().string() == ".ini");
	}
}

TEST_CASE("Constant buffer size calculations", "[GPU][Buffers]")
{
	SECTION("GetCBufferSize rounds up to 64-byte alignment")
	{
		auto calcSize = [](uint32_t size) -> uint32_t {
			return (size + (64 - 1)) & ~(64 - 1);
		};

		REQUIRE(calcSize(1) == 64);
		REQUIRE(calcSize(64) == 64);
		REQUIRE(calcSize(65) == 128);
		REQUIRE(calcSize(128) == 128);
		REQUIRE(calcSize(129) == 192);
		REQUIRE(calcSize(256) == 256);
	}

	SECTION("Buffer sizes are multiples of 16 bytes")
	{
		// Common GPU buffer patterns
		struct Float4
		{
			float v[4];
		};
		struct Matrix4x4
		{
			float m[16];
		};

		REQUIRE(sizeof(Float4) == 16);
		REQUIRE(sizeof(Float4) % 16 == 0);

		REQUIRE(sizeof(Matrix4x4) == 64);
		REQUIRE(sizeof(Matrix4x4) % 16 == 0);
	}

	SECTION("Padding ensures alignment")
	{
		struct ProperlyPadded
		{
			float vec[4];    // 16 bytes
			uint32_t count;  // 4 bytes
			float pad[3];    // 12 bytes padding -> total 32 bytes
		};

		struct NeedsPadding
		{
			float vec[4];    // 16 bytes
			uint32_t count;  // 4 bytes -> total 20 bytes (NOT aligned!)
		};

		REQUIRE(sizeof(ProperlyPadded) == 32);
		REQUIRE(sizeof(ProperlyPadded) % 16 == 0);

		REQUIRE(sizeof(NeedsPadding) == 20);
		REQUIRE_FALSE(sizeof(NeedsPadding) % 16 == 0);
	}
}
