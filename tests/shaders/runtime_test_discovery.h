// Runtime HLSL Test Discovery
// Scans HLSL files at test runtime and dynamically executes discovered tests
#pragma once

#include "test_common.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace HLSLTestDiscovery
{
	struct TestFunction
	{
		std::string name;
		std::string displayName;
		std::string filePath;
		std::vector<std::string> tags;
	};

	// Convert camelCase/PascalCase to space-separated words
	inline std::string camelToSpaces(const std::string& str)
	{
		std::string result;
		bool lastWasUpper = false;
		bool lastWasLower = false;

		for (size_t i = 0; i < str.length(); i++) {
			char c = str[i];
			bool isUpper = std::isupper(static_cast<unsigned char>(c)) != 0;
			bool isLower = std::islower(static_cast<unsigned char>(c)) != 0;

			if (isUpper && i > 0) {
				if (lastWasLower || (lastWasUpper && i + 1 < str.length() && std::islower(static_cast<unsigned char>(str[i + 1])))) {
					result += ' ';
				}
			}

			result += c;
			lastWasUpper = isUpper;
			lastWasLower = isLower;
		}

		return result;
	}

	// Extract module name from file path
	inline std::string extractModuleName(const std::string& filename)
	{
		std::string name = filename;
		if (name.find("Test") == 0) {
			name = name.substr(4);
		}
		if (name.length() >= 5 && name.substr(name.length() - 5) == ".hlsl") {
			name = name.substr(0, name.length() - 5);
		}
		return name;
	}

	// Generate human-readable display name
	inline std::string generateDisplayName(const std::string& functionName, const std::string& moduleName)
	{
		std::string name = functionName;
		if (name.find("Test") == 0) {
			name = name.substr(4);
		}
		name = camelToSpaces(name);
		return moduleName + " - " + name;
	}

	// Infer tags from function name
	inline std::vector<std::string> inferTags(const std::string& functionName)
	{
		std::vector<std::string> tags;
		std::string lower = functionName;
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		struct TagRule
		{
			const char* keyword;
			const char* tag;
		};

		static const TagRule rules[] = {
			{ "fresnel", "fresnel" },
			{ "diffuse", "diffuse" },
			{ "specular", "specular" },
			{ "lambert", "diffuse" },
			{ "burley", "diffuse" },
			{ "ggx", "ggx" },
			{ "beckmann", "beckmann" },
			{ "charlie", "sheen" },
			{ "visibility", "visibility" },
			{ "distribution", "ndf" },
			{ "brdf", "brdf" },
			{ "sqrt", "sqrt" },
			{ "rcp", "reciprocal" },
			{ "atan", "trig" },
			{ "asin", "trig" },
			{ "acos", "trig" },
			{ "sin", "trig" },
			{ "cos", "trig" },
			{ "tan", "trig" },
			{ "fastmath", "fastmath" },
			{ "pcg", "pcg" },
			{ "random", "random" },
			{ "noise", "noise" },
			{ "perlin", "noise" },
			{ "hash", "hash" },
			{ "murmur", "hash" },
			{ "color", "color" },
			{ "luminance", "luminance" },
			{ "gamma", "gamma" },
			{ "displaymapping", "displaymapping" },
			{ "math", "math" },
			{ "matrix", "matrix" },
			{ "gbuffer", "gbuffer" },
			{ "normal", "normal" },
			{ "anisotropic", "anisotropic" },
			{ "lightingcommon", "lightingcommon" }
		};

		std::set<std::string> uniqueTags;
		for (const auto& rule : rules) {
			if (lower.find(rule.keyword) != std::string::npos) {
				uniqueTags.insert(rule.tag);
			}
		}

		for (const auto& tag : uniqueTags) {
			tags.push_back(tag);
		}

		return tags;
	}

	// Scan HLSL file for test functions
	inline std::vector<TestFunction> scanHLSLFile(const std::filesystem::path& filePath)
	{
		std::vector<TestFunction> tests;
		std::ifstream file(filePath);
		if (!file.is_open()) {
			return tests;
		}

		std::string moduleName = extractModuleName(filePath.filename().string());
		std::regex numthreadsPattern(R"(\[numthreads\s*\(\s*1\s*,\s*1\s*,\s*1\s*\)\s*\])");
		std::regex functionPattern(R"(void\s+(\w+)\s*\(\s*\))");

		std::string line;
		while (std::getline(file, line)) {
			if (std::regex_search(line, numthreadsPattern)) {
				std::string nextLine;
				if (std::getline(file, nextLine)) {
					std::smatch match;
					if (std::regex_search(nextLine, match, functionPattern)) {
						TestFunction test;
						test.name = match[1].str();
						test.filePath = "/Shaders/Tests/" + filePath.filename().string();
						test.displayName = generateDisplayName(test.name, moduleName);
						test.tags = inferTags(test.name);
						tests.push_back(test);
					}
				}
			}
		}

		return tests;
	}

	// Discover all tests in shader directory
	inline std::vector<TestFunction> discoverAllTests()
	{
		std::vector<TestFunction> allTests;

		// Get shader test directory
		auto exeDir = ShaderTest::GetExecutableDirectory();
		auto shaderTestDir = exeDir / "Shaders" / "Tests";

		if (!std::filesystem::exists(shaderTestDir)) {
			return allTests;
		}

		// Scan all Test*.hlsl files
		for (const auto& entry : std::filesystem::directory_iterator(shaderTestDir)) {
			if (entry.is_regular_file()) {
				std::string filename = entry.path().filename().string();
				if (filename.find("Test") == 0 && filename.substr(filename.length() - 5) == ".hlsl") {
					auto tests = scanHLSLFile(entry.path());
					allTests.insert(allTests.end(), tests.begin(), tests.end());
				}
			}
		}

		return allTests;
	}

	// Run a single discovered test
	inline bool runTest(const TestFunction& test, std::string& errorMsg)
	{
		try {
			stf::ShaderTestFixture fixture(ShaderTest::GetFixtureDesc());
			auto shaderDir = (ShaderTest::GetExecutableDirectory() / "Shaders").wstring();

			auto result = fixture.RunTest(stf::ShaderTestFixture::RuntimeTestDesc{
				.CompilationEnv{ .Source = std::filesystem::path(test.filePath),
					.CompilationFlags = { L"-I", shaderDir } },
				.TestName = test.name,
				.ThreadGroupCount{ 1, 1, 1 } });

			if (!result) {
				errorMsg = "Test failed";
				return false;
			}
			return true;
		} catch (const std::exception& e) {
			errorMsg = e.what();
			return false;
		}
	}
}
