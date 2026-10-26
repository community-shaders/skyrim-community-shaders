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
	inline constexpr std::string_view kBehavior = "Behavior";
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
	inline constexpr std::string_view kTesting = "Testing";
	inline constexpr std::string_view kDisableAtBoot = "DisableAtBoot";
}
