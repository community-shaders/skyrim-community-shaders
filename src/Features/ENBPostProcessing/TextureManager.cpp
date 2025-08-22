#include "TextureManager.h"
#include "State.h"
#include "Utils/D3D.h"

TextureManager& TextureManager::GetSingleton()
{
	static TextureManager instance;
	return instance;
}

void TextureManager::Initialize()
{
	CreateCommonTextures();
}

ENBTexture* TextureManager::GetCommonTexture(const std::string& name)
{
	auto it = commonTextureCache.find(name);
	if (it != commonTextureCache.end()) {
		return &it->second;
	}
	return nullptr;
}

void TextureManager::CreateCommonTextures()
{
	auto state = globals::state;
	UINT screenWidth = static_cast<UINT>(state->screenSize.x);
	UINT screenHeight = static_cast<UINT>(state->screenSize.y);

	commonTextureCache.insert({ "TextureHDRTemp", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureHDRTemp") });
	commonTextureCache.insert({ "TextureHDRTemp2", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureHDRTemp2") });

	commonTextureCache.insert({ "RenderTargetRGBA32", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R8G8B8A8_UNORM, "TextureManager::RenderTargetRGBA32") });
	commonTextureCache.insert({ "RenderTargetRGBA64", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_UNORM, "TextureManager::RenderTargetRGBA64") });
	commonTextureCache.insert({ "RenderTargetRGBA64F", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::RenderTargetRGBA64F") });
	commonTextureCache.insert({ "RenderTargetR16F", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R16_FLOAT, "TextureManager::RenderTargetR16F") });
	commonTextureCache.insert({ "RenderTargetR32F", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R32_FLOAT, "TextureManager::RenderTargetR32F") });
	commonTextureCache.insert({ "RenderTargetRGB32F", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R11G11B10_FLOAT, "TextureManager::RenderTargetRGB32F") });

	commonTextureCache.insert({ "TextureSDRTemp", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R10G10B10A2_UNORM, "TextureManager::TextureSDRTemp") });
	commonTextureCache.insert({ "TextureSDRTemp2", CreateTexture(screenWidth, screenHeight, DXGI_FORMAT_R10G10B10A2_UNORM, "TextureManager::TextureSDRTemp2") });

	commonTextureCache.insert({ "TextureBloom", CreateTexture(1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureBloom") });
	commonTextureCache.insert({ "TextureLens", CreateTexture(1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureLens") });

	commonTextureCache.insert({ "TextureBloomLensTemp", CreateTexture(1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::TextureBloomLensTemp") });

	commonTextureCache.insert({ "TextureAdaptation", CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "TextureManager::TextureAdaptation") });
	commonTextureCache.insert({ "TextureAdaptationSwap", CreateTexture(1, 1, DXGI_FORMAT_R32_FLOAT, "TextureManager::TextureAdaptationSwap") });

	// Create fixed-size render targets for bloom/lens
	std::vector<std::pair<std::string, UINT>> fixedSizes = {
		{ "RenderTarget1024", 1024 },
		{ "RenderTarget512", 512 },
		{ "RenderTarget256", 256 },
		{ "RenderTarget128", 128 },
		{ "RenderTarget64", 64 },
		{ "RenderTarget32", 32 },
		{ "RenderTarget16", 16 }
	};

	for (auto& [name, size] : fixedSizes) {
		commonTextureCache[name] = CreateTexture(size, size, DXGI_FORMAT_R16G16B16A16_FLOAT, "TextureManager::" + name);
	}
}

ENBTexture TextureManager::CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName)
{
	ENBTexture result;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	DX::ThrowIfFailed(globals::d3d::device->CreateTexture2D(&texDesc, nullptr, result.texture.GetAddressOf()));

	if (!debugName.empty()) {
		result.texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(debugName.length()), debugName.c_str());
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	DX::ThrowIfFailed(globals::d3d::device->CreateRenderTargetView(result.texture.Get(), &rtvDesc, result.rtv.GetAddressOf()));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(result.texture.Get(), &srvDesc, result.srv.GetAddressOf()));

	return result;
}