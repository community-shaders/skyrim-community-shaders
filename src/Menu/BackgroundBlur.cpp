// Inspired by Unrimp rendering engine's separable blur implementation
// Credits: Christian Ofenberg and the Unrimp project (https://github.com/cofenberg/unrimp)
// License: MIT License

#include "BackgroundBlur.h"
#include "../Features/Upscaling.h"
#include "../Globals.h"
#include "../ShaderCache.h"
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

// Multiplier applied to BLUR_INTENSITY to derive the blur kernel radius
constexpr float BLUR_RADIUS_SCALE = 10.0f;

// Number of samples per blur pass (Gaussian kernel taps)
constexpr int BLUR_SAMPLE_COUNT = 9;

// Extra pixels added around scissor rect for anti-aliased rounded corner edges
constexpr float SCISSOR_AA_PADDING = 2.0f;

// Scale factor applied to BLUR_INTENSITY for the final composite blend alpha
constexpr float BLEND_ALPHA_SCALE = 0.8f;

// Vertex count for a fullscreen triangle draw call
constexpr UINT FULLSCREEN_TRIANGLE_VERTICES = 3;

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
		winrt::com_ptr<ID3D11PixelShader> clearPixelShader;      // For rounded corner UI buffer clearing
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

		// Cached SRV for non-upscaling path (avoids per-frame CreateShaderResourceView)
		winrt::com_ptr<ID3D11ShaderResourceView> cachedSourceSRV;
		ID3D11Texture2D* cachedSourceTexture = nullptr;  // raw pointer for cache invalidation check

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

		// Release all blur texture resources; caller must hold resourceMutex
		void ReleaseBlurTextures()
		{
			downsampleTexture = nullptr;
			downsampleRTV = nullptr;
			downsampleSRV = nullptr;
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
		}

		// Create a Texture2D with associated RTV and SRV
		bool CreateTextureSet(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc,
			winrt::com_ptr<ID3D11Texture2D>& tex,
			winrt::com_ptr<ID3D11RenderTargetView>& rtv,
			winrt::com_ptr<ID3D11ShaderResourceView>& srv,
			const char* name)
		{
			if (FAILED(device->CreateTexture2D(&desc, nullptr, tex.put()))) {
				logger::error("Failed to create {} texture", name);
				return false;
			}
			if (FAILED(device->CreateRenderTargetView(tex.get(), nullptr, rtv.put()))) {
				logger::error("Failed to create {} RTV", name);
				return false;
			}
			if (FAILED(device->CreateShaderResourceView(tex.get(), nullptr, srv.put()))) {
				logger::error("Failed to create {} SRV", name);
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

		// Compile shaders
		auto compileShader = [&](auto& shader, const wchar_t* path, const char* target, const char* entry, const char* name) -> bool {
			shader.attach(static_cast<decltype(shader.get())>(Util::CompileShader(path, {}, target, entry)));
			if (!shader) {
				logger::error("Failed to compile {}", name);
				initializationFailed = true;
				return false;
			}
			return true;
		};

		if (!compileShader(vertexShader, L"Data\\Shaders\\Menu\\BackgroundBlurHorizontal.hlsl", "vs_5_0", "VS_Main", "blur vertex shader") ||
			!compileShader(horizontalPixelShader, L"Data\\Shaders\\Menu\\BackgroundBlurHorizontal.hlsl", "ps_5_0", "PS_Main", "horizontal blur pixel shader") ||
			!compileShader(verticalPixelShader, L"Data\\Shaders\\Menu\\BackgroundBlurVertical.hlsl", "ps_5_0", "PS_Main", "vertical blur pixel shader") ||
			!compileShader(compositePixelShader, L"Data\\Shaders\\Menu\\BackgroundBlurComposite.hlsl", "ps_5_0", "PS_Main", "composite blur pixel shader") ||
			!compileShader(clearPixelShader, L"Data\\Shaders\\Menu\\BackgroundBlurComposite.hlsl", "ps_5_0", "PS_Clear", "clear pixel shader"))
			return false;

		auto checkCreate = [&](HRESULT hr, const char* name) -> bool {
			if (FAILED(hr)) {
				logger::error("Failed to create {}", name);
				initializationFailed = true;
				return false;
			}
			return true;
		};

		// Create constant buffers
		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		cbDesc.ByteWidth = sizeof(BlurConstants);
		if (!checkCreate(device->CreateBuffer(&cbDesc, nullptr, constantBuffer.put()), "blur constant buffer"))
			return false;

		cbDesc.ByteWidth = sizeof(WindowConstants);
		if (!checkCreate(device->CreateBuffer(&cbDesc, nullptr, windowConstantBuffer.put()), "window constant buffer"))
			return false;

		// Create sampler state
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (!checkCreate(device->CreateSamplerState(&samplerDesc, samplerState.put()), "blur sampler state"))
			return false;

		// Create blend states
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (!checkCreate(device->CreateBlendState(&blendDesc, blendState.put()), "blur blend state"))
			return false;

		// Composite: pre-multiplied alpha (SrcBlend=ONE, DestBlendAlpha=INV_SRC_ALPHA)
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		if (!checkCreate(device->CreateBlendState(&blendDesc, compositeBlendState.put()), "composite blend state"))
			return false;

		// Create scissor-enabled rasterizer state
		D3D11_RASTERIZER_DESC rsDesc = {};
		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.CullMode = D3D11_CULL_BACK;
		rsDesc.FrontCounterClockwise = FALSE;
		rsDesc.DepthClipEnable = TRUE;
		rsDesc.ScissorEnable = TRUE;
		if (!checkCreate(device->CreateRasterizerState(&rsDesc, scissorRasterizerState.put()), "scissor rasterizer state"))
			return false;

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

		ReleaseBlurTextures();

		UINT dsWidth = (std::max)(1u, width / DOWNSAMPLE_FACTOR);
		UINT dsHeight = (std::max)(1u, height / DOWNSAMPLE_FACTOR);

		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = dsWidth;
		texDesc.Height = dsHeight;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (!CreateTextureSet(device, texDesc, downsampleTexture, downsampleRTV, downsampleSRV, "downsample") ||
			!CreateTextureSet(device, texDesc, blurTexture1, blurRTV1, blurSRV1, "blur 1") ||
			!CreateTextureSet(device, texDesc, blurTexture2, blurRTV2, blurSRV2, "blur 2")) {
			ReleaseBlurTextures();
			return;
		}

		textureWidth = width;
		textureHeight = height;
		downsampledWidth = dsWidth;
		downsampledHeight = dsHeight;
	}

	void PerformBlur(ID3D11Texture2D* sourceTexture, ID3D11ShaderResourceView* sourceSRV, ID3D11RenderTargetView* targetRTV, ImVec2 menuMin, ImVec2 menuMax, float cornerRadius, ID3D11ShaderResourceView* uiBufferSRV = nullptr, ID3D11RenderTargetView* uiBufferRTV = nullptr)
	{
		std::lock_guard<std::mutex> lock(resourceMutex);

		auto context = globals::d3d::context;
		if (!context || !sourceTexture || !sourceSRV || !targetRTV) {
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

		ID3D11ShaderResourceView* nullSRV = nullptr;

		// Set up viewport for all blur passes (1/8 resolution for performance)
		D3D11_VIEWPORT blurViewport = {};
		blurViewport.Width = static_cast<FLOAT>(downsampledWidth);
		blurViewport.Height = static_cast<FLOAT>(downsampledHeight);
		blurViewport.MinDepth = 0.0f;
		blurViewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &blurViewport);

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
		context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);
		context->PSSetShaderResources(0, 1, &nullSRV);

		// Step 2: Blend UI buffer at downsampled resolution (pre-multiplied alpha)
		// Small HUD elements may be slightly softened but this is much faster
		if (uiBufferSRV && compositeBlendState) {
			context->OMSetBlendState(compositeBlendState.get(), nullptr, 0xFFFFFFFF);
			context->PSSetShaderResources(0, 1, &uiBufferSRV);
			context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);
			context->PSSetShaderResources(0, 1, &nullSRV);
			context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		}

		// Calculate blur parameters at eighth resolution
		float blurRadius = BLUR_INTENSITY * BLUR_RADIUS_SCALE;
		int sampleCount = BLUR_SAMPLE_COUNT;

		BlurConstants constants = {};
		constants.texelSize[0] = blurRadius / static_cast<float>(downsampledWidth);
		constants.texelSize[1] = blurRadius / static_cast<float>(downsampledHeight);
		constants.texelSize[2] = BLUR_INTENSITY;
		constants.blurParams[0] = sampleCount;

		context->UpdateSubresource(constantBuffer.get(), 0, nullptr, &constants, 0, 0);
		context->PSSetConstantBuffers(0, 1, &constantBufferPtr);

		// First pass: Horizontal blur (on downsampled texture)
		auto rtv1Ptr = blurRTV1.get();
		auto downsampleSRVPtr = downsampleSRV.get();
		context->OMSetRenderTargets(1, &rtv1Ptr, nullptr);
		context->PSSetShader(horizontalPixelShader.get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, &downsampleSRVPtr);
		context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);

		// Second pass: Vertical blur (on downsampled texture)
		context->PSSetShaderResources(0, 1, &nullSRV);
		auto rtv2Ptr = blurRTV2.get();
		auto srv1Ptr = blurSRV1.get();
		context->OMSetRenderTargets(1, &rtv2Ptr, nullptr);
		context->PSSetShader(verticalPixelShader.get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, &srv1Ptr);
		context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);
		context->PSSetShaderResources(0, 1, &nullSRV);

		// Final composition: upscale from quarter-res with rounded corner mask
		context->RSSetViewports(1, &originalViewport);

		// Expand scissor rect slightly for anti-aliased rounded corner edges
		D3D11_RECT scissorRect;
		scissorRect.left = static_cast<LONG>((std::max)(0.0f, menuMin.x - SCISSOR_AA_PADDING));
		scissorRect.top = static_cast<LONG>((std::max)(0.0f, menuMin.y - SCISSOR_AA_PADDING));
		scissorRect.right = static_cast<LONG>((std::min)(static_cast<FLOAT>(sourceDesc.Width), menuMax.x + SCISSOR_AA_PADDING));
		scissorRect.bottom = static_cast<LONG>((std::min)(static_cast<FLOAT>(sourceDesc.Height), menuMax.y + SCISSOR_AA_PADDING));

		context->RSSetState(scissorRasterizerState.get());
		context->RSSetScissorRects(1, &scissorRect);

		// Set up window constants for rounded corner shaders (used by both composite and clear)
		bool useRoundedCorners = compositePixelShader && clearPixelShader && windowConstantBuffer;
		if (useRoundedCorners) {
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
			auto windowConstantBufferPtr = windowConstantBuffer.get();
			context->PSSetConstantBuffers(1, 1, &windowConstantBufferPtr);
		}

		// Draw blur to target
		context->OMSetRenderTargets(1, &targetRTV, nullptr);
		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, BLUR_INTENSITY * BLEND_ALPHA_SCALE };
		context->OMSetBlendState(blendState.get(), blendFactor, 0xFFFFFFFF);
		context->PSSetShader(useRoundedCorners ? compositePixelShader.get() : verticalPixelShader.get(), nullptr, 0);
		auto srv2Ptr = blurSRV2.get();
		context->PSSetShaderResources(0, 1, &srv2Ptr);
		context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);
		context->PSSetShaderResources(0, 1, &nullSRV);

		// Clear UI buffer where blur was drawn (prevents HUD showing through)
		if (uiBufferRTV) {
			context->OMSetRenderTargets(1, &uiBufferRTV, nullptr);
			context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

			// Clear with same rounded shape - window constants already bound
			context->PSSetShader(clearPixelShader.get(), nullptr, 0);
			context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);
		}

		// Restore state
		context->OMSetRenderTargets(1, &originalRTV, originalDSV);
		context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		context->PSSetShaderResources(0, 1, &nullSRV);
		context->RSSetState(originalRS);
		context->RSSetScissorRects(0, nullptr);

		// Cleanup
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
		clearPixelShader = nullptr;
		constantBuffer = nullptr;
		windowConstantBuffer = nullptr;
		samplerState = nullptr;
		blendState = nullptr;
		compositeBlendState = nullptr;
		scissorRasterizerState = nullptr;

		ReleaseBlurTextures();

		cachedSourceSRV = nullptr;
		cachedSourceTexture = nullptr;

		enabled = false;
		initialized = false;
		initializationFailed = false;
	}

	void SetEnabled(bool enable)
	{
		enabled = enable;
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

		// Back buffer is black on main/loading menu during shader compilation without upscaling
		if (!useUpscalingBackbuffer && !(upscaling.loaded && upscaling.IsUpscalingActive())) {
			auto ui = globals::game::ui;
			bool isMainOrLoading = ui && (ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
			auto shaderCache = globals::shaderCache;
			if (isMainOrLoading && shaderCache && shaderCache->IsCompiling()) {
				return;
			}
		}

		winrt::com_ptr<ID3D11Texture2D> currentTexture;
		winrt::com_ptr<ID3D11RenderTargetView> currentRTV;
		ID3D11ShaderResourceView* sourceSRV = nullptr;  // Non-owning; lifetime managed elsewhere
		ID3D11ShaderResourceView* uiBufferSRV = nullptr;
		ID3D11RenderTargetView* uiBufferRTV = nullptr;

		if (useUpscalingBackbuffer) {
			// When D3D12 swap chain is active, get all resources in one call
			auto res = upscaling.GetBlurResources();
			if (!res.backbufferTex || !res.backbufferRTV || !res.backbufferSRV) {
				return;
			}
			currentTexture.copy_from(res.backbufferTex);
			currentRTV.copy_from(res.backbufferRTV);
			sourceSRV = res.backbufferSRV;

			// During gameplay (not paused), HUD is in separate UI buffer
			auto ui = globals::game::ui;
			if (ui && !ui->GameIsPaused()) {
				uiBufferSRV = res.uiBufferSRV;
				uiBufferRTV = res.uiBufferRTV;
			}
		} else {
			// Normal path: get current render target
			ID3D11RenderTargetView* rawRTV = nullptr;
			context->OMGetRenderTargets(1, &rawRTV, nullptr);
			if (!rawRTV) {
				return;
			}
			currentRTV.attach(rawRTV);  // Takes ownership of the AddRef from OMGetRenderTargets

			// Get render target texture
			winrt::com_ptr<ID3D11Resource> currentRT;
			currentRTV->GetResource(currentRT.put());

			winrt::com_ptr<ID3D11Texture2D> tex;
			if (FAILED(currentRT->QueryInterface(IID_PPV_ARGS(tex.put()))) || !tex) {
				return;
			}
			currentTexture = tex;

			// Cache SRV for non-upscaling path (avoids CreateShaderResourceView every frame)
			if (cachedSourceTexture != currentTexture.get()) {
				cachedSourceSRV = nullptr;
				HRESULT hr = device->CreateShaderResourceView(currentTexture.get(), nullptr, cachedSourceSRV.put());
				if (FAILED(hr)) {
					logger::error("Failed to create cached source SRV for blur");
					return;
				}
				cachedSourceTexture = currentTexture.get();
			}
			sourceSRV = cachedSourceSRV.get();
		}

		D3D11_TEXTURE2D_DESC texDesc;
		currentTexture->GetDesc(&texDesc);

		// Create blur textures if needed
		if (textureWidth != texDesc.Width || textureHeight != texDesc.Height) {
			CreateBlurTextures(texDesc.Width, texDesc.Height, texDesc.Format);
		}

		// Find ImGui windows that need blur
		ImGuiContext* ctx = ImGui::GetCurrentContext();
		if (!ctx || ctx->Windows.Size == 0) {
			return;
		}

		// Apply blur behind each visible ImGui window
		for (int i = 0; i < ctx->Windows.Size; i++) {
			ImGuiWindow* window = ctx->Windows[i];
			// Don't check Hidden - it causes a 1-frame blur delay when windows reappear
			if (!window || !window->WasActive || window->SkipItems) {
				continue;
			}

			// Skip child windows - only blur root windows to cover headers and footers
			// Exception: docked windows are visually independent even though ParentWindow is set
			if (window->ParentWindow != nullptr && !window->DockIsActive) {
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
			PerformBlur(currentTexture.get(), sourceSRV, currentRTV.get(), windowMin, windowMax, cornerRadius, uiBufferSRV, uiBufferRTV);
		}
	}

}  // namespace BackgroundBlur
