/**
 * High-impact configuration and JSON validation tests
 * Tests critical configuration logic without needing file I/O
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;
using json = nlohmann::json;

namespace ConfigValidation
{
	/**
	 * Validate JSON structure for feature settings
	 */
	bool IsValidFeatureSettingsJSON(const json& j)
	{
		// Feature settings should be an object
		if (!j.is_object())
			return false;

		// Should have at least feature name
		if (j.empty())
			return false;

		return true;
	}

	/**
	 * Validate feature version structure
	 */
	bool IsValidVersionJSON(const json& j)
	{
		if (!j.is_object())
			return false;

		if (!j.contains("major") || !j.contains("minor") || !j.contains("patch"))
			return false;

		return j["major"].is_number() && j["minor"].is_number() && j["patch"].is_number();
	}

	/**
	 * Parse INI-style key=value pair
	 */
	struct INIPair
	{
		std::string key;
		std::string value;
		bool valid = false;
	};

	INIPair ParseINILine(const std::string& line)
	{
		INIPair result;

		// Trim whitespace
		std::string trimmed = line;
		trimmed.erase(0, trimmed.find_first_not_of(" \t"));
		trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

		// Skip comments and empty lines
		if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#' || trimmed[0] == '[')
			return result;

		// Find equals sign
		size_t eqPos = trimmed.find('=');
		if (eqPos == std::string::npos)
			return result;

		result.key = trimmed.substr(0, eqPos);
		result.value = trimmed.substr(eqPos + 1);

		// Trim key and value
		result.key.erase(result.key.find_last_not_of(" \t") + 1);
		result.value.erase(0, result.value.find_first_not_of(" \t"));
		result.value.erase(result.value.find_last_not_of(" \t") + 1);

		result.valid = !result.key.empty();
		return result;
	}

	/**
	 * Validate file extension
	 */
	bool HasExpectedExtension(const std::filesystem::path& path, const std::string& ext)
	{
		return path.extension().string() == ext;
	}
}

TEST_CASE("JSON feature settings validation", "[Config][JSON]")
{
	using namespace ConfigValidation;

	SECTION("Valid feature settings JSON")
	{
		json settings = {
			{ "FeatureName", "TerrainShadows" },
			{ "Enabled", true },
			{ "Quality", "High" }
		};

		REQUIRE(IsValidFeatureSettingsJSON(settings));
	}

	SECTION("Empty JSON is invalid")
	{
		json settings = json::object();
		REQUIRE_FALSE(IsValidFeatureSettingsJSON(settings));
	}

	SECTION("Non-object JSON is invalid")
	{
		json settings = json::array();
		REQUIRE_FALSE(IsValidFeatureSettingsJSON(settings));

		json settingsString = "string value";
		REQUIRE_FALSE(IsValidFeatureSettingsJSON(settingsString));
	}

	SECTION("JSON roundtrip preserves data")
	{
		json original = {
			{ "feature", "ScreenSpaceGI" },
			{ "enabled", true },
			{ "samples", 16 },
			{ "radius", 1.5f }
		};

		// Serialize to string
		std::string serialized = original.dump();

		// Deserialize back
		json deserialized = json::parse(serialized);

		REQUIRE(original == deserialized);
		REQUIRE(deserialized["feature"] == "ScreenSpaceGI");
		REQUIRE(deserialized["enabled"] == true);
		REQUIRE(deserialized["samples"] == 16);
	}
}

TEST_CASE("Feature version JSON validation", "[Config][Version]")
{
	using namespace ConfigValidation;

	SECTION("Valid version JSON")
	{
		json version = {
			{ "major", 1 },
			{ "minor", 2 },
			{ "patch", 3 }
		};

		REQUIRE(IsValidVersionJSON(version));
	}

	SECTION("Missing version components")
	{
		json versionMissingPatch = {
			{ "major", 1 },
			{ "minor", 2 }
		};

		REQUIRE_FALSE(IsValidVersionJSON(versionMissingPatch));
	}

	SECTION("Invalid version types")
	{
		json versionWithStrings = {
			{ "major", "1" },
			{ "minor", "2" },
			{ "patch", "3" }
		};

		REQUIRE_FALSE(IsValidVersionJSON(versionWithStrings));
	}

	SECTION("Version comparison")
	{
		json v1 = { { "major", 1 }, { "minor", 0 }, { "patch", 0 } };
		json v2 = { { "major", 1 }, { "minor", 5 }, { "patch", 0 } };
		json v3 = { { "major", 2 }, { "minor", 0 }, { "patch", 0 } };

		// Manual comparison logic
		auto compareVersions = [](const json& a, const json& b) {
			if (a["major"] != b["major"])
				return a["major"] < b["major"];
			if (a["minor"] != b["minor"])
				return a["minor"] < b["minor"];
			return a["patch"] < b["patch"];
		};

		REQUIRE(compareVersions(v1, v2));
		REQUIRE(compareVersions(v2, v3));
		REQUIRE(compareVersions(v1, v3));
	}
}

TEST_CASE("INI file parsing", "[Config][INI]")
{
	using namespace ConfigValidation;

	SECTION("Parse valid INI lines")
	{
		auto result = ParseINILine("Version = 1-2-3");
		REQUIRE(result.valid);
		REQUIRE(result.key == "Version");
		REQUIRE(result.value == "1-2-3");
	}

	SECTION("Parse with spaces")
	{
		auto result = ParseINILine("  Key  =  Value  ");
		REQUIRE(result.valid);
		REQUIRE(result.key == "Key");
		REQUIRE(result.value == "Value");
	}

	SECTION("Skip comments")
	{
		REQUIRE_FALSE(ParseINILine("; This is a comment").valid);
		REQUIRE_FALSE(ParseINILine("# Also a comment").valid);
		REQUIRE_FALSE(ParseINILine("").valid);
		REQUIRE_FALSE(ParseINILine("   ").valid);
	}

	SECTION("Skip section headers")
	{
		REQUIRE_FALSE(ParseINILine("[Section]").valid);
		REQUIRE_FALSE(ParseINILine("[Feature Settings]").valid);
	}

	SECTION("Parse feature version")
	{
		auto result = ParseINILine("Version = 2-1-0");
		REQUIRE(result.valid);
		REQUIRE(result.key == "Version");
		REQUIRE(result.value == "2-1-0");
	}

	SECTION("Parse boolean values")
	{
		auto result1 = ParseINILine("Enabled = true");
		REQUIRE(result1.valid);
		REQUIRE(result1.value == "true");

		auto result2 = ParseINILine("Disabled = false");
		REQUIRE(result2.valid);
		REQUIRE(result2.value == "false");
	}

	SECTION("Parse numeric values")
	{
		auto result = ParseINILine("Quality = 3");
		REQUIRE(result.valid);
		REQUIRE(result.key == "Quality");
		REQUIRE(result.value == "3");
	}

	SECTION("Lines without equals sign are invalid")
	{
		REQUIRE_FALSE(ParseINILine("InvalidLine").valid);
		REQUIRE_FALSE(ParseINILine("No Equals Here").valid);
	}
}

TEST_CASE("File extension validation", "[Config][Files]")
{
	using namespace ConfigValidation;

	SECTION("INI file extensions")
	{
		REQUIRE(HasExpectedExtension("Data/Shaders/Features/TerrainShadows.ini", ".ini"));
		REQUIRE(HasExpectedExtension("Config/Settings.ini", ".ini"));
	}

	SECTION("JSON file extensions")
	{
		REQUIRE(HasExpectedExtension("Settings/User.json", ".json"));
		REQUIRE(HasExpectedExtension("Config/Feature.json", ".json"));
	}

	SECTION("HLSL file extensions")
	{
		REQUIRE(HasExpectedExtension("Shaders/Lighting.hlsl", ".hlsl"));
		REQUIRE(HasExpectedExtension("Shaders/Common.hlsli", ".hlsli"));
	}

	SECTION("Wrong extensions")
	{
		REQUIRE_FALSE(HasExpectedExtension("file.txt", ".ini"));
		REQUIRE_FALSE(HasExpectedExtension("file.ini", ".json"));
	}

	SECTION("No extension")
	{
		REQUIRE_FALSE(HasExpectedExtension("file", ".ini"));
	}
}

TEST_CASE("Settings path construction", "[Config][Paths]")
{
	SECTION("User settings path")
	{
		auto basePath = std::filesystem::path("Data/SKSE/Plugins/CommunityShaders");
		auto userSettings = basePath / "SettingsUser.json";

		REQUIRE(userSettings.string().find("SettingsUser.json") != std::string::npos);
		REQUIRE(userSettings.extension().string() == ".json");
	}

	SECTION("Feature INI paths")
	{
		auto featuresPath = std::filesystem::path("Data/Shaders/Features");

		auto terrainShadows = featuresPath / "TerrainShadows.ini";
		auto screenSpaceGI = featuresPath / "ScreenSpaceGI.ini";

		REQUIRE(terrainShadows.extension().string() == ".ini");
		REQUIRE(screenSpaceGI.extension().string() == ".ini");
	}

	SECTION("Override paths")
	{
		auto basePath = std::filesystem::path("Data/SKSE/Plugins/CommunityShaders");
		auto overridesDir = basePath / "Overrides";
		auto appliedOverrides = basePath / "AppliedOverrides.json";

		REQUIRE(overridesDir.filename().string() == "Overrides");
		REQUIRE(appliedOverrides.extension().string() == ".json");
	}
}

TEST_CASE("JSON error handling", "[Config][Error]")
{
	SECTION("Detect malformed JSON")
	{
		std::string malformedJSON = "{ invalid json }";

		bool parseError = false;
		try {
			auto parsed = json::parse(malformedJSON);
			(void)parsed;  // Suppress unused variable warning
		} catch (const json::parse_error&) {
			parseError = true;
		}

		REQUIRE(parseError);
	}

	SECTION("Handle missing keys gracefully")
	{
		json settings = {
			{ "feature", "TestFeature" },
			{ "enabled", true }
		};

		// Use value() with default for missing keys
		REQUIRE(settings.value("feature", "") == "TestFeature");
		REQUIRE(settings.value("enabled", false) == true);
		REQUIRE(settings.value("missing_key", "default") == "default");
		REQUIRE(settings.value("missing_number", 42) == 42);
	}

	SECTION("Validate JSON types")
	{
		json settings = {
			{ "string_value", "text" },
			{ "number_value", 123 },
			{ "bool_value", true },
			{ "array_value", json::array({ 1, 2, 3 }) },
			{ "object_value", json::object({ { "key", "value" } }) }
		};

		REQUIRE(settings["string_value"].is_string());
		REQUIRE(settings["number_value"].is_number());
		REQUIRE(settings["bool_value"].is_boolean());
		REQUIRE(settings["array_value"].is_array());
		REQUIRE(settings["object_value"].is_object());
	}
}

TEST_CASE("Configuration validation patterns", "[Config][Patterns]")
{
	SECTION("Feature enable/disable pattern")
	{
		json feature = {
			{ "name", "TerrainShadows" },
			{ "enabled", true },
			{ "loadAtBoot", true }
		};

		REQUIRE(feature["enabled"] == true);
		REQUIRE(feature["loadAtBoot"] == true);

		// Toggle feature
		feature["enabled"] = false;
		REQUIRE(feature["enabled"] == false);
	}

	SECTION("Settings override pattern")
	{
		json defaultSettings = {
			{ "quality", "Medium" },
			{ "samples", 16 },
			{ "radius", 1.0f }
		};

		json override = {
			{ "quality", "High" },
			{ "samples", 32 }
		};

		// Merge override into defaults
		defaultSettings.update(override);

		REQUIRE(defaultSettings["quality"] == "High");  // Overridden
		REQUIRE(defaultSettings["samples"] == 32);      // Overridden
		REQUIRE(defaultSettings["radius"] == 1.0f);     // Not overridden
	}

	SECTION("Multi-level configuration")
	{
		json config = {
			{ "global", { { "enabled", true },
							{ "debug", false } } },
			{ "features", { { "TerrainShadows", { { "enabled", true },
													{ "quality", 3 } } },
							  { "ScreenSpaceGI", { { "enabled", false } } } } }
		};

		REQUIRE(config["global"]["enabled"] == true);
		REQUIRE(config["features"]["TerrainShadows"]["quality"] == 3);
		REQUIRE(config["features"]["ScreenSpaceGI"]["enabled"] == false);
	}
}
