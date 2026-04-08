#pragma once

#include "Effect.h"

class ENBLocalTonemapping : public Effect
{
public:
	virtual std::string GetName() const override { return "enblocaltone.fx"; } // Dummy name for compatibility if needed

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;

	virtual bool Apply() override;
	virtual bool Load() override;
	virtual void Save() override;

	void Initialize();
	void RenderImGui();

private:
	struct LuminanceArgs
	{
		float exposure;
		float shadows;
		float highlights;
		uint32_t debugView;
		uint32_t useLegacyACES;
		uint32_t pad1;
		uint32_t pad2;
		uint32_t pad3;
	};

	struct ExposureWeightArgs
	{
		float sigmaSq;
		float offset;
		uint32_t debugView;
		uint32_t padding;
	};

	struct BlendArgs
	{
		float padding[3];
		uint32_t debugView;
	};

	struct BlendLaplacianArgs
	{
		uint32_t resolution[2];
		uint32_t boostLocalContrast;
		uint32_t debugView;
	};

	struct FinalCombineArgs
	{
		float mipPixelSize[4];
		uint32_t resolution[2];
		float exposure;
		uint32_t debugView;
		uint32_t finalizeWithACES;
		uint32_t performSRGBConversion;
		uint32_t pad0;
		uint32_t pad1;
		uint32_t ditherMode;
		uint32_t frameIndex;
		uint32_t useLegacyACES;
		uint32_t pad2;
	};

	struct MipmapArgs
	{
		uint32_t resolution[2];
		float texelSize[2];
	};

	void CreateShaders();
	void CreateTextures(uint32_t width, uint32_t height);

	winrt::com_ptr<ID3D11ComputeShader> luminanceCS;
	winrt::com_ptr<ID3D11ComputeShader> exposureWeightCS;
	winrt::com_ptr<ID3D11ComputeShader> blendCS;
	winrt::com_ptr<ID3D11ComputeShader> blendLaplacianCS;
	winrt::com_ptr<ID3D11ComputeShader> finalCombineCS;
	winrt::com_ptr<ID3D11ComputeShader> mipmapCS;

	winrt::com_ptr<ID3D11Buffer> luminanceCB;
	winrt::com_ptr<ID3D11Buffer> weightCB;
	winrt::com_ptr<ID3D11Buffer> blendCB;
	winrt::com_ptr<ID3D11Buffer> laplacianCB;
	winrt::com_ptr<ID3D11Buffer> finalCombineCB;
	winrt::com_ptr<ID3D11Buffer> mipmapCB;

	struct TexturePyramid
	{
		std::vector<winrt::com_ptr<ID3D11Texture2D>> textures;
		std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> srvs;
		std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> uavs;
		uint32_t width, height, mips;
	};

	TexturePyramid mipsLuminance;
	TexturePyramid mipsWeights;
	TexturePyramid mipsAssemble;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_mipCount = 0;

	// Settings (will be moved to SettingManager)
	int mip = 3;
	int displayMip = 0;
	bool boostLocalContrast = false;
	bool useGaussian = true;
	bool finalizeWithACES = true;
	float exposureValue = 0.75f;
	float shadowsValue = 2.0f;
	float highlightsValue = 4.0f;
	float exposurePreferenceSigma = 4.0f;
	float exposurePreferenceOffset = 0.0f;
};
