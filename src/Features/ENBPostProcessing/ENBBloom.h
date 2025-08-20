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

	virtual void Execute() override;

	// Override Apply to create bloom-specific textures
	virtual bool Apply() override;
	virtual void Unload() override;

private:
	void UpdateBloomVariables();
};