#include "HDR.h"

#include "PCH.h"

#include "Buffer.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Upscaling.h"
#include "Util.h"
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <imgui.h>

bool HDR::isHDRMonitor = false;

bool HDR::DetectHDRDisplay()
{
	IDXGIFactory1* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
		logger::warn("[HDR] Failed to create DXGI factory for HDR detection");
		return false;
	}

	bool hdrSupported = false;
	IDXGIAdapter1* adapter = nullptr;
	
	for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
		IDXGIOutput* output = nullptr;
		for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j) {
			IDXGIOutput6* output6 = nullptr;
			if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6)))) {
				DXGI_OUTPUT_DESC1 desc;
				if (SUCCEEDED(output6->GetDesc1(&desc))) {
					if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
						hdrSupported = true;
						logger::info("[HDR] Detected HDR display (Max Luminance: {} nits)", desc.MaxLuminance);
					}
				}
				output6->Release();
			}
			output->Release();
			if (hdrSupported) break;
		}
		adapter->Release();
		if (hdrSupported) break;
	}
	
	factory->Release();
	
	isHDRMonitor = hdrSupported;
	logger::info("[HDR] HDR display detection result: {}", hdrSupported ? "HDR supported" : "SDR only");
	return hdrSupported;
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDR::Settings,
	sdrMode,
	convertToGamma,
	enableTonemapping,
	hdrPaperWhite,
	hdrPeakNits);

void HDR::DrawSettings()
{
	if (isHDRMonitor) {
		ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "HDR Display Detected");
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "SDR Display (HDR not detected)");
	}
	ImGui::Spacing();
	ImGui::TextWrapped("Note: All options OFF = Raw linear HDR output for further post-processing.");
	ImGui::Spacing();
	
	ImGui::Text("Output Mode");
	
	bool oldSdrMode = settings.sdrMode;
	ImGui::Checkbox("SDR Mode (Clamp to 0-1)", &settings.sdrMode);
	if (oldSdrMode != settings.sdrMode) {
		logger::info("HDR: sdrMode changed to: {}", settings.sdrMode);
		UpdateHDRData();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("OFF = Linear HDR passthrough. ON = Compress to SDR range.");
	}

	bool oldConvertToGamma = settings.convertToGamma;
	ImGui::Checkbox("Convert to Gamma", &settings.convertToGamma);
	if (oldConvertToGamma != settings.convertToGamma) {
		logger::info("HDR: convertToGamma changed to: {}", settings.convertToGamma);
		UpdateHDRData();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Apply gamma curve to match Vanilla Skyrim. OFF = Linear output.");
	}

	bool oldEnableTonemapping = settings.enableTonemapping;
	ImGui::Checkbox("Enable Tonemapping", &settings.enableTonemapping);
	if (oldEnableTonemapping != settings.enableTonemapping) {
		logger::info("HDR: enableTonemapping changed to: {}", settings.enableTonemapping);
		UpdateHDRData();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Apply Reinhard tonemapping to compress HDR highlights.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	
	if (!settings.sdrMode) {
		ImGui::Text("HDR Settings");
		
		uint oldPaperWhite = settings.hdrPaperWhite;
		ImGui::SliderInt("HDR Paper White (nits)", reinterpret_cast<int*>(&settings.hdrPaperWhite), 80, 500);
		if (oldPaperWhite != settings.hdrPaperWhite) {
			UpdateHDRData();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Reference white brightness for HDR content.");
		}

		uint oldPeakNits = settings.hdrPeakNits;
		ImGui::SliderInt("HDR Peak Brightness (nits)", reinterpret_cast<int*>(&settings.hdrPeakNits), 400, 10000);
		if (oldPeakNits != settings.hdrPeakNits) {
			UpdateHDRData();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Maximum brightness for HDR highlights.");
		}
	}
}

void HDR::SaveSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	o_json = settings;
}

void HDR::LoadSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	settings = o_json;
}

void HDR::RestoreDefaultSettings()
{
	bool hdrMonitor = DetectHDRDisplay();
	
	if (hdrMonitor) {
		settings.sdrMode = false;
		settings.convertToGamma = true;
		settings.enableTonemapping = true;
		settings.hdrPaperWhite = 80;
		settings.hdrPeakNits = 600;
	} else {
		settings.sdrMode = true;
		settings.convertToGamma = true;
		settings.enableTonemapping = true;
	}
}

void HDR::SetupResources()
{
	logger::info("[HDR] SetupResources called");
	
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	// Get the actual swap chain format for output texture
	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R10G10B10A2_UNORM;  // HDR format
	DXGI_SWAP_CHAIN_DESC scDesc;
	if (SUCCEEDED(globals::d3d::swapChain->GetDesc(&scDesc))) {
		swapChainFormat = scDesc.BufferDesc.Format;
		logger::info("[HDR] Swap chain format: {}", static_cast<int>(swapChainFormat));
	}

	// Intermediate texture for HDR processing
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	hdrTexture = new Texture2D(texDesc);
	hdrTexture->CreateSRV(srvDesc);
	hdrTexture->CreateUAV(uavDesc);

	// Output texture - must match swap chain format for CopyResource to work
	texDesc.Format = swapChainFormat;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	outputTexture = new Texture2D(texDesc);
	outputTexture->CreateSRV(srvDesc);
	outputTexture->CreateUAV(uavDesc);

	// UI texture for separate UI rendering - use sRGB format for gamma-correct blending
	// This ensures ImGui's alpha blending produces correct anti-aliased text edges
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

	// Create RTV for UI texture - use SRGB view for gamma-correct blending during ImGui render
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	uiTexture->CreateRTV(rtvDesc);

	hdrDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDataCB>());

	UpdateHDRData();
	
	// Set up HDR on D3D11 swap chain (when not using Frame Gen)
	auto& upscaling = globals::features::upscaling;
	if (!upscaling.d3d12SwapChainActive) {
		IDXGISwapChain4* swapChain4 = nullptr;
		if (SUCCEEDED(globals::d3d::swapChain->QueryInterface(IID_PPV_ARGS(&swapChain4)))) {
			HRESULT hr = swapChain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
			if (SUCCEEDED(hr)) {
				logger::info("[HDR] Set D3D11 swap chain color space to HDR10 (PQ/BT.2020)");
				
				DXGI_HDR_METADATA_HDR10 hdrMetadata = {};
				// BT.2020 color primaries (hardcoded)
				hdrMetadata.RedPrimary[0] = 34000;    // 0.708 * 50000
				hdrMetadata.RedPrimary[1] = 16000;    // 0.292 * 50000
				hdrMetadata.GreenPrimary[0] = 8500;   // 0.170 * 50000
				hdrMetadata.GreenPrimary[1] = 39850;  // 0.797 * 50000
				hdrMetadata.BluePrimary[0] = 6550;    // 0.131 * 50000
				hdrMetadata.BluePrimary[1] = 2300;    // 0.046 * 50000
				hdrMetadata.WhitePoint[0] = 15635;    // D65: 0.3127 * 50000
				hdrMetadata.WhitePoint[1] = 16450;    // D65: 0.3290 * 50000
				hdrMetadata.MaxMasteringLuminance = settings.hdrPeakNits * 10000;
				hdrMetadata.MinMasteringLuminance = 1;
				hdrMetadata.MaxContentLightLevel = static_cast<UINT16>(settings.hdrPeakNits);
				hdrMetadata.MaxFrameAverageLightLevel = static_cast<UINT16>(settings.hdrPaperWhite);
				
				swapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdrMetadata), &hdrMetadata);
				logger::info("[HDR] Set D3D11 swap chain HDR10 metadata");
			} else {
				logger::warn("[HDR] Failed to set D3D11 swap chain color space (HDR not supported by display?)");
			}
			swapChain4->Release();
		} else {
			logger::warn("[HDR] D3D11 swap chain does not support IDXGISwapChain4");
		}
	}
	
	logger::info("[HDR] SetupResources complete - hdrDataCB={}, hdrTexture={}, outputTexture={}", 
		hdrDataCB ? "valid" : "NULL",
		hdrTexture ? "valid" : "NULL",
		outputTexture ? "valid" : "NULL");
}

void HDR::BeginUIRendering()
{
	// Skip if D3D12 frame gen is active - it has its own UI buffer handling
	if (globals::features::upscaling.d3d12SwapChainActive) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::info("[HDR] BeginUIRendering skipped - d3d12SwapChainActive");
			loggedOnce = true;
		}
		return;
	}

	if (!uiTexture || !uiTexture->rtv) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] BeginUIRendering skipped - uiTexture={}, rtv={}", 
				uiTexture ? "valid" : "NULL",
				(uiTexture && uiTexture->rtv) ? "valid" : "NULL");
			loggedOnce = true;
		}
		return;
	}

	static bool loggedOnce = false;
	if (!loggedOnce) {
		logger::info("[HDR] BeginUIRendering - setting uiTexture as render target for ImGui");
		loggedOnce = true;
	}

	auto context = globals::d3d::context;

	// Save current render target so we can restore after ImGui
	context->OMGetRenderTargets(1, &savedRTV, &savedDSV);

	// Do NOT clear - vanilla UI has already rendered to uiTexture via SetUIBuffer()
	// Just ensure ImGui also renders to the same texture
	ID3D11RenderTargetView* rtv = uiTexture->rtv.get();
	context->OMSetRenderTargets(1, &rtv, nullptr);

	renderingUI = true;
}

void HDR::EndUIRendering()
{
	// Skip if D3D12 frame gen is active
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;

	if (!renderingUI) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] EndUIRendering called but renderingUI=false");
			loggedOnce = true;
		}
		return;
	}

	static bool loggedOnce = false;
	if (!loggedOnce) {
		logger::info("[HDR] EndUIRendering - restoring render target, UI captured");
		loggedOnce = true;
	}

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

void HDR::SetUIBuffer()
{
	// Skip if D3D12 frame gen is active - it has its own UI buffer handling
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;

	// Skip if resources aren't ready
	if (!uiTexture || !uiTexture->rtv || !hdrDataCB || !outputTexture) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] SetUIBuffer skipped - resources not ready: uiTexture={}, rtv={}, hdrDataCB={}, outputTexture={}",
				uiTexture ? "valid" : "NULL",
				(uiTexture && uiTexture->rtv) ? "valid" : "NULL",
				hdrDataCB ? "valid" : "NULL",
				outputTexture ? "valid" : "NULL");
			loggedOnce = true;
		}
		return;
	}

	// Follow Frame Gen approach: redirect kFRAMEBUFFER.RTV to our UI texture
	// This way vanilla Skyrim UI renders to our texture
	auto& data = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
	
	// Save original RTV for restoration after Present
	if (!savedFramebufferRTV) {
		savedFramebufferRTV = data.RTV;
	}
	
	// Clear UI texture before vanilla UI renders (just like Frame Gen does after Present)
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	globals::d3d::context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);
	
	// Redirect to our UI texture
	data.RTV = uiTexture->rtv.get();
	globals::d3d::context->OMSetRenderTargets(1, &data.RTV, nullptr);

	static bool loggedOnce = false;
	if (!loggedOnce) {
		logger::info("[HDR] SetUIBuffer - redirected kFRAMEBUFFER.RTV to uiTexture");
		loggedOnce = true;
	}
}

void HDR::ClearUIBuffer()
{
	// Skip if D3D12 frame gen is active
	if (globals::features::upscaling.d3d12SwapChainActive)
		return;

	if (!uiTexture || !uiTexture->rtv)
		return;

	// Clear UI buffer with transparent black for next frame
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	globals::d3d::context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);

	// Restore original framebuffer RTV
	if (savedFramebufferRTV) {
		auto& data = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		data.RTV = savedFramebufferRTV;
		savedFramebufferRTV = nullptr;
	}
}

void HDR::ApplyHDR()
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	if (!hdrDataCB || !hdrTexture || !outputTexture) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] ApplyHDR early return: hdrDataCB={}, hdrTexture={}, outputTexture={}", 
				hdrDataCB ? "valid" : "NULL",
				hdrTexture ? "valid" : "NULL", 
				outputTexture ? "valid" : "NULL");
			loggedOnce = true;
		}
		return;
	}

	auto& upscaling = globals::features::upscaling;

	auto context = globals::d3d::context;
	auto state = globals::state;
	auto renderer = globals::game::renderer;

	// Update constant buffer before applying HDR
	UpdateHDRData();

	state->BeginPerfEvent("HDR Processing");

	static bool loggedDispatch = false;
	if (!loggedDispatch) {
		logger::info("[HDR] ApplyHDR running - dispatching compute shader");
		logger::info("[HDR] uiTexture={}, uiTexture->srv={}", 
			uiTexture ? "valid" : "NULL",
			(uiTexture && uiTexture->srv) ? "valid" : "NULL");
		loggedDispatch = true;
	}

	{
		auto dispatchCount = Util::GetScreenDispatchCount(false);

		// Both Frame Gen and non-Frame Gen paths read from kMAIN for consistent behavior
		auto& mainRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		ID3D11ShaderResourceView* sceneSRV = mainRT.SRV;

		// Bind scene texture (t0) and UI texture (t1)
		// Use Frame Gen's uiBufferWrapped when active, otherwise our uiTexture
		ID3D11ShaderResourceView* uiSRV = nullptr;
		if (upscaling.d3d12SwapChainActive && upscaling.dx12SwapChain.uiBufferWrapped) {
			uiSRV = upscaling.dx12SwapChain.uiBufferWrapped->srv;
		} else if (uiTexture && uiTexture->srv) {
			uiSRV = uiTexture->srv.get();
		}
		ID3D11ShaderResourceView* views[2] = { sceneSRV, uiSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { outputTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbs[1] = { hdrDataCB->CB() };

		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		auto computeShader = GetHDROutputCS();
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
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		uavs[0] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		cbs[0] = { nullptr };
		context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	// Copy result to appropriate destination
	// When Frame Gen is active, copy to the D3D12 swap chain buffer
	// Otherwise copy directly to the D3D11 swap chain back buffer
	if (upscaling.d3d12SwapChainActive) {
		// Frame Gen path: copy to D3D12 swap chain wrapped buffer
		context->CopyResource(upscaling.dx12SwapChain.swapChainBufferWrapped->resource11, outputTexture->resource.get());
	} else {
		// Normal path: copy directly to swap chain back buffer
		ID3D11Texture2D* backBuffer = nullptr;
		HRESULT hr = globals::d3d::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (SUCCEEDED(hr) && backBuffer) {
			context->CopyResource(backBuffer, outputTexture->resource.get());
			backBuffer->Release();
			static bool loggedOnce = false;
			if (!loggedOnce) {
				logger::info("[HDR] Copying to D3D11 swap chain back buffer");
				loggedOnce = true;
			}
		} else {
			static bool loggedOnce = false;
			if (!loggedOnce) {
				logger::error("[HDR] Failed to get swap chain back buffer!");
				loggedOnce = true;
			}
		}
	}

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
}

ID3D11ComputeShader* HDR::GetHDROutputCS()
{
	if (!hdrOutputCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		hdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDROutputCS.hlsl", defines, "cs_5_0"));
		if (!hdrOutputCS) {
			logger::error("HDR: Failed to compile HDROutputCS.hlsl");
		}
	}
	return hdrOutputCS;
}

void HDR::UpdateHDRData() const
{
	if (!hdrDataCB) {
		static bool loggedOnce = false;
		if (!loggedOnce) {
			logger::warn("[HDR] UpdateHDRData called but hdrDataCB is null!");
			loggedOnce = true;
		}
		return;
	}

	HDRDataCB data;
	
	data.parameters0 = DirectX::XMVectorSet(
		settings.sdrMode ? 1.f : 0.f, 
		settings.convertToGamma ? 1.f : 0.f, 
		settings.enableTonemapping ? 1.f : 0.f,
		static_cast<float>(settings.hdrPeakNits));
	data.parameters1 = DirectX::XMVectorSet(
		static_cast<float>(settings.hdrPaperWhite),
		0.f, 0.f, 0.f);
	hdrDataCB->Update(data);
}

void HDR::UpdateHDRMetadata() const
{
	// Hardcoded BT.2020 color primaries and D65 white point
	DXGI_HDR_METADATA_HDR10 hdrMetadata = {};
	hdrMetadata.RedPrimary[0] = 34000;    // 0.680
	hdrMetadata.RedPrimary[1] = 16000;    // 0.320
	hdrMetadata.GreenPrimary[0] = 13250;  // 0.265
	hdrMetadata.GreenPrimary[1] = 34500;  // 0.690
	hdrMetadata.BluePrimary[0] = 7500;    // 0.150
	hdrMetadata.BluePrimary[1] = 3000;    // 0.060
	hdrMetadata.WhitePoint[0] = 15635;    // 0.3127
	hdrMetadata.WhitePoint[1] = 16450;    // 0.3290
	hdrMetadata.MaxMasteringLuminance = 1000 * 10000;
	hdrMetadata.MinMasteringLuminance = 100;
	hdrMetadata.MaxContentLightLevel = 1000;
	hdrMetadata.MaxFrameAverageLightLevel = 400;

	auto& upscaling = globals::features::upscaling;

	if (upscaling.d3d12SwapChainActive && upscaling.dx12SwapChain.swapChain) {
		upscaling.dx12SwapChain.swapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdrMetadata), &hdrMetadata);
		logger::info("[HDR] Updated D3D12 swap chain HDR10 metadata");
	} else {
		IDXGISwapChain4* swapChain4 = nullptr;
		if (SUCCEEDED(globals::d3d::swapChain->QueryInterface(IID_PPV_ARGS(&swapChain4)))) {
			swapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdrMetadata), &hdrMetadata);
			swapChain4->Release();
			logger::info("[HDR] Updated D3D11 swap chain HDR10 metadata");
		}
	}
}
