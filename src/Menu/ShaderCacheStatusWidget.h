#pragma once

/**
 * @brief Shader cache status shared by the Settings page's Shaders & Cache section and the Home dashboard.
 */
namespace ShaderCacheStatusWidget
{
	/**
	 * @brief Draws the last build's compile breakdown as a stacked bar across the content width, with a
	 * per-category tooltip. Draws nothing while no shaders have been queued.
	 */
	void DrawBreakdownBar();

	/** @brief One coloured key entry per non-empty bar category with its count, wrapping across the content width. */
	void DrawBreakdownLegend();
}
