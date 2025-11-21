/**
 * FileSystem utility tests
 * Tests path construction and file operations from src/Utils/FileSystem.cpp
 *
 * This tests ACTUAL production code with mocked external dependencies (imgui, json)
 */

// CRITICAL: Include test stubs FIRST before any project headers
#include "test_stubs.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

// Now include the actual headers we're testing
#include "../src/Utils/FileSystem.h"

TEST_CASE("Path helpers construct correct paths", "[FileSystem][Paths]")
{
	using namespace Util::PathHelpers;

	SECTION("GetShadersPath returns Data/Shaders")
	{
		auto path = GetShadersPath();
		auto pathStr = path.string();

		// Should end with Data\Shaders (Windows) or Data/Shaders
		REQUIRE((pathStr.ends_with("Data\\Shaders") || pathStr.ends_with("Data/Shaders")));
	}

	SECTION("GetFeaturesPath returns Shaders/Features")
	{
		auto path = GetFeaturesPath();
		auto pathStr = path.string();

		REQUIRE((pathStr.ends_with("Shaders\\Features") || pathStr.ends_with("Shaders/Features")));
	}

	SECTION("GetCommunityShaderPath returns correct structure")
	{
		auto path = GetCommunityShaderPath();
		auto pathStr = path.string();

		// Should contain SKSE\Plugins\CommunityShaders
		REQUIRE((pathStr.find("SKSE\\Plugins\\CommunityShaders") != std::string::npos ||
				 pathStr.find("SKSE/Plugins/CommunityShaders") != std::string::npos));
	}

	SECTION("GetSettingsUserPath ends with SettingsUser.json")
	{
		auto path = GetSettingsUserPath();
		REQUIRE(path.filename().string() == "SettingsUser.json");
	}

	SECTION("GetSettingsTestPath ends with SettingsTest.json")
	{
		auto path = GetSettingsTestPath();
		REQUIRE(path.filename().string() == "SettingsTest.json");
	}

	SECTION("GetSettingsDefaultPath ends with SettingsDefault.json")
	{
		auto path = GetSettingsDefaultPath();
		REQUIRE(path.filename().string() == "SettingsDefault.json");
	}

	SECTION("GetFeatureIniPath constructs correct path")
	{
		auto path = GetFeatureIniPath("TerrainShadows");

		REQUIRE(path.filename().string() == "TerrainShadows.ini");
		// Parent should be Features directory
		auto parent = path.parent_path().filename().string();
		REQUIRE(parent == "Features");
	}

	SECTION("GetFeatureShaderPath constructs correct path")
	{
		auto path = GetFeatureShaderPath("WetnessEffects");
		REQUIRE(path.filename().string() == "WetnessEffects");
	}
}

TEST_CASE("SafeDelete handles edge cases gracefully", "[FileSystem][Delete]")
{
	using namespace Util::FileHelpers;

	SECTION("Empty path is considered successfully deleted")
	{
		auto result = SafeDelete("", "test empty path");
		REQUIRE(result.success);
		REQUIRE(result.deletedDescription == "test empty path: ");
	}

	SECTION("Non-existent path is considered successfully deleted")
	{
		auto result = SafeDelete("/this/path/does/not/exist/file.txt", "test non-existent");
		REQUIRE(result.success);
	}

	SECTION("Description is preserved in result")
	{
		auto result = SafeDelete("", "my custom description");
		REQUIRE(result.deletedDescription.find("my custom description") != std::string::npos);
	}
}

TEST_CASE("EnsureDirectoryExists doesn't crash", "[FileSystem][Directory]")
{
	using namespace Util::FileHelpers;

	SECTION("Can call with empty path")
	{
		REQUIRE_NOTHROW(EnsureDirectoryExists(""));
	}

	SECTION("Can call with existing directory")
	{
		REQUIRE_NOTHROW(EnsureDirectoryExists("."));
	}
}

TEST_CASE("Path construction is consistent", "[FileSystem][Consistency]")
{
	using namespace Util::PathHelpers;

	SECTION("All settings paths share common parent")
	{
		auto user = GetSettingsUserPath();
		auto test = GetSettingsTestPath();
		auto defaults = GetSettingsDefaultPath();
		auto theme = GetSettingsThemePath();

		// All should have same parent directory
		REQUIRE(user.parent_path() == test.parent_path());
		REQUIRE(test.parent_path() == defaults.parent_path());
		REQUIRE(defaults.parent_path() == theme.parent_path());
	}

	SECTION("Features path is child of Shaders path")
	{
		auto shaders = GetShadersPath();
		auto features = GetFeaturesPath();

		// Features should be Shaders/Features
		REQUIRE(features.string().find(shaders.string()) != std::string::npos);
	}

	SECTION("Interface paths share common parent")
	{
		auto interface = GetInterfacePath();
		auto fonts = GetFontsPath();
		auto icons = GetIconsPath();

		// Fonts and Icons should be children of Interface
		REQUIRE(fonts.string().find(interface.string()) != std::string::npos);
		REQUIRE(icons.string().find(interface.string()) != std::string::npos);
	}
}

TEST_CASE("LoadJsonDiff handles missing files gracefully", "[FileSystem][JSON]")
{
	using namespace Util::FileSystem;

	SECTION("Returns empty diff for non-existent files")
	{
		auto diff = LoadJsonDiff("/nonexistent/user.json", "/nonexistent/test.json");
		REQUIRE(diff.empty());
	}

	SECTION("Returns empty diff when one file is missing")
	{
		// Create a temporary file
		std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_community_shaders.json";
		{
			std::ofstream temp(tempPath);
			temp << "{}";
		}

		auto diff1 = LoadJsonDiff(tempPath, "/nonexistent.json");
		REQUIRE(diff1.empty());

		auto diff2 = LoadJsonDiff("/nonexistent.json", tempPath);
		REQUIRE(diff2.empty());

		// Cleanup
		std::filesystem::remove(tempPath);
	}
}

TEST_CASE("EnumerateDllVersions handles errors gracefully", "[FileSystem][DLL]")
{
	using namespace Util;

	SECTION("Returns empty vector for non-existent directory")
	{
		auto dlls = EnumerateDllVersions("/nonexistent/directory");
		REQUIRE(dlls.empty());
	}
}

TEST_CASE("Path helpers are consistent across calls", "[FileSystem][Idempotent]")
{
	using namespace Util::PathHelpers;

	SECTION("Same path helper returns same result")
	{
		auto path1 = GetDataPath();
		auto path2 = GetDataPath();
		REQUIRE(path1 == path2);

		auto shaders1 = GetShadersPath();
		auto shaders2 = GetShadersPath();
		REQUIRE(shaders1 == shaders2);
	}

	SECTION("GetRootRealPath is cached")
	{
		// This is cached in the implementation
		auto root1 = GetRootRealPath();
		auto root2 = GetRootRealPath();
		REQUIRE(root1 == root2);
	}
}
