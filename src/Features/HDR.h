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

	bool hdrDisplayDetected = false;
	static bool DetectHDRDisplayEarly(IDXGIAdapter* adapter);
	static bool DetectHDRDisplay();

	struct Settings
	{
		bool enableHDR = false;
		bool vanillaEyeAdaptation = true;
		bool vanillaBloom = true;
		bool bypassTonemapping = false;

		float exposure = 1.0f;
		float highlights = 1.0f;
		float shadows = 1.0f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float dechroma = 0.0f;
		float hueCorrectionStrength = 0.0f;

		uint paperWhite = 400;
		uint peakNits = 10000;
	};

	bool enabledSaveLater = false;

	Settings settings;
	std::mutex settingsMutex;

	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();

	void SetupResources();
	void UpdateHDRData() const;
	bool SetSwapChainColorSpace(bool enableHDR);
	
	// UI rendering in HDR - redirects UI to separate target for proper compositing
	void BeginUIRendering();
	void EndUIRendering();
	void CompositeUI();  // Composites UI onto HDR scene in linear space
	bool IsRenderingUI() const { return renderingUI; }

	void ApplyHDR();

	void DestroyResources() const;
	void ClearShaderCache();

	XM_ALIGNED_STRUCT(16)
	HDRDataCB
	{
		// parameters0.x = paperWhite
		// parameters0.y = peakNits
		// parameters0.z = exposure
		// parameters0.w = unused
		DirectX::XMVECTOR parameters0;
		// parameters1.x = highlights
		// parameters1.y = shadows
		// parameters1.z = contrast
		// parameters1.w = saturation
		DirectX::XMVECTOR parameters1;
		// parameters2.x = dechroma
		// parameters2.y = hueCorrectionStrength
		// parameters2.z = vanillaEyeAdaptation (1.0 = enabled)
		// parameters2.w = vanillaBloom (1.0 = enabled)
		DirectX::XMVECTOR parameters2;
		// parameters3.x = bypassTonemapping (1.0 = enabled)
		// parameters3.y = hdrMode (1.0 = HDR display detected)
		// parameters3.zw = unused
		DirectX::XMVECTOR parameters3;
	};

	static_assert((sizeof(HDRDataCB) % 16) == 0, "CB size not padded correctly");

	ConstantBuffer* hdrDataCB = nullptr;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;
	Texture2D* uiTexture = nullptr;  // Separate UI render target for HDR compositing

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	ID3D11ComputeShader* sdrOutputCS = nullptr;
	ID3D11ComputeShader* uiCompositeCS = nullptr;
	ID3D11ComputeShader* GetHDROutputCS();
	ID3D11ComputeShader* GetSDROutputCS();
	ID3D11ComputeShader* GetUICompositeCS();

	// Saved state for UI rendering redirection
	bool renderingUI = false;
	ID3D11RenderTargetView* savedRTV = nullptr;
	ID3D11DepthStencilView* savedDSV = nullptr;

	// Format constants to be used elsewhere
	static constexpr auto BSGraphics_HDR_Format = RE::BSGraphics::Format::kR16G16B16A16_FLOAT;
	static constexpr auto BSGraphics_HDR_R10_Format = RE::BSGraphics::Format::kR10G10B10A2_UNORM;
};
