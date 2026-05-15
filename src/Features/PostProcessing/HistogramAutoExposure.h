#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct HistogramAutoExposure : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Histogram Auto Exposure"; }
	virtual inline std::string GetDesc() const override { return "Auto exposure/Eye adaptation method that uses histogram to calculate average screen brightness. "
																 "Expects HDR linear RGB inputs."; }
	virtual inline bool DisableInMainLoadingMenu() const override { return true; }

	/// This feature no longer writes to the main texture.
	/// It only computes the adaptation value which is consumed by the Composite pass.
	virtual bool WritesToMainTexture() const override { return false; }

	virtual bool SupportsVR() const override { return true; }

	struct Settings
	{
		float ExposureCompensation = 0.f;

		// auto exposure
		float2 AdaptationRange = { -.5f, .2f };  // EV
		float2 AdaptArea = { .6f, .6f };

		float AdaptSpeed = 1.5f;

		// purkinje
		float PurkinjeStartEV = -1.5f;  // EV
		float PurkinjeMaxEV = -4.f;     // EV
		float PurkinjeStrength = 0.f;
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

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;

	/// Get the adaptation structured buffer SRV (contains a single float: adapted luminance).
	/// Used by the Composite pass to apply exposure.
	ID3D11ShaderResourceView* GetAdaptationSRV() const { return adaptationSB ? adaptationSB->SRV() : nullptr; }

	/// Get the constant buffer containing exposure parameters (for Composite pass).
	ID3D11Buffer* GetConstantBuffer() const { return autoExposureCB ? autoExposureCB->CB() : nullptr; }
};