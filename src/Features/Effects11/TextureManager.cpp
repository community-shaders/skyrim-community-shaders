#include "TextureManager.h"

#include <d3dcompiler.h>

#include "EffectManager.h"
#include "Globals.h"
#include "State.h"

namespace
{
	struct CommonTextureSpec
	{
		std::string_view name;
		uint32_t size;
		DXGI_FORMAT format;
	};

	constexpr CommonTextureSpec CommonTextureSpecs[] = {
		{ "TextureHDRTemp", 0, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "TextureHDRTemp2", 0, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "RenderTargetRGBA32", 0, DXGI_FORMAT_R8G8B8A8_UNORM },
		{ "RenderTargetRGBA64", 0, DXGI_FORMAT_R16G16B16A16_UNORM },
		{ "RenderTargetRGBA64F", 0, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "RenderTargetR16F", 0, DXGI_FORMAT_R16_FLOAT },
		{ "RenderTargetR32F", 0, DXGI_FORMAT_R32_FLOAT },
		{ "RenderTargetRGB32F", 0, DXGI_FORMAT_R11G11B10_FLOAT },
		{ "TextureSDRTemp", 0, DXGI_FORMAT_R10G10B10A2_UNORM },
		{ "TextureSDRTemp2", 0, DXGI_FORMAT_R10G10B10A2_UNORM },
		{ "TextureBloom", 1024, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "TextureLens", 0, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "TextureBloomTemp", 1024, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "TextureAdaptation", 1, DXGI_FORMAT_R32_FLOAT },
		{ "TextureAdaptationSwap", 1, DXGI_FORMAT_R32_FLOAT },
		{ "RenderTarget1024", 1024, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "RenderTarget512", 512, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "RenderTarget256", 256, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "RenderTarget128", 128, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "RenderTarget64", 64, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "RenderTarget32", 32, DXGI_FORMAT_R16G16B16A16_FLOAT },
		{ "RenderTarget16", 16, DXGI_FORMAT_R16G16B16A16_FLOAT },
	};
}

TextureManager& TextureManager::GetSingleton()
{
	static TextureManager instance;
	return instance;
}

void TextureManager::Initialize()
{
	CreateDownsampleResources();
}

TextureManager::Texture* TextureManager::FindCommonTexture(const std::string& name)
{
	auto it = commonTextureCache.find(name);
	return it != commonTextureCache.end() ? &it->second : nullptr;
}

TextureManager::Texture* TextureManager::GetCommonTexture(const std::string& name)
{
	if (auto* texture = FindCommonTexture(name))
		return texture;

	for (const auto& spec : CommonTextureSpecs) {
		if (spec.name != name)
			continue;
		const uint32_t width = spec.size ? spec.size : globals::game::graphicsState->screenWidth;
		const uint32_t height = spec.size ? spec.size : globals::game::graphicsState->screenHeight;
		return &commonTextureCache.emplace(name, CreateTexture(width, height, spec.format, "TextureManager::" + name)).first->second;
	}
	return nullptr;
}

void TextureManager::SwapTextures(const std::string& name1, const std::string& name2)
{
	auto it1 = commonTextureCache.find(name1);
	auto it2 = commonTextureCache.find(name2);
	if (it1 != commonTextureCache.end() && it2 != commonTextureCache.end()) {
		std::swap(it1->second, it2->second);
	}
}

TextureManager::Texture TextureManager::CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName)
{
	TextureManager::Texture result;

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

	DX::ThrowIfFailed(globals::d3d::device->CreateTexture2D(&texDesc, nullptr, result.texture.put()));

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	DX::ThrowIfFailed(globals::d3d::device->CreateRenderTargetView(result.texture.get(), &rtvDesc, result.rtv.put()));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(result.texture.get(), &srvDesc, result.srv.put()));

	if (!debugName.empty()) {
		Util::SetResourceName(result.texture.get(), debugName.c_str());
		Util::SetResourceName(result.rtv.get(), (debugName + " RTV").c_str());
		Util::SetResourceName(result.srv.get(), (debugName + " SRV").c_str());
	}

	return result;
}

void TextureManager::CreateDownsampleResources()
{
	auto device = globals::d3d::device;

	// Create linear sampler
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));

	// Create downsample vertex shader
	auto vertexShaderSource = EffectManager::LoadShaderFile("Data\\Shaders\\Effects11\\QuadVS.hlsl");
	if (vertexShaderSource.empty())
		return;

	winrt::com_ptr<ID3DBlob> vertexShaderBlob;
	winrt::com_ptr<ID3DBlob> vertexErrorBlob;

	HRESULT vsResult = D3DCompile(
		vertexShaderSource.data(),
		vertexShaderSource.size(),
		"QuadVS.hlsl",
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		0,
		0,
		vertexShaderBlob.put(),
		vertexErrorBlob.put());

	if (FAILED(vsResult)) {
		if (vertexErrorBlob) {
			logger::error("[TextureManager] Downsample vertex shader compilation failed: {}",
				static_cast<const char*>(vertexErrorBlob->GetBufferPointer()));
		}
		return;
	}

	DX::ThrowIfFailed(device->CreateVertexShader(
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize(),
		nullptr,
		downsampleVS.put()));

	// Create downsample pixel shader
	auto pixelShaderSource = EffectManager::LoadShaderFile("Data\\Shaders\\Effects11\\DownsamplePS.hlsl");
	if (pixelShaderSource.empty())
		return;

	winrt::com_ptr<ID3DBlob> pixelShaderBlob;
	winrt::com_ptr<ID3DBlob> errorBlob;

	HRESULT result = D3DCompile(
		pixelShaderSource.data(),
		pixelShaderSource.size(),
		"DownsamplePS.hlsl",
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		0,
		0,
		pixelShaderBlob.put(),
		errorBlob.put());

	if (FAILED(result)) {
		if (errorBlob) {
			logger::error("[TextureManager] Downsample shader compilation failed: {}",
				static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		return;
	}

	DX::ThrowIfFailed(device->CreatePixelShader(
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize(),
		nullptr,
		downsamplePS.put()));

	// Create shared downsample texture
	sharedDownsampleTexture = CreateDownsampleTexture(DXGI_FORMAT_R11G11B10_FLOAT);
}

TextureManager::DownsampleTexture TextureManager::CreateDownsampleTexture(DXGI_FORMAT format)
{
	auto device = globals::d3d::device;

	DownsampleTexture fixedTexture;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = 1024;
	texDesc.Height = 1024;
	texDesc.MipLevels = 3;  // 1024, 512, 256
	texDesc.ArraySize = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, fixedTexture.texture.put()));

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	DX::ThrowIfFailed(device->CreateRenderTargetView(fixedTexture.texture.get(), &rtvDesc, fixedTexture.rtv.put()));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 3;
	srvDesc.Texture2D.MostDetailedMip = 0;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.get(), &srvDesc, fixedTexture.srvChain.put()));

	srvDesc.Texture2D.MipLevels = 1;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.get(), &srvDesc, fixedTexture.srv.put()));

	srvDesc.Texture2D.MostDetailedMip = 2;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.get(), &srvDesc, fixedTexture.srvBlurry.put()));

	// Set debug names
	Util::SetResourceName(fixedTexture.texture.get(), "TextureManager::DownsampleTexture (1024x1024, 3 mips)");
	Util::SetResourceName(fixedTexture.rtv.get(), "TextureManager::DownsampleTexture RTV");
	Util::SetResourceName(fixedTexture.srvChain.get(), "TextureManager::DownsampleTexture SRV Chain");
	Util::SetResourceName(fixedTexture.srv.get(), "TextureManager::DownsampleTexture SRV 1024x1024");
	Util::SetResourceName(fixedTexture.srvBlurry.get(), "TextureManager::DownsampleTexture SRV 256x256");

	logger::info("[TextureManager] Created downsample texture: 1024x1024 with 3 mips (1024, 512, 256)");

	return fixedTexture;
}

void TextureManager::DownsampleToFixed(ID3D11ShaderResourceView* source, DownsampleTexture& texture)
{
	if (!source || !texture.rtv || !downsampleVS || !downsamplePS || !linearSampler || !texture.srvChain) {
		return;
	}

	auto context = globals::d3d::context;

	D3D11_VIEWPORT viewport = {};
	viewport.Width = 1024.0f;
	viewport.Height = 1024.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	auto& effectManager = EffectManager::GetSingleton();
	context->RSSetState(effectManager.rasterizerState.get());
	context->OMSetBlendState(effectManager.blendState.get(), nullptr, 0xFFFFFFFF);
	context->OMSetDepthStencilState(nullptr, 0);

	context->VSSetShader(downsampleVS.get(), nullptr, 0);

	ID3D11SamplerState* samplerArray[] = { linearSampler.get() };
	context->PSSetSamplers(0, 1, samplerArray);

	ID3D11RenderTargetView* rtvArray[] = { texture.rtv.get() };
	context->OMSetRenderTargets(1, rtvArray, nullptr);
	context->PSSetShaderResources(0, 1, &source);
	context->PSSetShader(downsamplePS.get(), nullptr, 0);
	globals::profiler->BeginPass("Effects11::Downsample");
	context->Draw(4, 0);
	globals::profiler->EndPass();

	context->GenerateMips(texture.srvChain.get());
}

void TextureManager::UpdateDownsampledTexture(ID3D11ShaderResourceView* source)
{
	DownsampleToFixed(source, sharedDownsampleTexture);
}

ID3D11ShaderResourceView* TextureManager::GetDownsampleTexture() const
{
	return sharedDownsampleTexture.srv.get();
}

ID3D11ShaderResourceView* TextureManager::GetDownsampleTextureBlurry() const
{
	return sharedDownsampleTexture.srvBlurry.get();
}