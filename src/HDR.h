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

	// Taken from the DirectX Toolkit
	// Copyright (c) Microsoft Corporation.
	// Licensed under the MIT License.
	//
	// http://go.microsoft.com/fwlink/?LinkId=248929
	// HDTV to UHDTV (Rec.709 color primaries into Rec.2020)
	const float c_from709to2020[12] = {
		0.6274040f,
		0.3292820f,
		0.0433136f,
		0.f,
		0.0690970f,
		0.9195400f,
		0.0113612f,
		0.f,
		0.0163916f,
		0.0880132f,
		0.8955950f,
		0.f,
	};

	// DCI-P3-D65 https://en.wikipedia.org/wiki/DCI-P3 to UHDTV (DCI-P3-D65 color primaries into Rec.2020)
	const float c_fromP3D65to2020[12] = {
		0.753845f,
		0.198593f,
		0.047562f,
		0.f,
		0.0457456f,
		0.941777f,
		0.0124772f,
		0.f,
		-0.00121055f,
		0.0176041f,
		0.983607f,
		0.f,
	};

	// HDTV to DCI-P3-D65 (a.k.a. Display P3 or P3D65)
	const float c_from709toP3D65[12] = {
		0.822461969f,
		0.1775380f,
		0.f,
		0.f,
		0.033194199f,
		0.9668058f,
		0.f,
		0.f,
		0.017082631f,
		0.0723974f,
		0.9105199f,
		0.f,
	};
	// End of DXTK

	struct Settings
	{
		bool enableHDR = false;
		// Settings for the DXTK based tonemapper
		bool useDXTonemapping = false;
		uint dxOperator = 0;
		uint dxTransferFunction = 2;
		uint dxColorRotation = 0;
		float dxExposure = 1.0f;
		uint dxPaperWhite = 1000;
		bool dxLinearToPq = false;
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
	HDRDxDataCB
	{
		// linearExposure is .x
		// paperWhiteNits is .y
		// tonemapSelection is .z
		DirectX::XMVECTOR parameters;
		DirectX::XMVECTOR colorRotation[3];
	};

	static_assert((sizeof(HDRDxDataCB) % 16) == 0, "CB size not padded correctly");

	ConstantBuffer* hdrDxDataCB = nullptr;
	ConstantBuffer* hdrDataCB = nullptr;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	ID3D11ComputeShader* hdrDxOutputCS = nullptr;
	ID3D11ComputeShader* GetHDROutputCS();
	ID3D11ComputeShader* GetDxHDROutputCS();

	// Format constants to be used elsewhere
	static constexpr auto BSGraphics_HDR_Format = RE::BSGraphics::Format::kR16G16B16A16_FLOAT;
	static constexpr auto DXGI_HDR_Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static constexpr auto FSR_HDR_Format = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	static constexpr auto FSR_FG_HDR_Format = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	static constexpr auto ReShade_HDR_Format = reshade::api::format::r10g10b10a2_unorm;
};
