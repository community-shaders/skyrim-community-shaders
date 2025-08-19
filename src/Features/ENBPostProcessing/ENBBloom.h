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

	virtual LPCSTR GetSourceTexture() const override { return "TextureDownsampled"; }

	virtual void Execute() override;

	void UpdateEffectVariables();

	// Override Apply to create bloom-specific textures
	virtual bool Apply() override;
	virtual void Unload() override;

private:
	void CreateBloomTextures();
	void UpdateBloomVariables();
};