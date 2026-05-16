#include "DLSSperf.h"
#include "State.h"
#include "Upscaling.h"

// Quality mode → render-scale resolution is supplied by the FFX SDK helper
// (same one Upscaling.cpp uses at ConfigureUpscaling), avoiding a duplicate
// scale table here. Decoupled from the original PR's DlssEnhancer::Bridge so
// DLSSperf can ship without the larger enhancer framework.
#include <FidelityFX/host/ffx_fsr3.h>

void DLSSperf::InstallRenderTargetSizeHook()
{
	if (!globals::game::isVR)
		return;

	if (hookActive)
		return;

	// Eager capture — get real HMD resolution BEFORE installing hook
	auto* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		logger::error("[DLSSperf] BSOpenVR or vrSystem not available — hook NOT installed");
		return;
	}

	uint32_t w = 0, h = 0;
	openvr->vrSystem->GetRecommendedRenderTargetSize(&w, &h);
	if (w == 0 || h == 0) {
		logger::error("[DLSSperf] GetRecommendedRenderTargetSize returned {}x{} — hook NOT installed", w, h);
		return;
	}

	displayEyeWidth = w;
	displayEyeHeight = h;

	// BSShaderRenderTargets::Create runs after SKSE feature settings load, so
	// upscaling.settings.qualityMode here reflects the user-saved value. We
	// snapshot the corresponding scale at install time and never re-read it —
	// the engine's RT allocations happen once, so a later UI quality change
	// can't shrink/grow RTs anyway. (Requires a game restart, same as DLSS.)
	const uint32_t qualityMode = globals::features::upscaling.settings.qualityMode;
	const float scale = ffxFsr3GetUpscaleRatioFromQualityMode(static_cast<FfxFsr3QualityMode>(qualityMode));
	renderEyeWidth = std::max<uint32_t>(1, (uint32_t)(w / scale));
	renderEyeHeight = std::max<uint32_t>(1, (uint32_t)(h / scale));

	stl::write_vfunc<0x12, GetRenderTargetSize_Hook>(RE::VTABLE_BSOpenVR[0]);
	hookActive = true;
}

void DLSSperf::GetRenderTargetSize_Hook::thunk(RE::BSOpenVR* a_this, uint32_t* a_width, uint32_t* a_height)
{
	// Call original to get real HMD resolution
	func(a_this, a_width, a_height);

	auto& dlssPerf = globals::features::dlssPerf;

	*a_width = dlssPerf.renderEyeWidth;
	*a_height = dlssPerf.renderEyeHeight;
}

void DLSSperf::SetupResources()
{
	if (!globals::game::isVR)
		return;

	auto renderer = globals::game::renderer;
	auto& mainRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	if (!mainRT.texture) {
		logger::error("[DLSSperf] kMAIN texture not available in SetupResources");
		return;
	}

	D3D11_TEXTURE2D_DESC mainDesc{};
	static_cast<ID3D11Texture2D*>(mainRT.texture)->GetDesc(&mainDesc);

	D3D11_TEXTURE2D_DESC desc{};
	if (hookActive) {
		desc.Width = displayEyeWidth * 2;
		desc.Height = displayEyeHeight;
	} else {
		desc.Width = mainDesc.Width;
		desc.Height = mainDesc.Height;
	}
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = mainDesc.Format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;

	auto device = globals::d3d::device;
	HRESULT hr = device->CreateTexture2D(&desc, nullptr, testTexture.put());
	if (FAILED(hr)) {
		logger::error("[DLSSperf] Failed to create test texture: {:#x}", (uint32_t)hr);
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	hr = device->CreateShaderResourceView(testTexture.get(), &srvDesc, testTextureSRV.put());
	if (FAILED(hr)) {
		logger::error("[DLSSperf] Failed to create test texture SRV: {:#x}", (uint32_t)hr);
		testTexture = nullptr;
		testTextureUAV = nullptr;
		return;
	}

	// UAV for testTexture
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		hr = device->CreateUnorderedAccessView(testTexture.get(), &uavDesc, testTextureUAV.put());
		if (FAILED(hr)) {
			logger::error("[DLSSperf] Failed to create testTexture UAV: {:#x}", (uint32_t)hr);
		}
	}

	// RTV for testTexture (ISRefraction output)
	if (hookActive) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = desc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		hr = device->CreateRenderTargetView(testTexture.get(), &rtvDesc, testTextureRTV.put());
		if (FAILED(hr)) {
			logger::error("[DLSSperf] Failed to create testTexture RTV: {:#x}", (uint32_t)hr);
		}
	}

	// refraTempTex: copy of testTexture for ISRefraction input
	if (hookActive) {
		D3D11_TEXTURE2D_DESC refraDesc = desc;
		refraDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		hr = device->CreateTexture2D(&refraDesc, nullptr, refraTempTex.put());
		if (FAILED(hr)) {
			logger::error("[DLSSperf] Failed to create refraTempTex: {:#x}", (uint32_t)hr);
		} else {
			D3D11_SHADER_RESOURCE_VIEW_DESC refraSrvDesc{};
			refraSrvDesc.Format = refraDesc.Format;
			refraSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			refraSrvDesc.Texture2D.MipLevels = 1;
			refraSrvDesc.Texture2D.MostDetailedMip = 0;
			hr = device->CreateShaderResourceView(refraTempTex.get(), &refraSrvDesc, refraTempSRV.put());
			if (FAILED(hr)) {
				logger::error("[DLSSperf] Failed to create refraTempSRV: {:#x}", (uint32_t)hr);
				refraTempTex = nullptr;
			}
		}
	}

	// Fake DepthStencil at DisplayRes, matching engine kMAIN DS format.
	if (hookActive) {
		auto& dsData = renderer->GetDepthStencilData();
		auto* mainDSTex = dsData.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture;
		if (mainDSTex) {
			D3D11_TEXTURE2D_DESC dsDesc{};
			mainDSTex->GetDesc(&dsDesc);

			D3D11_TEXTURE2D_DESC fakeDesc = dsDesc;
			fakeDesc.Width = displayEyeWidth * 2;
			fakeDesc.Height = displayEyeHeight;

			HRESULT hr2 = device->CreateTexture2D(&fakeDesc, nullptr, fakeDS.put());
			if (FAILED(hr2)) {
				logger::error("[DLSSperf] Failed to create fake DS texture: {:#x}", (uint32_t)hr2);
			} else {
				// Create DSV — format depends on typeless base format
				D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
				dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Texture2D.MipSlice = 0;

				// Map typeless→typed DSV format
				if (fakeDesc.Format == DXGI_FORMAT_R32G8X24_TYPELESS)
					dsvDesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
				else if (fakeDesc.Format == DXGI_FORMAT_R24G8_TYPELESS)
					dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				else if (fakeDesc.Format == DXGI_FORMAT_R32_TYPELESS)
					dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
				else if (fakeDesc.Format == DXGI_FORMAT_R16_TYPELESS)
					dsvDesc.Format = DXGI_FORMAT_D16_UNORM;
				else
					dsvDesc.Format = fakeDesc.Format;  // fallback: hope it's already a DS format

				hr2 = device->CreateDepthStencilView(fakeDS.get(), &dsvDesc, fakeDSV.put());
				if (FAILED(hr2)) {
					logger::error("[DLSSperf] Failed to create fake DSV: {:#x}", (uint32_t)hr2);
					fakeDS = nullptr;
				}
			}
		} else {
			logger::warn("[DLSSperf] kMAIN DS texture not available, skipping fake DS creation");
		}
	}

	if (hookActive && fakeDSV) {
		auto* ctx = globals::d3d::context;
		ctx->ClearDepthStencilView(fakeDSV.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}

	// IS shader hooks (must be installed AFTER FrameAnnotations)
	if (hookActive && !tonemapHookInstalled) {
		stl::write_vfunc<0x1, TonemapRender_Hook>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[3]);
		tonemapHookInstalled = true;
	}

	if (hookActive && !refractionHookInstalled && testTextureRTV && refraTempSRV) {
		stl::write_vfunc<0x1, RefractionRender_Hook>(RE::VTABLE_BSImagespaceShaderRefraction[3]);
		refractionHookInstalled = true;
	}

	if (hookActive && !uiPassHookInstalled && fakeDSV) {
		stl::write_vfunc<0x2A, UIPassDispatch_Hook>(RE::VTABLE_BSShaderAccumulator[0]);
		uiPassHookInstalled = true;
	}

	// PlayerView end hook: chains after FrameAnnotations' Main_RenderPlayerView.
	// Clears postChainDone so Present-前 UI and next frame use normal VP.
	if (hookActive && !playerViewHookInstalled) {
		stl::detour_thunk<PlayerViewRender_Hook>(REL::RelocationID(35560, 36559));
		playerViewHookInstalled = true;
	}

	// Downscale shaders
	if (hookActive && !bilinearCopyPS) {
		bilinearCopyPS.attach(static_cast<ID3D11PixelShader*>(
			Util::CompileShader(L"Data/Shaders/DLSSperf/BilinearCopyPS.hlsl", { { "PSHADER", "" } }, "ps_5_0")));
		if (!bilinearCopyPS)
			logger::error("[DLSSperf] Failed to compile BilinearCopyPS");
	}
	if (hookActive && !bilinearCopyVS) {
		bilinearCopyVS.attach(static_cast<ID3D11VertexShader*>(
			Util::CompileShader(L"Data/Shaders/Upscaling/UpscaleVS.hlsl", { { "VSHADER", "" } }, "vs_5_0")));
		if (!bilinearCopyVS)
			logger::error("[DLSSperf] Failed to compile BilinearCopyVS");
	}
	if (hookActive && !linearSampler) {
		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxAnisotropy = 1;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&sd, linearSampler.put())))
			logger::error("[DLSSperf] Failed to create linear sampler");
	}
}

// ============================================================================
// TonemapRender_Hook: IS shader hook for ISHDRTonemapBlendCinematic
// ============================================================================
// Installed via stl::write_vfunc<0x1> on vtable[3], chains after FrameAnnotations.
// Inner layer of two-layer swap: swaps kMAIN SRV → testTextureSRV and
// kMAIN DS → fakeDS before tonemap Render(), restores after.

void DLSSperf::TonemapRender_Hook::thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	auto& dlssPerf = globals::features::dlssPerf;

	if (!dlssPerf.hookActive || !dlssPerf.testTextureSRV || !dlssPerf.fakeDSV) {
		func(imageSpaceShader, shape, param);
		return;
	}

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& rtData = renderer->GetRuntimeData();
	auto& dsData = renderer->GetDepthStencilData();

	// --- Swap kMAIN SRV → testTextureSRV (so tonemap reads 3k upscaled color) ---
	auto& kmainRT = rtData.renderTargets[RE::RENDER_TARGETS::kMAIN];
	dlssPerf.savedKMainSRV = kmainRT.SRV;
	kmainRT.SRV = dlssPerf.testTextureSRV.get();

	// --- Also swap kMAIN_COPY SRV (refraction path reads this instead of kMAIN) ---
	auto& kmainCopyRT = rtData.renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];
	dlssPerf.savedKMainCopySRV = kmainCopyRT.SRV;
	kmainCopyRT.SRV = dlssPerf.testTextureSRV.get();

	// --- Swap kMAIN DS views → fakeDS (so 3k RT doesn't mismatch 1k DS) ---
	auto& kmainDS = dsData.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	for (int i = 0; i < 8; i++) {
		dlssPerf.savedKMainViews[i] = kmainDS.views[i];
		if (kmainDS.views[i])
			kmainDS.views[i] = dlssPerf.fakeDSV.get();
	}
	for (int i = 0; i < 8; i++) {
		dlssPerf.savedKMainReadOnlyViews[i] = kmainDS.readOnlyViews[i];
		if (kmainDS.readOnlyViews[i])
			kmainDS.readOnlyViews[i] = dlssPerf.fakeDSV.get();
	}

	// --- Call original (or FrameAnnotations chain) ---
	func(imageSpaceShader, shape, param);

	// --- Restore kMAIN SRV ---
	kmainRT.SRV = dlssPerf.savedKMainSRV;
	dlssPerf.savedKMainSRV = nullptr;

	// --- Restore kMAIN_COPY SRV ---
	kmainCopyRT.SRV = dlssPerf.savedKMainCopySRV;
	dlssPerf.savedKMainCopySRV = nullptr;

	// --- Restore kMAIN DS views ---
	for (int i = 0; i < 8; i++)
		kmainDS.views[i] = dlssPerf.savedKMainViews[i];
	for (int i = 0; i < 8; i++)
		kmainDS.readOnlyViews[i] = dlssPerf.savedKMainReadOnlyViews[i];
}

// ============================================================================
// RefractionRender_Hook: IS shader hook for ISRefraction
// ============================================================================
// Strategy: let func() run normally (1k refraction, kMAIN→kMAIN_COPY).
// After func() returns, D3D11 state is sticky (PS/CB/sampler/IA all still bound).
// We replay the draw with our own RT (testTexture 3k), VP (3k), and SRV (refraTempTex 3k).

void DLSSperf::RefractionRender_Hook::thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	auto& dlssPerf = globals::features::dlssPerf;

	if (!dlssPerf.hookActive || !dlssPerf.testTextureRTV || !dlssPerf.refraTempSRV) {
		func(imageSpaceShader, shape, param);
		return;
	}

	// --- Pass 1: engine's normal 1k refraction (untouched) ---
	func(imageSpaceShader, shape, param);

	// --- Pass 2: our 3k refraction replay ---
	// func() left PS/CB/sampler/IA/VB/IB all bound on the D3D context.
	// We only change RT, VP, and t0 SRV, then DrawIndexed with the same geometry.

	auto* context = globals::d3d::context;

	// Save current RT so we can restore after our draw
	ID3D11RenderTargetView* savedRTV = nullptr;
	ID3D11DepthStencilView* savedDSV = nullptr;
	context->OMGetRenderTargets(1, &savedRTV, &savedDSV);

	// Save current VP
	D3D11_VIEWPORT savedVP = {};
	UINT numVP = 1;
	context->RSGetViewports(&numVP, &savedVP);

	// Save current t0 SRV (kMAIN.SRV used by ISRefraction as scene input)
	ID3D11ShaderResourceView* savedSRV0 = nullptr;
	context->PSGetShaderResources(0, 1, &savedSRV0);

	// Set 3k output: testTexture RTV, no DS needed for fullscreen IS shader
	ID3D11RenderTargetView* rtv3k = dlssPerf.testTextureRTV.get();
	context->OMSetRenderTargets(1, &rtv3k, nullptr);

	// Set 3k VP
	D3D11_VIEWPORT vp3k = {};
	vp3k.TopLeftX = 0.0f;
	vp3k.TopLeftY = 0.0f;
	vp3k.Width = static_cast<float>(dlssPerf.displayEyeWidth * 2);
	vp3k.Height = static_cast<float>(dlssPerf.displayEyeHeight);
	vp3k.MinDepth = 0.0f;
	vp3k.MaxDepth = 1.0f;
	context->RSSetViewports(1, &vp3k);

	// Set 3k input: refraTempTex as t0 (scene color for refraction sampling)
	ID3D11ShaderResourceView* srv3k = dlssPerf.refraTempSRV.get();
	context->PSSetShaderResources(0, 1, &srv3k);

	// Draw with the same geometry (BSTriShape fullscreen quad, IA still bound)
	context->DrawIndexed(6, 0, 0);

	// --- Restore D3D state so engine continues normally ---
	context->OMSetRenderTargets(1, &savedRTV, savedDSV);
	context->RSSetViewports(1, &savedVP);
	context->PSSetShaderResources(0, 1, &savedSRV0);

	// Release COM refs from Get calls
	if (savedRTV)
		savedRTV->Release();
	if (savedDSV)
		savedDSV->Release();
	if (savedSRV0)
		savedSRV0->Release();
}

// ============================================================================
// UIPassDispatch_Hook: swap KMAIN DS → fakeDS for UI pass (renderMode==24)
// ============================================================================
// UI pass draws VR HUD to kMENUBG (now 3k). Engine binds KMAIN(DS) as DS,
// which is still 1k → size mismatch. Swap to fakeDS (3k) before, restore after.

void DLSSperf::UIPassDispatch_Hook::thunk(RE::BSGraphics::BSShaderAccumulator* shaderAccumulator, uint32_t renderFlags)
{
	auto& dlssPerf = globals::features::dlssPerf;

	// Only intercept renderMode==24 (UI pass) when hook is active
	auto& rtData = shaderAccumulator->GetRuntimeData();
	if (!dlssPerf.hookActive || !dlssPerf.fakeDSV || rtData.renderMode != 24) {
		func(shaderAccumulator, renderFlags);
		return;
	}

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& dsData = renderer->GetDepthStencilData();
	auto& kmainDS = dsData.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	// Save original KMAIN DS views and swap to fakeDS
	ID3D11DepthStencilView* savedViews[8] = {};
	ID3D11DepthStencilView* savedReadOnlyViews[8] = {};
	for (int i = 0; i < 8; i++) {
		savedViews[i] = kmainDS.views[i];
		if (kmainDS.views[i])
			kmainDS.views[i] = dlssPerf.fakeDSV.get();
	}
	for (int i = 0; i < 8; i++) {
		savedReadOnlyViews[i] = kmainDS.readOnlyViews[i];
		if (kmainDS.readOnlyViews[i])
			kmainDS.readOnlyViews[i] = dlssPerf.fakeDSV.get();
	}

	// Force engine to re-bind DS from struct
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	// Force 3k VP: engine may not call UpdateViewPort during UI pass,
	// so we directly set shadowState viewport to DisplayRes and mark dirty.
	auto* ss = globals::game::shadowState;
	D3D11_VIEWPORT savedVP = {};
	if (ss) {
		auto& vp = ss->GetVRRuntimeData().viewPort;
		savedVP = vp;
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		vp.Width = static_cast<float>(dlssPerf.displayEyeWidth * 2);
		vp.Height = static_cast<float>(dlssPerf.displayEyeHeight);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
	}

	// Skip VP compression in UpdateViewPort hook during UI pass
	dlssPerf.postInterceptActive = true;

	func(shaderAccumulator, renderFlags);

	dlssPerf.postInterceptActive = false;

	// Restore viewport
	if (ss) {
		ss->GetVRRuntimeData().viewPort = savedVP;
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT);
	}

	// Restore original KMAIN DS views
	for (int i = 0; i < 8; i++)
		kmainDS.views[i] = savedViews[i];
	for (int i = 0; i < 8; i++)
		kmainDS.readOnlyViews[i] = savedReadOnlyViews[i];

	// Re-dirty so subsequent passes get correct DS
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

// ============================================================================
// PlayerViewRender_Hook: clear postChainDone at PlayerView end
// ============================================================================
// PlayerView covers the entire VR pipeline (World→Post→UI→Submit).
// After func() returns, clear postChainDone so the Present-前 UI chain
// and the next frame use normal VP compression.

void DLSSperf::PlayerViewRender_Hook::thunk(void* a1, bool a2, bool a3)
{
	func(a1, a2, a3);

	globals::features::dlssPerf.ClearPostChainDone();
}

// ============================================================================
// BeginPostIntercept / EndPostIntercept
// ============================================================================
// Outer layer of two-layer swap: swaps kMAIN_COPY DS → fakeDS before the
// entire Post chain (covers the copy step #10 which binds kMAIN_COPY DS).
// Inner layer (tonemap hook) handles kMAIN DS + kMAIN SRV for step #9.

void DLSSperf::BeginPostIntercept()
{
	if (!hookActive || !fakeDSV)
		return;

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& dsData = renderer->GetDepthStencilData();
	auto& kmainCopyDS = dsData.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];

	postInterceptActive = true;

	// Swap kMAIN_COPY DS views → fakeDS
	for (int i = 0; i < 8; i++) {
		savedKMainCopyViews[i] = kmainCopyDS.views[i];
		if (kmainCopyDS.views[i])
			kmainCopyDS.views[i] = fakeDSV.get();
	}
	for (int i = 0; i < 8; i++) {
		savedKMainCopyReadOnlyViews[i] = kmainCopyDS.readOnlyViews[i];
		if (kmainCopyDS.readOnlyViews[i])
			kmainCopyDS.readOnlyViews[i] = fakeDSV.get();
	}
}

void DLSSperf::EndPostIntercept()
{
	if (!hookActive || !fakeDSV)
		return;

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& dsData = renderer->GetDepthStencilData();
	auto& kmainCopyDS = dsData.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];

	postInterceptActive = false;
	postChainDone = true;

	// Restore kMAIN_COPY DS views
	for (int i = 0; i < 8; i++)
		kmainCopyDS.views[i] = savedKMainCopyViews[i];
	for (int i = 0; i < 8; i++)
		kmainCopyDS.readOnlyViews[i] = savedKMainCopyReadOnlyViews[i];
}

// ============================================================================
// DownscaleToKMain: Box 3×3 downscale testTexture (3k) → kMAIN (1k)
// ============================================================================
// Called before the Post chain so the HDR pyramid builds from AA'd DLSS output
// instead of the raw 1k render, eliminating shimmer in bloom/exposure.
// Only kMAIN needs writing:
//   - No refraction: kMAIN is the pyramid input directly.
//   - With refraction: engine composites kMAIN → kMAIN_COPY, which enters pyramid.

void DLSSperf::DownscaleToKMain()
{
	if (!hookActive || !testTextureSRV || !bilinearCopyPS || !bilinearCopyVS || !linearSampler)
		return;

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto& rtData = renderer->GetRuntimeData();

	auto& kmain = rtData.renderTargets[RE::RENDER_TARGETS::kMAIN];

	if (!kmain.RTV)
		return;

	// Save all D3D state that we overwrite
	ID3D11RenderTargetView* savedRTV = nullptr;
	ID3D11DepthStencilView* savedDSV = nullptr;
	context->OMGetRenderTargets(1, &savedRTV, &savedDSV);

	D3D11_VIEWPORT savedVP = {};
	UINT numVP = 1;
	context->RSGetViewports(&numVP, &savedVP);

	ID3D11BlendState* savedBlend = nullptr;
	FLOAT savedBlendFactor[4] = {};
	UINT savedSampleMask = 0;
	context->OMGetBlendState(&savedBlend, savedBlendFactor, &savedSampleMask);

	ID3D11DepthStencilState* savedDSState = nullptr;
	UINT savedStencilRef = 0;
	context->OMGetDepthStencilState(&savedDSState, &savedStencilRef);

	ID3D11GeometryShader* savedGS = nullptr;
	context->GSGetShader(&savedGS, nullptr, nullptr);

	ID3D11HullShader* savedHS = nullptr;
	context->HSGetShader(&savedHS, nullptr, nullptr);

	ID3D11DomainShader* savedDS = nullptr;
	context->DSGetShader(&savedDS, nullptr, nullptr);

	ID3D11RasterizerState* savedRS = nullptr;
	context->RSGetState(&savedRS);

	// IA: fullscreen triangle (no vertex/index buffers)
	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Shaders — clear GS/HS/DS to prevent pipeline interference
	context->VSSetShader(bilinearCopyVS.get(), nullptr, 0);
	context->PSSetShader(bilinearCopyPS.get(), nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);

	// Input: testTexture SRV (3k DLSS output, AA'd)
	ID3D11ShaderResourceView* srvs[] = { testTextureSRV.get() };
	context->PSSetShaderResources(0, 1, srvs);

	ID3D11SamplerState* samplers[] = { linearSampler.get() };
	context->PSSetSamplers(0, 1, samplers);

	// Opaque overwrite: no blending, no depth test, default rasterizer
	context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	context->OMSetDepthStencilState(nullptr, 0);
	context->RSSetState(nullptr);

	// Viewport at RenderRes SBS (1k)
	D3D11_VIEWPORT vp = {};
	vp.Width = static_cast<float>(renderEyeWidth * 2);
	vp.Height = static_cast<float>(renderEyeHeight);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	context->RSSetViewports(1, &vp);

	// Draw to kMAIN
	ID3D11RenderTargetView* rtv = kmain.RTV;
	context->OMSetRenderTargets(1, &rtv, nullptr);
	context->Draw(3, 0);

	// Unbind SRV to prevent SRV↔RT hazard in subsequent engine passes
	ID3D11ShaderResourceView* nullSRV[] = { nullptr };
	context->PSSetShaderResources(0, 1, nullSRV);

	// Restore
	context->OMSetRenderTargets(1, &savedRTV, savedDSV);
	context->RSSetViewports(1, &savedVP);
	context->OMSetBlendState(savedBlend, savedBlendFactor, savedSampleMask);
	context->OMSetDepthStencilState(savedDSState, savedStencilRef);
	context->GSSetShader(savedGS, nullptr, 0);
	context->HSSetShader(savedHS, nullptr, 0);
	context->DSSetShader(savedDS, nullptr, 0);
	context->RSSetState(savedRS);
	if (savedRTV)
		savedRTV->Release();
	if (savedDSV)
		savedDSV->Release();
	if (savedBlend)
		savedBlend->Release();
	if (savedDSState)
		savedDSState->Release();
	if (savedGS)
		savedGS->Release();
	if (savedHS)
		savedHS->Release();
	if (savedDS)
		savedDS->Release();
	if (savedRS)
		savedRS->Release();
}

void DLSSperf::HandlePostProcessing(const std::function<void()>& enginePost)
{
	// Copy testTexture → refraTempTex before Post, so ISRefraction can read 3k scene
	if (refraTempTex) {
		globals::d3d::context->CopyResource(refraTempTex.get(), testTexture.get());
	}

	// Downscale testTexture (3k AA'd) → kMAIN (1k) so the HDR pyramid and
	// bloom compute from anti-aliased content instead of raw 1k render.
	DownscaleToKMain();

	// Outer layer: swap kMAIN_COPY DS + SRV for refraction path coverage
	BeginPostIntercept();

	// Run full engine Post chain; IS shader hook handles tonemap step (#9) swap/restore
	enginePost();

	// Restore kMAIN_COPY DS
	EndPostIntercept();
}

void DLSSperf::DrawSettings()
{
	// DLSSperf has no user-facing settings of its own — enablement is gated
	// at install time by whether the BSShaderRenderTargets::Create hook ran
	// successfully. A future PR may surface diagnostic info here.
}
