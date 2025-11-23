#pragma once

#include "Buffer.h"
#include "PostProcessFeature.h"

struct Border : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Border"; }
	virtual inline std::string GetDesc() const override { return "Add colored border, optionally with depth threshold"; }
	virtual inline bool DrawAfterColorGrading() const override { return true; }
	
	struct Settings
	{
		float3 BorderColor;
        float DepthThreshold;
        float4 Scale;
	} settings;

	struct alignas(16) BorderCB
	{
		float4 BorderColor;
        float4 Scale;
	};
	eastl::unique_ptr<ConstantBuffer> borderCB = nullptr;

    eastl::unique_ptr<Texture2D> texOutput = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> borderCS = nullptr;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
};