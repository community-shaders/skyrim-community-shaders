#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace RE
{
	namespace BSGraphics
	{
		struct RenderTargetData;
	}
}

class Downsampler
{
public:
	static Downsampler& GetSingleton();

	struct DownsampleChain
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		UINT baseWidth;
		UINT baseHeight;
		UINT targetWidth;
		UINT targetHeight;
		UINT targetMipLevel;
		UINT totalMipLevels;
		DXGI_FORMAT format;
	};

	// Initialize the downsampler with shared resources
	void Initialize();

	// Create a downsample chain to achieve target resolution (finds nearest mip level by pixel count)
	DownsampleChain CreateDownsampleChain(UINT baseWidth, UINT baseHeight, UINT targetWidth, UINT targetHeight, DXGI_FORMAT format);

	// Perform downsampling using GenerateMips
	void Downsample(ID3D11ShaderResourceView* source, DownsampleChain& chain);

	// Get the target resolution level from the chain
	ID3D11ShaderResourceView* GetTargetLevel(const DownsampleChain& chain) const;

	// Get a specific mip level from the chain
	ID3D11ShaderResourceView* GetMipLevel(const DownsampleChain& chain, UINT mipLevel) const;

	// Find the best mip level for a target resolution
	UINT FindBestMipLevel(const DownsampleChain& chain, UINT targetWidth, UINT targetHeight) const;

private:
	// Helper methods
	UINT CalculateMipLevels(UINT width, UINT height) const;
	UINT FindNearestMipLevel(UINT baseWidth, UINT baseHeight, UINT targetWidth, UINT targetHeight) const;

	// Cache of SRVs for different mip levels
	mutable std::unordered_map<UINT, ComPtr<ID3D11ShaderResourceView>> mipLevelSRVs;
};