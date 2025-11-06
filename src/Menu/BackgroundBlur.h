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
	 * @brief Creates or recreates blur textures with specified dimensions
	 * @param width Texture width in pixels
	 * @param height Texture height in pixels
	 * @param format Texture format
	 */
	void CreateBlurTextures(UINT width, UINT height, DXGI_FORMAT format);

	/**
	 * @brief Performs two-pass Gaussian blur on source texture
	 * @param sourceTexture Input texture to blur
	 * @param targetRTV Output render target
	 * @param menuMin Top-left corner of menu area (for scissor test)
	 * @param menuMax Bottom-right corner of menu area (for scissor test)
	 */
	void PerformBlur(ID3D11Texture2D* sourceTexture, ID3D11RenderTargetView* targetRTV, ImVec2 menuMin, ImVec2 menuMax);

	/**
	 * @brief Cleans up all blur resources
	 */
	void Cleanup();

	/**
	 * @brief Sets the blur intensity for next render
	 * @param intensity Blur strength (0.0 = disabled, 1.0 = maximum)
	 */
	void SetIntensity(float intensity);

	/**
	 * @brief Gets current blur intensity
	 * @return Current blur intensity value
	 */
	float GetIntensity();

	/**
	 * @brief Checks if blur is enabled
	 * @return True if blur intensity > 0
	 */
	bool IsEnabled();

	/**
	 * @brief Gets current blur texture dimensions
	 * @param outWidth Output width
	 * @param outHeight Output height
	 */
	void GetTextureDimensions(UINT& outWidth, UINT& outHeight);

}  // namespace BackgroundBlur
