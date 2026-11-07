#pragma once

#include <d3d11.h>
#include <mutex>
#include <winrt/base.h>

struct ImVec2;

namespace BackgroundBlur
{
	/**
	 * @brief Initializes blur shaders and GPU resources
	 * @return True if initialization succeeded
	 */
	bool Initialize();

	/**
	 * @brief Renders background blur behind all visible ImGui windows
	 * This is the main entry point - call after ImGui::Render() but before ImGui_ImplDX11_RenderDrawData()
	 */
	void RenderBackgroundBlur();

	/**
	 * @brief Cleans up all blur resources
	 */
	void Cleanup();

	/** @brief Drops the cached source view and texture pointer; the caller must guarantee exclusion. */
	void ReleaseSourceCache();

	/** @brief Turns the blur on or off. */
	void SetEnabled(bool enable);

	/** @brief Scales blur strength from 0 (hidden) to 1 (full); set each frame before RenderBackgroundBlur(). */
	void SetFade(float newFade);

	/** @brief When true, a single fullscreen blur replaces per-window blur (CS editor mode). */
	void SetCSEditorActive(bool active);
}  // namespace BackgroundBlur
