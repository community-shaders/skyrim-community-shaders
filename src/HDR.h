#pragma once
#include "FidelityFX/host/ffx_types.h"
#include "ffx_api_types.h"
#include "reshade/reshade.hpp"

#include <PostProcess.h>

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
		bool enableHDR = false;

		// Settings for the advanced tonemapper
		bool useAdvancedTonemapping = false;
		uint advOperator = 0;
		float advExposure = 1.0f;
		uint advPaperWhite = 1000;
		uint advMaxNits = 1000;

		// Settings for (old) CS tonemapper
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

	struct alignas(16) HDRDataCB
	{
		float4 HDRData;
	};

	static_assert((sizeof(HDRDataCB) % 16) == 0, "CB size not padded correctly");

	XM_ALIGNED_STRUCT(16)
	HDRAdvDataCB
	{
		// linearExposure is .x
		// paperWhiteNits is .y
		// maxNits is .z
		// tonemapSelection is .w
		DirectX::XMVECTOR parameters;
	};

	static_assert((sizeof(HDRAdvDataCB) % 16) == 0, "CB size not padded correctly");

	ConstantBuffer* hdrAdvDataCB = nullptr;
	ConstantBuffer* hdrDataCB = nullptr;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	ID3D11ComputeShader* hdrAdvOutputCS = nullptr;
	ID3D11ComputeShader* GetHDROutputCS();

	// Format constants to be used elsewhere
	static constexpr auto BSGraphics_HDR_Format = RE::BSGraphics::Format::kR16G16B16A16_FLOAT;
};
