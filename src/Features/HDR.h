#pragma once

#include "Buffer.h"

#include <DirectXMath.h>
#include <dxgi.h>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class HDR
{
public:
	static HDR* GetSingleton()
	{
		static HDR singleton;
		return &singleton;
	}

	static std::string GetShortName() { return "High Dynamic Range"; }

	struct Settings
	{
		bool enableHDR = false;        // false = vanilla SDR, true = HDR output
		uint hdrPaperWhite = 203;      // Reference white brightness in nits for HDR
		uint hdrPeakNits = 1000;       // Maximum display brightness in nits for HDR
		float hdrUIBrightness = 1.0f;  // UI brightness multiplier (1.0 = SDR equivalent)
	};

	Settings settings;
	std::mutex settingsMutex;

	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();

	void SetupResources();
	void UpdateHDRData() const;
	void UpdateHDRMetadata() const;
	void UpdateSwapChainColorSpace() const;

	// UI rendering - redirects UI to separate target for proper compositing
	void BeginUIRendering();
	void EndUIRendering();
	bool IsRenderingUI() const { return renderingUI; }

	// Frame Gen style UI buffer - redirects kFRAMEBUFFER.RTV for vanilla UI capture
	void SetUIBuffer();
	void ClearUIBuffer();

	// Scale UI brightness in uiBufferWrapped for SDR Frame Gen (FidelityFX composites gamma UI over gamma scene)
	// For HDR, UI compositing is handled in ApplyHDR to ensure correct gamma-space blending
	void ScaleUIBrightnessForFG();

	void ApplyHDR();

	void DestroyResources() const;
	void ClearShaderCache();

	XM_ALIGNED_STRUCT(16)
	HDRDataCB
	{
		// parameters0.x = enableHDR (1.0 = HDR output with PQ, 0.0 = SDR output with gamma)
		// parameters0.y = paperWhite (reference white brightness in nits for HDR)
		// parameters0.z = peakNits (maximum display brightness in nits for HDR)
		// parameters0.w = skipUIComposite (1.0 = FG handles UI, skip our compositing)
		DirectX::XMVECTOR parameters0;
		// parameters1.x = uiBrightness (UI brightness multiplier)
		// parameters1.y = isSceneLinear (1.0 = Linear Lighting active, scene already linear)
		DirectX::XMVECTOR parameters1;
	};

	static_assert((sizeof(HDRDataCB) % 16) == 0, "CB size not padded correctly");

	ConstantBuffer* hdrDataCB = nullptr;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;
	Texture2D* uiTexture = nullptr;  // Separate UI render target for proper compositing

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	ID3D11ComputeShader* GetHDROutputCS();

	ID3D11ComputeShader* uiBrightnessCS = nullptr;
	ID3D11ComputeShader* GetUIBrightnessCS();

	static bool DetectHDRDisplay();
	static bool isHDRMonitor;

	// Saved state for UI rendering redirection
	bool renderingUI = false;
	ID3D11RenderTargetView* savedRTV = nullptr;
	ID3D11DepthStencilView* savedDSV = nullptr;
	ID3D11RenderTargetView* savedFramebufferRTV = nullptr;  // Original kFRAMEBUFFER.RTV for restoration

	// Format constants to be used elsewhere
	static constexpr auto BSGraphics_HDR_Format = RE::BSGraphics::Format::kR16G16B16A16_FLOAT;
	static constexpr auto BSGraphics_HDR_R10_Format = RE::BSGraphics::Format::kR10G10B10A2_UNORM;
};
