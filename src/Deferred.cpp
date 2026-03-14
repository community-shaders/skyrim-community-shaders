#include "Deferred.h"

#include <DDSTextureLoader.h>

#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"

#include "Features/DynamicCubemaps.h"
#include "Features/IBL.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Features/WeatherEditor.h"

#include "Hooks.h"

struct DepthStates
{
	ID3D11DepthStencilState* a[6][40];
};

struct BlendStates
{
	ID3D11BlendState* a[7][2][13][2];

	static BlendStates* GetSingleton()
	{
		static auto blendStates = reinterpret_cast<BlendStates*>(REL::RelocationID(524749, 411364).address());
		return blendStates;
	}
};

void SetupRenderTarget(RE::RENDER_TARGET target, D3D11_TEXTURE2D_DESC texDesc, D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc, D3D11_RENDER_TARGET_VIEW_DESC rtvDesc, D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc, DXGI_FORMAT format, uint bindFlags)
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	texDesc.BindFlags = bindFlags;
	texDesc.Format = format;
	srvDesc.Format = format;
	rtvDesc.Format = format;
	uavDesc.Format = format;

	auto& data = renderer->GetRuntimeData().renderTargets[target];
	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, &data.texture));

	if (texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
		DX::ThrowIfFailed(device->CreateShaderResourceView(data.texture, &srvDesc, &data.SRV));

	if (texDesc.BindFlags & D3D11_BIND_RENDER_TARGET)
		DX::ThrowIfFailed(device->CreateRenderTargetView(data.texture, &rtvDesc, &data.RTV));

	if (texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(data.texture, &uavDesc, &data.UAV));
}

void Deferred::SetupResources()
{
	auto renderer = globals::game::renderer;

	{
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		main.texture->GetDesc(&texDesc);
		main.SRV->GetDesc(&srvDesc);
		main.RTV->GetDesc(&rtvDesc);
		main.UAV->GetDesc(&uavDesc);

		// Available targets:
		// MAIN ONLY ALPHA
		// WATER REFLECTIONS
		// BLURFULL_BUFFER
		// LENSFLAREVIS
		// SAO DOWNSCALED
		// SAO CAMERAZ+MIP_LEVEL_0_ESRAM
		// SAO_RAWAO_DOWNSCALED
		// SAO_RAWAO_PREVIOUS_DOWNSCALDE
		// SAO_TEMP_BLUR_DOWNSCALED
		// INDIRECT
		// INDIRECT_DOWNSCALED
		// RAWINDIRECT
		// RAWINDIRECT_DOWNSCALED
		// RAWINDIRECT_PREVIOUS
		// RAWINDIRECT_PREVIOUS_DOWNSCALED
		// RAWINDIRECT_SWAP
		// VOLUMETRIC_LIGHTING_HALF_RES
		// VOLUMETRIC_LIGHTING_BLUR_HALF_RES
		// VOLUMETRIC_LIGHTING_QUARTER_RES
		// VOLUMETRIC_LIGHTING_BLUR_QUARTER_RES
		// TEMPORAL_AA_WATER_1
		// TEMPORAL_AA_WATER_2

		// Albedo
		SetupRenderTarget(ALBEDO, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Specular
		SetupRenderTarget(SPECULAR, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Reflectance
		SetupRenderTarget(REFLECTANCE, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Normal + Roughness
		SetupRenderTarget(NORMALROUGHNESS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Masks
		SetupRenderTarget(MASKS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

		// TAA Water Buffers
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_1, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_2, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	}

	// kSHADOWMAPS is created by BSShaderRenderTargets_Create before SetupResources() is called,
	// so the SRV is available here. Re-runs on resolution change (render targets recreated).
	if (auto* shadowMapsSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kSHADOWMAPS].depthSRV) {
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		shadowMapsSRV->GetDesc(&desc);
		if (desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY && desc.Texture2DArray.ArraySize > 0) {
			shadowMapSlots = desc.Texture2DArray.ArraySize;
			logger::info("[Deferred] kSHADOWMAPS ArraySize = {}, effective shadowMapSlots = {}", desc.Texture2DArray.ArraySize, shadowMapSlots);
		} else {
			logger::warn("[Deferred] kSHADOWMAPS SRV not a Texture2DArray or ArraySize=0; keeping shadowMapSlots = {}", shadowMapSlots);
		}
	} else {
		logger::warn("[Deferred] kSHADOWMAPS depthSRV is null at SetupResources; keeping shadowMapSlots = {}", shadowMapSlots);
	}

	{
		auto device = globals::d3d::device;

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &linearSampler));

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &pointSampler));

		// PCF comparison sampler bound to s14 for ShadowSampling.hlsli.
		D3D11_SAMPLER_DESC cmpDesc = {};
		cmpDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		cmpDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		cmpDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		cmpDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		cmpDesc.MaxAnisotropy = 1;
		cmpDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		cmpDesc.MinLOD = 0;
		cmpDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&cmpDesc, &shadowCmpSampler));
	}

	{
		D3D11_TEXTURE2D_DESC texDesc;
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(DirectionalShadowData);
		sbDesc.ByteWidth = sizeof(DirectionalShadowData);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;

		perDirectionalShadow = new Buffer(sbDesc);
		perDirectionalShadow->CreateSRV(srvDesc);
	}

	if (shadowMapSlots > 0) {
		delete perShadows;
		perShadows = nullptr;

		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(ShadowData);
		sbDesc.ByteWidth = shadowMapSlots * sizeof(ShadowData);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = shadowMapSlots;

		perShadows = new Buffer(sbDesc);
		perShadows->CreateSRV(srvDesc);
	}
}

void Deferred::ReflectionsPrepasses()
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto state = globals::state;

	state->activeReflections = true;
	state->UpdateSharedData(false, false);

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Reflections Prepass");

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	Feature::ForEachLoadedFeature("ReflectionsPrepass", [](Feature* feature) {
		feature->ReflectionsPrepass();
	});
}

void Deferred::EarlyPrepasses()
{
	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	globals::state->UpdateSharedData(false, true);

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Early Prepass");

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	// Shadow maps have just been rendered — upload BSShadowDirectionalLight data to t19.
	CopyShadowData();

	Feature::ForEachLoadedFeature("EarlyPrepass", [](Feature* feature) {
		feature->EarlyPrepass();
	});
}

void Deferred::PrepassPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::truePBR->PrePass();
	Feature::ForEachLoadedFeature("Prepass", [](Feature* feature) {
		feature->Prepass();
	});
}

void Deferred::StartDeferred()
{
	if (!globals::state->inWorld)
		return;
	globals::state->UpdateSharedData(true, false);

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(renderTargets, shadowState)
	GET_INSTANCE_MEMBER(setRenderTargetMode, shadowState)
	GET_INSTANCE_MEMBER(stateUpdateFlags, shadowState)

	// Backup original render targets
	for (uint i = 0; i < 4; i++) {
		forwardRenderTargets[i] = renderTargets[i];
	}

	RE::RENDER_TARGET targets[8]{
		RE::RENDER_TARGET::kMAIN,
		RE::RENDER_TARGET::kMOTION_VECTOR,
		NORMALROUGHNESS,
		ALBEDO,
		SPECULAR,
		REFLECTANCE,
		MASKS,
		RE::RENDER_TARGET::kNONE
	};

	for (uint i = 2; i < 8; i++) {
		renderTargets[i] = targets[i];                                             // We must use unused targets to be indexable
		setRenderTargetMode[i] = RE::BSGraphics::SetRenderTargetMode::SRTM_CLEAR;  // Dirty from last frame, this calls ClearRenderTargetView once
	}

	stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	deferredPass = true;

	{
		auto context = globals::d3d::context;

		ID3D11Buffer* buffers[1] = { *globals::game::perFrame.get() };

		ID3D11Buffer* vrBuffer = nullptr;

		if (REL::Module::IsVR()) {
			static REL::Relocation<ID3D11Buffer**> VRValues{ REL::Offset(0x3180688) };
			vrBuffer = *VRValues.get();
		}
		if (vrBuffer) {
			context->CSSetConstantBuffers(12, 1, buffers);
			context->CSSetConstantBuffers(13, 1, &vrBuffer);
		} else {
			context->CSSetConstantBuffers(12, 1, buffers);
		}
	}

	PrepassPasses();

	OverrideBlendStates();

	// VR: Classify Eye 1 pixels and write hardware stencil marks before geometry rendering.
	// Only enable stencil culling when overwrite reprojection is available for this frame.
	if (globals::game::isVR && globals::features::vr.IsStereoOptimizationCullingReady()) {
		globals::features::vr.stereoOpt.DispatchStencil();
	}
}

void Deferred::DeferredPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Deferred");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	{
		ID3D11Buffer* buffers[1] = { *globals::game::perFrame };
		ID3D11Buffer* vrBuffer = nullptr;

		if (REL::Module::IsVR()) {
			static REL::Relocation<ID3D11Buffer**> VRValues{ REL::Offset(0x3180688) };
			vrBuffer = *VRValues.get();
		}
		if (vrBuffer) {
			context->CSSetConstantBuffers(12, 1, buffers);
			context->CSSetConstantBuffers(13, 1, &vrBuffer);
		} else {
			context->CSSetConstantBuffers(12, 1, buffers);
		}
	}

	auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
	auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
	auto normalRoughness = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
	auto masks = renderer->GetRuntimeData().renderTargets[MASKS];

	auto main = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[0]];
	auto normals = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[2]];
	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto reflectance = renderer->GetRuntimeData().renderTargets[REFLECTANCE];

	auto motionVectors = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	bool interior = Util::IsInterior();

	auto& skylighting = globals::features::skylighting;

	auto& ssgi = globals::features::screenSpaceGI;
	if (ssgi.loaded)
		ssgi.DrawSSGI();
	auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec] = ssgi.GetOutputTextures();
	bool ssgi_hq_spec = ssgi.settings.EnableExperimentalSpecularGI;

	auto dispatchCount = Util::GetScreenDispatchCount(true);

	auto& sss = globals::features::subsurfaceScattering;
	if (sss.loaded)
		sss.DrawSSS();

	auto& dynamicCubemaps = globals::features::dynamicCubemaps;
	if (dynamicCubemaps.loaded)
		dynamicCubemaps.UpdateCubemap();

	auto& ibl = globals::features::ibl;

	// Deferred Composite
	{
		TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite");

		ID3D11ShaderResourceView* srvs[16]{
			specular.SRV,
			albedo.SRV,
			normalRoughness.SRV,
			masks.SRV,
			dynamicCubemaps.loaded || REL::Module::IsVR() ? Util::GetCurrentSceneDepthSRV(true) : nullptr,
			dynamicCubemaps.loaded ? reflectance.SRV : nullptr,
			dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr,
			dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr,
			dynamicCubemaps.loaded && skylighting.loaded ? skylighting.texProbeArray->srv.get() : nullptr,
			dynamicCubemaps.loaded && skylighting.loaded ? skylighting.stbn_vec3_2Dx1D_128x128x64.get() : nullptr,
			ssgi_ao,
			ssgi_hq_spec ? nullptr : ssgi_y,
			ssgi_hq_spec ? nullptr : ssgi_cocg,
			ssgi_hq_spec ? ssgi_gi_spec : nullptr,
			ibl.loaded ? ibl.envIBLTexture->srv.get() : nullptr,
			ibl.loaded ? ibl.skyIBLTexture->srv.get() : nullptr,
		};

		if (dynamicCubemaps.loaded)
			context->CSSetSamplers(0, 1, &linearSampler);

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		// Bind VRStereoOptimizations mode texture for Eye 1 skip.
		// Bind null when disabled so stale mode data doesn't cause incorrect early-exits
		// in DeferredCompositeCS (null SRV reads return 0 = MODE_DISOCCLUDED, all pixels composite normally).
		auto& vrStereoOpt = globals::features::vr.stereoOpt;
		bool stereoCullingReady = globals::features::vr.IsStereoOptimizationCullingReady();
		ID3D11ShaderResourceView* modeSRV = stereoCullingReady ? vrStereoOpt.GetModeTextureSRV() : nullptr;
		context->CSSetShaderResources(16, 1, &modeSRV);

		ID3D11UnorderedAccessView* uavs[3]{ main.UAV, normals.UAV, motionVectors.UAV };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		auto shader = interior ? GetComputeMainCompositeInterior() : GetComputeMainComposite();
		context->CSSetShader(shader, nullptr, 0);

		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		// Unbind mode texture SRV
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->CSSetShaderResources(16, 1, &nullSRV);
	}

	// VR: Deactivate stencil culling now that geometry rendering is complete.
	// Must happen before StereoBlend so the blend pass itself isn't stencil-blocked.
	if (globals::game::isVR) {
		auto& stereoOpt = globals::features::vr.stereoOpt;
		if (stereoOpt.IsStencilActive()) {
			stereoOpt.DeactivateStencil();
		}
	}

	// VR: Stereo reprojection fills Eye 1 holes here (after DeferredComposite, before SSR/water/sky)
	// so that ISReflectionsRayTracing sees valid pixels in both eyes.
	if (globals::game::isVR) {
		globals::features::vr.DrawStereoBlend();
	}

	// Clear
	{
		ID3D11ShaderResourceView* views[16]{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3]{ nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* buffers[1] = { nullptr };
		context->CSSetConstantBuffers(12, 1, buffers);

		context->CSSetShader(nullptr, nullptr, 0);
	}

	if (dynamicCubemaps.loaded)
		dynamicCubemaps.PostDeferred();
}

void Deferred::EndDeferred()
{
	if (!globals::state->inWorld)
		return;

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(renderTargets, shadowState)
	GET_INSTANCE_MEMBER(stateUpdateFlags, shadowState)

	// Do not render to our targets past this point
	for (uint i = 0; i < 4; i++) {
		renderTargets[i] = forwardRenderTargets[i];
	}

	for (uint i = 4; i < 8; i++) {
		renderTargets[i] = RE::RENDER_TARGET::kNONE;
	}

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	DeferredPasses();  // Perform deferred passes and composite forward buffers

	stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	deferredPass = false;

	ResetBlendStates();
}

void Deferred::OverrideBlendStates()
{
	auto blendStates = BlendStates::GetSingleton();

	static std::once_flag setup;
	std::call_once(setup, [&]() {
		auto device = globals::d3d::device;

		for (int a = 0; a < 7; a++) {
			for (int b = 0; b < 2; b++) {
				for (int c = 0; c < 13; c++) {
					for (int d = 0; d < 2; d++) {
						forwardBlendStates[a][b][c][d] = blendStates->a[a][b][c][d];

						if (auto blendState = forwardBlendStates[a][b][c][d]) {
							D3D11_BLEND_DESC blendDesc;
							forwardBlendStates[a][b][c][d]->GetDesc(&blendDesc);

							blendDesc.IndependentBlendEnable = true;

							// Default to original blending method
							for (int i = 1; i < 8; i++) {
								blendDesc.RenderTarget[i].BlendEnable = blendDesc.RenderTarget[0].BlendEnable;
								blendDesc.RenderTarget[i].SrcBlend = blendDesc.RenderTarget[0].SrcBlend;
								blendDesc.RenderTarget[i].DestBlend = blendDesc.RenderTarget[0].DestBlend;
								blendDesc.RenderTarget[i].BlendOp = blendDesc.RenderTarget[0].BlendOp;
								blendDesc.RenderTarget[i].SrcBlendAlpha = blendDesc.RenderTarget[0].SrcBlendAlpha;
								blendDesc.RenderTarget[i].DestBlendAlpha = blendDesc.RenderTarget[0].DestBlendAlpha;
								blendDesc.RenderTarget[i].BlendOpAlpha = blendDesc.RenderTarget[0].BlendOpAlpha;
								blendDesc.RenderTarget[i].RenderTargetWriteMask = blendDesc.RenderTarget[0].RenderTargetWriteMask;
							}

							// Normals and motion vectors must use alpha blending
							for (int i = 1; i < 3; i++) {
								blendDesc.RenderTarget[i].BlendEnable = blendDesc.RenderTarget[0].BlendEnable;
								blendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_SRC_ALPHA;
								blendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
								blendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
								blendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
								blendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
								blendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
								blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
							}

							// RT[5] = REFLECTANCE: enable alpha writes for POM depth data
							// stored in Reflectance.w, used by StereoBlendCS for depth-aware reprojection
							blendDesc.RenderTarget[5].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

							DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &deferredBlendStates[a][b][c][d]));
						} else {
							deferredBlendStates[a][b][c][d] = nullptr;
						}
					}
				}
			}
		}
	});

	// Set modified blend states
	for (int a = 0; a < 7; a++) {
		for (int b = 0; b < 2; b++) {
			for (int c = 0; c < 13; c++) {
				for (int d = 0; d < 2; d++) {
					blendStates->a[a][b][c][d] = deferredBlendStates[a][b][c][d];
				}
			}
		}
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

void Deferred::ResetBlendStates()
{
	auto blendStates = BlendStates::GetSingleton();

	// Restore modified blend states
	for (int a = 0; a < 7; a++) {
		for (int b = 0; b < 2; b++) {
			for (int c = 0; c < 13; c++) {
				for (int d = 0; d < 2; d++) {
					blendStates->a[a][b][c][d] = forwardBlendStates[a][b][c][d];
				}
			}
		}
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

void Deferred::SetShadowCascadeParameters(RE::BSShadowLight::RUNTIME_DATA& lightData, DirectionalShadowData& dd)
{
	for (uint i = 0; i < lightData.shadowmapDescriptors.size(); i++) {
		auto proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&lightData.shadowmapDescriptors[i].lightTransform));
		DirectX::XMStoreFloat4x4(&dd.ShadowProj[i], proj);

		DirectX::XMMATRIX invProj = DirectX::XMMatrixTranspose(proj);
		DirectX::XMStoreFloat4x4(&dd.InvShadowProj[i], invProj);
	}
}

void Deferred::SetShadowCascadeParameters(RE::BSShadowLight::RUNTIME_DATA_VR& lightData, DirectionalShadowData& dd)
{
	for (uint i = 0; i < lightData.shadowmapDescriptors.size(); i++) {
		auto proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&lightData.shadowmapDescriptors[i].lightTransform));
		DirectX::XMStoreFloat4x4(&dd.ShadowProj[i], proj);

		DirectX::XMMATRIX invProj = DirectX::XMMatrixTranspose(proj);
		DirectX::XMStoreFloat4x4(&dd.InvShadowProj[i], invProj);
	}
}

void Deferred::SetShadowParameters(RE::BSShadowLight::RUNTIME_DATA& lightData, ShadowData& sd)
{
	auto& desc = lightData.shadowmapDescriptors[0];
	DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&desc.lightTransform));
	DirectX::XMStoreFloat4x4(&sd.ShadowProj, proj);

	DirectX::XMMATRIX invProj = DirectX::XMMatrixTranspose(proj);
	DirectX::XMStoreFloat4x4(&sd.InvShadowProj, invProj);
}

void Deferred::SetShadowParameters(RE::BSShadowLight::RUNTIME_DATA_VR& lightData, ShadowData& sd)
{
	auto& desc = lightData.shadowmapDescriptors[0];
	DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&desc.lightTransform));
	DirectX::XMStoreFloat4x4(&sd.ShadowProj, proj);

	DirectX::XMMATRIX invProj = DirectX::XMMatrixTranspose(proj);
	DirectX::XMStoreFloat4x4(&sd.InvShadowProj, invProj);
}

void Deferred::CopyShadowData()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "CopyShadowData");

	if (!perShadows || shadowMapSlots == 0)
		return;

	auto* shadowSceneNode = globals::game::smState->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;

	auto* sunShadowLight = shadowSceneNode->GetRuntimeData().sunShadowDirLight;
	if (!sunShadowLight)
		return;

	DirectionalShadowData dd{};
	std::vector<ShadowData> sd(shadowMapSlots);

	auto context = globals::d3d::context;

	auto& dirData = sunShadowLight->GetShadowDirectionalLightRuntimeData();
	dd.EndSplitDistances = { dirData.endSplitDistances[0], dirData.endSplitDistances[1] };
	dd.StartSplitDistances = { dirData.startSplitDistances[0], dirData.startSplitDistances[1] };

	ID3D11ShaderResourceView* cascadeSRV = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kSHADOWMAPS_ESRAM].depthSRV;

	if (globals::game::isVR)
		SetShadowCascadeParameters(sunShadowLight->GetVRRuntimeData(), dd);
	else
		SetShadowCascadeParameters(sunShadowLight->GetRuntimeData(), dd);

	ID3D11ShaderResourceView* shadowMapsSRV = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kSHADOWMAPS].depthSRV;

	{
		// shadowLightsAccum is the CPU-side slot-indexed mirror of kSHADOWMAPS:
		// shadowLightsAccum[i] holds the light whose shadow was rendered into kSHADOWMAPS[i]
		// during Main_RenderShadowMaps. Trusting it directly avoids false rejections from
		// cross-referencing activeShadowLights, which is a culling set and may not match.
		uint32_t lightCount = 0;
		uint32_t unshadowedLights = 0;
		int mapIndex = 0;
		while (true) {
			RE::BSShadowLight* light = shadowSceneNode->GetRuntimeData().shadowLightsAccum[mapIndex];
			if (!light)
				break;

			uint32_t depthSlot = globals::game::isVR ?
			                         light->GetVRRuntimeData().shadowmapDescriptors[0].shadowmapIndex :
			                         light->GetRuntimeData().shadowmapDescriptors[0].shadowmapIndex;

			if (lightCount < shadowMapSlots) {
				sd[depthSlot].ShadowParam.x = light->GetIsParabolicLight() ? float(light->shadowMapCount == 2 ? 2 : 1) : 0.f;

				if (globals::game::isVR)
					SetShadowParameters(light->GetVRRuntimeData(), sd[depthSlot]);
				else
					SetShadowParameters(light->GetRuntimeData(), sd[depthSlot]);

				sd[depthSlot].ShadowParam.y = light->light->GetLightRuntimeData().radius.x;
			} else {
				unshadowedLights++;
			}

			mapIndex += light->shadowMapCount;
			lightCount++;
		}

		uint32_t slotCount = static_cast<uint32_t>(mapIndex);
		if (lightCount != shadowLightCount) {
			shadowLightCount = lightCount;
			if (unshadowedLights > 0)
				logger::warn("[Deferred] {} shadow lights using {} / {} slots; {} lights exceed capacity and will render without shadow",
					lightCount, slotCount, shadowMapSlots, unshadowedLights);
			else
				logger::info("[Deferred] {} shadow lights using {} / {} slots", lightCount, slotCount, shadowMapSlots);
		}
	}

	context->PSSetShaderResources(99, 1, &cascadeSRV);
	context->PSSetShaderResources(101, 1, &shadowMapsSRV);
	context->PSSetSamplers(14, 1, &shadowCmpSampler);

	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		DX::ThrowIfFailed(context->Map(perDirectionalShadow->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		memcpy(mapped.pData, &dd, sizeof(DirectionalShadowData));
		context->Unmap(perDirectionalShadow->resource.get(), 0);
		ID3D11ShaderResourceView* srv = perDirectionalShadow->srv.get();
		context->PSSetShaderResources(98, 1, &srv);
	}

	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		DX::ThrowIfFailed(context->Map(perShadows->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		memcpy(mapped.pData, sd.data(), shadowMapSlots * sizeof(ShadowData));
		context->Unmap(perShadows->resource.get(), 0);
		ID3D11ShaderResourceView* srv = perShadows->srv.get();
		context->PSSetShaderResources(100, 1, &srv);
	}
}

void Deferred::ClearShaderCache()
{
	if (mainCompositeCS) {
		mainCompositeCS->Release();
		mainCompositeCS = nullptr;
	}
	if (mainCompositeInteriorCS) {
		mainCompositeInteriorCS->Release();
		mainCompositeInteriorCS = nullptr;
	}
}

ID3D11ComputeShader* Deferred::GetComputeMainComposite()
{
	if (!mainCompositeCS) {
		logger::debug("Compiling DeferredCompositeCS");

		std::vector<std::pair<const char*, const char*>> defines;

		if (globals::features::dynamicCubemaps.loaded)
			defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

		if (globals::features::skylighting.loaded)
			defines.push_back({ "SKYLIGHTING", nullptr });

		if (globals::features::screenSpaceGI.loaded)
			defines.push_back({ "SSGI", nullptr });

		if (globals::features::ibl.loaded)
			defines.push_back({ "IBL", nullptr });

		if (REL::Module::IsVR())
			defines.push_back({ "FRAMEBUFFER", nullptr });

		if (REL::Module::IsVR() && globals::features::vr.stereoOpt.loaded)
			defines.push_back({ "VR_STEREO_OPT", nullptr });

		mainCompositeCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeCS;
}

ID3D11ComputeShader* Deferred::GetComputeMainCompositeInterior()
{
	if (!mainCompositeInteriorCS) {
		logger::debug("Compiling DeferredCompositeCS INTERIOR");

		std::vector<std::pair<const char*, const char*>> defines;
		defines.push_back({ "INTERIOR", nullptr });

		if (globals::features::dynamicCubemaps.loaded)
			defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

		if (globals::features::screenSpaceGI.loaded)
			defines.push_back({ "SSGI", nullptr });

		if (globals::features::ibl.loaded)
			defines.push_back({ "IBL", nullptr });

		if (REL::Module::IsVR())
			defines.push_back({ "FRAMEBUFFER", nullptr });

		if (REL::Module::IsVR() && globals::features::vr.stereoOpt.loaded)
			defines.push_back({ "VR_STEREO_OPT", nullptr });

		mainCompositeInteriorCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeInteriorCS;
}

void Deferred::Hooks::Main_RenderShadowMaps::thunk()
{
	func();
	globals::deferred->EarlyPrepasses();
};

void Deferred::Hooks::Main_RenderWorld::thunk(bool a1)
{
	auto* const state = globals::state;
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
	state->inWorld = true;
	func(a1);

	state->inWorld = false;
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
};

void Deferred::Hooks::Main_RenderWorld_Start::thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup)
{
	if (globals::shaderCache->IsEnabled() && globals::state->inWorld) {
		// Here is where the first opaque objects start rendering
		globals::deferred->StartDeferred();
	}

	func(This, StartRange, EndRanges, RenderFlags, GeometryGroup);  // RenderBatches
};

void Deferred::Hooks::Main_RenderWorld_BlendedDecals::thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags)
{
	auto deferred = globals::deferred;

	if (globals::shaderCache->IsEnabled() && globals::state->inWorld) {
		auto& terrainBlending = globals::features::terrainBlending;
		// Defer terrain rendering until after everything else
		if (terrainBlending.loaded && terrainBlending.settings.Enabled) {
			terrainBlending.RenderTerrainBlendingPasses();
		}
	}

	// Deferred blended decals

	func(This, RenderFlags);

	deferred->EndDeferred();

	// Copy depth from before water
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	context->CopyResource(depthCopy.texture, depth.texture);

	// After this point, water starts rendering
};

void Deferred::Hooks::BSCubeMapCamera_RenderCubemap::thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5)
{
	auto deferred = globals::deferred;
	auto state = globals::state;

	deferred->ReflectionsPrepasses();
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections);
	func(camera, a2, a3, a4, a5);
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections);
}

void Deferred::Hooks::Main_RenderFirstPersonView::thunk(bool a1, bool a2)
{
	auto* const state = globals::state;
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
	func(a1, a2);
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
}

void Deferred::Hooks::Renderer_ResetState::thunk(void* This)
{
	func(This);

	auto* const state = globals::state;
	auto* const context = globals::d3d::context;

	ID3D11Buffer* buffers[3] = { state->permutationCB->CB(), state->sharedDataCB->CB(), state->featureDataCB->CB() };
	context->PSSetConstantBuffers(4, 3, buffers);
	context->CSSetConstantBuffers(5, 2, buffers + 1);

	auto* singleton = globals::truePBR;
	singleton->SetupFrame();
}
