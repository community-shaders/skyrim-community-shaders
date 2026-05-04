#include "Deferred.h"

#include <DDSTextureLoader.h>

#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"

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
		Util::SetResourceName(linearSampler, "Deferred::LinearSampler");

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &pointSampler));
		Util::SetResourceName(pointSampler, "Deferred::PointSampler");
	}

	// Directional shadow structured buffer (t98): CPU-written each frame, read-only on GPU.
	// One element holds the sun cascade data uploaded from BSShadowDirectionalLight.
	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(DirectionalShadowLightData);
		sbDesc.ByteWidth = sizeof(DirectionalShadowLightData);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;

		delete directionalShadowLights;
		directionalShadowLights = new Buffer(sbDesc, nullptr, "Deferred::DirectionalShadowLights");
		directionalShadowLights->CreateSRV(srvDesc);
	}
}

void Deferred::ReflectionsPrepasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Reflections Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto state = globals::state;

	state->activeReflections = true;
	state->UpdateSharedData(false, false);

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	Feature::ForEachLoadedFeature("ReflectionsPrepass", [](Feature* feature) { feature->ReflectionsPrepass(); }, true);
}

void Deferred::EarlyPrepasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Early Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	globals::state->UpdateSharedData(false, true);

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	// Shadow maps have just been rendered — upload BSShadowDirectionalLight data to t98.
	CopyShadowLightData();

	Feature::ForEachLoadedFeature("EarlyPrepass", [](Feature* feature) { feature->EarlyPrepass(); }, true);
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

	Feature::ForEachLoadedFeature("Prepass", [](Feature* feature) { feature->Prepass(); }, true);
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

		// Clear POM offset texture to -1.0 sentinel so pixels the Lighting PS never touches read "no POM"
		if (globals::features::vr.stereoOpt.loaded)
			globals::features::vr.stereoOpt.ClearPomOffsetTexture();

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
			specular.SRV,                                                                                    // t0  SpecularTexture
			albedo.SRV,                                                                                      // t1  AlbedoTexture
			normalRoughness.SRV,                                                                             // t2  NormalRoughnessTexture
			masks.SRV,                                                                                       // t3  MasksTexture
			dynamicCubemaps.loaded || REL::Module::IsVR() ? Util::GetCurrentSceneDepthSRV(false) : nullptr,  // t4  DepthTexture (24/32-bit; HLSL type baked at compile via TERRAIN_BLENDING)
			dynamicCubemaps.loaded ? reflectance.SRV : nullptr,                                              // t5  ReflectanceTexture
			dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr,                        // t6  EnvTexture
			dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr,             // t7  EnvReflectionsTexture
			dynamicCubemaps.loaded && skylighting.loaded ? skylighting.texProbeArray->srv.get() : nullptr,   // t8  SkylightingProbeArray
			nullptr,                                                                                         // t9  unused
			ssgi_ao,                                                                                         // t10 SsgiAoTexture
			ssgi_hq_spec ? nullptr : ssgi_y,                                                                 // t11 SsgiYTexture
			ssgi_hq_spec ? nullptr : ssgi_cocg,                                                              // t12 SsgiCoCgTexture
			ssgi_hq_spec ? ssgi_gi_spec : nullptr,                                                           // t13 SsgiSpecularTexture
			ibl.loaded ? ibl.envIBLTexture->srv.get() : nullptr,                                             // t14 EnvIBLTexture
			ibl.loaded ? ibl.skyIBLTexture->srv.get() : nullptr,                                             // t15 SkyIBLTexture
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

		{
			TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite - Dispatch");
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

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

template <typename T>
void Deferred::SetShadowCascadeParameters(T& lightData, DirectionalShadowLightData& dd)
{
	const auto count = std::min(lightData.shadowmapDescriptors.size(), static_cast<uint32_t>(std::size(dd.ShadowProj)));
	for (uint32_t i = 0; i < count; i++) {
		auto proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&lightData.shadowmapDescriptors[i].lightTransform));
		DirectX::XMStoreFloat4x4(&dd.ShadowProj[i], proj);

		DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
		DirectX::XMStoreFloat4x4(&dd.InvShadowProj[i], invProj);
	}
}

void Deferred::CopyShadowLightData()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "CopyShadowLightData");

	auto* shadowSceneNode = globals::game::smState->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;

	auto* sunShadowLight = shadowSceneNode->GetRuntimeData().sunShadowDirLight;
	if (!sunShadowLight)
		return;

	DirectionalShadowLightData dd{};
	auto context = globals::d3d::context;

	auto& dirData = sunShadowLight->GetShadowDirectionalLightRuntimeData();
	dd.EndSplitDistances = { dirData.endSplitDistances[0], dirData.endSplitDistances[1] };
	dd.StartSplitDistances = { dirData.startSplitDistances[0], dirData.startSplitDistances[1] };

	if (globals::game::isVR)
		SetShadowCascadeParameters(sunShadowLight->GetVRRuntimeData(), dd);
	else
		SetShadowCascadeParameters(sunShadowLight->GetRuntimeData(), dd);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	DX::ThrowIfFailed(context->Map(directionalShadowLights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	memcpy(mapped.pData, &dd, sizeof(DirectionalShadowLightData));
	context->Unmap(directionalShadowLights->resource.get(), 0);

	ID3D11ShaderResourceView* srv = directionalShadowLights->srv.get();
	context->PSSetShaderResources(98, 1, &srv);
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

		if (REL::Module::IsVR())
			defines.push_back({ "VR_STEREO_OPT", nullptr });

		// TERRAIN_BLENDING flips DepthTexture's HLSL type from `Texture2D<unorm float>`
		// (R24_UNORM_X8_TYPELESS game depth) to `Texture2D<float>` (R32_FLOAT blendedDepth).
		if (globals::features::terrainBlending.loaded)
			defines.push_back({ "TERRAIN_BLENDING", nullptr });

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

		if (REL::Module::IsVR())
			defines.push_back({ "VR_STEREO_OPT", nullptr });

		// TERRAIN_BLENDING flips DepthTexture's HLSL type from `Texture2D<unorm float>`
		// (R24_UNORM_X8_TYPELESS game depth) to `Texture2D<float>` (R32_FLOAT blendedDepth).
		if (globals::features::terrainBlending.loaded)
			defines.push_back({ "TERRAIN_BLENDING", nullptr });

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
}
