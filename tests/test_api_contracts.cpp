/**
 * API Contract Tests (Golden Tests)
 * These tests verify that public API function signatures don't change during refactoring.
 * They work at COMPILE TIME - if signatures change, tests won't compile!
 *
 * This is critical for:
 * - Preventing accidental API breakage during refactoring
 * - Ensuring backward compatibility
 * - Catching parameter order changes, return type changes
 */

#include "test_stubs.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

// Include actual headers we're testing
#include "../src/Utils/FileSystem.h"
#include "../src/Utils/Format.h"

TEST_CASE("Format.h API contracts", "[API][Contract][Format]")
{
	SECTION("String formatting functions exist with correct signatures")
	{
		// These assignments will FAIL TO COMPILE if signatures change!
		// This is intentional - it catches refactoring mistakes at compile time

		std::string (*formatMs)(float) = &Util::FormatMilliseconds;
		std::string (*formatUs)(float) = &Util::FormatMicroseconds;
		std::string (*formatPct)(float) = &Util::FormatPercent;
		std::string (*formatSize)(uint64_t) = &Util::FormatFileSize;

		REQUIRE(formatMs != nullptr);
		REQUIRE(formatUs != nullptr);
		REQUIRE(formatPct != nullptr);
		REQUIRE(formatSize != nullptr);
	}

	SECTION("Math functions exist with correct signatures")
	{
		float (*calcPct)(float, float, float) = &Util::CalculatePercentage;
		float (*calcCost)(float, float) = &Util::CalculateCostPerCall;
		float (*calcOther)(float, float) = &Util::CalculateOtherFrameTime;

		REQUIRE(calcPct != nullptr);
		REQUIRE(calcCost != nullptr);
		REQUIRE(calcOther != nullptr);
	}

	SECTION("Version formatting exists")
	{
		std::string (*getVersion)(const REL::Version&) = &Util::GetFormattedVersion;
		REQUIRE(getVersion != nullptr);
	}

	SECTION("Path utilities exist")
	{
		std::string (*fixPath)(const std::string&) = &Util::FixFilePath;
		REQUIRE(fixPath != nullptr);
	}
}

TEST_CASE("FileSystem.h API contracts", "[API][Contract][FileSystem]")
{
	using namespace Util::PathHelpers;

	SECTION("Path helper functions exist with correct signatures")
	{
		// All these must return std::filesystem::path and take no arguments
		std::filesystem::path (*getDataPath)() = &GetDataPath;
		std::filesystem::path (*getCommunityShaderPath)() = &GetCommunityShaderPath;
		std::filesystem::path (*getImGuiIniPath)() = &GetImGuiIniPath;
		std::filesystem::path (*getInterfacePath)() = &GetInterfacePath;
		std::filesystem::path (*getFontsPath)() = &GetFontsPath;
		std::filesystem::path (*getIconsPath)() = &GetIconsPath;
		std::filesystem::path (*getShadersPath)() = &GetShadersPath;
		std::filesystem::path (*getFeaturesPath)() = &GetFeaturesPath;
		std::filesystem::path (*getThemesPath)() = &GetThemesPath;
		std::filesystem::path (*getOverridesPath)() = &GetOverridesPath;

		REQUIRE(getDataPath != nullptr);
		REQUIRE(getCommunityShaderPath != nullptr);
		REQUIRE(getImGuiIniPath != nullptr);
		REQUIRE(getInterfacePath != nullptr);
		REQUIRE(getFontsPath != nullptr);
		REQUIRE(getIconsPath != nullptr);
		REQUIRE(getShadersPath != nullptr);
		REQUIRE(getFeaturesPath != nullptr);
		REQUIRE(getThemesPath != nullptr);
		REQUIRE(getOverridesPath != nullptr);
	}

	SECTION("Settings path functions exist")
	{
		std::filesystem::path (*getUserPath)() = &GetSettingsUserPath;
		std::filesystem::path (*getTestPath)() = &GetSettingsTestPath;
		std::filesystem::path (*getDefaultPath)() = &GetSettingsDefaultPath;
		std::filesystem::path (*getThemePath)() = &GetSettingsThemePath;

		REQUIRE(getUserPath != nullptr);
		REQUIRE(getTestPath != nullptr);
		REQUIRE(getDefaultPath != nullptr);
		REQUIRE(getThemePath != nullptr);
	}

	SECTION("Feature path functions exist with correct signatures")
	{
		// These take a string parameter
		std::filesystem::path (*getFeatureIni)(const std::string&) = &GetFeatureIniPath;
		std::filesystem::path (*getFeatureShader)(const std::string&) = &GetFeatureShaderPath;

		REQUIRE(getFeatureIni != nullptr);
		REQUIRE(getFeatureShader != nullptr);
	}

	SECTION("Real path functions exist")
	{
		std::filesystem::path (*getCurrentModulePath)() = &GetCurrentModuleRealPath;
		std::filesystem::path (*getRootPath)() = &GetRootRealPath;
		std::filesystem::path (*getShadersRealPath)() = &GetShadersRealPath;
		std::filesystem::path (*getThemesRealPath)() = &GetThemesRealPath;
		std::filesystem::path (*getFeaturesRealPath)() = &GetFeaturesRealPath;

		REQUIRE(getCurrentModulePath != nullptr);
		REQUIRE(getRootPath != nullptr);
		REQUIRE(getShadersRealPath != nullptr);
		REQUIRE(getThemesRealPath != nullptr);
		REQUIRE(getFeaturesRealPath != nullptr);
	}

	SECTION("File helper functions exist")
	{
		using namespace Util::FileHelpers;

		DeletionResult (*safeDelete)(const std::string&, const std::string&) = &SafeDelete;
		void (*ensureDir)(const std::filesystem::path&) = &EnsureDirectoryExists;

		REQUIRE(safeDelete != nullptr);
		REQUIRE(ensureDir != nullptr);
	}
}

TEST_CASE("API contract consistency", "[API][Contract]")
{
	SECTION("All Format functions return std::string")
	{
		// This section verifies return types at compile time
		std::string result1 = Util::FormatMilliseconds(1.0f);
		std::string result2 = Util::FormatMicroseconds(1.0f);
		std::string result3 = Util::FormatPercent(1.0f);
		std::string result4 = Util::FormatFileSize(1024);

		// Just verify they compile and return strings
		REQUIRE(!result1.empty());
		REQUIRE(!result2.empty());
		REQUIRE(!result3.empty());
		REQUIRE(!result4.empty());
	}

	SECTION("All Calculate functions return float")
	{
		float result1 = Util::CalculatePercentage(1.0f, 100.0f, 0.0f);
		float result2 = Util::CalculateCostPerCall(16.0f, 1000.0f);
		float result3 = Util::CalculateOtherFrameTime(16.0f, 10.0f);

		// Verify they return floats and don't crash
		REQUIRE(std::isfinite(result1));
		REQUIRE(std::isfinite(result2));
		REQUIRE(std::isfinite(result3));
	}

	SECTION("All Path helpers return std::filesystem::path")
	{
		using namespace Util::PathHelpers;

		std::filesystem::path p1 = GetDataPath();
		std::filesystem::path p2 = GetShadersPath();
		std::filesystem::path p3 = GetFeaturesPath();

		// Verify they return paths
		REQUIRE(!p1.empty());
		REQUIRE(!p2.empty());
		REQUIRE(!p3.empty());
	}
}

TEST_CASE("Function parameter order is stable", "[API][Contract][Regression]")
{
	SECTION("CalculatePercentage parameter order: part, total, default")
	{
		// Verify parameter order doesn't accidentally swap
		float result = Util::CalculatePercentage(
			50.0f,   // part
			100.0f,  // total
			0.0f     // default
		);

		// 50 / 100 = 50%
		REQUIRE(result == 50.0f);
	}

	SECTION("FormatDeltaWithPercent parameter order")
	{
		// Verify 2-parameter version exists
		std::string result = Util::FormatDeltaWithPercent(
			10.0f,  // a
			15.0f   // b
		);

		REQUIRE(!result.empty());
	}
}
