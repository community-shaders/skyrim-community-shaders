#pragma once

#include "Buffer.h"
#include "PostProcessFeature.h"

struct Vignette : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Vignette"; }
	virtual inline std::string GetDesc() const override { return "Simulates natural vignetting due to angled rays impinging on the film or sensor array."; }
	virtual inline bool DrawAfterColorGrading() const override { return true; }
	
	struct Settings
	{
		float FocalLength = 1.f;
		float Anamorphism = 1.f;
		float Power = 3.f;
	} settings;

	struct alignas(16) VignetteCB
	{
		Settings settings;
		float AspectRatio;

		float2 RcpDynRes;
		float pad[2];
	};
	eastl::unique_ptr<ConstantBuffer> vignetteCB = nullptr;

	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> vignetteCS = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
};