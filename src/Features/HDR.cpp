#include "HDR.h"

#include "PCH.h"

#include "Buffer.h"
#include "Globals.h"
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
	tonemapOperator,
	exposure,
	highlights,
	shadows,
	contrast,
	saturation,
	dechroma,
	hueCorrectionStrength,
	paperWhite,
	peakNits);

void HDR::DrawSettings()
{
	const char* operators[] = {
		"None",
		"Saturate",
		"Frostbite",
		"Reinhard-Jodie",
		"ACES",
		"Uncharted 2",
		"DICE Plus",
		"RenoDRT"
	};

	if (hdrDisplayDetected) {
		ImGui::TextColored({ 0, 1, 0, 1 }, "HDR display detected");
		if (ImGui::Checkbox("HDR Enabled", &settings.enableHDR)) {
			enabledSaveLater = settings.enableHDR;
			// Update swap chain color space when HDR setting changes
			if (!globals::features::upscaling.d3d12SwapChainActive) {
				SetSwapChainColorSpace(settings.enableHDR);
			}
		}
	} else {
		ImGui::TextColored({ 1, 0.5f, 0, 1 }, "No HDR display detected - using SDR tonemapping");
		if (ImGui::Checkbox("Enable Tonemapping", &settings.enableHDR)) {
			enabledSaveLater = settings.enableHDR;
		}
	}

	if (ImGui::Button("Reset HDR Settings", { -1, 0 })) {
		settings.tonemapOperator = 0;

		settings.exposure = 1.0f;
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

	ImGui::SliderInt("Tonemap Operator", reinterpret_cast<int*>(&settings.tonemapOperator), 0, 7, std::format("{}", operators[settings.tonemapOperator]).c_str());

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

	// Try to set color space directly - skip CheckColorSpaceSupport as it can crash with Streamline
	hr = swapChain3->SetColorSpace1(colorSpace);
	if (FAILED(hr)) {
		logger::warn("HDR: Failed to set color space (hr=0x{:08X}), trying fallback", static_cast<unsigned>(hr));
		// If HDR fails, try SDR as fallback
		if (enableHDR) {
			hr = swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
			if (SUCCEEDED(hr)) {
				logger::info("HDR: Fell back to SDR color space");
				return false;
			}
		}
		return false;
	}

	logger::info("HDR: Set swap chain color space to {}", enableHDR ? "HDR10 (PQ/BT.2020)" : "SDR (sRGB/BT.709)");
	return true;
}

void HDR::BeginUIRendering()
{
	if (!hdrDisplayDetected || !settings.enableHDR)
		return;
		
	auto swapChain = globals::d3d::swapChain;
	if (!swapChain)
		return;

	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
	if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) || !swapChain3)
		return;

	swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
}

void HDR::EndUIRendering()
{
	if (!hdrDisplayDetected || !settings.enableHDR)
		return;
		
	auto swapChain = globals::d3d::swapChain;
	if (!swapChain)
		return;

	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
	if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) || !swapChain3)
		return;

	swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
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
		// For D3D12 proxy (frame generation), color space was already set during swap chain creation
		// For D3D11 path, we need to set it here
		if (!upscaling.d3d12SwapChainActive) {
			// D3D11 path - safe to call SetSwapChainColorSpace
			SetSwapChainColorSpace(settings.enableHDR);
		} else {
			logger::info("HDR: D3D12 proxy active, color space set during swap chain creation");
		}
	} else {
		logger::info("No HDR display detected - SDR tonemapping available");
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

	hdrDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDataCB>());

	logger::info("HDR: Resources created - hdrTexture format: {}, outputTexture format: {}, mainFormat: {}",
		(int)DXGI_FORMAT_R16G16B16A16_FLOAT, (int)texDesc.Format, (int)mainFormat);

	UpdateHDRData();
}

void HDR::ApplyHDR()
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	if (!settings.enableHDR)
		return;

	if (!hdrDataCB || !hdrTexture || !outputTexture) {
		logger::warn("HDR: Resources not initialized - hdrDataCB:{} hdrTexture:{} outputTexture:{}", 
			(void*)hdrDataCB, (void*)hdrTexture, (void*)outputTexture);
		return;
	}

	auto context = globals::d3d::context;
	auto state = globals::state;
	auto renderer = globals::game::renderer;

	state->BeginPerfEvent(hdrDisplayDetected ? "HDR" : "Tonemapping");

	// Update constant buffer before applying HDR
	UpdateHDRData();

	auto& inputRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	{
		auto dispatchCount = Util::GetScreenDispatchCount(false);

		// Read directly from kMAIN's SRV - no format conversion needed
		ID3D11ShaderResourceView* views[1] = { inputRT.SRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

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
		views[0] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		uavs[0] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		cbs[0] = { nullptr };
		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	// Copy result back to framebuffer
	ID3D11Resource* outputTextureResource;
	auto& outputRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
	outputRT.SRV->GetResource(&outputTextureResource);
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
}

ID3D11ComputeShader* HDR::GetHDROutputCS()
{
	if (!hdrOutputCS) {
		logger::debug("Compiling HDROutputCS.hlsl");
		hdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDROutputCS.hlsl", {}, "cs_5_0"));
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
		sdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDROutputCS.hlsl", { { "SDR_OUTPUT", "" } }, "cs_5_0"));
		if (!sdrOutputCS) {
			logger::error("HDR: Failed to compile HDROutputCS.hlsl (SDR mode)");
		}
	}
	return sdrOutputCS;
}

void HDR::UpdateHDRData() const
{
	if (!hdrDataCB)
		return;

	HDRDataCB data;

	data.parameters0 = DirectX::XMVectorSet(static_cast<float>(settings.tonemapOperator), static_cast<float>(settings.paperWhite), static_cast<float>(settings.peakNits), settings.exposure);
	data.parameters1 = DirectX::XMVectorSet(settings.highlights, settings.shadows, settings.contrast, settings.saturation);
	data.parameters2 = DirectX::XMVectorSet(settings.dechroma, settings.hueCorrectionStrength, 0.f, 0.f);

	hdrDataCB->Update(data);
}
