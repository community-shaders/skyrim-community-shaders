#pragma once

// Forward declaration
class Menu;

/**
 * @brief Draws the Advanced sections of the Settings page (see SettingsPageRenderer): developer tools,
 * logging, shader debugging and A/B testing.
 */
class AdvancedSettingsRenderer
{
public:
	/** @brief Log level, shader defines, compiler threads, and the log and ini dump buttons. */
	static void RenderLoggingSection();
	/** @brief Shader dump and replacement toggles, shader blocking and the active shader list. */
	static void RenderShaderDebugSection();
	/** @brief Developer mode, file watcher, compiler flags, the content store and build statistics. */
	static void RenderDeveloperSection();
	/** @brief A/B testing controls. */
	static void RenderTestingSection();
};
