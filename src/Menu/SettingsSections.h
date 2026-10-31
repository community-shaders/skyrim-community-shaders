#pragma once

#include <string_view>

/**
 * @brief Canonical, untranslated IDs of the Settings page sections.
 *
 * Page logic and Menu::Settings::LastSettingsSection use these rather than the translated titles, so they
 * work in every locale and survive a language change.
 */
namespace SettingsSectionIds
{
	// General
	inline constexpr std::string_view kShadersCache = "ShadersCache";
	inline constexpr std::string_view kKeybindings = "Keybindings";
	inline constexpr std::string_view kLanguage = "Language";
	// Appearance
	inline constexpr std::string_view kTheme = "Theme";
	inline constexpr std::string_view kFonts = "Fonts";
	inline constexpr std::string_view kColors = "Colors";
	inline constexpr std::string_view kStyling = "Styling";
	inline constexpr std::string_view kHeaderEffects = "HeaderEffects";
	// Advanced
	inline constexpr std::string_view kDeveloper = "Developer";
	inline constexpr std::string_view kLogging = "Logging";
	inline constexpr std::string_view kShaderDebug = "ShaderDebug";
	inline constexpr std::string_view kActiveShaders = "ActiveShaders";
	inline constexpr std::string_view kTesting = "Testing";
	inline constexpr std::string_view kDisableAtBoot = "DisableAtBoot";
}

/**
 * @brief One searchable setting label of a Settings section. Each section renderer declares a static index of
 * these next to its draw functions; the page's search resolves the labels through T(), so it works in every locale.
 */
struct SettingsSearchEntry
{
	std::string_view sectionId;  ///< SettingsSectionIds value of the section that draws the setting
	const char* labelKey;        ///< Translation key of the label, or nullptr when the label is not translated
	const char* fallback;        ///< English label, also matched so English guides keep working in other locales
};
