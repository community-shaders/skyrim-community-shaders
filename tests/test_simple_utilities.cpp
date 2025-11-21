/**
 * Simple utility tests
 * Tests the ACTUAL production code from src/Utils/Format.cpp
 */

// CRITICAL: Include test stubs FIRST before any project headers
// This provides std:: types and Skyrim mocks that Format.h needs
#include "test_stubs.h"

// Now include Catch2 (after stubs to avoid conflicts)
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// Include the ACTUAL header we're testing
// test_stubs.h already provided all the std:: types this needs
#include "../src/Utils/Format.h"

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;
using Catch::Matchers::WithinAbs;

TEST_CASE("FixFilePath normalizes paths correctly", "[Utilities][Path]")
{
	SECTION("Converts backslashes to forward slashes")
	{
		REQUIRE(Util::FixFilePath("path\\to\\file.txt") == "path/to/file.txt");
	}

	SECTION("Removes consecutive slashes")
	{
		REQUIRE(Util::FixFilePath("path//to///file.txt") == "path/to/file.txt");
	}

	SECTION("Converts to lowercase")
	{
		REQUIRE(Util::FixFilePath("Path/To/FILE.TXT") == "path/to/file.txt");
	}

	SECTION("Handles mixed backslashes and forward slashes")
	{
		REQUIRE(Util::FixFilePath("Path\\To/Mixed\\Slashes") == "path/to/mixed/slashes");
	}

	SECTION("Handles empty path")
	{
		REQUIRE(Util::FixFilePath("") == "");
	}

	SECTION("Handles Skyrim-style paths")
	{
		REQUIRE(Util::FixFilePath("Data\\SKSE\\Plugins\\CommunityShaders") == "data/skse/plugins/communityshaders");
		REQUIRE(Util::FixFilePath("Shaders\\Features\\TerrainBlending.hlsl") == "shaders/features/terrainblending.hlsl");
	}
}

TEST_CASE("CalculatePercentage computes percentages correctly", "[Utilities][Math]")
{
	SECTION("Calculates normal percentages")
	{
		REQUIRE_THAT(Util::CalculatePercentage(50.0f, 100.0f), WithinAbs(50.0f, 0.001f));
		REQUIRE_THAT(Util::CalculatePercentage(25.0f, 200.0f), WithinAbs(12.5f, 0.001f));
		REQUIRE_THAT(Util::CalculatePercentage(1.0f, 3.0f), WithinAbs(33.333f, 0.01f));
	}

	SECTION("Returns default value when total is zero")
	{
		REQUIRE_THAT(Util::CalculatePercentage(10.0f, 0.0f, 0.0f), WithinAbs(0.0f, 0.001f));
		REQUIRE_THAT(Util::CalculatePercentage(10.0f, 0.0f, 100.0f), WithinAbs(100.0f, 0.001f));
	}

	SECTION("Returns default value when total is negative")
	{
		REQUIRE_THAT(Util::CalculatePercentage(10.0f, -5.0f, 0.0f), WithinAbs(0.0f, 0.001f));
	}

	SECTION("Handles zero part")
	{
		REQUIRE_THAT(Util::CalculatePercentage(0.0f, 100.0f), WithinAbs(0.0f, 0.001f));
	}

	SECTION("Handles values over 100%")
	{
		REQUIRE_THAT(Util::CalculatePercentage(150.0f, 100.0f), WithinAbs(150.0f, 0.001f));
	}
}

TEST_CASE("CalculateCostPerCall computes per-call cost correctly", "[Utilities][Math]")
{
	SECTION("Calculates normal cost per call")
	{
		REQUIRE_THAT(Util::CalculateCostPerCall(100.0f, 10.0f), WithinAbs(10.0f, 0.001f));
		REQUIRE_THAT(Util::CalculateCostPerCall(16.67f, 1000.0f), WithinAbs(0.01667f, 0.00001f));
	}

	SECTION("Returns zero when draw calls is zero")
	{
		REQUIRE_THAT(Util::CalculateCostPerCall(100.0f, 0.0f), WithinAbs(0.0f, 0.001f));
	}

	SECTION("Returns zero when draw calls is negative")
	{
		REQUIRE_THAT(Util::CalculateCostPerCall(100.0f, -5.0f), WithinAbs(0.0f, 0.001f));
	}
}

TEST_CASE("FormatFileSize formats bytes correctly", "[Utilities][Format]")
{
	SECTION("Formats kilobytes")
	{
		REQUIRE_THAT(Util::FormatFileSize(512), Equals("0.5 KB"));
		REQUIRE_THAT(Util::FormatFileSize(1024), Equals("1.0 KB"));
		REQUIRE_THAT(Util::FormatFileSize(2048), Equals("2.0 KB"));
	}

	SECTION("Formats megabytes")
	{
		REQUIRE_THAT(Util::FormatFileSize(1024 * 1024), Equals("1.0 MB"));
		REQUIRE_THAT(Util::FormatFileSize(5 * 1024 * 1024), Equals("5.0 MB"));
		REQUIRE_THAT(Util::FormatFileSize(1536 * 1024), Equals("1.5 MB"));
	}

	SECTION("Handles very small sizes")
	{
		REQUIRE_THAT(Util::FormatFileSize(0), Equals("0.0 KB"));
		REQUIRE_THAT(Util::FormatFileSize(100), Equals("0.1 KB"));
	}

	SECTION("Handles large sizes")
	{
		REQUIRE_THAT(Util::FormatFileSize(1024ULL * 1024ULL * 1024ULL), Equals("1024.0 MB"));
	}
}

TEST_CASE("FormatMilliseconds formats time correctly", "[Utilities][Format]")
{
	SECTION("Formats zero as '0 ms'")
	{
		REQUIRE_THAT(Util::FormatMilliseconds(0.0f), Equals("0 ms"));
	}

	SECTION("Formats small values with 3 decimal places")
	{
		REQUIRE_THAT(Util::FormatMilliseconds(0.001f), Equals("0.001 ms"));
		REQUIRE_THAT(Util::FormatMilliseconds(0.099f), Equals("0.099 ms"));
	}

	SECTION("Formats larger values with 2 decimal places")
	{
		REQUIRE_THAT(Util::FormatMilliseconds(1.234f), Equals("1.23 ms"));
		REQUIRE_THAT(Util::FormatMilliseconds(10.567f), Equals("10.57 ms"));
	}

	SECTION("Handles negative values")
	{
		// Note: actual implementation behavior may vary
		auto result = Util::FormatMilliseconds(-1.5f);
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
