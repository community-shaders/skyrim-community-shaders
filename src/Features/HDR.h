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
		bool sdrMode = true;           // true = Clamped Linear SDR, false = Linear HDR
		bool convertToGamma = true;    // Apply TrueLinearToGamma conversion
		bool enableTonemapping = false; // Apply Reinhard tonemapping
		
		uint hdrPaperWhite = 250;
		uint hdrPeakNits = 800;
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

	// UI rendering - redirects UI to separate target for proper compositing
	void BeginUIRendering();
	void EndUIRendering();
	bool IsRenderingUI() const { return renderingUI; }

	// Frame Gen style UI buffer - redirects kFRAMEBUFFER.RTV for vanilla UI capture
	void SetUIBuffer();
	void ClearUIBuffer();

	void ApplyHDR();

	void DestroyResources() const;
	void ClearShaderCache();

	XM_ALIGNED_STRUCT(16)
	HDRDataCB
	{
		// parameters0.x = sdrMode (1.0 = clamp to SDR, 0.0 = linear HDR passthrough)
		// parameters0.y = convertToGamma (1.0 = apply gamma, 0.0 = keep linear)
		// parameters0.z = enableTonemapping (1.0 = apply Reinhard, 0.0 = no tonemap)
		// parameters0.w = peakNits (for tonemapping clip point)
		DirectX::XMVECTOR parameters0;
		// parameters1.x = paperWhite (reference white brightness in nits)
		DirectX::XMVECTOR parameters1;
	};

	static_assert((sizeof(HDRDataCB) % 16) == 0, "CB size not padded correctly");

	ConstantBuffer* hdrDataCB = nullptr;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;
	Texture2D* uiTexture = nullptr;  // Separate UI render target for proper compositing

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	ID3D11ComputeShader* GetHDROutputCS();

	// Saved state for UI rendering redirection
	bool renderingUI = false;
	ID3D11RenderTargetView* savedRTV = nullptr;
	ID3D11DepthStencilView* savedDSV = nullptr;
	ID3D11RenderTargetView* savedFramebufferRTV = nullptr;  // Original kFRAMEBUFFER.RTV for restoration

	// Format constants to be used elsewhere
	static constexpr auto BSGraphics_HDR_Format = RE::BSGraphics::Format::kR16G16B16A16_FLOAT;
	static constexpr auto BSGraphics_HDR_R10_Format = RE::BSGraphics::Format::kR10G10B10A2_UNORM;
};
