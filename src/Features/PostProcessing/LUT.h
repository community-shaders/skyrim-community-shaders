#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct LUT : PostProcessFeature
{
	virtual inline std::string GetType() const override { return "LUT"; }
	virtual inline std::string GetDesc() const override { return "Look-up table application."; }

	int LutType = -1;  // -1 - null, 0 - 1d luma, 1 - 1d per channel, 2 - 3d in 2d, 3 - 3d

	std::string errMsg = "";
	std::string tempPath = "";

	struct Settings
	{
		std::string LutPath = "";
		float3 InputMin{ 0.f };
		float3 InputMax{ 1.f };
	} settings;

	struct alignas(16) LUTCB
	{
		float3 InputMin;
		float pad;
		float3 InputMax;
		int LutType;
	};
	eastl::unique_ptr<ConstantBuffer> lutCB = nullptr;

	eastl::unique_ptr<Texture2D> texLUT2D = nullptr;
	eastl::unique_ptr<Texture3D> texLUT3D = nullptr;
	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> lutCS = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;
	void inline Clear()
	{
		LutType = -1;
		settings.LutPath = "";
		errMsg = "";
		if (texLUT2D)
			texLUT2D.reset();
		if (texLUT3D)
			texLUT3D.reset();
	}
	void ReadTexture(std::filesystem::path path);

	virtual void Draw(TextureInfo&) override;

	bool firstLoad = true;
};