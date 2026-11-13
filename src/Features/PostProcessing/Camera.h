#pragma once

#include "Buffer.h"
#include "PostProcessFeature.h"

struct Camera : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Camera"; }
	virtual inline std::string GetDisplayName() const override { return T("feature.post_processing.camera.name", "Camera"); }
	virtual inline std::string GetDesc() const override { return T("feature.post_processing.camera.description", "Camera effects including fisheye, chromatic aberration, and film grain."); }
	virtual inline bool DrawAfterColorGrading() const override { return true; }

	struct Settings
	{
		// Fisheye
		bool UseFE = false;
		uint8_t pad[3];
		float FEFoV = 90.0f;
		float FECrop = 0.0f;

		// Chromatic aberration
		float CAStrength = 0.04f;

		// Noise
		float NoiseStrength = 0.08f;
		int NoiseType = 0;
	} settings;

	struct alignas(16) CameraCB
	{
		float FEFoV;
		float FECrop;
		float CAStrength;
		float NoiseStrength;
		int NoiseType;
		float2 res;
		bool UseFE;
		uint8_t pad[3];
	};

	eastl::unique_ptr<ConstantBuffer> cameraCB = nullptr;

	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	winrt::com_ptr<ID3D11PixelShader> cameraPS = nullptr;

	winrt::com_ptr<ID3D11SamplerState> colorSampler = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileRasterShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
};
