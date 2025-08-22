#include "ENBDownsampler.h"
#include "Globals.h"
#include "Utils/D3D.h"
#include <algorithm>
#include <cmath>
#include <d3dcompiler.h>

ENBDownsampler& ENBDownsampler::GetSingleton()
{
	static ENBDownsampler instance;
	return instance;
}

void ENBDownsampler::Initialize()
{
	if (!CompileShaders()) {
		logger::error("[ENBPP] Failed to compile downsampler shaders");
		return;
	}
	logger::info("[ENBPP] Downsampler initialized with custom shaders");
}

ENBDownsampler::FixedDownsampleTexture ENBDownsampler::CreateFixedDownsampleTexture(DXGI_FORMAT format)
{
	auto device = globals::d3d::device;
	FixedDownsampleTexture fixedTexture;

	// Create 1024x1024 texture with 3 mip levels (1024, 512, 256)
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

	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, fixedTexture.texture.GetAddressOf()));

	// Create RTV for mip 0 (1024x1024)
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	DX::ThrowIfFailed(device->CreateRenderTargetView(fixedTexture.texture.Get(), &rtvDesc, fixedTexture.rtv.GetAddressOf()));

	// Create SRVs for each mip level
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 3;

	srvDesc.Texture2D.MostDetailedMip = 0;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.Get(), &srvDesc, fixedTexture.srvChain.GetAddressOf()));

	srvDesc.Texture2D.MipLevels = 1;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.Get(), &srvDesc, fixedTexture.srv.GetAddressOf()));

	srvDesc.Texture2D.MostDetailedMip = 2;
	DX::ThrowIfFailed(device->CreateShaderResourceView(fixedTexture.texture.Get(), &srvDesc, fixedTexture.srvBlurry.GetAddressOf()));

	// Set debug names
	Util::SetResourceName(fixedTexture.texture.Get(), "ENBDownsampler::FixedTexture (1024x1024, 3 mips)");
	Util::SetResourceName(fixedTexture.rtv.Get(), "ENBDownsampler::FixedTexture RTV");
	Util::SetResourceName(fixedTexture.srvChain.Get(), "ENBDownsampler::FixedTexture SRV Chain");
	Util::SetResourceName(fixedTexture.srv.Get(), "ENBDownsampler::FixedTexture SRV 1024x1024");
	Util::SetResourceName(fixedTexture.srvBlurry.Get(), "ENBDownsampler::FixedTexture SRV 256x256");

	logger::info("[ENBPP] Created fixed downsample texture: 1024x1024 with 3 mips (1024, 512, 256)");

	return fixedTexture;
}

void ENBDownsampler::DownsampleToFixed(ID3D11ShaderResourceView* source, FixedDownsampleTexture& texture)
{
	auto context = globals::d3d::context;

	// Get source texture dimensions
	ComPtr<ID3D11Resource> sourceResource;
	source->GetResource(&sourceResource);
	ComPtr<ID3D11Texture2D> sourceTexture;
	sourceResource.As(&sourceTexture);
	D3D11_TEXTURE2D_DESC sourceDesc;
	sourceTexture->GetDesc(&sourceDesc);

	// Calculate source texel size for the shader
	float sourceTexelSizeX = 1.0f / static_cast<float>(sourceDesc.Width);
	float sourceTexelSizeY = 1.0f / static_cast<float>(sourceDesc.Height);

	// Update constant buffer with source texel size
	struct DownsampleConstants
	{
		float sourceTexelSize[2];  // x = 1/width, y = 1/height
		float padding[2];          // padding to 16-byte alignment
	} constants;

	constants.sourceTexelSize[0] = sourceTexelSizeX;
	constants.sourceTexelSize[1] = sourceTexelSizeY;
	constants.padding[0] = 0.0f;
	constants.padding[1] = 0.0f;

	// Create and update constant buffer (simple approach for now)
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.Usage = D3D11_USAGE_IMMUTABLE;
	cbDesc.ByteWidth = sizeof(constants);
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	D3D11_SUBRESOURCE_DATA cbData = {};
	cbData.pSysMem = &constants;

	ComPtr<ID3D11Buffer> constantBuffer;
	auto device = globals::d3d::device;
	device->CreateBuffer(&cbDesc, &cbData, &constantBuffer);

	// Set render target to the 1024x1024 mip level 0
	context->OMSetRenderTargets(1, texture.rtv.GetAddressOf(), nullptr);

	// Set viewport to 1024x1024
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = 1024.0f;
	viewport.Height = 1024.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Set shaders and resources
	context->PSSetShader(downsamplePS.Get(), nullptr, 0);
	context->PSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
	context->PSSetShaderResources(0, 1, &source);
	context->PSSetSamplers(0, 1, linearSampler.GetAddressOf());

	// Draw fullscreen quad
	context->Draw(4, 0);

	// Clear bindings before GenerateMips
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11Buffer* nullCB = nullptr;
	context->PSSetShaderResources(0, 1, &nullSRV);
	context->PSSetConstantBuffers(0, 1, &nullCB);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	// Generate mips for the remaining levels (512, 256)
	context->GenerateMips(texture.srvChain.Get());
}

ID3D11ShaderResourceView* ENBDownsampler::GetTexture(const FixedDownsampleTexture& texture) const
{
	return texture.srv.Get();
}

ID3D11ShaderResourceView* ENBDownsampler::GetTextureBlurry(const FixedDownsampleTexture& texture) const
{
	return texture.srvBlurry.Get();
}

bool ENBDownsampler::CompileShaders()
{
	auto device = globals::d3d::device;

	// Compile pixel shader
	ComPtr<ID3DBlob> psBlob;
	ComPtr<ID3DBlob> errorBlob;

	auto hr = D3DCompile(
		GetDownsamplePixelShaderSource(),
		strlen(GetDownsamplePixelShaderSource()),
		nullptr,
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		0,
		0,
		&psBlob,
		&errorBlob);

	if (FAILED(hr)) {
		if (errorBlob) {
			logger::error("[ENBPP] Pixel shader compilation failed: {}",
				static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		return false;
	}

	hr = device->CreatePixelShader(
		psBlob->GetBufferPointer(),
		psBlob->GetBufferSize(),
		nullptr,
		&downsamplePS);

	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to create pixel shader");
		return false;
	}

	// Create linear sampler state
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	hr = device->CreateSamplerState(&samplerDesc, &linearSampler);
	if (FAILED(hr)) {
		logger::error("[ENBPP] Failed to create sampler state");
		return false;
	}

	logger::info("[ENBPP] Successfully compiled downsampler shader and created resources");
	return true;
}

const char* ENBDownsampler::GetDownsamplePixelShaderSource()
{
	return R"HLSL(
Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer DownsampleConstants : register(b0)
{
    float2 SourceTexelSize;
};

// Color luminance calculation
namespace Color
{
    float RGBToLuminance(float3 rgb)
    {
        return dot(rgb, float3(0.2126, 0.7152, 0.0722));
    }
}

float4 KarisAverage(float4 a, float4 b, float4 c, float4 d)
{
    float wa = rcp(1.0 + Color::RGBToLuminance(a.rgb));
    float wb = rcp(1.0 + Color::RGBToLuminance(b.rgb));
    float wc = rcp(1.0 + Color::RGBToLuminance(c.rgb));
    float wd = rcp(1.0 + Color::RGBToLuminance(d.rgb));
    float wsum = wa + wb + wc + wd;
    return (a * wa + b * wb + c * wc + d * wd) / wsum;
}

float4 DownsampleCODFirstMip(Texture2D tex, SamplerState samp, float2 uv, float2 out_px_size)
{
    int x, y;
    float4 retval = 0;
    float4 fetches2x2[4];
    float4 fetches3x3[9];

    [unroll] for (x = 0; x < 2; ++x)
        [unroll] for (y = 0; y < 2; ++y)
            fetches2x2[x * 2 + y] = tex.SampleLevel(samp, uv + (int2(x, y) * 2 - 1) * out_px_size, 0);

    [unroll] for (x = 0; x < 3; ++x)
        [unroll] for (y = 0; y < 3; ++y)
            fetches3x3[x * 3 + y] = tex.SampleLevel(samp, uv + (int2(x, y) - 1) * 2 * out_px_size, 0);

    retval += 0.5 * KarisAverage(fetches2x2[0], fetches2x2[1], fetches2x2[2], fetches2x2[3]);

    [unroll] for (x = 0; x < 2; ++x)
        [unroll] for (y = 0; y < 2; ++y)
            retval += 0.125 * KarisAverage(fetches3x3[x * 3 + y], fetches3x3[(x + 1) * 3 + y], fetches3x3[x * 3 + y + 1], fetches3x3[(x + 1) * 3 + y + 1]);

    return retval;
}

struct PS_INPUT { float4 pos : SV_POSITION; float2 txcoord0 : TEXCOORD0; };

float4 main(PS_INPUT input) : SV_TARGET {
    return DownsampleCODFirstMip(SourceTexture, LinearSampler, input.txcoord0, SourceTexelSize);
}
)HLSL";
}