#pragma once

#include "Buffer.h"
#include "PostProcessFeature.h"

struct Camera : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Camera"; }
	virtual inline std::string GetDisplayName() const override { return T("feature.post_processing.camera.name", "Camera"); }
	virtual inline std::string GetDesc() const override { return T("feature.post_processing.camera.description", "Lens distortion, chromatic aberration, peripheral softness, and film grain."); }
	virtual inline bool DrawAfterColorGrading() const override { return true; }

	struct Settings
	{
		float LensDistortion = 0.12f;
		float LensFOV = 90.0f;
		float3 ChromaticAberration = { -1.6f, -0.8f, 0.0f };
		float AberrationFalloff = 3.5f;
		float BlurRadius = 25.0f;
		float BlurStrength = 1.0f;
		float BlurFalloff = 10.0f;
		float GrainIntensity = 0.08f;
		float GrainSize = 1.25f;
		float GrainColor = 0.15f;
	} settings;

	struct alignas(16) CameraCB
	{
		float4 Screen;
		float4 Lens;
		float4 Aberration;
		float4 Softness;
		float4 Grain;
	};

	eastl::unique_ptr<ConstantBuffer> cameraCB = nullptr;

	eastl::unique_ptr<Texture2D> texLens = nullptr;
	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	winrt::com_ptr<ID3D11PixelShader> cameraPS = nullptr;
	winrt::com_ptr<ID3D11PixelShader> finishPS = nullptr;

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
