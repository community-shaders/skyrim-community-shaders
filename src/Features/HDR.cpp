#include "HDR.h"

#include "PCH.h"

#include "Buffer.h"
#include "Deferred.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Upscaling.h"
#include "Util.h"
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <imgui.h>

bool HDR::DetectHDRDisplayEarly(IDXGIAdapter* adapter)
{
	if (!adapter)
		return false;

	Microsoft::WRL::ComPtr<IDXGIOutput> output;
	if (FAILED(adapter->EnumOutputs(0, &output)))
		return false;

	Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
	if (FAILED(output.As(&output6)))
		return false;

	DXGI_OUTPUT_DESC1 desc1{};
	if (FAILED(output6->GetDesc1(&desc1)))
		return false;

	bool detected = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
	GetSingleton()->hdrDisplayDetected = detected;
	return detected;
}

bool HDR::DetectHDRDisplay()
{
	auto swapChain = globals::d3d::swapChain;
	if (!swapChain)
		return false;

	Microsoft::WRL::ComPtr<IDXGIOutput> output;
	if (FAILED(swapChain->GetContainingOutput(&output)))
		return false;

	Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
	if (FAILED(output.As(&output6)))
		return false;

	DXGI_OUTPUT_DESC1 desc1{};
	if (FAILED(output6->GetDesc1(&desc1)))
		return false;

	return desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDR::Settings,
	enableHDR,
	exposure,
	highlights,
	shadows,
	contrast,
	saturation,
	dechroma,
	hueCorrectionStrength,
	bypassTonemapping,
	paperWhite,
	peakNits);

void HDR::DrawSettings()
{
	// HDR Debug Status Panel
	if (ImGui::CollapsingHeader("HDR Status Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::BeginChild("HDRDebugPanel", ImVec2(0, 180), true);
		
		// HDR state
		ImGui::Text("HDR State:");
		ImGui::Indent();
		ImGui::Text("HDR Enabled: %s", settings.enableHDR ? "YES" : "NO");
		ImGui::Text("Display Detected: %s", hdrDisplayDetected ? "YES" : "NO");
		if (globals::features::upscaling.d3d12SwapChainActive) {
			ImGui::TextColored({ 1, 1, 0, 1 }, "Mode: D3D12 (Frame Generation)");
			ImGui::TextColored({ 1, 1, 0, 1 }, "Colorspace managed by Streamline");
		} else {
			ImGui::Text("Mode: D3D11");
		}
		ImGui::Unindent();
		
		// Output settings
		ImGui::Text("Output Configuration:");
		ImGui::Indent();
		ImGui::Text("Paper White: %u nits", settings.paperWhite);
		ImGui::Text("Peak Brightness: %u nits", settings.peakNits);
		ImGui::Text("Exposure: %.2f", settings.exposure);
		ImGui::Unindent();
		
		// Only query swapchain if NOT using D3D12/Streamline (causes crash)
		if (!globals::features::upscaling.d3d12SwapChainActive) {
			auto swapChain = globals::d3d::swapChain;
			
			ImGui::Text("Swapchain Info:");
			ImGui::Indent();
			if (swapChain) {
				Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
				if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
					DXGI_SWAP_CHAIN_DESC1 desc;
					if (SUCCEEDED(swapChain3->GetDesc1(&desc))) {
						ImGui::Text("Format: %s", 
							desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM ? "R10G10B10A2_UNORM" :
							desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? "R16G16B16A16_FLOAT" :
							desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? "R8G8B8A8_UNORM" : "Unknown");
						ImGui::Text("Resolution: %ux%u", desc.Width, desc.Height);
						
						// Show expected colorspace
						DXGI_COLOR_SPACE_TYPE currentCS = GetCurrentColorSpace();
						ImGui::Text("Expected Colorspace: %s", GetColorSpaceName(currentCS));
						
						if (settings.enableHDR && hdrDisplayDetected && 
						    currentCS != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
							ImGui::Separator();
							ImGui::TextColored({ 1, 0, 0, 1 }, "Not in HDR mode!");
							if (ImGui::Button("Set HDR Colorspace", { -1, 0 })) {
								logger::info("=== USER MANUAL COLORSPACE SET ===");
								bool result = SetSwapChainColorSpace(true);
								logger::info("=== Result: {} ===", result ? "SUCCESS" : "FAILED");
							}
						}
					}
				}
			} else {
				ImGui::Text("Swapchain: NULL");
			}
			ImGui::Unindent();
		}
		
		ImGui::EndChild();
	}
	
	ImGui::Separator();
	if (hdrDisplayDetected) {
		ImGui::TextColored({ 0, 1, 0, 1 }, "HDR display detected");
		if (ImGui::Checkbox("HDR Enabled", &settings.enableHDR)) {
			enabledSaveLater = settings.enableHDR;
			logger::info("HDR checkbox toggled to: {}", settings.enableHDR);
			if (!globals::features::upscaling.d3d12SwapChainActive) {
				logger::info("Calling SetSwapChainColorSpace({})...", settings.enableHDR);
				bool result = SetSwapChainColorSpace(settings.enableHDR);
				logger::info("SetSwapChainColorSpace result: {}", result);
			} else {
				logger::info("D3D12 swap chain active, skipping SetSwapChainColorSpace");
			}
			// Clear both in-memory cache and disk cache for ISHDR to ensure HDR_OUTPUT define change takes effect
			globals::shaderCache->Clear("Data\\Shaders\\ISHDR.hlsl");
			globals::shaderCache->Clear(RE::BSShader::Type::ImageSpace);
		}
	} else {
		ImGui::TextColored({ 1, 0.5f, 0, 1 }, "No HDR display detected");
		if (ImGui::Checkbox("Enable HDR Processing", &settings.enableHDR)) {
			enabledSaveLater = settings.enableHDR;
			globals::shaderCache->Clear("Data\\Shaders\\ISHDR.hlsl");
			globals::shaderCache->Clear(RE::BSShader::Type::ImageSpace);
		}
	}

	if (ImGui::Button("Reset HDR Settings", { -1, 0 })) {
		settings.exposure = 1.0f;
		settings.vanillaEyeAdaptation = true;
		settings.vanillaBloom = true;
		settings.bypassTonemapping = false;
		settings.highlights = 1.0f;
		settings.shadows = 1.0f;
		settings.contrast = 1.0f;
		settings.saturation = 1.0f;
		settings.dechroma = 0.0f;
		settings.hueCorrectionStrength = 0.0f;

		settings.paperWhite = 400;
		settings.peakNits = 10000;
	}

	if (ImGui::Button("Reload HDR shaders", { -1, 0 })) {
		ClearShaderCache();
		if (hdrDisplayDetected)
			GetHDROutputCS();
		else
			GetSDROutputCS();
	}

	if (hdrDisplayDetected) {
		ImGui::SliderInt("Paper White (nits)", reinterpret_cast<int*>(&settings.paperWhite), 1, 500);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Paper White sets the game's reference white brightness.");
		}

		ImGui::SliderInt("Peak Brightness (nits)", reinterpret_cast<int*>(&settings.peakNits), 1, 10000);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Peak Brightness defines the maximum brightness level.");
		}
	}

	ImGui::SliderFloat("Exposure", &settings.exposure, 0.f, 2.f);
	
	ImGui::Checkbox("Vanilla Eye Adaptation", &settings.vanillaEyeAdaptation);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable the game's automatic eye adaptation. Disable if using external post-processing.");
	}
	
	ImGui::Checkbox("Vanilla Bloom", &settings.vanillaBloom);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable the game's bloom effect. Disable if using external post-processing bloom.");
	}
	
	if (hdrDisplayDetected && settings.enableHDR) {
		ImGui::Checkbox("Bypass Tonemapping", &settings.bypassTonemapping);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Skip internal tonemapping for external post-processing. Outputs linear HDR data.");
		}
	}
	
	ImGui::SliderFloat("Highlights", &settings.highlights, 0.f, 2.f);
	ImGui::SliderFloat("Shadows", &settings.shadows, 0.f, 2.f);
	ImGui::SliderFloat("Contrast", &settings.contrast, 0.f, 2.f);
	ImGui::SliderFloat("Saturation", &settings.saturation, 0.f, 2.f);
	ImGui::SliderFloat("Dechroma", &settings.dechroma, 0.f, 2.f);
	ImGui::SliderFloat("Hue Correction Strength", &settings.hueCorrectionStrength, 0.f, 2.f);
}

void HDR::SaveSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	auto settingsCopy = settings;
	settingsCopy.enableHDR = enabledSaveLater;
	o_json = settingsCopy;
}

void HDR::LoadSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	settings = o_json;
	enabledSaveLater = settings.enableHDR;
}

void HDR::RestoreDefaultSettings()
{
	settings = {};
}

const char* HDR::GetColorSpaceName(DXGI_COLOR_SPACE_TYPE colorSpace)
{
	switch (colorSpace) {
		case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "sRGB/BT.709 (SDR)";
		case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "HDR10 PQ/BT.2020";
		case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "Linear BT.709";
		case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709: return "Studio sRGB/BT.709";
		case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020: return "Studio HDR10";
		default: return "Unknown";
	}
}

DXGI_COLOR_SPACE_TYPE HDR::GetCurrentColorSpace()
{
	auto swapChain = globals::d3d::swapChain;
	if (!swapChain) {
		return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	}

	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
	if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
		return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	}

	// Unfortunately, there's no direct API to query the current colorspace
	// Return what we think it should be based on our settings
	if (settings.enableHDR && hdrDisplayDetected) {
		return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
	}
	return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
}

bool HDR::SetSwapChainColorSpace(bool enableHDR)
{
	auto swapChain = globals::d3d::swapChain;
	if (!swapChain) {
		logger::warn("HDR: No swap chain available");
		return false;
	}

	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
	HRESULT hr = swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3));
	if (FAILED(hr) || !swapChain3) {
		logger::warn("HDR: Failed to get IDXGISwapChain3 interface (hr=0x{:08X})", static_cast<unsigned>(hr));
		return false;
	}

	DXGI_COLOR_SPACE_TYPE colorSpace = enableHDR ? 
		DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 :  // HDR10: PQ transfer, BT.2020 primaries
		DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;       // SDR: sRGB gamma, BT.709 primaries

	logger::info("HDR: SetColorSpace1({}) [{}]", static_cast<int>(colorSpace), GetColorSpaceName(colorSpace));

	hr = swapChain3->SetColorSpace1(colorSpace);
	
	logger::info("HDR: SetColorSpace1 returned hr=0x{:08X} ({})", 
		static_cast<unsigned>(hr), SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

	if (FAILED(hr)) {
		logger::error("HDR: SetColorSpace1 FAILED - colorspace NOT changed!");
		return false;
	}

	logger::info("HDR: Successfully set swap chain colorspace to {}", GetColorSpaceName(colorSpace));
	return true;
}

void HDR::BeginUIRendering()
{
	if (!hdrDisplayDetected || !settings.enableHDR)
		return;
	
	// Skip if D3D12 frame gen is active - it has its own UI buffer handling
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;
	
	if (!uiTexture || !uiTexture->rtv)
		return;
	
	auto context = globals::d3d::context;
	
	// Save current render target
	context->OMGetRenderTargets(1, &savedRTV, &savedDSV);
	
	// Clear UI texture with transparent black
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);
	
	// Set UI texture as render target
	ID3D11RenderTargetView* rtv = uiTexture->rtv.get();
	context->OMSetRenderTargets(1, &rtv, nullptr);
	
	renderingUI = true;
}

void HDR::EndUIRendering()
{
	if (!hdrDisplayDetected || !settings.enableHDR)
		return;
	
	// Skip if D3D12 frame gen is active
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;
	
	if (!renderingUI)
		return;
	
	auto context = globals::d3d::context;
	
	// Restore original render target
	context->OMSetRenderTargets(1, &savedRTV, savedDSV);
	
	if (savedRTV) {
		savedRTV->Release();
		savedRTV = nullptr;
	}
	if (savedDSV) {
		savedDSV->Release();
		savedDSV = nullptr;
	}
	
	renderingUI = false;
}

void HDR::CompositeUI()
{
	if (!hdrDisplayDetected || !settings.enableHDR)
		return;
	
	// Skip if D3D12 frame gen is active - FidelityFX handles UI compositing
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;
	
	if (!uiTexture || !hdrDataCB)
		return;
	
	auto context = globals::d3d::context;
	auto state = globals::state;
	auto renderer = globals::game::renderer;
	
	state->BeginPerfEvent("HDR UI Composite");
	
	// Read from kMAIN which has the linear HDR scene (pre-tonemapping)
	// We skip ISHDR's output (which goes to clamped kFRAMEBUFFER) and handle exposure + bloom ourselves
	auto& sceneRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& bloomRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kHDR_BLOOM];
	auto& adaptRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kHDR_DOWNSAMPLE13];
	
	auto dispatchCount = Util::GetScreenDispatchCount(false);
	
	// Bind inputs: HDR scene, UI buffer, bloom, and eye adaptation
	ID3D11ShaderResourceView* views[4] = { sceneRT.SRV, uiTexture->srv.get(), bloomRT.SRV, adaptRT.SRV };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);
	
	// Sampler for adaptation texture (it's a small texture, needs bilinear sampling)
	auto linearSampler = Deferred::GetSingleton()->linearSampler;
	context->CSSetSamplers(0, 1, &linearSampler);
	
	// Output to hdrTexture (intermediate)
	ID3D11UnorderedAccessView* uavs[1] = { hdrTexture->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	
	ID3D11Buffer* cbs[1] = { hdrDataCB->CB() };
	context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);
	
	auto compositeShader = GetUICompositeCS();
	if (!compositeShader) {
		logger::error("HDR: Failed to get UI composite shader");
		state->EndPerfEvent();
		return;
	}
	
	context->CSSetShader(compositeShader, nullptr, 0);
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	
	// Cleanup
	views[0] = nullptr;
	views[1] = nullptr;
	views[2] = nullptr;
	views[3] = nullptr;
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);
	
	uavs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	
	cbs[0] = nullptr;
	context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);
	
	ID3D11SamplerState* nullSampler = nullptr;
	context->CSSetSamplers(0, 1, &nullSampler);
	
	context->CSSetShader(nullptr, nullptr, 0);
	
	state->EndPerfEvent();
}

void HDR::SetupResources()
{
	logger::info("HDR: SetupResources called");
	
	if (!hdrDisplayDetected) {
		hdrDisplayDetected = DetectHDRDisplay();
	}
	
	auto& upscaling = globals::features::upscaling;
	
	if (hdrDisplayDetected) {
		logger::info("HDR display detected");
		
		// Set color space based on user preference
		// For D3D12 proxy (frame generation), color space is set during swap chain creation in DX12SwapChain.cpp
		// For D3D11 path, we set it here dynamically
		if (!upscaling.d3d12SwapChainActive) {
			// D3D11 path - safe to call SetSwapChainColorSpace
			logger::info("HDR: SetupResources calling SetSwapChainColorSpace({})...", settings.enableHDR);
			bool result = SetSwapChainColorSpace(settings.enableHDR);
			logger::info("HDR: SetupResources SetSwapChainColorSpace result: {}", result);
		} else {
			logger::info("HDR: D3D12 proxy active - colorspace was configured in DX12SwapChain::CreateSwapChain");
		}
	} else {
		logger::info("No HDR display detected");
	}

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	DXGI_FORMAT mainFormat = texDesc.Format;

	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	hdrTexture = new Texture2D(texDesc);
	hdrTexture->CreateSRV(srvDesc);
	hdrTexture->CreateUAV(uavDesc);

	if (hdrDisplayDetected) {
		texDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	} else {
		texDesc.Format = mainFormat;
	}
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	outputTexture = new Texture2D(texDesc);
	outputTexture->CreateSRV(srvDesc);
	outputTexture->CreateUAV(uavDesc);

	// UI texture for separate UI rendering (sRGB format for proper UI authoring)
	D3D11_TEXTURE2D_DESC uiTexDesc = texDesc;
	uiTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uiTexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	
	D3D11_SHADER_RESOURCE_VIEW_DESC uiSrvDesc = srvDesc;
	uiSrvDesc.Format = uiTexDesc.Format;
	
	D3D11_UNORDERED_ACCESS_VIEW_DESC uiUavDesc = uavDesc;
	uiUavDesc.Format = uiTexDesc.Format;
	
	uiTexture = new Texture2D(uiTexDesc);
	uiTexture->CreateSRV(uiSrvDesc);
	uiTexture->CreateUAV(uiUavDesc);
	
	// Create RTV for UI texture
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = uiTexDesc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	uiTexture->CreateRTV(rtvDesc);

	hdrDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDataCB>());

	logger::info("HDR: Resources created - hdrTexture format: {}, outputTexture format: {}, uiTexture format: {}, mainFormat: {}",
		(int)DXGI_FORMAT_R16G16B16A16_FLOAT, (int)texDesc.Format, (int)uiTexDesc.Format, (int)mainFormat);

	UpdateHDRData();
}

void HDR::ApplyHDR()
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	// If HDR display not detected, vanilla ISHDR output is fine
	if (!hdrDisplayDetected)
		return;

	if (!hdrDataCB || !hdrTexture || !outputTexture) {
		logger::warn("HDR: Resources not initialized - hdrDataCB:{} hdrTexture:{} outputTexture:{}", 
			(void*)hdrDataCB, (void*)hdrTexture, (void*)outputTexture);
		return;
	}
	
	auto& upscaling = globals::features::upscaling;

	auto context = globals::d3d::context;
	auto state = globals::state;
	auto renderer = globals::game::renderer;

	// Update constant buffer before applying HDR
	UpdateHDRData();

	state->BeginPerfEvent(hdrDisplayDetected ? "HDR Output" : "SDR Processing");

	{
		auto dispatchCount = Util::GetScreenDispatchCount(false);

		auto& bloomRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kHDR_BLOOM];
		auto& adaptRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kHDR_DOWNSAMPLE13];
		
		ID3D11ShaderResourceView* sceneSRV;
		
		// When Frame Gen is active, ISHDR has already rendered to FRAMEBUFFER
		// We need to copy it to hdrTexture first to avoid read/write conflict
		if (upscaling.d3d12SwapChainActive) {
			auto& frameBufferRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
			ID3D11Resource* frameBufferResource;
			frameBufferRT.SRV->GetResource(&frameBufferResource);
			context->CopyResource(hdrTexture->resource.get(), frameBufferResource);
			sceneSRV = hdrTexture->srv.get();
		} else {
			// Normal path: read directly from kMAIN
			auto& mainRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			sceneSRV = mainRT.SRV;
		}
		
		ID3D11ShaderResourceView* views[4] = { sceneSRV, uiTexture->srv.get(), bloomRT.SRV, adaptRT.SRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);
		
		// Sampler for adaptation texture
		auto linearSampler = Deferred::GetSingleton()->linearSampler;
		context->CSSetSamplers(0, 1, &linearSampler);

		ID3D11UnorderedAccessView* uavs[1] = { outputTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbs[1] = { hdrDataCB->CB() };

		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		auto computeShader = hdrDisplayDetected ? GetHDROutputCS() : GetSDROutputCS();
		if (!computeShader) {
			logger::error("HDR: Failed to get compute shader");
			state->EndPerfEvent();
			return;
		}

		context->CSSetShader(computeShader, nullptr, 0);

		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		// Cleanup
		views[0] = nullptr;
		views[1] = nullptr;
		views[2] = nullptr;
		views[3] = nullptr;
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);
		
		ID3D11SamplerState* nullSampler = nullptr;
		context->CSSetSamplers(0, 1, &nullSampler);

		uavs[0] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		cbs[0] = { nullptr };
		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	// Copy result to appropriate destination
	// When Frame Gen is active, copy to the D3D12 swap chain buffer
	// Otherwise copy to the regular framebuffer (D3D11 swap chain path)
	ID3D11Resource* outputTextureResource;
	if (upscaling.d3d12SwapChainActive) {
		// Frame Gen path: copy to D3D12 swap chain wrapped buffer
		outputTextureResource = upscaling.dx12SwapChain.swapChainBufferWrapped->resource11;
	} else {
		// Normal path: copy to D3D11 framebuffer
		auto& outputRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		outputRT.SRV->GetResource(&outputTextureResource);
	}
	context->CopyResource(outputTextureResource, outputTexture->resource.get());

	state->EndPerfEvent();
}

void HDR::DestroyResources() const
{
	hdrTexture->srv = nullptr;
	hdrTexture->uav = nullptr;
	hdrTexture->resource = nullptr;
	delete hdrTexture;

	outputTexture->srv = nullptr;
	outputTexture->uav = nullptr;
	outputTexture->resource = nullptr;
	delete outputTexture;
	
	if (uiTexture) {
		uiTexture->srv = nullptr;
		uiTexture->uav = nullptr;
		uiTexture->rtv = nullptr;
		uiTexture->resource = nullptr;
		delete uiTexture;
	}
}

void HDR::ClearShaderCache()
{
	if (hdrOutputCS) {
		hdrOutputCS->Release();
		hdrOutputCS = nullptr;
	}
	if (sdrOutputCS) {
		sdrOutputCS->Release();
		sdrOutputCS = nullptr;
	}
	if (uiCompositeCS) {
		uiCompositeCS->Release();
		uiCompositeCS = nullptr;
	}
}

ID3D11ComputeShader* HDR::GetHDROutputCS()
{
	if (!hdrOutputCS) {
		logger::debug("Compiling HDROutputCS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines;
		// HDR mode - no defines needed, uses #else path
		hdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDROutputCS.hlsl", defines, "cs_5_0"));
		if (!hdrOutputCS) {
			logger::error("HDR: Failed to compile HDROutputCS.hlsl");
		}
	}
	return hdrOutputCS;
}

ID3D11ComputeShader* HDR::GetSDROutputCS()
{
	if (!sdrOutputCS) {
		logger::debug("Compiling HDROutputCS.hlsl (SDR mode)");
		std::vector<std::pair<const char*, const char*>> defines = { { "SDR_OUTPUT", "" } };
		// SDR mode - stays in gamma space, no linear conversion
		sdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDROutputCS.hlsl", defines, "cs_5_0"));
		if (!sdrOutputCS) {
			logger::error("HDR: Failed to compile HDROutputCS.hlsl (SDR mode)");
		}
	}
	return sdrOutputCS;
}

ID3D11ComputeShader* HDR::GetUICompositeCS()
{
	if (!uiCompositeCS) {
		logger::debug("Compiling UICompositeCS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines;
		// Tell the shader if the input (kMAIN) is HDR format or SDR
		if (hdrDisplayDetected) {
			defines.push_back({ "HDR_INPUT", "" });
			logger::debug("UICompositeCS: Compiling with HDR_INPUT (kMAIN is R16G16B16A16_FLOAT)");
		} else {
			logger::debug("UICompositeCS: Compiling without HDR_INPUT (kMAIN is SDR format)");
		}
		uiCompositeCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\UICompositeCS.hlsl", defines, "cs_5_0"));
		if (!uiCompositeCS) {
			logger::error("HDR: Failed to compile UICompositeCS.hlsl");
		}
	}
	return uiCompositeCS;
}

void HDR::UpdateHDRData() const
{
	if (!hdrDataCB)
		return;

	HDRDataCB data;

	data.parameters0 = DirectX::XMVectorSet(static_cast<float>(settings.paperWhite), static_cast<float>(settings.peakNits), settings.exposure, 0.f);
	data.parameters1 = DirectX::XMVectorSet(settings.highlights, settings.shadows, settings.contrast, settings.saturation);
	data.parameters2 = DirectX::XMVectorSet(settings.dechroma, settings.hueCorrectionStrength, settings.vanillaEyeAdaptation ? 1.f : 0.f, settings.vanillaBloom ? 1.f : 0.f);
	data.parameters3 = DirectX::XMVectorSet(settings.bypassTonemapping ? 1.f : 0.f, hdrDisplayDetected ? 1.f : 0.f, 0.f, 0.f);

	hdrDataCB->Update(data);
}
