#include "ThemeManager.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>

#include "Utils/FileSystem.h"

using namespace SKSE;

namespace
{
	/**
	 * @brief Gets file modification time
	 */
	std::time_t GetFileModTime(const std::filesystem::path& filePath)
	{
		try {
			auto fileTime = std::filesystem::last_write_time(filePath);
			auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
			return std::chrono::system_clock::to_time_t(systemTime);
		} catch (...) {
			return 0;
		}
	}
}

size_t ThemeManager::DiscoverThemes()
{
	if (discovered) {
		return themes.size();
	}

	themes.clear();

	auto themesDir = GetThemesDirectory();
	if (!std::filesystem::exists(themesDir)) {
		logger::info("Themes directory does not exist: {}", themesDir.string());
		discovered = true;
		return 0;
	}

	logger::info("Discovering themes in: {}", themesDir.string());

	try {
		for (const auto& entry : std::filesystem::directory_iterator(themesDir)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}

			// Check file size
			auto fileSize = entry.file_size();
			if (fileSize > MAX_FILE_SIZE) {
				logger::warn("Theme file too large, skipping: {} ({}MB)", 
					entry.path().filename().string(), fileSize / (1024 * 1024));
				continue;
			}

			if (themes.size() >= MAX_THEMES) {
				logger::warn("Maximum number of themes ({}) reached, skipping remaining files", MAX_THEMES);
				break;
			}

			auto themeInfo = LoadThemeFile(entry.path());
			if (themeInfo && themeInfo->isValid) {
				themes.push_back(std::move(*themeInfo));
				logger::info("Discovered theme: {} ({})", themes.back().name, themes.back().displayName);
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error discovering themes: {}", e.what());
	}

	// Sort themes alphabetically by display name
	std::sort(themes.begin(), themes.end(), [](const ThemeInfo& a, const ThemeInfo& b) {
		return a.displayName < b.displayName;
	});

	discovered = true;
	logger::info("Theme discovery complete. Found {} themes", themes.size());
	return themes.size();
}

std::vector<std::string> ThemeManager::GetThemeNames() const
{
	std::vector<std::string> names;
	names.reserve(themes.size());
	
	for (const auto& theme : themes) {
		names.push_back(theme.name);
	}
	
	return names;
}

bool ThemeManager::LoadTheme(const std::string& themeName, json& themeSettings)
{
	if (!discovered) {
		DiscoverThemes();
	}

	if (themeName.empty()) {
		// Empty theme name means use current/custom theme
		return true;
	}

	auto it = std::find_if(themes.begin(), themes.end(), 
		[&themeName](const ThemeInfo& theme) { return theme.name == themeName; });

	if (it == themes.end()) {
		logger::warn("Theme not found: {}", themeName);
		return false;
	}

	if (!it->isValid) {
		logger::warn("Theme is invalid: {}", themeName);
		return false;
	}

	try {
		if (it->themeData.contains("Theme") && it->themeData["Theme"].is_object()) {
			themeSettings = it->themeData["Theme"];
			logger::info("Loaded theme: {} ({})", it->name, it->displayName);
			return true;
		} else {
			logger::warn("Theme file missing 'Theme' object: {}", themeName);
			return false;
		}
	} catch (const std::exception& e) {
		logger::warn("Error loading theme {}: {}", themeName, e.what());
		return false;
	}
}

const ThemeManager::ThemeInfo* ThemeManager::GetThemeInfo(const std::string& themeName) const
{
	auto it = std::find_if(themes.begin(), themes.end(), 
		[&themeName](const ThemeInfo& theme) { return theme.name == themeName; });
	
	return (it != themes.end()) ? &(*it) : nullptr;
}

void ThemeManager::RefreshThemes()
{
	discovered = false;
	DiscoverThemes();
}

std::filesystem::path ThemeManager::GetThemesDirectory() const
{
	return Util::PathHelpers::GetThemesPath();
}

void ThemeManager::CreateDefaultThemeFiles()
{
	auto themesDir = GetThemesDirectory();
	
	try {
		std::filesystem::create_directories(themesDir);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Failed to create themes directory: {}", e.what());
		return;
	}

	// Define default themes as JSON strings (what users would create)
	struct DefaultTheme {
		std::string name;
		std::string content;
	};

	std::vector<DefaultTheme> defaultThemes = {
		{"Default", R"({
	"DisplayName": "Default Dark",
	"Description": "The classic Community Shaders dark theme",
	"Version": "1.0.0",
	"Author": "Community Shaders Team",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.09, 0.09, 0.09, 0.95],
			"Text": [1.0, 1.0, 1.0, 1.0],
			"Border": [0.5, 0.5, 0.5, 0.8]
		}
	}
})"},

		{"Light", R"({
	"DisplayName": "Light Mode",
	"Description": "Clean light theme with dark text",
	"Version": "1.0.0",
	"Author": "Community Shaders Team",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.9, 0.9, 0.9, 0.95],
			"Text": [0.1, 0.1, 0.1, 1.0],
			"Border": [0.3, 0.3, 0.3, 0.8]
		}
	}
})"},

		{"Ocean", R"({
	"DisplayName": "Ocean Blue",
	"Description": "Cool blue tones with subtle gradients",
	"Version": "1.0.0",
	"Author": "Community Shaders Team",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.1, 0.2, 0.4, 0.9],
			"Text": [0.9, 0.95, 1.0, 1.0],
			"Border": [0.3, 0.5, 0.8, 0.8]
		}
	}
})"},

		{"Forest", R"({
	"DisplayName": "Forest Green",
	"Description": "Natural green theme inspired by Skyrim's forests",
	"Version": "1.0.0",
	"Author": "Community Shaders Team",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.1, 0.3, 0.15, 0.9],
			"Text": [0.9, 1.0, 0.9, 1.0],
			"Border": [0.4, 0.7, 0.4, 0.8]
		}
	}
})"},

		{"Mystic", R"({
	"DisplayName": "Mystic Purple",
	"Description": "Magical purple theme with mystical vibes",
	"Version": "1.0.0",
	"Author": "Community Shaders Team",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.2, 0.1, 0.3, 0.9],
			"Text": [0.95, 0.9, 1.0, 1.0],
			"Border": [0.6, 0.4, 0.8, 0.8]
		}
	}
})"},

		{"Amber", R"({
	"DisplayName": "Warm Amber",
	"Description": "Warm amber tones reminiscent of candlelight",
	"Version": "1.0.0",
	"Author": "Community Shaders Team",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.2, 0.15, 0.05, 0.9],
			"Text": [1.0, 0.9, 0.7, 1.0],
			"Border": [0.8, 0.6, 0.3, 0.8]
		}
	}
})"},

		{"HighContrast", R"({
	"DisplayName": "High Contrast",
	"Description": "High contrast theme for accessibility",
	"Version": "1.0.0",
	"Author": "Community Shaders Team",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.0, 0.0, 0.0, 0.95],
			"Text": [1.0, 1.0, 1.0, 1.0],
			"Border": [1.0, 1.0, 1.0, 0.9]
		}
	}
})"}}
	};

	// Create theme files
	for (const auto& theme : defaultThemes) {
		auto themeFile = themesDir / (theme.name + ".json");
		
		// Only create if it doesn't exist (don't overwrite user modifications)
		if (std::filesystem::exists(themeFile)) {
			continue;
		}

		try {
			std::ofstream file(themeFile);
			if (!file.is_open()) {
				logger::warn("Failed to create theme file: {}", themeFile.string());
				continue;
			}

			file << theme.content;
			file.close();
			
			logger::info("Created default theme file: {}", theme.name);
		} catch (const std::exception& e) {
			logger::warn("Error creating theme file {}: {}", theme.name, e.what());
		}
	}
}

std::unique_ptr<ThemeManager::ThemeInfo> ThemeManager::LoadThemeFile(const std::filesystem::path& filePath)
{
	auto themeInfo = std::make_unique<ThemeInfo>();
	themeInfo->name = filePath.stem().string();
	themeInfo->filePath = filePath.string();
	themeInfo->lastModified = GetFileModTime(filePath);

	try {
		std::ifstream file(filePath);
		if (!file.is_open()) {
			logger::warn("Cannot open theme file: {}", filePath.string());
			return themeInfo;
		}

		json themeJson;
		file >> themeJson;
		file.close();

		if (!ValidateThemeData(themeJson)) {
			logger::warn("Invalid theme data in: {}", filePath.string());
			return themeInfo;
		}

		themeInfo->themeData = themeJson;
		
		// Extract metadata
		if (themeJson.contains("DisplayName") && themeJson["DisplayName"].is_string()) {
			themeInfo->displayName = themeJson["DisplayName"];
		} else {
			themeInfo->displayName = themeInfo->name;  // Fallback to filename
		}

		if (themeJson.contains("Description") && themeJson["Description"].is_string()) {
			themeInfo->description = themeJson["Description"];
		}

		if (themeJson.contains("Version") && themeJson["Version"].is_string()) {
			themeInfo->version = themeJson["Version"];
		}

		if (themeJson.contains("Author") && themeJson["Author"].is_string()) {
			themeInfo->author = themeJson["Author"];
		}

		themeInfo->isValid = true;

	} catch (const std::exception& e) {
		logger::warn("Error loading theme file {}: {}", filePath.string(), e.what());
	}

	return themeInfo;
}

bool ThemeManager::ValidateThemeData(const json& themeData) const
{
	try {
		// Must have Theme object
		if (!themeData.contains("Theme") || !themeData["Theme"].is_object()) {
			return false;
		}

		const auto& theme = themeData["Theme"];

		// Basic validation - check for required structure
		// This is a minimal check; the Menu system will handle detailed validation
		if (theme.contains("UseSimplePalette") && theme["UseSimplePalette"].is_boolean()) {
			if (theme["UseSimplePalette"] == true) {
				// Simple palette should have Palette object
				if (!theme.contains("Palette") || !theme["Palette"].is_object()) {
					return false;
				}
			}
		}

		return true;
	} catch (...) {
		return false;
	}
}