#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <imgui.h>

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

	static void SetupImGuiStyle(const class Menu& menu);
	static void ReloadFont(const class Menu& menu, float& cachedFontSize);
	// Returns the effective font size to use. If the user setting is <= 0, a dynamic
	// default based on current screen resolution is returned; otherwise the user value.
	static float ResolveFontSize(const class Menu& menu);


	struct Constants
	{
		// Font size constants
		static constexpr float DEFAULT_SCREEN_HEIGHT = 1080.0f;  // Default screen resolution to use for subsequent calculations
		static constexpr float DEFAULT_FONT_RATIO = 0.025f;      // Default 2.5% of screen height
		static constexpr float MIN_FONT_SIZE = 16.0f;            // ~1.5% @ 1080px height
		static constexpr float MAX_FONT_SIZE = 108.0f;           // 5.0% @ 2160px height
		static constexpr float DEFAULT_FONT_SIZE = 27.0f;

		// Global scale constants
		static constexpr float DEFAULT_GLOBAL_SCALE = 0.0f;      // Default global scale for built-in themes

		// Font configuration constants
		static constexpr int FCONF_OVERSAMPLE_H = 3;              // ImGui default = 2
		static constexpr int FCONF_OVERSAMPLE_V = 2;              // ImGui default = 1
		static constexpr bool FCONF_PIXELSNAP_H = true;           // ImGui default = false
		static constexpr float FCONF_RASTERIZER_MULTIPLY = 1.1f;  // ImGui default = 1.0f

		// Header rendering constants
		static constexpr float HEADER_BASE_TEXT_SCALE = 1.7f;
		static constexpr float HEADER_BASE_ICON_MULTIPLIER = 1.5f;
		static constexpr float HEADER_FALLBACK_TEXT_SCALE = 1.5f;
		static constexpr float DOCKED_ICON_SIZE_MULTIPLIER = 1.25f;
		static constexpr float DOCKED_ICON_SPACING = 8.0f;
		static constexpr float DOCKED_RIGHT_MARGIN = 45.0f;
		static constexpr float WATERMARK_HEIGHT_PERCENT = 0.50f;
		static constexpr float UNDOCKED_ICON_PADDING_REDUCTION = 4.0f;
		static constexpr float DOCKED_ICON_PADDING_REDUCTION = 2.0f;

		// UI Layout constants
		static constexpr float BUTTON_PADDING = 16.0f;
		static constexpr float BUTTON_SPACING = 8.0f;
		static constexpr float OVERLAY_WINDOW_POSITION = 10.0f;
		static constexpr float FONT_CACHE_EPSILON = 0.01f;
		static constexpr float CURSOR_POSITION_PADDING = 5.0f;
		static constexpr float SEPARATOR_THICKNESS = 3.0f;
		static constexpr float UNDOCKED_ICON_ITEM_SPACING = 6.0f;
	};

	static ThemeManager* GetSingleton()
	{
		static ThemeManager instance;
		return &instance;
	}

	// Static UI helper methods
	static void SetupImGuiStyle(const class Menu& menu);
	static void ReloadFont(const class Menu& menu, float& cachedFontSize);

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
	 * @brief Saves current theme settings to a new theme file
	 * @param themeName Name for the new theme file
	 * @param themeSettings Theme settings to save
	 * @param displayName Display name for the theme
	 * @param description Description for the theme
	 * @return True if theme was saved successfully
	 */
	bool SaveTheme(const std::string& themeName, const json& themeSettings, 
	               const std::string& displayName, const std::string& description);

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