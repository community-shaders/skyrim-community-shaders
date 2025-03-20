#pragma once
#include "FidelityFX/host/ffx_types.h"
#include "ffx_api_types.h"
#include "reshade/reshade.hpp"

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
		bool enabled = false;
		uint displayPeakBrightness = 1000;
		uint gameBrightness = 400;
		uint uiBrightness = 400;
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

	void ApplyHDR();

	void DestroyResources() const;
	void ClearShaderCache();

	float4 GetHDRData() const;

	struct alignas(16) HDRDataCB
	{
		float4 HDRData;
	};

	ConstantBuffer* hdrDataCB = nullptr;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	ID3D11ComputeShader* GetHDROutputCS();

	// Format constants to be used elsewhere
	static constexpr auto BSGraphics_HDR_Format = RE::BSGraphics::Format::kR16G16B16A16_FLOAT;
	static constexpr auto DXGI_HDR_Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static constexpr auto FSR_HDR_Format = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	static constexpr auto FSR_FG_HDR_Format = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	static constexpr auto ReShade_HDR_Format = reshade::api::format::r16g16b16a16_float;
};
