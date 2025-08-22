#pragma once

#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class TextureManager
{
public:
	struct Texture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
	};
	static TextureManager& GetSingleton();

	void Initialize();
	Texture* GetCommonTexture(const std::string& name);
	const std::unordered_map<std::string, Texture>& GetAllCommonTextures() const { return commonTextureCache; }

private:
	void CreateCommonTextures();
	static Texture CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName);

	std::unordered_map<std::string, Texture> commonTextureCache;
};