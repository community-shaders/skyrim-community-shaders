// Inspired by Unrimp rendering engine's separable blur implementation
// Credits: Christian Ofenberg and the Unrimp project (https://github.com/cofenberg/unrimp)
// License: MIT License

#include "BackgroundBlur.h"
#include "../Features/Upscaling.h"
#include "../Globals.h"
#include "../Util.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

#include "RE/Skyrim.h"

using namespace std::literals;

// Blur intensity hardcoded. Super downscaled blur is very sensitive, this value looks best.
constexpr float BLUR_INTENSITY = 0.03f;

// Downsampling factor (8 = eighth resolution for performance)
constexpr UINT DOWNSAMPLE_FACTOR = 8;

namespace BackgroundBlur
{
	// Module-local state
	namespace
	{
		std::mutex resourceMutex;
		bool enabled = false;

		// DirectX resources (RAII managed)
		winrt::com_ptr<ID3D11VertexShader> vertexShader;
		winrt::com_ptr<ID3D11PixelShader> horizontalPixelShader;
		winrt::com_ptr<ID3D11PixelShader> verticalPixelShader;
		winrt::com_ptr<ID3D11PixelShader> compositePixelShader;  // For rounded corner compositing
		winrt::com_ptr<ID3D11Buffer> constantBuffer;
		winrt::com_ptr<ID3D11Buffer> windowConstantBuffer;  // For window rect and corner radius
		winrt::com_ptr<ID3D11SamplerState> samplerState;
		winrt::com_ptr<ID3D11BlendState> blendState;
		winrt::com_ptr<ID3D11RasterizerState> scissorRasterizerState;

		// Blend state for compositing UI over game world (alpha blending)
		winrt::com_ptr<ID3D11BlendState> compositeBlendState;

		// Downsampled textures for blur (1/8 res for performance)
		winrt::com_ptr<ID3D11Texture2D> downsampleTexture;
		winrt::com_ptr<ID3D11RenderTargetView> downsampleRTV;
		winrt::com_ptr<ID3D11ShaderResourceView> downsampleSRV;

		// Intermediate blur textures (at downsampled resolution)
		winrt::com_ptr<ID3D11Texture2D> blurTexture1;
		winrt::com_ptr<ID3D11Texture2D> blurTexture2;
		winrt::com_ptr<ID3D11RenderTargetView> blurRTV1;
		winrt::com_ptr<ID3D11RenderTargetView> blurRTV2;
		winrt::com_ptr<ID3D11ShaderResourceView> blurSRV1;
		winrt::com_ptr<ID3D11ShaderResourceView> blurSRV2;

		// Legacy composite texture members (kept for cleanup compatibility)
		winrt::com_ptr<ID3D11Texture2D> compositeTexture;
		winrt::com_ptr<ID3D11RenderTargetView> compositeRTV;
		winrt::com_ptr<ID3D11ShaderResourceView> compositeSRV;

		// 1x1 transparent black texture for clearing with scissor
		winrt::com_ptr<ID3D11Texture2D> clearTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> clearSRV;

		UINT textureWidth = 0;
		UINT textureHeight = 0;
		UINT downsampledWidth = 0;
		UINT downsampledHeight = 0;

		bool initialized = false;
		bool initializationFailed = false;

		// Blur shader constants structure
		struct BlurConstants
		{
			float texelSize[4];  // x = 1/width, y = 1/height, z = blur strength, w = unused
			int blurParams[4];   // x = samples, y = unused, z = unused, w = unused
		};

		// Window constants for rounded corner compositing
		struct WindowConstants
		{
			float windowRect[4];    // x = minX, y = minY, z = maxX, w = maxY (in pixels)
			float windowParams[4];  // x = cornerRadius, y = screenWidth, z = screenHeight, w = unused
		};

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

		// Compile vertex shader from horizontal blur file (both share same vertex shader)
		vertexShader.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\Menu\\BackgroundBlurHorizontal.hlsl", {}, "vs_5_0", "VS_Main")));
		if (!vertexShader) {
			logger::error("Failed to compile blur vertex shader");
			initializationFailed = true;
			return false;
		}

		// Compile horizontal pixel shader
		horizontalPixelShader.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Menu\\BackgroundBlurHorizontal.hlsl", {}, "ps_5_0", "PS_Main")));
		if (!horizontalPixelShader) {
			logger::error("Failed to compile horizontal blur pixel shader");
			initializationFailed = true;
			return false;
		}

		// Compile vertical pixel shader
		verticalPixelShader.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Menu\\BackgroundBlurVertical.hlsl", {}, "ps_5_0", "PS_Main")));
		if (!verticalPixelShader) {
			logger::error("Failed to compile vertical blur pixel shader");
			initializationFailed = true;
			return false;
		}

		// Compile composite pixel shader (for rounded corner compositing)
		compositePixelShader.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Menu\\BackgroundBlurComposite.hlsl", {}, "ps_5_0", "PS_Main")));
		if (!compositePixelShader) {
			logger::error("Failed to compile composite blur pixel shader");
			initializationFailed = true;
			return false;
		}

		// Create constant buffer
		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = sizeof(BlurConstants);
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, constantBuffer.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur constant buffer");
			initializationFailed = true;
			return false;
		}

		// Create window constant buffer (for rounded corner parameters)
		D3D11_BUFFER_DESC windowBufferDesc = {};
		windowBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		windowBufferDesc.ByteWidth = sizeof(WindowConstants);
		windowBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		hr = device->CreateBuffer(&windowBufferDesc, nullptr, windowConstantBuffer.put());
		if (FAILED(hr)) {
			logger::error("Failed to create window constant buffer");
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

		// Create blend state for compositing UI over game world (pre-multiplied alpha blend)
		// UI buffer uses pre-multiplied alpha, so SrcBlend=ONE and DestBlend=INV_SRC_ALPHA
		D3D11_BLEND_DESC compositeBlendDesc = {};
		compositeBlendDesc.RenderTarget[0].BlendEnable = TRUE;
		compositeBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		compositeBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		compositeBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		compositeBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		compositeBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		compositeBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		compositeBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hr = device->CreateBlendState(&compositeBlendDesc, compositeBlendState.put());
		if (FAILED(hr)) {
			logger::error("Failed to create composite blend state");
			initializationFailed = true;
			return false;
		}

		// Create 1x1 transparent black texture for clearing with scissor
		D3D11_TEXTURE2D_DESC clearTexDesc = {};
		clearTexDesc.Width = 1;
		clearTexDesc.Height = 1;
		clearTexDesc.MipLevels = 1;
		clearTexDesc.ArraySize = 1;
		clearTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		clearTexDesc.SampleDesc.Count = 1;
		clearTexDesc.Usage = D3D11_USAGE_IMMUTABLE;
		clearTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		uint32_t clearPixel = 0x00000000;  // RGBA = 0,0,0,0 (transparent black)
		D3D11_SUBRESOURCE_DATA clearData = {};
		clearData.pSysMem = &clearPixel;
		clearData.SysMemPitch = 4;

		hr = device->CreateTexture2D(&clearTexDesc, &clearData, clearTexture.put());
		if (FAILED(hr)) {
			logger::error("Failed to create clear texture");
			initializationFailed = true;
			return false;
		}

		hr = device->CreateShaderResourceView(clearTexture.get(), nullptr, clearSRV.put());
		if (FAILED(hr)) {
			logger::error("Failed to create clear SRV");
			initializationFailed = true;
			return false;
		}

		// Create scissor-enabled rasterizer state
		D3D11_RASTERIZER_DESC rsDesc = {};
		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.CullMode = D3D11_CULL_BACK;
		rsDesc.FrontCounterClockwise = FALSE;
		rsDesc.DepthClipEnable = TRUE;
		rsDesc.ScissorEnable = TRUE;

		hr = device->CreateRasterizerState(&rsDesc, scissorRasterizerState.put());
		if (FAILED(hr)) {
			logger::error("Failed to create scissor rasterizer state");
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

		// Calculate downsampled dimensions
		UINT dsWidth = (std::max)(1u, width / DOWNSAMPLE_FACTOR);
		UINT dsHeight = (std::max)(1u, height / DOWNSAMPLE_FACTOR);

		// Release old textures
		downsampleTexture = nullptr;
		downsampleRTV = nullptr;
		downsampleSRV = nullptr;
		blurTexture1 = nullptr;
		blurTexture2 = nullptr;
		blurRTV1 = nullptr;
		blurRTV2 = nullptr;
		blurSRV1 = nullptr;
		blurSRV2 = nullptr;
		compositeTexture = nullptr;
		compositeRTV = nullptr;
		compositeSRV = nullptr;

		// Create downsampled texture description (no full-res composite needed - all work at 1/8 res)
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = dsWidth;
		texDesc.Height = dsHeight;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		// Create downsample texture
		HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, downsampleTexture.put());
		if (FAILED(hr)) {
			logger::error("Failed to create downsample texture");
			return;
		}

		hr = device->CreateRenderTargetView(downsampleTexture.get(), nullptr, downsampleRTV.put());
		if (FAILED(hr)) {
			logger::error("Failed to create downsample RTV");
			downsampleTexture = nullptr;
			return;
		}

		hr = device->CreateShaderResourceView(downsampleTexture.get(), nullptr, downsampleSRV.put());
		if (FAILED(hr)) {
			logger::error("Failed to create downsample SRV");
			downsampleTexture = nullptr;
			downsampleRTV = nullptr;
			return;
		}

		// Create first blur texture (at downsampled resolution)
		hr = device->CreateTexture2D(&texDesc, nullptr, blurTexture1.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur texture 1");
			downsampleTexture = nullptr;
			downsampleRTV = nullptr;
			downsampleSRV = nullptr;
			return;
		}

		// Create second blur texture
		hr = device->CreateTexture2D(&texDesc, nullptr, blurTexture2.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur texture 2");
			blurTexture1 = nullptr;
			downsampleTexture = nullptr;
			downsampleRTV = nullptr;
			downsampleSRV = nullptr;
			return;
		}

		// Create render target views
		hr = device->CreateRenderTargetView(blurTexture1.get(), nullptr, blurRTV1.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur RTV 1");
			blurTexture1 = nullptr;
			blurTexture2 = nullptr;
			downsampleTexture = nullptr;
			downsampleRTV = nullptr;
			downsampleSRV = nullptr;
			return;
		}

		hr = device->CreateRenderTargetView(blurTexture2.get(), nullptr, blurRTV2.put());
		if (FAILED(hr)) {
			logger::error("Failed to create blur RTV 2");
			blurTexture1 = nullptr;
			blurTexture2 = nullptr;
			blurRTV1 = nullptr;
			downsampleTexture = nullptr;
			downsampleRTV = nullptr;
			downsampleSRV = nullptr;
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
			downsampleTexture = nullptr;
			downsampleRTV = nullptr;
			downsampleSRV = nullptr;
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
			downsampleTexture = nullptr;
			downsampleRTV = nullptr;
			downsampleSRV = nullptr;
			return;
		}

		textureWidth = width;
		textureHeight = height;
		downsampledWidth = dsWidth;
		downsampledHeight = dsHeight;
	}

	void PerformBlur(ID3D11Texture2D* sourceTexture, ID3D11RenderTargetView* targetRTV, ImVec2 menuMin, ImVec2 menuMax, float cornerRadius, ID3D11ShaderResourceView* uiBufferSRV = nullptr, ID3D11RenderTargetView* uiBufferRTV = nullptr)
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

		// Save current state
		ID3D11RenderTargetView* originalRTV = nullptr;
		ID3D11DepthStencilView* originalDSV = nullptr;
		context->OMGetRenderTargets(1, &originalRTV, &originalDSV);

		D3D11_VIEWPORT originalViewport;
		UINT numViewports = 1;
		context->RSGetViewports(&numViewports, &originalViewport);

		ID3D11RasterizerState* originalRS = nullptr;
		context->RSGetState(&originalRS);

		auto constantBufferPtr = constantBuffer.get();
		auto samplerStatePtr = samplerState.get();

		// Create SRV for source texture
		ID3D11ShaderResourceView* sourceSRV = nullptr;
		HRESULT hr = globals::d3d::device->CreateShaderResourceView(sourceTexture, nullptr, &sourceSRV);
		if (FAILED(hr)) {
			logger::error("Failed to create source SRV for blur");
			if (originalRTV)
				originalRTV->Release();
			if (originalDSV)
				originalDSV->Release();
			if (originalRS)
				originalRS->Release();
			return;
		}

		ID3D11ShaderResourceView* nullSRV = nullptr;

		// Set up downsample viewport - all work done at 1/8 resolution for performance
		D3D11_VIEWPORT downsampleViewport = {};
		downsampleViewport.Width = static_cast<FLOAT>(downsampledWidth);
		downsampleViewport.Height = static_cast<FLOAT>(downsampledHeight);
		downsampleViewport.MinDepth = 0.0f;
		downsampleViewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &downsampleViewport);

		auto downsampleRTVPtr = downsampleRTV.get();
		context->OMSetRenderTargets(1, &downsampleRTVPtr, nullptr);
		context->VSSetShader(vertexShader.get(), nullptr, 0);
		context->PSSetShader(horizontalPixelShader.get(), nullptr, 0);
		context->PSSetSamplers(0, 1, &samplerStatePtr);

		// Step 1: Downsample game world directly (bilinear filtering does the work)
		BlurConstants downsampleConstants = {};
		downsampleConstants.texelSize[0] = 1.0f / static_cast<float>(sourceDesc.Width);
		downsampleConstants.texelSize[1] = 1.0f / static_cast<float>(sourceDesc.Height);
		downsampleConstants.blurParams[0] = 1;  // Single sample for downsample
		context->UpdateSubresource(constantBuffer.get(), 0, nullptr, &downsampleConstants, 0, 0);
		context->PSSetConstantBuffers(0, 1, &constantBufferPtr);
		context->PSSetShaderResources(0, 1, &sourceSRV);
		context->Draw(3, 0);
		context->PSSetShaderResources(0, 1, &nullSRV);

		// Step 2: Blend UI buffer at downsampled resolution (pre-multiplied alpha)
		// Small HUD elements may be slightly softened but this is much faster
		if (uiBufferSRV && compositeBlendState) {
			context->OMSetBlendState(compositeBlendState.get(), nullptr, 0xFFFFFFFF);
			context->PSSetShaderResources(0, 1, &uiBufferSRV);
			context->Draw(3, 0);
			context->PSSetShaderResources(0, 1, &nullSRV);
			context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		}

		// Calculate blur parameters at eighth resolution
		float blurRadius = BLUR_INTENSITY * 10.0f;
		int sampleCount = 9;

		BlurConstants constants = {};
		constants.texelSize[0] = blurRadius / static_cast<float>(downsampledWidth);
		constants.texelSize[1] = blurRadius / static_cast<float>(downsampledHeight);
		constants.texelSize[2] = BLUR_INTENSITY;
		constants.texelSize[3] = 0.0f;
		constants.blurParams[0] = sampleCount;
		constants.blurParams[1] = 0;
		constants.blurParams[2] = 0;
		constants.blurParams[3] = 0;

		context->UpdateSubresource(constantBuffer.get(), 0, nullptr, &constants, 0, 0);

		// Set up viewport for blur (quarter resolution)
		D3D11_VIEWPORT blurViewport = {};
		blurViewport.Width = static_cast<FLOAT>(downsampledWidth);
		blurViewport.Height = static_cast<FLOAT>(downsampledHeight);
		blurViewport.MinDepth = 0.0f;
		blurViewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &blurViewport);

		context->PSSetConstantBuffers(0, 1, &constantBufferPtr);

		// First pass: Horizontal blur (on downsampled texture)
		auto rtv1Ptr = blurRTV1.get();
		auto downsampleSRVPtr = downsampleSRV.get();
		context->OMSetRenderTargets(1, &rtv1Ptr, nullptr);
		context->PSSetShader(horizontalPixelShader.get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, &downsampleSRVPtr);
		context->Draw(3, 0);

		// Second pass: Vertical blur (on downsampled texture)
		context->PSSetShaderResources(0, 1, &nullSRV);
		auto rtv2Ptr = blurRTV2.get();
		auto srv1Ptr = blurSRV1.get();
		context->OMSetRenderTargets(1, &rtv2Ptr, nullptr);
		context->PSSetShader(verticalPixelShader.get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, &srv1Ptr);
		context->Draw(3, 0);
		context->PSSetShaderResources(0, 1, &nullSRV);

		// Final composition: upscale from quarter-res with rounded corner mask
		context->RSSetViewports(1, &originalViewport);

		// Expand scissor rect slightly for anti-aliased rounded corner edges
		D3D11_RECT scissorRect;
		scissorRect.left = static_cast<LONG>((std::max)(0.0f, menuMin.x - 2.0f));
		scissorRect.top = static_cast<LONG>((std::max)(0.0f, menuMin.y - 2.0f));
		scissorRect.right = static_cast<LONG>((std::min)(static_cast<FLOAT>(sourceDesc.Width), menuMax.x + 2.0f));
		scissorRect.bottom = static_cast<LONG>((std::min)(static_cast<FLOAT>(sourceDesc.Height), menuMax.y + 2.0f));

		context->RSSetState(scissorRasterizerState.get());
		context->RSSetScissorRects(1, &scissorRect);

		context->OMSetRenderTargets(1, &targetRTV, nullptr);
		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, BLUR_INTENSITY * 0.8f };
		context->OMSetBlendState(blendState.get(), blendFactor, 0xFFFFFFFF);

		// Use composite shader with rounded corner mask if available, otherwise fallback
		if (compositePixelShader && windowConstantBuffer) {
			// Set up window constants for rounded corner compositing
			WindowConstants windowConstants = {};
			windowConstants.windowRect[0] = menuMin.x;
			windowConstants.windowRect[1] = menuMin.y;
			windowConstants.windowRect[2] = menuMax.x;
			windowConstants.windowRect[3] = menuMax.y;
			windowConstants.windowParams[0] = cornerRadius;
			windowConstants.windowParams[1] = static_cast<float>(sourceDesc.Width);
			windowConstants.windowParams[2] = static_cast<float>(sourceDesc.Height);
			windowConstants.windowParams[3] = 0.0f;
			context->UpdateSubresource(windowConstantBuffer.get(), 0, nullptr, &windowConstants, 0, 0);

			context->PSSetShader(compositePixelShader.get(), nullptr, 0);
			auto windowConstantBufferPtr = windowConstantBuffer.get();
			context->PSSetConstantBuffers(0, 1, &constantBufferPtr);
			context->PSSetConstantBuffers(1, 1, &windowConstantBufferPtr);
		}
		// Note: if composite shader not available, vertical shader is still set from blur pass

		// Use blurred quarter-res texture, bilinear filtering upscales smoothly
		auto srv2Ptr = blurSRV2.get();
		context->PSSetShaderResources(0, 1, &srv2Ptr);
		context->Draw(3, 0);
		context->PSSetShaderResources(0, 1, &nullSRV);

		// Clear the UI buffer in the scissor area so the HUD doesn't draw on top of our blur
		// The HUD outside the menu area remains visible
		if (uiBufferRTV && clearSRV) {
			// IMPORTANT: Switch back to horizontal shader for UI buffer clearing
			// The composite shader expects WindowBuffer which isn't set up for this pass
			context->PSSetShader(horizontalPixelShader.get(), nullptr, 0);

			// Restore scissor to exact window bounds for clearing
			D3D11_RECT clearScissorRect;
			clearScissorRect.left = static_cast<LONG>((std::max)(0.0f, menuMin.x));
			clearScissorRect.top = static_cast<LONG>((std::max)(0.0f, menuMin.y));
			clearScissorRect.right = static_cast<LONG>((std::min)(static_cast<FLOAT>(sourceDesc.Width), menuMax.x));
			clearScissorRect.bottom = static_cast<LONG>((std::min)(static_cast<FLOAT>(sourceDesc.Height), menuMax.y));
			context->RSSetScissorRects(1, &clearScissorRect);

			// Draw transparent black over just the scissor area to clear the HUD there
			context->OMSetRenderTargets(1, &uiBufferRTV, nullptr);
			// Use opaque blend to overwrite
			context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
			// Draw the 1x1 transparent black texture (shader will sample and output transparent)
			auto clearSRVPtr = clearSRV.get();
			context->PSSetShaderResources(0, 1, &clearSRVPtr);
			context->Draw(3, 0);
			context->PSSetShaderResources(0, 1, &nullSRV);
		}

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
	}

	void Cleanup()
	{
		std::lock_guard<std::mutex> lock(resourceMutex);

		vertexShader = nullptr;
		horizontalPixelShader = nullptr;
		verticalPixelShader = nullptr;
		compositePixelShader = nullptr;
		constantBuffer = nullptr;
		windowConstantBuffer = nullptr;
		samplerState = nullptr;
		blendState = nullptr;
		compositeBlendState = nullptr;
		scissorRasterizerState = nullptr;

		downsampleTexture = nullptr;
		downsampleRTV = nullptr;
		downsampleSRV = nullptr;

		compositeTexture = nullptr;
		compositeRTV = nullptr;
		compositeSRV = nullptr;

		clearTexture = nullptr;
		clearSRV = nullptr;

		blurTexture1 = nullptr;
		blurTexture2 = nullptr;
		blurRTV1 = nullptr;
		blurRTV2 = nullptr;
		blurSRV1 = nullptr;
		blurSRV2 = nullptr;

		textureWidth = 0;
		textureHeight = 0;
		downsampledWidth = 0;
		downsampledHeight = 0;
		enabled = false;
		initialized = false;
		initializationFailed = false;
	}

	void SetEnabled(bool enable)
	{
		enabled = enable;
	}

	bool GetEnabled()
	{
		return enabled;
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
		if (!enabled) {
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

		// Check if upscaling with D3D12 swap chain is active
		auto& upscaling = globals::features::upscaling;
		bool useUpscalingBackbuffer = upscaling.d3d12SwapChainActive;

		ID3D11RenderTargetView* currentRTV = nullptr;
		ID3D11Texture2D* currentTexture = nullptr;
		ID3D11ShaderResourceView* uiBufferSRV = nullptr;  // For compositing HUD before blur
		ID3D11RenderTargetView* uiBufferRTV = nullptr;    // For clearing HUD in blur area
		bool ownsRTV = false;                             // Track if we need to release the RTV

		if (useUpscalingBackbuffer) {
			// When D3D12 swap chain is active, get the backbuffer directly from upscaling
			// because OMGetRenderTargets returns the UI buffer, not the game world
			currentTexture = upscaling.GetD3D11BackbufferTexture();
			currentRTV = upscaling.GetD3D11BackbufferRTV();
			if (!currentTexture || !currentRTV) {
				return;
			}
			// AddRef since we'll Release later in cleanup (to match non-upscaling path)
			currentTexture->AddRef();
			currentRTV->AddRef();
			ownsRTV = true;

			// During gameplay (not paused), HUD is in separate UI buffer
			// We'll composite it onto the backbuffer before blurring
			auto ui = globals::game::ui;
			bool gameNotPaused = ui && !ui->GameIsPaused();
			if (gameNotPaused) {
				uiBufferSRV = upscaling.GetD3D11UIBufferSRV();
				uiBufferRTV = upscaling.GetD3D11UIBufferRTV();
			}
		} else {
			// Normal path: get current render target
			context->OMGetRenderTargets(1, &currentRTV, nullptr);

			if (!currentRTV) {
				return;
			}
			ownsRTV = true;

			// Get render target texture and its dimensions
			ID3D11Resource* currentRT = nullptr;
			currentRTV->GetResource(&currentRT);

			HRESULT hr = currentRT->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&currentTexture);

			if (FAILED(hr) || !currentTexture) {
				if (currentRT)
					currentRT->Release();
				if (currentRTV)
					currentRTV->Release();
				return;
			}

			currentRT->Release();
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

			// Skip tooltip windows
			if (window->Flags & ImGuiWindowFlags_Tooltip) {
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

			// Get window corner rounding from the window's style
			float cornerRadius = window->WindowRounding;

			// Perform blur for this window area with rounded corners
			// Pass UI buffer SRV/RTV for compositing and clearing during upscaling gameplay
			PerformBlur(currentTexture, currentRTV, windowMin, windowMax, cornerRadius, uiBufferSRV, uiBufferRTV);
		}

		// Cleanup
		currentTexture->Release();
		currentRTV->Release();
	}

}  // namespace BackgroundBlur
