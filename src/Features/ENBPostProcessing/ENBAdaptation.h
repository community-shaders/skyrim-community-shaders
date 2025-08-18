#pragma once

#include "Effect.h"
#include "Downsampler.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

class ENBAdaptation : public Effect
{
public:
	virtual std::string GetName() const override { return "enbadaptation.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute(RE::BSGraphics::RenderTargetData& input,
		RE::BSGraphics::RenderTargetData& swap,
		RE::BSGraphics::RenderTargetData& output) override;

	void UpdateEffectVariables();

	// Override Apply to create adaptation-specific textures
	bool Apply() override;
	void Unload() override;

private:
	struct AdaptationTexture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
	};

	std::unordered_map<std::string, AdaptationTexture> adaptationTextures;
	
	void CreateAdaptationTextures();
	void UpdateAdaptationVariables();
};