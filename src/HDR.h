#pragma once

#include "Menu.h"

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
		bool enabled = true;
		uint displayPeakBrightness = 400;
		uint gameBrightness = 200;
		uint uiBrightness = 200;
	};

	Settings settings;
	std::mutex settingsMutex;

	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();
	void SetupResources();
	void UpdateHDRData() const;
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
};
