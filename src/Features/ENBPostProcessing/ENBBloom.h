#pragma once

#include "Downsampler.h"
#include "Effect.h"
#include <d3d11.h>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class ENBBloom : public Effect
{
public:
	virtual std::string GetName() const override { return "enbbloom.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute(RE::BSGraphics::RenderTargetData& input,
		RE::BSGraphics::RenderTargetData& swap,
		RE::BSGraphics::RenderTargetData& output) override;

	void UpdateEffectVariables();

	// Override Apply to create bloom-specific textures
	virtual bool Apply() override;
	virtual void Unload() override;

private:
	struct BloomTexture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
	};

	std::unordered_map<std::string, BloomTexture> bloomTextures;

	void CreateBloomTextures();
	void UpdateBloomVariables();
};