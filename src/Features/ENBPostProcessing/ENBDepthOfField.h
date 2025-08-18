#pragma once

#include "Effect.h"
#include <d3d11.h>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class ENBDepthOfField : public Effect
{
public:
	virtual std::string GetName() const override { return "enbdepthoffield.fx"; }

	virtual LPCSTR GetSourceTexture() const override { return "TextureColor"; }

	virtual void Execute(RE::BSGraphics::RenderTargetData& input,
		RE::BSGraphics::RenderTargetData& swap,
		RE::BSGraphics::RenderTargetData& output) override;

	void UpdateEffectVariables();

	// Override Apply to create depth of field-specific textures
	virtual bool Apply() override;
	virtual void Unload() override;

private:
	struct DepthOfFieldTexture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
	};

	std::unordered_map<std::string, DepthOfFieldTexture> dofTextures;
	void CreateDepthOfFieldTextures();
	void UpdateDepthOfFieldVariables();
};