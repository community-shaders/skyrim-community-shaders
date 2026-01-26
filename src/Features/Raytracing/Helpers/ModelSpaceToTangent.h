#pragma once

#include "PCH.h"

#include "Utils/D3D.h"
#include <d3d11_4.h>

/*float4 Position : POSITION0;
float2 TexCoord0 : TEXCOORD0;
float4 Normal : NORMAL0;
float4 Tangent : TANGENT0;
float4 Color : COLOR0;
float4 Bitangent : BINORMAL0;
float4 LandBlendWeights1 : TEXCOORD1;
float4 LandBlendWeights2 : TEXCOORD2;*/

struct ModelSpaceToTangent
{
	winrt::com_ptr<ID3D11VertexShader> vertexShader = nullptr;
	winrt::com_ptr<ID3D11PixelShader> pixelShader = nullptr;

	winrt::com_ptr<ID3D11InputLayout> inputLayout = nullptr;

	winrt::com_ptr<ID3D11SamplerState> samplerState = nullptr;

	winrt::com_ptr<ID3D11DepthStencilState> depthStencilState = nullptr;

	winrt::com_ptr<ID3D11RasterizerState> rasterState = nullptr;

	winrt::com_ptr<ID3D11BlendState> blendState = nullptr;

	struct UnpackedVertex
	{
		float3 position;
		half2 texcoord;
		float3 normal;
		float3 tangent;
		uint32_t color;
		float3 binormal;

		UnpackedVertex& operator=(const Vertex& src)
		{
			position = src.Position;
			texcoord = src.Texcoord0;
			normal = src.Normal;
			tangent = src.Tangent;
			color = src.Color.packed;
			binormal = src.Bitangent;
			return *this;
		}
	};

	ModelSpaceToTangent()
	{
		std::vector<D3D11_INPUT_ELEMENT_DESC> inputDesc = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		if (auto rawPtr = reinterpret_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\ModelSpaceToTangent.hlsl", {}, "vs_5_0", "vertex", inputDesc, inputLayout.put())); rawPtr)
			vertexShader.attach(rawPtr);

		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\ModelSpaceToTangent.hlsl", {}, "ps_5_0", "pixel")); rawPtr)
			pixelShader.attach(rawPtr);

		auto device = globals::d3d::device;

		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.MipLODBias = 0.0f;
		sampDesc.MaxAnisotropy = 1;
		sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		sampDesc.BorderColor[0] = 0;
		sampDesc.BorderColor[1] = 0;
		sampDesc.BorderColor[2] = 0;
		sampDesc.BorderColor[3] = 0;
		sampDesc.MinLOD = 0;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

		DX::ThrowIfFailed(device->CreateSamplerState(&sampDesc, samplerState.put()));

		D3D11_DEPTH_STENCIL_DESC dsDesc{};
		dsDesc.DepthEnable = FALSE;
		dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		dsDesc.StencilEnable = FALSE;

		DX::ThrowIfFailed(device->CreateDepthStencilState(&dsDesc, depthStencilState.put()));

		D3D11_RASTERIZER_DESC rsDesc{};
		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.CullMode = D3D11_CULL_NONE;  // IMPORTANT
		rsDesc.FrontCounterClockwise = FALSE;
		rsDesc.DepthBias = 0;
		rsDesc.DepthBiasClamp = 0.0f;
		rsDesc.SlopeScaledDepthBias = 0.0f;
		rsDesc.DepthClipEnable = TRUE;
		rsDesc.ScissorEnable = FALSE;
		rsDesc.MultisampleEnable = FALSE;
		rsDesc.AntialiasedLineEnable = FALSE;

		device->CreateRasterizerState(&rsDesc, rasterState.put());

		D3D11_BLEND_DESC blendDesc{};
		blendDesc.AlphaToCoverageEnable = FALSE;
		blendDesc.IndependentBlendEnable = FALSE;
		blendDesc.RenderTarget[0].BlendEnable = FALSE;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		device->CreateBlendState(&blendDesc, blendState.put());
	}

	void Setup(ID3D11Texture2D* texture) const
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texture->GetDesc(&texDesc);

		auto context = globals::d3d::context;

		context->OMSetDepthStencilState(depthStencilState.get(), 0);

		context->RSSetState(rasterState.get());

		float blendFactor[4] = { 0, 0, 0, 0 };
		context->OMSetBlendState(blendState.get(), blendFactor, 0xffffffff);

		context->VSSetShader(vertexShader.get(), nullptr, 0);
		context->PSSetShader(pixelShader.get(), nullptr, 0);

		ID3D11SamplerState* sampler = samplerState.get();
		context->PSSetSamplers(0, 1, &sampler);

		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<float>(texDesc.Width);
		viewport.Height = static_cast<float>(texDesc.Height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		D3D11_RECT rect{};
		rect.left = 0;
		rect.top = 0;
		rect.right = static_cast<LONG>(texDesc.Width);
		rect.bottom = static_cast<LONG>(texDesc.Height);
		context->RSSetScissorRects(1, &rect);
	}

	void Draw(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, uint triangleCount) const
	{
		auto context = globals::d3d::context;

		context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R16_UINT, 0);

		UINT stride = sizeof(UnpackedVertex);
		UINT offset = 0;

		context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

		context->IASetInputLayout(inputLayout.get());

		context->DrawIndexed(triangleCount * 3, 0, 0);
	}
};