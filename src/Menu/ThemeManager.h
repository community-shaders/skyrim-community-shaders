#pragma once

#include <d3d11.h>
#include <filesystem>
#include <imgui.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <winrt/base.h>

using json = nlohmann::json;

/**
 * @brief Manages hot-swappable theme system for Community Shaders menu
 *
 * THEME JSON SCHEMA:
 * ==================
 * Theme files use JSON format with the following structure:
 *
 * {
 *   "DisplayName": "Human-readable theme name",
 *   "Description": "Theme description",
 *   "Version": "1.0.0",
 *   "Author": "Your name",
 *   "Theme": {
 *     "FontSize": 27.0,                    // Base font size (16-108px range)
 *     "FontName": "Jost/Jost-Regular.ttf", // Legacy font path (use FontRoles instead)
 *     "GlobalScale": 0.0,                   // UI scale exponent (-2.0 to 2.0, 0.0=100%)
 *
 *     // Font Role System (4 roles: Body, Heading, Subheading, Subtitle)
 *     "FontRoles": [
 *       { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
 *       { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
 *       { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 },
 *       { "Family": "Jost", "Style": "Regular", "File": "Jost/Jost-Regular.ttf", "SizeScale": 1.0 }
 *     ],
 *
 *     "TooltipHoverDelay": 0.5,            // Seconds before tooltip appears
 *     "ShowActionIcons": true,             // Show icons on action buttons
 *
 *     // Simple color palette (6 key colors)
 *     "Palette": {
 *       "Background": [0.03, 0.03, 0.03, 0.39],  // Window background RGBA
 *       "Text": [1.0, 1.0, 1.0, 1.0],            // Primary text color
 *       "WindowBorder": [0.5, 0.5, 0.5, 0.8],    // Outer window borders
 *       "FrameBorder": [0.4, 0.4, 0.4, 0.7],     // Button/input borders
 *       "Separator": [0.5, 0.5, 0.5, 0.6],       // Divider lines
 *       "ResizeGrip": [0.6, 0.6, 0.6, 0.8]       // Window resize handle
 *     },
 *
 *     // Status indicator colors
 *     "StatusPalette": {
 *       "Disable": [0.5, 0.5, 0.5, 1.0],         // Disabled elements
 *       "Error": [1.0, 0.4, 0.4, 1.0],           // Error messages
 *       "Warning": [1.0, 0.6, 0.2, 1.0],         // Warning messages
 *       "RestartNeeded": [0.4, 1.0, 0.4, 1.0],   // Restart required indicator
 *       "CurrentHotkey": [1.0, 1.0, 0.0, 1.0],   // Active hotkey highlight
 *       "SuccessColor": [0.0, 1.0, 0.0, 1.0],    // Success messages
 *       "InfoColor": [0.2, 0.6, 1.0, 1.0]        // Info messages
 *     },
 *
 *     // Feature header styling
 *     "FeatureHeading": {
 *       "ColorDefault": [0.8, 0.8, 0.8, 1.0],    // Default header color
 *       "ColorHovered": [0.6, 0.6, 0.6, 1.0],    // Hovered header color
 *       "MinimizedFactor": 0.7                    // Alpha multiplier when minimized
 *     },
 *
 *     // Scrollbar transparency
 *     "ScrollbarOpacity": {
 *       "Background": 0.0,                        // Scrollbar track alpha
 *       "Thumb": 0.5,                             // Scroll handle alpha
 *       "ThumbHovered": 0.75,                     // Hovered handle alpha
 *       "ThumbActive": 0.9                        // Dragged handle alpha
 *     },
 *
 *     // ImGui style settings (spacing, rounding, etc.)
 *     "Style": {
 *       "WindowBorderSize": 2.0,
 *       "WindowPadding": [8.0, 8.0],
 *       "WindowRounding": 12.0,
 *       "FrameRounding": 4.0,
 *       // ... see ImGuiStyle for all available fields
 *     },
 *
 *     // Full ImGui color palette (55 colors, overrides simple palette)
 *     "FullPalette": [ [r,g,b,a], [r,g,b,a], ... ]
 *   }
 * }
 *
 * FONT ROLE SYSTEM:
 * =================
 * Replaces legacy single-font system with semantic font roles:
 * - Role 0 (Body):       Default UI text, settings labels
 * - Role 1 (Heading):    Feature section headers
 * - Role 2 (Subheading): Subsection headers
 * - Role 3 (Subtitle):   Secondary text, descriptions
 *
 * Each role can have different font family, style, and size scale.
 * Fonts must exist in Data\SKSE\Plugins\CommunityShaders\Fonts\
 *
 * BLUR SHADER SYSTEM:
 * ===================
 * Separable Gaussian blur (horizontal + vertical passes) rendered at eighth resolution.
 * Hardcoded intensity (0.04) for consistent appearance. Toggle via BackgroundBlurEnabled.
 * Based on Unrimp rendering engine: https://github.com/cofenberg/unrimp
 *
 * MIGRATION FROM OLD CONFIGS:
 * ===========================
 * Legacy "FontName" field still supported for backward compatibility.
 * New themes should use "FontRoles" array instead.
 * Old themes without FontRoles auto-populate with defaults on load.
 *
 * Theme files location: Data\SKSE\Plugins\CommunityShaders\Themes\
 * File naming: {ThemeName}.json (e.g., "Default.json", "DragonBlood.json")
 */
class ThemeManager
{
public:
	struct ThemeInfo
	{
		std::string name;         // Filename without extension
		std::string displayName;  // Human-readable name from JSON
		std::string description;  // Theme description from JSON
		std::string filePath;     // Full path to theme file
		json themeData;           // Complete theme settings
		bool isValid = false;     // Whether theme loaded successfully

		// Metadata
		std::string version;
		std::string author;
		std::time_t lastModified = 0;
	};

	// Returns the effective font size to use. If the user setting is <= 0, a dynamic
	// default based on current screen resolution is returned; otherwise the user value.
	static float ResolveFontSize(const class Menu& menu);

	// Static UI helper methods
	static void SetupImGuiStyle(const class Menu& menu);
	static bool ReloadFont(const class Menu& menu, float& cachedFontSize);
	static void ForceApplyDefaultTheme();  // Force Default.json colors to ImGui (bypass hardcoded defaults)

	struct Constants
	{
		// Font size constants
		static constexpr float DEFAULT_SCREEN_HEIGHT = 1080.0f;  // Default screen resolution to use for subsequent calculations
		static constexpr float DEFAULT_FONT_RATIO = 0.025f;      // Default 2.5% of screen height
		static constexpr float MIN_FONT_SIZE = 16.0f;            // ~1.5% @ 1080px height
		static constexpr float MAX_FONT_SIZE = 108.0f;           // 5.0% @ 2160px height
		static constexpr float DEFAULT_FONT_SIZE = 27.0f;

		// Global scale constants
		static constexpr float DEFAULT_GLOBAL_SCALE = 0.0f;  // Default global scale for built-in themes

		// Font configuration constants
		static constexpr int FCONF_OVERSAMPLE_H = 3;              // ImGui default = 2
		static constexpr int FCONF_OVERSAMPLE_V = 2;              // ImGui default = 1
		static constexpr bool FCONF_PIXELSNAP_H = true;           // ImGui default = false
		static constexpr float FCONF_RASTERIZER_MULTIPLY = 1.1f;  // ImGui default = 1.0f

		// Header rendering constants
		static constexpr float HEADER_BASE_TEXT_SCALE = 1.7f;
		static constexpr float HEADER_BASE_ICON_MULTIPLIER = 1.85f;
		static constexpr float HEADER_FALLBACK_TEXT_SCALE = 1.5f;
		static constexpr float DOCKED_ICON_SIZE_MULTIPLIER = 1.5f;
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
		static constexpr float CURSOR_POSITION_PADDING = 14.0f;
		static constexpr float SEPARATOR_THICKNESS = 3.0f;
		static constexpr float UNDOCKED_ICON_ITEM_SPACING = 6.0f;
		static constexpr float POPUP_BUTTON_WIDTH = 180.0f;
	};

	static ThemeManager* GetSingleton()
	{
		static ThemeManager instance;
		return &instance;
	}

	// Static UI helper methods

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
	static constexpr size_t MAX_THEMES = 100;             // Prevent excessive theme loading
	static constexpr size_t MAX_FILE_SIZE = 1024 * 1024;  // 1MB max theme file size
};