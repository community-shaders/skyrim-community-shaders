#pragma once

#include "PCH.h"

#include <d3d11_4.h>
#include "Utils/D3D.h"

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
	
	ModelSpaceToTangent()
	{
		std::vector<D3D11_INPUT_ELEMENT_DESC> inputDesc = { 
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 14, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BINORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 30, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		if (auto rawPtr = reinterpret_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\ModelSpaceToTangent.hlsl", {}, "vs_5_0", "vertex", inputDesc, inputLayout.put())); rawPtr)
			vertexShader.attach(rawPtr);
		
		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\ModelSpaceToTangent.hlsl", {}, "ps_5_0", "pixel")); rawPtr)
			pixelShader.attach(rawPtr);
	}

	void Convert(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, [[maybe_unused]] uint16_t vertexCount, uint16_t triangleCount, RE::BSGraphics::Texture* input, ID3D11RenderTargetView* output)
	{
		auto context = globals::d3d::context;

		context->VSSetShader(vertexShader.get(), nullptr, 0);
		context->PSSetShader(pixelShader.get(), nullptr, 0);

		context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R16_UINT, 0);

		D3D11_BUFFER_DESC desc;
		vertexBuffer->GetDesc(&desc);

		UINT stride = desc.ByteWidth;
		UINT offset = 0;

		context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

		context->IASetInputLayout(inputLayout.get());

		D3D11_TEXTURE2D_DESC texDesc;
		input->texture->GetDesc(&texDesc);

		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<float>(texDesc.Width);
		viewport.Height = static_cast<float>(texDesc.Height);

		context->RSSetViewports(1, &viewport);

		context->PSSetShaderResources(0, 1, &input->resourceView);

		context->OMSetRenderTargets(1, &output, nullptr);

		context->DrawIndexed(triangleCount * 3, 0, 0);
	}
};