#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct HistogramAutoExposure : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Histogram Auto Exposure"; }
	virtual inline std::string GetDesc() const override { return "Auto exposure/Eye adaptation method that uses histogram to calculate average screen brightness. "
																 "Expects HDR linear RGB inputs."; }

	// virtual inline bool DrawBeforeUpscaling() const override { return true; }

	virtual bool SupportsVR() { return true; }

	struct Settings
	{
		float ExposureCompensation = 0.f;
		bool EnableToD = false;
		std::array<float, 6> ToDExposure = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
		bool EnableInterior = false;
		float InteriorExposure = 0.f;

		// auto exposure
		float2 AdaptationRange = { -.5f, .2f };  // EV
		std::array<float2, 6> ToDAdaptationRange = { float2{ -.5f, .2f }, float2{ -.5f, .2f }, float2{ -.5f, .2f }, float2{ -.5f, .2f }, float2{ -.5f, .2f }, float2{ -.5f, .2f } };
		float2 InteriorAdaptationRange = { -.5f, .2f };
		float2 AdaptArea = { .6f, .6f };

		float AdaptSpeed = 1.5f;

		// purkinje
		float PurkinjeStartEV = -1.5f;  // EV
		float PurkinjeMaxEV = -4.f;     // EV
		float PurkinjeStrength = 1.f;
	} settings;

	// buffers
	struct alignas(16) AutoExposureCB
	{
		float2 AdaptArea;
		float2 AdaptationRange;
		float AdaptLerp;
		float ExposureCompensation;
		float PurkinjeStartEV;
		float PurkinjeMaxEV;
		float PurkinjeStrength;

		float pad[3];
	};
	std::unique_ptr<ConstantBuffer> autoExposureCB = nullptr;
	std::unique_ptr<StructuredBuffer> histogramSB = nullptr;
	std::unique_ptr<StructuredBuffer> adaptationSB = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> histogramCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> histogramAvgCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> adaptCS = nullptr;

	std::unique_ptr<Texture2D> texAdapt = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
};