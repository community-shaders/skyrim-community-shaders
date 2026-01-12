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

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDR::Settings,
	tonemapToSDR,
	paperWhite,
	peakNits,
	redPrimaryX,
	redPrimaryY,
	greenPrimaryX,
	greenPrimaryY,
	bluePrimaryX,
	bluePrimaryY,
	whitePointX,
	whitePointY,
	maxMasteringLuminance,
	minMasteringLuminance,
	maxContentLightLevel,
	maxFrameAverageLightLevel);

void HDR::DrawSettings()
{
	bool oldTonemapToSDR = settings.tonemapToSDR;
	ImGui::Checkbox("Tonemap to SDR", &settings.tonemapToSDR);
	if (oldTonemapToSDR != settings.tonemapToSDR) {
		logger::info("HDR: tonemapToSDR changed to: {}", settings.tonemapToSDR);
		logger::info("HDR: hdrDataCB = {}", hdrDataCB ? "valid" : "NULL");
		UpdateHDRData();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Apply SDR tonemapping for SDR displays. Disable for HDR displays.");
	}

	ImGui::Spacing();
	uint oldPaperWhite = settings.paperWhite;
	ImGui::SliderInt("Paper White (nits)", reinterpret_cast<int*>(&settings.paperWhite), 80, 500);
	if (oldPaperWhite != settings.paperWhite) {
		UpdateHDRData();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Reference white brightness for SDR content.");
	}

	uint oldPeakNits = settings.peakNits;
	ImGui::SliderInt("Peak Brightness (nits)", reinterpret_cast<int*>(&settings.peakNits), 400, 10000);
	if (oldPeakNits != settings.peakNits) {
		UpdateHDRData();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Maximum brightness for HDR highlights.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	
	if (ImGui::CollapsingHeader("HDR10 Display Metadata")) {
		bool metadataChanged = false;

		ImGui::Text("Color Primaries (CIE 1931 xy coordinates)");
		
		uint oldRedX = settings.redPrimaryX;
		uint oldRedY = settings.redPrimaryY;
		ImGui::SliderInt("Red Primary X", reinterpret_cast<int*>(&settings.redPrimaryX), 0, 50000);
		ImGui::SliderInt("Red Primary Y", reinterpret_cast<int*>(&settings.redPrimaryY), 0, 50000);
		if (oldRedX != settings.redPrimaryX || oldRedY != settings.redPrimaryY)
			metadataChanged = true;

		uint oldGreenX = settings.greenPrimaryX;
		uint oldGreenY = settings.greenPrimaryY;
		ImGui::SliderInt("Green Primary X", reinterpret_cast<int*>(&settings.greenPrimaryX), 0, 50000);
		ImGui::SliderInt("Green Primary Y", reinterpret_cast<int*>(&settings.greenPrimaryY), 0, 50000);
		if (oldGreenX != settings.greenPrimaryX || oldGreenY != settings.greenPrimaryY)
			metadataChanged = true;

		uint oldBlueX = settings.bluePrimaryX;
		uint oldBlueY = settings.bluePrimaryY;
		ImGui::SliderInt("Blue Primary X", reinterpret_cast<int*>(&settings.bluePrimaryX), 0, 50000);
		ImGui::SliderInt("Blue Primary Y", reinterpret_cast<int*>(&settings.bluePrimaryY), 0, 50000);
		if (oldBlueX != settings.bluePrimaryX || oldBlueY != settings.bluePrimaryY)
			metadataChanged = true;

		ImGui::Spacing();
		ImGui::Text("White Point");
		
		uint oldWhiteX = settings.whitePointX;
		uint oldWhiteY = settings.whitePointY;
		ImGui::SliderInt("White Point X", reinterpret_cast<int*>(&settings.whitePointX), 0, 50000);
		ImGui::SliderInt("White Point Y", reinterpret_cast<int*>(&settings.whitePointY), 0, 50000);
		if (oldWhiteX != settings.whitePointX || oldWhiteY != settings.whitePointY)
			metadataChanged = true;

		ImGui::Spacing();
		ImGui::Text("Luminance");
		
		uint oldMaxMaster = settings.maxMasteringLuminance;
		ImGui::SliderInt("Max Mastering Luminance (nits)", reinterpret_cast<int*>(&settings.maxMasteringLuminance), 400, 10000);
		if (oldMaxMaster != settings.maxMasteringLuminance)
			metadataChanged = true;

		uint oldMinMaster = settings.minMasteringLuminance;
		ImGui::SliderInt("Min Mastering Luminance (0.0001 nits)", reinterpret_cast<int*>(&settings.minMasteringLuminance), 1, 1000);
		if (oldMinMaster != settings.minMasteringLuminance)
			metadataChanged = true;

		uint oldMaxCLL = settings.maxContentLightLevel;
		ImGui::SliderInt("Max Content Light Level (nits)", reinterpret_cast<int*>(&settings.maxContentLightLevel), 400, 10000);
		if (oldMaxCLL != settings.maxContentLightLevel)
			metadataChanged = true;

		uint oldMaxFALL = settings.maxFrameAverageLightLevel;
		ImGui::SliderInt("Max Frame Average Light Level (nits)", reinterpret_cast<int*>(&settings.maxFrameAverageLightLevel), 100, 4000);
		if (oldMaxFALL != settings.maxFrameAverageLightLevel)
			metadataChanged = true;

		if (metadataChanged) {
			UpdateHDRMetadata();
		}

		if (ImGui::Button("Reset to BT.2020 Defaults")) {
			settings.redPrimaryX = 34000;
			settings.redPrimaryY = 16000;
			settings.greenPrimaryX = 13250;
			settings.greenPrimaryY = 34500;
			settings.bluePrimaryX = 7500;
			settings.bluePrimaryY = 3000;
			settings.whitePointX = 15635;
			settings.whitePointY = 16450;
			settings.maxMasteringLuminance = 1000;
			settings.minMasteringLuminance = 100;
			settings.maxContentLightLevel = 1000;
			settings.maxFrameAverageLightLevel = 400;
			UpdateHDRMetadata();
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
	settings = {};
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

	// UI texture for separate UI rendering
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
				hdrMetadata.RedPrimary[0] = static_cast<UINT16>(settings.redPrimaryX);
				hdrMetadata.RedPrimary[1] = static_cast<UINT16>(settings.redPrimaryY);
				hdrMetadata.GreenPrimary[0] = static_cast<UINT16>(settings.greenPrimaryX);
				hdrMetadata.GreenPrimary[1] = static_cast<UINT16>(settings.greenPrimaryY);
				hdrMetadata.BluePrimary[0] = static_cast<UINT16>(settings.bluePrimaryX);
				hdrMetadata.BluePrimary[1] = static_cast<UINT16>(settings.bluePrimaryY);
				hdrMetadata.WhitePoint[0] = static_cast<UINT16>(settings.whitePointX);
				hdrMetadata.WhitePoint[1] = static_cast<UINT16>(settings.whitePointY);
				hdrMetadata.MaxMasteringLuminance = settings.maxMasteringLuminance * 10000;
				hdrMetadata.MinMasteringLuminance = settings.minMasteringLuminance;
				hdrMetadata.MaxContentLightLevel = static_cast<UINT16>(settings.maxContentLightLevel);
				hdrMetadata.MaxFrameAverageLightLevel = static_cast<UINT16>(settings.maxFrameAverageLightLevel);
				
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
		logger::info("[HDR] BeginUIRendering - redirecting UI to uiTexture");
		loggedOnce = true;
	}

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

		// Bind scene texture (t0) and UI texture (t1)
		ID3D11ShaderResourceView* uiSRV = (uiTexture && uiTexture->srv) ? uiTexture->srv.get() : nullptr;
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
	data.parameters0 = DirectX::XMVectorSet(static_cast<float>(settings.paperWhite), static_cast<float>(settings.peakNits), settings.tonemapToSDR ? 1.f : 0.f, 0.f);
	hdrDataCB->Update(data);
}

void HDR::UpdateHDRMetadata() const
{
	DXGI_HDR_METADATA_HDR10 hdrMetadata = {};
	hdrMetadata.RedPrimary[0] = static_cast<UINT16>(settings.redPrimaryX);
	hdrMetadata.RedPrimary[1] = static_cast<UINT16>(settings.redPrimaryY);
	hdrMetadata.GreenPrimary[0] = static_cast<UINT16>(settings.greenPrimaryX);
	hdrMetadata.GreenPrimary[1] = static_cast<UINT16>(settings.greenPrimaryY);
	hdrMetadata.BluePrimary[0] = static_cast<UINT16>(settings.bluePrimaryX);
	hdrMetadata.BluePrimary[1] = static_cast<UINT16>(settings.bluePrimaryY);
	hdrMetadata.WhitePoint[0] = static_cast<UINT16>(settings.whitePointX);
	hdrMetadata.WhitePoint[1] = static_cast<UINT16>(settings.whitePointY);
	hdrMetadata.MaxMasteringLuminance = settings.maxMasteringLuminance * 10000;
	hdrMetadata.MinMasteringLuminance = settings.minMasteringLuminance;
	hdrMetadata.MaxContentLightLevel = static_cast<UINT16>(settings.maxContentLightLevel);
	hdrMetadata.MaxFrameAverageLightLevel = static_cast<UINT16>(settings.maxFrameAverageLightLevel);

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
