#pragma once

#include "Menu/SettingsSections.h"

#include <span>

// Forward declaration
class Menu;

/**
 * @brief Draws the Advanced sections of the Settings page (see SettingsPageRenderer): developer tools,
 * logging, shader debugging and A/B testing, plus the compiler controls of Shaders & Cache.
 */
class AdvancedSettingsRenderer
{
public:
	/** @brief Searchable setting labels of the sections drawn here. */
	static std::span<const SettingsSearchEntry> GetSearchIndex();

	/** @brief Compiler thread counts, Dump Shaders and Clear Shader Cache, drawn in the Shaders & Cache section. */
	static void RenderCompilerSection();
	/** @brief Log level, shader defines, and the log and ini dump buttons. */
	static void RenderLoggingSection();
	/** @brief Shader replacement toggles, shader blocking and the active shader list. */
	static void RenderShaderDebugSection();
	/** @brief Developer mode, file watcher, compiler flags, the content store and build statistics. */
	static void RenderDeveloperSection();
	/** @brief A/B testing controls. */
	static void RenderTestingSection();
};
