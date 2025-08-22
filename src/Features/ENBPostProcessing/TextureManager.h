#pragma once

#include "ENBTexture.h"
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class TextureManager
{
public:
	static TextureManager& GetSingleton();

	void Initialize();
	ENBTexture* GetCommonTexture(const std::string& name);
	const std::unordered_map<std::string, ENBTexture>& GetAllCommonTextures() const { return commonTextureCache; }

private:
	void CreateCommonTextures();
	static ENBTexture CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName);

	std::unordered_map<std::string, ENBTexture> commonTextureCache;
};