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

	void SetEnabled(bool enable);

	/// When true, a single fullscreen blur replaces per-window blur (weather editor mode)
	void SetWeatherEditorActive(bool active);
	bool IsWeatherEditorActive();

}  // namespace BackgroundBlur
