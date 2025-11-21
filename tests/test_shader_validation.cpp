/**
 * High-impact shader validation tests
 * Tests critical shader-related validation without compiling actual shaders
 */

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cctype>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

namespace ShaderValidation
{
	/**
	 * Validate shader define name follows conventions
	 * Community Shaders uses UPPER_SNAKE_CASE for shader defines
	 */
	bool IsValidDefineName(const std::string& name)
	{
		if (name.empty())
			return false;

		for (char c : name) {
			if (!std::isupper(c) && c != '_' && !std::isdigit(c))
				return false;
		}
		return true;
	}

	/**
	 * Check if a shader define value is a valid numeric string
	 */
	bool IsValidNumericValue(const std::string& value)
	{
		if (value.empty())
			return false;

		bool hasDigit = false;
		bool hasDot = false;

		for (size_t i = 0; i < value.length(); i++) {
			char c = value[i];
			if (std::isdigit(c)) {
				hasDigit = true;
			} else if (c == '.') {
				if (hasDot)
					return false;  // Multiple dots
				hasDot = true;
			} else if (c == '-' && i == 0) {
				// Negative sign only at start
				continue;
			} else if (c == 'f' && i == value.length() - 1) {
				// Float suffix at end
				continue;
			} else {
				return false;
			}
		}

		return hasDigit;
	}

	/**
	 * Convert shader macro array to string representation
	 * Simulates logic from Format.h DefinesToString
	 */
	struct ShaderMacro
	{
		const char* Name;
		const char* Definition;
	};

	std::string MacrosToString(const std::vector<ShaderMacro>& macros)
	{
		std::string result;
		for (const auto& macro : macros) {
			if (macro.Name != nullptr) {
				result += macro.Name;
				if (macro.Definition != nullptr && std::string(macro.Definition).length() > 0) {
					result += "=";
					result += macro.Definition;
				}
				result += " ";
			}
		}
		return result;
	}

	/**
	 * Parse a version string like "1-2-3" into components
	 * Simulates FeatureVersions.h logic
	 */
	struct Version
	{
		int major = 0;
		int minor = 0;
		int patch = 0;

		bool operator==(const Version& other) const
		{
			return major == other.major && minor == other.minor && patch == other.patch;
		}

		bool operator<(const Version& other) const
		{
			if (major != other.major)
				return major < other.major;
			if (minor != other.minor)
				return minor < other.minor;
			return patch < other.patch;
		}
	};

	Version ParseVersionString(const std::string& versionStr)
	{
		Version v;
		size_t pos1 = versionStr.find('-');
		size_t pos2 = versionStr.find('-', pos1 + 1);

		if (pos1 != std::string::npos && pos2 != std::string::npos) {
			try {
				v.major = std::stoi(versionStr.substr(0, pos1));
				v.minor = std::stoi(versionStr.substr(pos1 + 1, pos2 - pos1 - 1));
				v.patch = std::stoi(versionStr.substr(pos2 + 1));
			} catch (...) {
				// Parse failed, return 0.0.0
			}
		}

		return v;
	}
}

TEST_CASE("Shader define name validation", "[Shader][Validation]")
{
	using namespace ShaderValidation;

	SECTION("Valid define names")
	{
		REQUIRE(IsValidDefineName("TERRAIN_SHADOWS"));
		REQUIRE(IsValidDefineName("SCREEN_SPACE_GI"));
		REQUIRE(IsValidDefineName("WETNESS_EFFECTS"));
		REQUIRE(IsValidDefineName("IBL"));
		REQUIRE(IsValidDefineName("VR"));
		REQUIRE(IsValidDefineName("LIGHT_LIMIT_FIX"));
		REQUIRE(IsValidDefineName("DYNAMIC_CUBEMAPS"));
		REQUIRE(IsValidDefineName("MAX_LIGHTS_32"));
	}

	SECTION("Invalid define names")
	{
		REQUIRE_FALSE(IsValidDefineName("invalid-define"));   // Hyphens not allowed
		REQUIRE_FALSE(IsValidDefineName("lowerCase"));        // Lowercase not allowed
		REQUIRE_FALSE(IsValidDefineName("Mixed_Case_Name"));  // Mixed case not allowed
		REQUIRE_FALSE(IsValidDefineName("has space"));        // Spaces not allowed
		REQUIRE_FALSE(IsValidDefineName("special@char"));     // Special chars not allowed
		REQUIRE_FALSE(IsValidDefineName(""));                 // Empty not allowed
	}

	SECTION("Edge cases")
	{
		REQUIRE(IsValidDefineName("_"));                     // Single underscore valid
		REQUIRE(IsValidDefineName("A"));                     // Single letter valid
		REQUIRE(IsValidDefineName("_LEADING_UNDERSCORE"));   // Leading underscore valid
		REQUIRE(IsValidDefineName("TRAILING_UNDERSCORE_"));  // Trailing underscore valid
		REQUIRE(IsValidDefineName("NUMBER_123"));            // Numbers valid
	}
}

TEST_CASE("Shader define value validation", "[Shader][Validation]")
{
	using namespace ShaderValidation;

	SECTION("Valid numeric values")
	{
		REQUIRE(IsValidNumericValue("1"));
		REQUIRE(IsValidNumericValue("123"));
		REQUIRE(IsValidNumericValue("1.0"));
		REQUIRE(IsValidNumericValue("0.5"));
		REQUIRE(IsValidNumericValue("-1"));
		REQUIRE(IsValidNumericValue("-1.5"));
		REQUIRE(IsValidNumericValue("1.0f"));
		REQUIRE(IsValidNumericValue("16"));
		REQUIRE(IsValidNumericValue("0"));
	}

	SECTION("Invalid numeric values")
	{
		REQUIRE_FALSE(IsValidNumericValue(""));       // Empty
		REQUIRE_FALSE(IsValidNumericValue("abc"));    // Letters
		REQUIRE_FALSE(IsValidNumericValue("1.2.3"));  // Multiple dots
		REQUIRE_FALSE(IsValidNumericValue("--1"));    // Multiple negatives
		REQUIRE_FALSE(IsValidNumericValue("1-"));     // Negative at end
		REQUIRE_FALSE(IsValidNumericValue("f"));      // Just f suffix
	}
}

TEST_CASE("Shader macro string conversion", "[Shader][Macros]")
{
	using namespace ShaderValidation;

	SECTION("Empty macro list")
	{
		std::vector<ShaderMacro> macros;
		REQUIRE(MacrosToString(macros) == "");
	}

	SECTION("Single macro without value")
	{
		std::vector<ShaderMacro> macros = {
			{ "FEATURE_ENABLED", nullptr }
		};
		REQUIRE_THAT(MacrosToString(macros), Equals("FEATURE_ENABLED "));
	}

	SECTION("Single macro with value")
	{
		std::vector<ShaderMacro> macros = {
			{ "MAX_LIGHTS", "16" }
		};
		REQUIRE_THAT(MacrosToString(macros), Equals("MAX_LIGHTS=16 "));
	}

	SECTION("Multiple macros")
	{
		std::vector<ShaderMacro> macros = {
			{ "TERRAIN_SHADOWS", "1" },
			{ "SCREEN_SPACE_GI", nullptr },
			{ "LIGHT_LIMIT", "32" }
		};

		std::string result = MacrosToString(macros);
		REQUIRE_THAT(result, ContainsSubstring("TERRAIN_SHADOWS=1"));
		REQUIRE_THAT(result, ContainsSubstring("SCREEN_SPACE_GI"));
		REQUIRE_THAT(result, ContainsSubstring("LIGHT_LIMIT=32"));
	}

	SECTION("Macro with empty string value")
	{
		std::vector<ShaderMacro> macros = {
			{ "EMPTY_VALUE", "" }
		};
		// Empty string should not add '='
		REQUIRE_THAT(MacrosToString(macros), Equals("EMPTY_VALUE "));
	}

	SECTION("Common Community Shaders defines")
	{
		std::vector<ShaderMacro> macros = {
			{ "WETNESS_EFFECTS", "1" },
			{ "CLOUD_SHADOWS", "1" },
			{ "DYNAMIC_CUBEMAPS", "1" },
			{ "VR", nullptr }
		};

		std::string result = MacrosToString(macros);
		REQUIRE_THAT(result, ContainsSubstring("WETNESS_EFFECTS=1"));
		REQUIRE_THAT(result, ContainsSubstring("CLOUD_SHADOWS=1"));
		REQUIRE_THAT(result, ContainsSubstring("DYNAMIC_CUBEMAPS=1"));
		REQUIRE_THAT(result, ContainsSubstring("VR "));
	}
}

TEST_CASE("Version string parsing", "[Version][Parsing]")
{
	using namespace ShaderValidation;

	SECTION("Parse valid version strings")
	{
		REQUIRE(ParseVersionString("1-2-3") == Version{ 1, 2, 3 });
		REQUIRE(ParseVersionString("0-0-1") == Version{ 0, 0, 1 });
		REQUIRE(ParseVersionString("10-20-30") == Version{ 10, 20, 30 });
		REQUIRE(ParseVersionString("2-0-0") == Version{ 2, 0, 0 });
	}

	SECTION("Parse invalid version strings")
	{
		// Should return 0.0.0 for invalid formats
		REQUIRE(ParseVersionString("") == Version{ 0, 0, 0 });
		REQUIRE(ParseVersionString("1.2.3") == Version{ 0, 0, 0 });  // Wrong separator
		REQUIRE(ParseVersionString("1-2") == Version{ 0, 0, 0 });    // Missing patch
		REQUIRE(ParseVersionString("abc") == Version{ 0, 0, 0 });    // Not numbers
	}

	SECTION("Version comparison")
	{
		Version v1{ 1, 0, 0 };
		Version v2{ 1, 5, 0 };
		Version v3{ 2, 0, 0 };

		REQUIRE(v1 < v2);
		REQUIRE(v2 < v3);
		REQUIRE(v1 < v3);
		REQUIRE_FALSE(v2 < v1);
	}

	SECTION("Version equality")
	{
		Version v1{ 1, 2, 3 };
		Version v2{ 1, 2, 3 };
		Version v3{ 1, 2, 4 };

		REQUIRE(v1 == v2);
		REQUIRE_FALSE(v1 == v3);
	}
}

TEST_CASE("Feature name validation", "[Features][Validation]")
{
	SECTION("Valid feature names from codebase")
	{
		std::vector<std::string> validNames = {
			"CloudShadows",
			"DynamicCubemaps",
			"ExtendedMaterials",
			"ExtendedTranslucency",
			"GrassCollision",
			"GrassLighting",
			"HairSpecular",
			"IBL",
			"InteriorSun",
			"InverseSquareLighting",
			"LightLimitFix",
			"LODBlending",
			"PerformanceOverlay",
			"ScreenSpaceGI",
			"ScreenSpaceShadows",
			"SkySync",
			"Skylighting",
			"SubsurfaceScattering",
			"TerrainBlending",
			"TerrainHelper",
			"TerrainShadows",
			"TerrainVariation",
			"Upscaling",
			"VolumetricLighting",
			"VR",
			"WaterEffects",
			"WeatherPicker",
			"WetnessEffects"
		};

		for (const auto& name : validNames) {
			// Feature names should not be empty
			REQUIRE_FALSE(name.empty());

			// Should not contain special characters (except for compound names)
			bool hasValidChars = std::all_of(name.begin(), name.end(), [](char c) {
				return std::isalnum(c) || c == '_' || c == '-' || c == ' ';
			});
			REQUIRE(hasValidChars);
		}
	}

	SECTION("Feature INI path construction")
	{
		auto makeIniPath = [](const std::string& featureName) {
			return "Data/Shaders/Features/" + featureName + ".ini";
		};

		REQUIRE(makeIniPath("TerrainShadows") == "Data/Shaders/Features/TerrainShadows.ini");
		REQUIRE(makeIniPath("ScreenSpaceGI") == "Data/Shaders/Features/ScreenSpaceGI.ini");
		REQUIRE(makeIniPath("VR") == "Data/Shaders/Features/VR.ini");
	}
}

TEST_CASE("Shader permutation validation", "[Shader][Permutations]")
{
	SECTION("Calculate number of permutations")
	{
		// If we have N binary defines, we have 2^N permutations
		auto calcPermutations = [](int numDefines) {
			return 1 << numDefines;  // 2^N
		};

		REQUIRE(calcPermutations(0) == 1);   // No defines = 1 permutation
		REQUIRE(calcPermutations(1) == 2);   // 1 define = 2 permutations
		REQUIRE(calcPermutations(2) == 4);   // 2 defines = 4 permutations
		REQUIRE(calcPermutations(3) == 8);   // 3 defines = 8 permutations
		REQUIRE(calcPermutations(4) == 16);  // 4 defines = 16 permutations
		REQUIRE(calcPermutations(5) == 32);  // 5 defines = 32 permutations
	}

	SECTION("Permutation explosion awareness")
	{
		// Too many defines leads to permutation explosion
		// This test documents the exponential growth
		int numFeatures = 10;
		int permutations = 1 << numFeatures;

		REQUIRE(permutations == 1024);  // 10 features = 1024 permutations
										// This is why shader compilation can take a long time!
	}
}
