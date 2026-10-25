#pragma once

#include <functional>
#include <string>

// Forward declaration
class Menu;

/**
 * @brief Renders the Advanced Settings page of the in-game menu.
 *
 * Provides tabbed sections for developer tools, shader debugging, logging
 * configuration, disable-at-boot toggles, and A/B testing controls.
 */
class AdvancedSettingsRenderer
{
public:
	/**
	 * @brief Renders the full Advanced Settings tab bar with all sub-sections.
	 *
	 * Draws a tab bar containing Developer, Disable at Boot, Logging,
	 * Shader Debug, and Testing tabs.
	 *
	 * @param drawDisableAtBootSettings Callback that renders the per-feature
	 *        disable-at-boot checkboxes (provided by the Menu class).
	 */
	static void RenderAdvancedSettings(
		const std::function<void()>& drawDisableAtBootSettings);

	/** @brief Log level, shader defines, compiler threads, and the log and ini dump buttons. */
	static void RenderLoggingSection();
	/** @brief Shader dump and replacement toggles, shader blocking and the active shader list. */
	static void RenderShaderDebugSection();
	/** @brief Developer mode, file watcher, compiler flags, the content store and build statistics. */
	static void RenderDeveloperSection();
	/** @brief A/B testing controls. */
	static void RenderTestingSection();

private:
	static void RenderDisableAtBootSection(const std::function<void()>& drawDisableAtBootSettings);
};