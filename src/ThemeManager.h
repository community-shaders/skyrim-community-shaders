#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

/**
 * @brief Manages hot-swappable theme system for Community Shaders
 *
 * This class handles discovery and loading of theme JSON files from the Themes directory,
 * allowing users to create and modify themes without code changes. Similar to the
 * SettingsOverrideManager but specifically for theme management.
 *
 * Theme files should be placed in: Data\SKSE\Plugins\CommunityShaders\Themes\
 * File format: {ThemeName}.json
 */
class ThemeManager
{
public:
	struct ThemeInfo
	{
		std::string name;          // Filename without extension
		std::string displayName;   // Human-readable name from JSON
		std::string description;   // Theme description from JSON
		std::string filePath;      // Full path to theme file
		json themeData;           // Complete theme settings
		bool isValid = false;     // Whether theme loaded successfully
		
		// Metadata
		std::string version;
		std::string author;
		std::time_t lastModified = 0;
	};

	static ThemeManager* GetSingleton()
	{
		static ThemeManager instance;
		return &instance;
	}

	/**
	 * @brief Discovers all theme files in the themes directory
	 * @return Number of theme files discovered
	 */
	size_t DiscoverThemes();

	/**
	 * @brief Gets list of all discovered themes
	 * @return Vector of theme information
	 */
	const std::vector<ThemeInfo>& GetThemes() const { return themes; }

	/**
	 * @brief Gets theme names for dropdown display
	 * @return Vector of theme names
	 */
	std::vector<std::string> GetThemeNames() const;

	/**
	 * @brief Loads a specific theme by name
	 * @param themeName Name of the theme to load
	 * @param themeSettings Output parameter for loaded theme settings
	 * @return True if theme was loaded successfully
	 */
	bool LoadTheme(const std::string& themeName, json& themeSettings);

	/**
	 * @brief Gets theme info by name
	 * @param themeName Name of the theme
	 * @return Pointer to theme info or nullptr if not found
	 */
	const ThemeInfo* GetThemeInfo(const std::string& themeName) const;

	/**
	 * @brief Refreshes theme discovery (for runtime updates)
	 */
	void RefreshThemes();

	/**
	 * @brief Checks if themes have been discovered
	 */
	bool IsDiscovered() const { return discovered; }

	/**
	 * @brief Gets the themes directory path
	 */
	std::filesystem::path GetThemesDirectory() const;

	/**
	 * @brief Creates default theme files if they don't exist
	 */
	void CreateDefaultThemeFiles();

private:
	ThemeManager() = default;
	~ThemeManager() = default;
	ThemeManager(const ThemeManager&) = delete;
	ThemeManager& operator=(const ThemeManager&) = delete;

	/**
	 * @brief Loads a single theme file
	 * @param filePath Path to the theme file
	 * @return Theme info if successful, nullptr otherwise
	 */
	std::unique_ptr<ThemeInfo> LoadThemeFile(const std::filesystem::path& filePath);

	/**
	 * @brief Validates theme data structure
	 * @param themeData JSON data to validate
	 * @return True if theme data is valid
	 */
	bool ValidateThemeData(const json& themeData) const;

	std::vector<ThemeInfo> themes;
	bool discovered = false;

	// Constants
	static constexpr size_t MAX_THEMES = 100;  // Prevent excessive theme loading
	static constexpr size_t MAX_FILE_SIZE = 1024 * 1024;  // 1MB max theme file size
};