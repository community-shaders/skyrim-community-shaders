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

	struct DownsampleTexture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srvChain;   // Mip 0 -> Mip 1 -> Mip2
		ComPtr<ID3D11ShaderResourceView> srv;        // Mip 0: 1024x1024
		ComPtr<ID3D11ShaderResourceView> srvBlurry;  // Mip 2: 256x256
		ComPtr<ID3D11RenderTargetView> rtv;
	};

	static TextureManager& GetSingleton();

	void Initialize();
	Texture* GetCommonTexture(const std::string& name);
	const std::unordered_map<std::string, Texture>& GetAllCommonTextures() const { return commonTextureCache; }

	// Downsampled texture methods
	void UpdateDownsampledTexture(ID3D11ShaderResourceView* source);
	ID3D11ShaderResourceView* GetDownsampleTexture() const;
	ID3D11ShaderResourceView* GetDownsampleTextureBlurry() const;

private:
	void CreateCommonTextures();
	void CreateDownsampleResources();
	static Texture CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName);
	static DownsampleTexture CreateDownsampleTexture(DXGI_FORMAT format);
	void DownsampleToFixed(ID3D11ShaderResourceView* source, DownsampleTexture& texture);

	std::unordered_map<std::string, Texture> commonTextureCache;

	// Downsampling resources
	ComPtr<ID3D11VertexShader> downsampleVS;
	ComPtr<ID3D11PixelShader> downsamplePS;
	ComPtr<ID3D11SamplerState> linearSampler;
	DownsampleTexture sharedDownsampleTexture;
};