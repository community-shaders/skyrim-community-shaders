#include "Downsampler.h"
#include "Globals.h"
#include <algorithm>
#include <cmath>

Downsampler& Downsampler::GetSingleton()
{
	static Downsampler instance;
	return instance;
}

void Downsampler::Initialize()
{
	logger::info("Downsampler initialized");
}

Downsampler::DownsampleChain Downsampler::CreateDownsampleChain(UINT baseWidth, UINT baseHeight, UINT targetWidth, UINT targetHeight, DXGI_FORMAT format)
{
	auto device = globals::d3d::device;
	DownsampleChain chain;

	// Calculate total mip levels possible
	UINT totalMipLevels = CalculateMipLevels(baseWidth, baseHeight);

	// Find the mip level that gives us the closest resolution to target by pixel count
	UINT targetMipLevel = FindNearestMipLevel(baseWidth, baseHeight, targetWidth, targetHeight);

	// Calculate actual target resolution at the chosen mip level
	UINT actualTargetWidth = std::max(1U, baseWidth >> targetMipLevel);
	UINT actualTargetHeight = std::max(1U, baseHeight >> targetMipLevel);

	// Create texture with full mip chain
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = baseWidth;
	texDesc.Height = baseHeight;
	texDesc.MipLevels = totalMipLevels;
	texDesc.ArraySize = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, chain.texture.GetAddressOf()));

	// Create SRV for all mip levels (used for GenerateMips)
	D3D11_SHADER_RESOURCE_VIEW_DESC fullChainSrvDesc = {};
	fullChainSrvDesc.Format = format;
	fullChainSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	fullChainSrvDesc.Texture2D.MostDetailedMip = 0;
	fullChainSrvDesc.Texture2D.MipLevels = totalMipLevels;

	DX::ThrowIfFailed(device->CreateShaderResourceView(chain.texture.Get(), &fullChainSrvDesc, chain.fullChainSRV.GetAddressOf()));

	// Create SRV for the specific mip level we want
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = targetMipLevel;
	srvDesc.Texture2D.MipLevels = 1;

	DX::ThrowIfFailed(device->CreateShaderResourceView(chain.texture.Get(), &srvDesc, chain.srv.GetAddressOf()));

	// Store chain properties
	chain.baseWidth = baseWidth;
	chain.baseHeight = baseHeight;
	chain.targetWidth = actualTargetWidth;
	chain.targetHeight = actualTargetHeight;
	chain.targetMipLevel = targetMipLevel;
	chain.totalMipLevels = totalMipLevels;
	chain.format = format;

	logger::info("Created downsample chain: {}x{} -> {}x{} (mip level {}/{})",
		baseWidth, baseHeight,
		actualTargetWidth, actualTargetHeight,
		targetMipLevel, totalMipLevels - 1);

	return chain;
}

void Downsampler::Downsample(ID3D11ShaderResourceView* source, DownsampleChain& chain)
{
	auto context = globals::d3d::context;

	// Copy source to mip level 0 of our chain texture
	ComPtr<ID3D11Resource> sourceResource;
	source->GetResource(sourceResource.GetAddressOf());

	context->CopySubresourceRegion(
		chain.texture.Get(), 0,   // dest texture, mip 0
		0, 0, 0,                  // dest x, y, z
		sourceResource.Get(), 0,  // source texture, mip 0
		nullptr                   // copy entire resource
	);

	// Generate mips automatically using the full chain SRV
	context->GenerateMips(chain.fullChainSRV.Get());
}

ID3D11ShaderResourceView* Downsampler::GetTargetLevel(const DownsampleChain& chain) const
{
	return chain.srv.Get();
}

ID3D11ShaderResourceView* Downsampler::GetMipLevel(const DownsampleChain& chain, UINT mipLevel) const
{
	if (mipLevel >= chain.totalMipLevels) {
		return nullptr;
	}

	// Check cache first
	auto it = mipLevelSRVs.find(mipLevel);
	if (it != mipLevelSRVs.end()) {
		return it->second.Get();
	}

	// Create SRV for this mip level
	auto device = globals::d3d::device;
	ComPtr<ID3D11ShaderResourceView> srv;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = chain.format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = mipLevel;
	srvDesc.Texture2D.MipLevels = 1;

	HRESULT hr = device->CreateShaderResourceView(chain.texture.Get(), &srvDesc, srv.GetAddressOf());
	if (SUCCEEDED(hr)) {
		mipLevelSRVs[mipLevel] = srv;
		return srv.Get();
	}

	return nullptr;
}

UINT Downsampler::FindBestMipLevel(const DownsampleChain& chain, UINT targetWidth, UINT targetHeight) const
{
	return FindNearestMipLevel(chain.baseWidth, chain.baseHeight, targetWidth, targetHeight);
}

UINT Downsampler::CalculateMipLevels(UINT width, UINT height) const
{
	UINT levels = 1;
	while (width > 1 || height > 1) {
		width = std::max(1U, width / 2);
		height = std::max(1U, height / 2);
		levels++;
	}
	return levels;
}

UINT Downsampler::FindNearestMipLevel(UINT baseWidth, UINT baseHeight, UINT targetWidth, UINT targetHeight) const
{
	UINT targetPixelCount = targetWidth * targetHeight;
	UINT bestMipLevel = 0;
	UINT bestPixelDiff = UINT_MAX;

	UINT currentWidth = baseWidth;
	UINT currentHeight = baseHeight;
	UINT mipLevel = 0;

	while (currentWidth >= 1 && currentHeight >= 1) {
		UINT currentPixelCount = currentWidth * currentHeight;
		UINT pixelDiff = (currentPixelCount > targetPixelCount) ?
		                     (currentPixelCount - targetPixelCount) :
		                     (targetPixelCount - currentPixelCount);

		if (pixelDiff < bestPixelDiff) {
			bestPixelDiff = pixelDiff;
			bestMipLevel = mipLevel;
		}

		// If we've found an exact match or gone below target, we can stop
		if (currentPixelCount <= targetPixelCount) {
			break;
		}

		currentWidth = std::max(1U, currentWidth / 2);
		currentHeight = std::max(1U, currentHeight / 2);
		mipLevel++;
	}

	return bestMipLevel;
}