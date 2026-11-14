#pragma once

#include "Ndf.h"

struct LocalNdfManager
{
	void SetupResources();
	void CompileShaders(Util::AsyncShaderSet& a_shaders);
	bool Update(const NdfSettings& settings, TextureManager& textures, const std::string& worldspace);
	void DrawSettings(NdfSettings& settings, const std::string& worldspace, float2 cameraMeters);
	void DrawDebug(TextureManager& textures, float scale);
	void RefreshAssets();
	static std::filesystem::path AssetPath(const std::string& asset);
	float4 GetRect() const { return rect; }
	float4 GetAltitude() const { return altitude; }
	NdfTextureSet GetTextures() const;
	ID3D11ShaderResourceView* GetMaximum() const { return maximum ? maximum->srv.get() : nullptr; }
	ID3D11ShaderResourceView* GetWeights() const { return weights ? weights->SRV() : nullptr; }

private:
	struct Parameters
	{
		float4 rect;
		float4 centerSize;
		float4 rotationWeights;
		float4 heightFeather;
		float4 altitude;
		uint32_t hasMask;
		uint32_t modelingAlpha;
		uint32_t dimension;
		uint32_t modelingBlend;
	};
	static_assert(sizeof(Parameters) == 96);
	eastl::unique_ptr<Texture2D> height, modeling, maximum;
	eastl::unique_ptr<StructuredBuffer> weights;
	std::vector<std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 3>> targets;
	std::vector<std::array<eastl::unique_ptr<Texture2D>, 3>> previews;
	std::vector<float4> layerWeights;
	eastl::unique_ptr<ConstantBuffer> cb;
	winrt::com_ptr<ID3D11ComputeShader> program;
	winrt::com_ptr<ID3D11SamplerState> sampler;
	std::vector<std::string> assets;
	std::vector<std::string> activeAssets;
	std::vector<std::string> rejectedAssets;
	std::string key;
	uint64_t textureRevision = 0;
	float4 rect = {};
	float4 altitude = {};
	float effectiveTexelSize = 0.f;
	bool scanned = false;
};
