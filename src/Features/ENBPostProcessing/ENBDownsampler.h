#pragma once

#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class ENBDownsampler
{
public:
	static ENBDownsampler& GetSingleton();

	struct FixedDownsampleTexture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srvChain;   // Mip 0 -> Mip 1 -> Mip2
		ComPtr<ID3D11ShaderResourceView> srv;        // Mip 0: 1024x1024
		ComPtr<ID3D11ShaderResourceView> srvBlurry;  // Mip 2: 256x256
		ComPtr<ID3D11RenderTargetView> rtv;
	};

	// Initialize the downsampler with shared resources
	void Initialize();

	// Create fixed 1024x1024 texture with 3 mips (1024, 512, 256)
	FixedDownsampleTexture CreateFixedDownsampleTexture(DXGI_FORMAT format);

	// Perform downsampling using custom shader
	void DownsampleToFixed(ID3D11ShaderResourceView* source, FixedDownsampleTexture& texture);

	// Access to shared downsample texture
	const FixedDownsampleTexture& GetSharedDownsampleTexture() const { return sharedDownsampleTexture; }

	ID3D11ShaderResourceView* GetTexture() const;
	ID3D11ShaderResourceView* GetTextureBlurry() const;

private:
	// Compile the downsample shaders
	bool CompileShaders();

	// Shader source strings
	static const char* GetDownsamplePixelShaderSource();

	// Compiled shader objects
	ComPtr<ID3D11PixelShader> downsamplePS;
	ComPtr<ID3D11SamplerState> linearSampler;

	// Shared downsample texture used by all effects
	FixedDownsampleTexture sharedDownsampleTexture;
};