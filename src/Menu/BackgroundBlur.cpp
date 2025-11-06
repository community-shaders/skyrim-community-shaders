// Based on Unrimp rendering engine's separable blur implementation
// Credits: Christian Ofenberg and the Unrimp project (https://github.com/cofenberg/unrimp)
// License: MIT License

#include "BackgroundBlur.h"
#include "../Globals.h"

#include <algorithm>
#include <cmath>
#include <d3dcompiler.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "RE/Skyrim.h"

using namespace std::literals;

/**
 * THEME MANAGER IMPLEMENTATION NOTES
 * ===================================
 *
 * BLUR SHADER PARAMETERS:
 * -----------------------
 * The background blur system uses constant buffers to pass parameters to HLSL shaders:
 *
 * BlurBuffer (cbuffer b0):
 *   - TexelSize.xy: Inverse texture dimensions (1/width, 1/height) for UV calculations
 *   - TexelSize.z:  Blur strength multiplier (0.0-1.0 from BackgroundBlur theme setting)
 *   - BlurParams.x: Number of blur samples (default: 13, must be odd for centered kernel)
 *
 * The blur uses a separable Gaussian kernel split into two passes:
 *   1. Horizontal pass: Samples along X-axis, outputs to intermediate texture
 *   2. Vertical pass:   Samples along Y-axis from intermediate, outputs final result
 */

// Blur System Constants
// ---------------------
// Text contrast boost per unit blur: Compensates for reduced clarity behind blurred backgrounds
constexpr float BLUR_TEXT_CONTRAST_FACTOR = 0.05f;  // 5% brightness boost at max blur

// Gaussian blur sigma: Controls blur kernel spread (standard deviation)
constexpr float GAUSSIAN_BLUR_SIGMA = 2.0f;

namespace BackgroundBlur
{
	// Module-local state
	namespace
	{
		std::mutex resourceMutex;
		float currentIntensity = 0.0f;
		bool enabled = false;

		// DirectX resources (RAII managed)
		winrt::com_ptr<ID3D11VertexShader> vertexShader;
		winrt::com_ptr<ID3D11PixelShader> horizontalPixelShader;
		winrt::com_ptr<ID3D11PixelShader> verticalPixelShader;
		winrt::com_ptr<ID3D11Buffer> constantBuffer;
		winrt::com_ptr<ID3D11SamplerState> samplerState;
		winrt::com_ptr<ID3D11BlendState> blendState;

		// Intermediate blur textures
		winrt::com_ptr<ID3D11Texture2D> blurTexture1;
		winrt::com_ptr<ID3D11Texture2D> blurTexture2;
		winrt::com_ptr<ID3D11RenderTargetView> blurRTV1;
		winrt::com_ptr<ID3D11RenderTargetView> blurRTV2;
		winrt::com_ptr<ID3D11ShaderResourceView> blurSRV1;
		winrt::com_ptr<ID3D11ShaderResourceView> blurSRV2;

		UINT textureWidth = 0;
		UINT textureHeight = 0;

		bool initialized = false;
		bool initializationFailed = false;

		// Blur shader constants structure
		struct BlurConstants
		{
			float texelSize[4];  // x = 1/width, y = 1/height, z = blur strength, w = unused
			int blurParams[4];   // x = samples, y = unused, z = unused, w = unused
		};

		// Inline HLSL shader code - Horizontal Pass
		const char* GetHorizontalBlurShader()
		{
			return R"(
cbuffer BlurBuffer : register(b0)
{
    float4 TexelSize;
    int4   BlurParams;
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT VS_Main(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
    output.Position.y = -output.Position.y;
    return output;
}

float GaussianWeight(float offset)
{
    const float SIGMA = 2.0f;
    const float v = 2.0f * SIGMA * SIGMA;
    return exp(-(offset * offset) / v) / (3.14159265f * v);
}

float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    const int samples = min(BlurParams.x, 15);
    const int halfSamples = samples / 2;

    for (int i = -halfSamples; i <= halfSamples; ++i)
    {
        float2 sampleCoord = input.TexCoord + float2(i * TexelSize.x, 0.0f);
        float weight = GaussianWeight(float(i));

        if (sampleCoord.x >= 0.0f && sampleCoord.x <= 1.0f)
        {
            result += InputTexture.Sample(LinearSampler, sampleCoord) * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0f)
        result /= totalWeight;

    return result;
}
)";
		}

		const char* GetVerticalBlurShader()
		{
			return R"(
cbuffer BlurBuffer : register(b0)
{
    float4 TexelSize;
    int4   BlurParams;
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT VS_Main(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
    output.Position.y = -output.Position.y;
    return output;
}

float GaussianWeight(float offset)
{
    const float SIGMA = 2.0f;
    const float v = 2.0f * SIGMA * SIGMA;
    return exp(-(offset * offset) / v) / (3.14159265f * v);
}

float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    const int samples = min(BlurParams.x, 15);
    const int halfSamples = samples / 2;

    for (int i = -halfSamples; i <= halfSamples; ++i)
    {
        float2 sampleCoord = input.TexCoord + float2(0.0f, i * TexelSize.y);
        float weight = GaussianWeight(float(i));

        if (sampleCoord.y >= 0.0f && sampleCoord.y <= 1.0f)
        {
            result += InputTexture.Sample(LinearSampler, sampleCoord) * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0f)
        result /= totalWeight;

    return result;
}
)";
		}

		bool CompileShader(const char* shaderSource, const char* entryPoint, const char* target,
			ID3DBlob** outBlob)
		{
			ID3DBlob* errorBlob = nullptr;
			HRESULT hr = D3DCompile(
				shaderSource,
				strlen(shaderSource),
				"InlineBlurShader",
				nullptr,
				nullptr,
				entryPoint,
				target,
				D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0,
				outBlob,
				&errorBlob);

			if (FAILED(hr)) {
				if (errorBlob) {
					logger::error("Blur shader compilation failed: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
					errorBlob->Release();
				}
				return false;
			}

			return true;
		}

	}  // anonymous namespace

	bool Initialize()
	{
		std::lock_guard<std::mutex> lock(resourceMutex);

		if (initialized || initializationFailed) {
			return initialized;
		}

		auto device = globals::d3d::device;
		if (!device) {
			initializationFailed = true;
			return false;
		}

		// Compile vertex shader
		ID3DBlob* vsBlob = nullptr;
		if (!CompileShader(GetHorizontalBlurShader(), "VS_Main", "vs_5_0", &vsBlob)) {
			initializationFailed = true;
			return false;
		}

		HRESULT hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vertexShader.put());
		vsBlob->Release();

		if (FAILED(hr)) {
			logger::error("Failed to create blur vertex shader");
			initializationFailed = true;
			return false;
		}

		// Compile horizontal pixel shader
		ID3DBlob* hpsBlob = nullptr;
		if (!CompileShader(GetHorizontalBlurShader(), "PS_Main", "ps_5_0", &hpsBlob)) {
			initializationFailed = true;
			return false;
		}

		hr = device->CreatePixelShader(hpsBlob->GetBufferPointer(), hpsBlob->GetBufferSize(), nullptr, horizontalPixelShader.put());
		hpsBlob->Release();

		if (FAILED(hr)) {
			logger::error("Failed to create horizontal blur pixel shader");
			initializationFailed = true;
			return false;
		}

		// Compile vertical pixel shader
		ID3DBlob* vpsBlob = nullptr;
		if (!CompileShader(GetVerticalBlurShader(), "PS_Main", "ps_5_0", &vpsBlob)) {
			initializationFailed = true;
			return false;
		}

		hr = device->CreatePixelShader(vpsBlob->GetBufferPointer(), vpsBlob->GetBufferSize(), nullptr, verticalPixelShader.put());
		vpsBlob->Release();

		if (FAILED(hr)) {
			logger::error("Failed to create vertical blur pixel shader");
			initializationFailed = true;
			return false;
		}

		// Create constant buffer
		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = sizeof(BlurConstants);
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		hr = device->CreateBuffer(&bufferDesc, nullptr, constantBuffer.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur constant buffer");
			initializationFailed = true;
			return false;
		}

		// Create sampler state
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		hr = device->CreateSamplerState(&samplerDesc, samplerState.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur sampler state");
			initializationFailed = true;
			return false;
		}

		// Create blend state
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hr = device->CreateBlendState(&blendDesc, blendState.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur blend state");
			initializationFailed = true;
			return false;
		}

		initialized = true;
		return true;
	}

	void CreateBlurTextures(UINT width, UINT height, DXGI_FORMAT format)
	{
		std::lock_guard<std::mutex> lock(resourceMutex);

		if (width == textureWidth && height == textureHeight && blurTexture1 && blurTexture2) {
			return;
		}

		auto device = globals::d3d::device;
		if (!device) {
			return;
		}

		// Release old textures
		blurTexture1 = nullptr;
		blurTexture2 = nullptr;
		blurRTV1 = nullptr;
		blurRTV2 = nullptr;
		blurSRV1 = nullptr;
		blurSRV2 = nullptr;

		// Create texture description
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = width;
		texDesc.Height = height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		// Create first blur texture
		HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, blurTexture1.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur texture 1");
			return;
		}

		// Create second blur texture
		hr = device->CreateTexture2D(&texDesc, nullptr, blurTexture2.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur texture 2");
			blurTexture1 = nullptr;
			return;
		}

		// Create render target views
		hr = device->CreateRenderTargetView(blurTexture1.get(), nullptr, blurRTV1.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur RTV 1");
			blurTexture1 = nullptr;
			blurTexture2 = nullptr;
			return;
		}

		hr = device->CreateRenderTargetView(blurTexture2.get(), nullptr, blurRTV2.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur RTV 2");
			blurTexture1 = nullptr;
			blurTexture2 = nullptr;
			blurRTV1 = nullptr;
			return;
		}

		// Create shader resource views
		hr = device->CreateShaderResourceView(blurTexture1.get(), nullptr, blurSRV1.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur SRV 1");
			blurTexture1 = nullptr;
			blurTexture2 = nullptr;
			blurRTV1 = nullptr;
			blurRTV2 = nullptr;
			return;
		}

		hr = device->CreateShaderResourceView(blurTexture2.get(), nullptr, blurSRV2.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur SRV 2");
			blurTexture1 = nullptr;
			blurTexture2 = nullptr;
			blurRTV1 = nullptr;
			blurRTV2 = nullptr;
			blurSRV1 = nullptr;
			return;
		}

		textureWidth = width;
		textureHeight = height;
	}

	void PerformBlur(ID3D11Texture2D* sourceTexture, ID3D11RenderTargetView* targetRTV, ImVec2 menuMin, ImVec2 menuMax)
	{
		std::lock_guard<std::mutex> lock(resourceMutex);

		auto context = globals::d3d::context;
		if (!context || !sourceTexture || !targetRTV) {
			return;
		}

		if (!vertexShader || !horizontalPixelShader || !verticalPixelShader) {
			return;
		}

		if (!blurTexture1 || !blurTexture2) {
			return;
		}

		// Get source texture description
		D3D11_TEXTURE2D_DESC sourceDesc;
		sourceTexture->GetDesc(&sourceDesc);

		// Create SRV for source
		ID3D11ShaderResourceView* sourceSRV = nullptr;
		HRESULT hr = globals::d3d::device->CreateShaderResourceView(sourceTexture, nullptr, &sourceSRV);
		if (FAILED(hr)) {
			logger::error("Failed to create source SRV for blur");
			return;
		}

		// Save current state
		ID3D11RenderTargetView* originalRTV = nullptr;
		ID3D11DepthStencilView* originalDSV = nullptr;
		context->OMGetRenderTargets(1, &originalRTV, &originalDSV);

		D3D11_VIEWPORT originalViewport;
		UINT numViewports = 1;
		context->RSGetViewports(&numViewports, &originalViewport);

		ID3D11RasterizerState* originalRS = nullptr;
		context->RSGetState(&originalRS);

		// Calculate blur parameters
		float blurRadius = currentIntensity * 10.0f;
		int sampleCount = (std::max)(5, (std::min)(15, static_cast<int>(9 + currentIntensity * 6)));

		BlurConstants constants = {};
		constants.texelSize[0] = blurRadius / static_cast<float>(textureWidth);
		constants.texelSize[1] = blurRadius / static_cast<float>(textureHeight);
		constants.texelSize[2] = currentIntensity;
		constants.texelSize[3] = 0.0f;
		constants.blurParams[0] = sampleCount;
		constants.blurParams[1] = 0;
		constants.blurParams[2] = 0;
		constants.blurParams[3] = 0;

		context->UpdateSubresource(constantBuffer.get(), 0, nullptr, &constants, 0, 0);

		// Set up viewport for blur
		D3D11_VIEWPORT blurViewport = {};
		blurViewport.Width = static_cast<FLOAT>(textureWidth);
		blurViewport.Height = static_cast<FLOAT>(textureHeight);
		blurViewport.MinDepth = 0.0f;
		blurViewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &blurViewport);

		// Set common shader resources
		auto constantBufferPtr = constantBuffer.get();
		auto samplerStatePtr = samplerState.get();
		context->VSSetShader(vertexShader.get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, &constantBufferPtr);
		context->PSSetSamplers(0, 1, &samplerStatePtr);

		// First pass: Horizontal blur
		auto rtv1Ptr = blurRTV1.get();
		context->OMSetRenderTargets(1, &rtv1Ptr, nullptr);
		context->PSSetShader(horizontalPixelShader.get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, &sourceSRV);
		context->Draw(3, 0);

		// Second pass: Vertical blur
		ID3D11ShaderResourceView* nullSRV = nullptr;
		auto rtv2Ptr = blurRTV2.get();
		auto srv1Ptr = blurSRV1.get();
		context->OMSetRenderTargets(1, &rtv2Ptr, nullptr);
		context->PSSetShader(verticalPixelShader.get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, &nullSRV);
		context->PSSetShaderResources(0, 1, &srv1Ptr);
		context->Draw(3, 0);

		// Final composition with scissor test
		context->RSSetViewports(1, &originalViewport);

		D3D11_RECT scissorRect;
		scissorRect.left = static_cast<LONG>((std::max)(0.0f, menuMin.x));
		scissorRect.top = static_cast<LONG>((std::max)(0.0f, menuMin.y));
		scissorRect.right = static_cast<LONG>((std::min)(static_cast<FLOAT>(sourceDesc.Width), menuMax.x));
		scissorRect.bottom = static_cast<LONG>((std::min)(static_cast<FLOAT>(sourceDesc.Height), menuMax.y));

		D3D11_RASTERIZER_DESC rsDesc = {};
		if (originalRS) {
			originalRS->GetDesc(&rsDesc);
		} else {
			rsDesc.FillMode = D3D11_FILL_SOLID;
			rsDesc.CullMode = D3D11_CULL_BACK;
			rsDesc.FrontCounterClockwise = FALSE;
			rsDesc.DepthClipEnable = TRUE;
		}
		rsDesc.ScissorEnable = TRUE;

		ID3D11RasterizerState* scissorRS = nullptr;
		globals::d3d::device->CreateRasterizerState(&rsDesc, &scissorRS);
		if (scissorRS) {
			context->RSSetState(scissorRS);
			context->RSSetScissorRects(1, &scissorRect);
		}

		context->OMSetRenderTargets(1, &targetRTV, nullptr);
		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, currentIntensity * 0.8f };
		context->OMSetBlendState(blendState.get(), blendFactor, 0xFFFFFFFF);

		auto srv2Ptr = blurSRV2.get();
		context->PSSetShaderResources(0, 1, &nullSRV);
		context->PSSetShaderResources(0, 1, &srv2Ptr);
		context->Draw(3, 0);

		// Restore state
		context->OMSetRenderTargets(1, &originalRTV, originalDSV);
		context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		context->PSSetShaderResources(0, 1, &nullSRV);
		context->RSSetState(originalRS);
		context->RSSetScissorRects(0, nullptr);

		// Cleanup
		if (sourceSRV)
			sourceSRV->Release();
		if (originalRTV)
			originalRTV->Release();
		if (originalDSV)
			originalDSV->Release();
		if (originalRS)
			originalRS->Release();
		if (scissorRS)
			scissorRS->Release();
	}

	void Cleanup()
	{
		std::lock_guard<std::mutex> lock(resourceMutex);

		vertexShader = nullptr;
		horizontalPixelShader = nullptr;
		verticalPixelShader = nullptr;
		constantBuffer = nullptr;
		samplerState = nullptr;
		blendState = nullptr;

		blurTexture1 = nullptr;
		blurTexture2 = nullptr;
		blurRTV1 = nullptr;
		blurRTV2 = nullptr;
		blurSRV1 = nullptr;
		blurSRV2 = nullptr;

		textureWidth = 0;
		textureHeight = 0;
		enabled = false;
		currentIntensity = 0.0f;
		initialized = false;
		initializationFailed = false;
	}

	void SetIntensity(float intensity)
	{
		currentIntensity = std::clamp(intensity, 0.0f, 1.0f);
		enabled = (currentIntensity > 0.0f);
	}

	float GetIntensity()
	{
		return currentIntensity;
	}

	bool IsEnabled()
	{
		return enabled && initialized;
	}

	void GetTextureDimensions(UINT& outWidth, UINT& outHeight)
	{
		std::lock_guard<std::mutex> lock(resourceMutex);
		outWidth = textureWidth;
		outHeight = textureHeight;
	}

	void RenderBackgroundBlur()
	{
		if (!enabled || currentIntensity <= 0.0f) {
			return;
		}

		if (!initialized || initializationFailed) {
			return;
		}

		auto device = globals::d3d::device;
		auto context = globals::d3d::context;
		if (!device || !context) {
			return;
		}

		// Get current render target
		ID3D11RenderTargetView* currentRTV = nullptr;
		context->OMGetRenderTargets(1, &currentRTV, nullptr);

		if (!currentRTV) {
			return;
		}

		// Get render target texture and its dimensions
		ID3D11Resource* currentRT = nullptr;
		currentRTV->GetResource(&currentRT);

		ID3D11Texture2D* currentTexture = nullptr;
		HRESULT hr = currentRT->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&currentTexture);

		if (FAILED(hr) || !currentTexture) {
			if (currentRT)
				currentRT->Release();
			if (currentRTV)
				currentRTV->Release();
			return;
		}

		D3D11_TEXTURE2D_DESC texDesc;
		currentTexture->GetDesc(&texDesc);

		// Create blur textures if needed
		UINT currentWidth, currentHeight;
		GetTextureDimensions(currentWidth, currentHeight);
		if (currentWidth != texDesc.Width || currentHeight != texDesc.Height) {
			CreateBlurTextures(texDesc.Width, texDesc.Height, texDesc.Format);
		}

		// Find ImGui windows that need blur
		ImGuiContext* ctx = ImGui::GetCurrentContext();
		if (!ctx || ctx->Windows.Size == 0) {
			currentTexture->Release();
			currentRT->Release();
			currentRTV->Release();
			return;
		}

		// Apply blur behind each visible ImGui window
		for (int i = 0; i < ctx->Windows.Size; i++) {
			ImGuiWindow* window = ctx->Windows[i];
			if (!window || window->Hidden || !window->WasActive || window->SkipItems) {
				continue;
			}

			// Skip child windows - only blur root windows to cover headers and footers
			if (window->ParentWindow != nullptr) {
				continue;
			}

			// Skip Performance Overlay window (no blur)
			if (window->Name && std::string_view(window->Name) == "Performance Overlay") {
				continue;
			}

			// Skip if window has no background (fully transparent)
			if (window->Flags & ImGuiWindowFlags_NoBackground) {
				continue;
			}

			// Get window outer bounds (includes title bar, borders, etc.)
			// Use window's inner rect which includes all content drawn inside the window
			// including custom headers and footers, not just OuterRectClipped
			ImRect windowRect = window->Rect();
			ImVec2 windowMin = windowRect.Min;
			ImVec2 windowMax = windowRect.Max;

			// Perform blur for this window area
			PerformBlur(currentTexture, currentRTV, windowMin, windowMax);
		}

		// Cleanup
		currentTexture->Release();
		currentRT->Release();
		currentRTV->Release();
	}

}  // namespace BackgroundBlur
