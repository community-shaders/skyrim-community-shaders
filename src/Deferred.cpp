#include "Deferred.h"

#include <DDSTextureLoader.h>

#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"
#include "VanillaBinding.h"

#include "Features/CSEditor.h"
#include "Features/DynamicCubemaps.h"
#include "Features/Effects11.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/IBL.h"
#include "Features/NRD.h"
#include "Features/PhysicalSky.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceReflections.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/Upscaling.h"

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

	if (data.UAV) {
		data.UAV->Release();
		data.UAV = nullptr;
	}
	if (data.RTV) {
		data.RTV->Release();
		data.RTV = nullptr;
	}
	if (data.SRVCopy) {
		data.SRVCopy->Release();
		data.SRVCopy = nullptr;
	}
	if (data.SRV) {
		data.SRV->Release();
		data.SRV = nullptr;
	}
	if (data.textureCopy) {
		data.textureCopy->Release();
		data.textureCopy = nullptr;
	}
	if (data.texture) {
		data.texture->Release();
		data.texture = nullptr;
	}

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
		// Masks2 (vertexAO; fp16 to allow blending)
		SetupRenderTarget(MASKS2, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

		// TAA water history buffers need RGBA16: alpha stores premultiplied coverage for ISWaterBlend
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_1, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_2, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
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

		delete debugViewCB;
		debugViewCB = nullptr;
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

	sceneDepthFinal = false;

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
	sceneDepthFinal = false;
	if (!globals::state->inWorld)
		return;
	globals::state->UpdateSharedData(true, false);

	auto shadowState = globals::game::shadowState;
	auto& renderTargets = shadowState->GetRuntimeData().renderTargets;
	auto& setRenderTargetMode = shadowState->GetRuntimeData().setRenderTargetMode;
	auto& stateUpdateFlags = shadowState->GetRuntimeData().stateUpdateFlags;

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
		MASKS2
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
		context->CSSetConstantBuffers(12, 1, buffers);
	}

	PrepassPasses();

	OverrideBlendStates();
}

void Deferred::DeferredPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Deferred");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	{
		ID3D11Buffer* buffers[1] = { *globals::game::perFrame };
		context->CSSetConstantBuffers(12, 1, buffers);
	}

	auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
	auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
	auto normalRoughness = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
	auto masks = renderer->GetRuntimeData().renderTargets[MASKS];
	auto masks2 = renderer->GetRuntimeData().renderTargets[MASKS2];

	auto main = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[0]];
	auto normals = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[2]];
	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	// Publish the finished opaque depth before any post-geometry pass reads it. The z-prepass
	// depth omits alpha-tested geometry (rocks, cliffs, road edges), so SSGI, SSS and the
	// composite would otherwise see the background where those objects stand. Water also
	// samples this copy for edge fade and refraction.
	auto finalDepthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	context->CopyResource(finalDepthCopy.texture, depth.texture);
	sceneDepthFinal = true;
	auto reflectance = renderer->GetRuntimeData().renderTargets[REFLECTANCE];

	auto motionVectors = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	bool interior = Util::IsInterior();

	auto& skylighting = globals::features::skylighting;

	auto& nrd = globals::features::nrd;
	if (nrd.loaded)
		nrd.PrepareGuides();

	auto& ssgi = globals::features::screenSpaceGI;
	if (ssgi.loaded) {
		ssgi.DrawSSGI();
		ssgi.Composite();
	}

	auto& ssr = globals::features::screenSpaceReflections;
	if (ssr.loaded)
		ssr.DrawSSR();

	auto ssgiOutput = ssgi.GetDiffuseOutputTexture();
	auto ssgiSH1Output = ssgi.GetDiffuseSH1Texture();
	auto ssrOutput = ssr.GetOutputTexture();

	auto dispatchCount = Util::GetScreenDispatchCount(true);

	auto& sss = globals::features::subsurfaceScattering;
	if (sss.loaded)
		sss.DrawSSS();

	auto& dynamicCubemaps = globals::features::dynamicCubemaps;
	if (dynamicCubemaps.loaded)
		dynamicCubemaps.UpdateCubemap();

	auto& ibl = globals::features::ibl;

	auto& physSky = globals::features::physicalSky;

	// Deferred Composite
	{
		TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite");

		ID3D11ShaderResourceView* srvs[]{
			specular.SRV,                                                                                   // t0  SpecularTexture
			albedo.SRV,                                                                                     // t1  AlbedoTexture
			normalRoughness.SRV,                                                                            // t2  NormalRoughnessTexture
			masks.SRV,                                                                                      // t3  MasksTexture
			dynamicCubemaps.loaded ? Util::GetCurrentSceneDepthSRV(false) : nullptr,                        // t4  DepthTexture (24/32-bit; HLSL type baked at compile via TERRAIN_BLENDING)
			dynamicCubemaps.loaded ? reflectance.SRV : nullptr,                                             // t5  ReflectanceTexture
			dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr,                       // t6  EnvTexture
			dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr,            // t7  EnvReflectionsTexture
			dynamicCubemaps.loaded && skylighting.loaded ? skylighting.texProbeArray->srv.get() : nullptr,  // t8  SkylightingProbeArray
			ssgiOutput,                                                                                     // t9  SsgiTexture / SH0
			ssgiSH1Output,                                                                                  // t10 SsgiSH1Texture
			ibl.loaded ? ibl.envIBLTexture->srv.get() : nullptr,                                            // t11 EnvIBLTexture
			ibl.loaded ? ibl.skyIBLTexture->srv.get() : nullptr,                                            // t12 SkyIBLTexture
			ssrOutput,                                                                                      // t13 SsrTexture (Screen Space Reflections)
			physSky.loaded ? physSky.texApLut->srv.get() : nullptr,                                         // t14 PhysicalSky AP LUT
			physSky.loaded ? physSky.texApShadow->srv.get() : nullptr,                                      // t15 PhysicalSky AP Shadow
			physSky.loaded && physSky.texVolTr ? physSky.texVolTr->srv.get() : nullptr,                     // t16 PhysicalSky Volumetric Transmittance
			physSky.loaded && physSky.texVolLum ? physSky.texVolLum->srv.get() : nullptr,                   // t17 PhysicalSky Volumetric Luminance
			physSky.loaded && physSky.texShadowVolume ? physSky.texShadowVolume->srv.get() : nullptr,       // t18 PhysicalSky Shadow Volume
			masks2.SRV,                                                                                     // t19 Masks2Texture
		};

		ID3D11SamplerState* samplers[]{
			dynamicCubemaps.loaded ? linearSampler : nullptr,
			physSky.loaded ? physSky.sampSv.get() : nullptr,
		};
		context->CSSetSamplers(0, ARRAYSIZE(samplers), samplers);

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[3]{ main.UAV, normals.UAV, motionVectors.UAV };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		if (debugView != DebugView::Off) {
			if (!debugViewCB) {
				debugViewCB = new ConstantBuffer(ConstantBufferDesc<DebugViewCB>(), "Deferred::DebugViewCB");
			}
			DebugViewCB cb{};
			cb.mode = static_cast<uint>(debugView);
			debugViewCB->Update(cb);

			ID3D11Buffer* debugBuf = debugViewCB->CB();
			context->CSSetConstantBuffers(13, 1, &debugBuf);
		}

		auto shader = interior ? GetComputeMainCompositeInterior() : GetComputeMainComposite();
		context->CSSetShader(shader, nullptr, 0);

		{
			TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite - Dispatch");
			globals::profiler->BeginPass("DeferredComposite");
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
			globals::profiler->EndPass();
		}
	}

	// Clear
	{
		ID3D11ShaderResourceView* views[20]{};
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3]{ nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* buffers[1] = { nullptr };
		context->CSSetConstantBuffers(12, 1, buffers);

		ID3D11SamplerState* samplers[2]{ nullptr, nullptr };
		context->CSSetSamplers(0, ARRAYSIZE(samplers), samplers);

		if (debugViewCB) {
			ID3D11Buffer* nullDebugBuf = nullptr;
			context->CSSetConstantBuffers(13, 1, &nullDebugBuf);
		}

		context->CSSetShader(nullptr, nullptr, 0);
	}

	if (dynamicCubemaps.loaded)
		dynamicCubemaps.PostDeferred();

	if (globals::features::effects11.loaded)
		globals::features::effects11.DrawVolumetricRays();
}

void Deferred::EndDeferred()
{
	if (!globals::state->inWorld)
		return;

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto shadowState = globals::game::shadowState;
	auto& renderTargets = shadowState->GetRuntimeData().renderTargets;
	auto& stateUpdateFlags = shadowState->GetRuntimeData().stateUpdateFlags;

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
	mediumCompositeCS = nullptr;
	mediumShaderRequested = false;
	if (mainCompositeCS) {
		mainCompositeCS->Release();
		mainCompositeCS = nullptr;
	}
	if (mainCompositeInteriorCS) {
		mainCompositeInteriorCS->Release();
		mainCompositeInteriorCS = nullptr;
	}
}

bool Deferred::MediumCompositeEnabled()
{
	if (!postWaterHookInstalled || !globals::shaderCache->IsEnabled() ||
		!globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].UAV)
		return false;
	const auto& sky = globals::features::physicalSky;
	const auto& fog = globals::features::exponentialHeightFog;
	if (!(sky.loaded && sky.cbData.enabled && sky.cbData.enableVolumetricClouds) && !(fog.loaded && fog.settings.enabled))
		return false;
	if (!mediumShaderRequested) {
		mediumShaderRequested = true;
		std::vector<std::pair<const char*, const char*>> defines;
		if (sky.loaded)
			defines.emplace_back("PHYSICAL_SKY", "");
		if (fog.loaded)
			defines.emplace_back("EXP_HEIGHT_FOG", "");
		if (globals::features::ibl.loaded)
			defines.emplace_back("IBL", "");
		if (globals::features::dynamicCubemaps.loaded)
			defines.emplace_back("DYNAMIC_CUBEMAPS", "");
		if (auto* shader = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\MediumCompositeCS.hlsl", defines, "cs_5_0")))
			mediumCompositeCS.attach(shader);
	}
	return mediumCompositeCS != nullptr;
}

void Deferred::CompositeAfterWater()
{
	if (!MediumCompositeEnabled() ||
		!globals::state->inWorld ||
		(globals::state->permutationData.ExtraShaderDescriptor & static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections)) != 0 ||
		mediumCompositeFrame == globals::state->frameCount)
		return;
	auto* context = globals::d3d::context;
	auto* renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	if (!main.UAV)
		return;
	context->OMSetRenderTargets(0, nullptr, nullptr);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	ID3D11Buffer* perFrame = *globals::game::perFrame;
	context->CSSetConstantBuffers(12, 1, &perFrame);
	ID3D11Buffer* sharedBuffers[] = { globals::state->sharedDataCB->CB(), globals::state->featureDataCB->CB() };
	context->CSSetConstantBuffers(5, 2, sharedBuffers);
	auto& sky = globals::features::physicalSky;
	if (sky.loaded && sky.cbData.enabled && sky.cbData.enableVolumetricClouds)
		sky.RenderVolumetricClouds(PhysicalSky::VolumetricCloudPass::kMainViewAndCubemap);

	const std::array<uint32_t, 7> inheritedSlots = { 19, 22, 31, 76, 77, 78, 79 };
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 7> inherited;
	for (size_t i = 0; i < inheritedSlots.size(); ++i) {
		context->PSGetShaderResources(inheritedSlots[i], 1, inherited[i].put());
		auto* srv = inherited[i].get();
		context->CSSetShaderResources(inheritedSlots[i], 1, &srv);
	}
	std::array<ID3D11ShaderResourceView*, 4> atmosphere = {
		sky.texTrLut ? sky.texTrLut->srv.get() : nullptr, sky.texSvLut ? sky.texSvLut->srv.get() : nullptr, sky.texApLut ? sky.texApLut->srv.get() : nullptr, sky.texApShadow ? sky.texApShadow->srv.get() : nullptr
	};
	context->CSSetShaderResources(61, 4, atmosphere.data());
	std::array<ID3D11ShaderResourceView*, 3> clouds = {
		sky.texVolFilteredTr ? sky.texVolFilteredTr->srv.get() : nullptr,
		sky.texVolFilteredLum ? sky.texVolFilteredLum->srv.get() : nullptr,
		sky.texShadowVolume ? sky.texShadowVolume->srv.get() : nullptr
	};
	context->CSSetShaderResources(110, 3, clouds.data());
	std::array<ID3D11ShaderResourceView*, 2> depths = {
		renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV,
		sky.texVolFilteredAux ? sky.texVolFilteredAux->srv.get() : nullptr
	};
	context->CSSetShaderResources(40, 2, depths.data());
	std::array<ID3D11SamplerState*, 5> samplers = { linearSampler, nullptr, nullptr, linearSampler, linearSampler };
	context->CSSetSamplers(0, 5, samplers.data());
	context->CSSetUnorderedAccessViews(0, 1, &main.UAV, nullptr);
	context->CSSetShader(mediumCompositeCS.get(), nullptr, 0);
	D3D11_TEXTURE2D_DESC desc;
	main.texture->GetDesc(&desc);
	const auto scale = globals::game::frameBufferCached.GetDynamicResolutionParams1();
	const uint32_t width = static_cast<uint32_t>(std::clamp(desc.Width * scale.x, 1.f, float(desc.Width)));
	const uint32_t height = static_cast<uint32_t>(std::clamp(desc.Height * scale.y, 1.f, float(desc.Height)));
	globals::state->BeginPerfEvent("Medium Composite");
	globals::profiler->BeginPass("Deferred::MediumComposite");
	context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
	globals::profiler->EndPass();
	globals::state->EndPerfEvent();
	ID3D11UnorderedAccessView* nullUav = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
	std::array<ID3D11ShaderResourceView*, 128> nullSrvs = {};
	context->CSSetShaderResources(0, static_cast<uint32_t>(nullSrvs.size()), nullSrvs.data());
	context->CSSetShader(nullptr, nullptr, 0);
	ID3D11Buffer* nullBuffer = nullptr;
	context->CSSetConstantBuffers(12, 1, &nullBuffer);
	std::array<ID3D11SamplerState*, 5> nullSamplers = {};
	context->CSSetSamplers(0, static_cast<uint32_t>(nullSamplers.size()), nullSamplers.data());
	if (sky.texVolFilteredAux) {
		auto* aux = sky.texVolFilteredAux->srv.get();
		context->PSSetShaderResources(116, 1, &aux);
	}
	mediumCompositeFrame = globals::state->frameCount;
}

void Deferred::Hooks::PostWater_SetCamera::thunk(void* graphicsState, RE::NiCamera* camera, uint32_t flags)
{
	func(graphicsState, camera, flags);
	globals::deferred->CompositeAfterWater();
}

void Deferred::Hooks::InstallPostWaterHook()
{
	const auto address = REL::RelocationID(99939, 106584).address();
	DWORD64 imageBase = 0;
	const auto* entry = RtlLookupFunctionEntry(address, &imageBase, nullptr);
	if (!entry) {
		logger::error("Deferred post-water function bounds were not found; retaining existing composition.");
		return;
	}
	constexpr std::array<uint8_t, 19> pattern = { 0x48, 0x8B, 0x53, 0x10, 0x48, 0x8D, 0x0D, 0, 0, 0, 0, 0x44, 0x8B, 0xC7, 0xE8, 0, 0, 0, 0 };
	std::uintptr_t callSite = 0;
	for (auto p = address; p + pattern.size() <= imageBase + entry->EndAddress; ++p) {
		const auto* bytes = reinterpret_cast<const uint8_t*>(p);
		bool match = true;
		for (size_t i = 0; i < pattern.size(); ++i)
			if ((i < 7 || (i >= 11 && i < 15)) && bytes[i] != pattern[i])
				match = false;
		if (!match)
			continue;
		if (callSite) {
			logger::error("Deferred post-water boundary is ambiguous; retaining existing composition.");
			return;
		}
		callSite = p + 14;
	}
	if (!callSite) {
		logger::error("Deferred post-water boundary was not found; retaining existing composition.");
		return;
	}
	const auto cameraTarget = callSite + 5 + *reinterpret_cast<const int32_t*>(callSite + 1);
	if (cameraTarget != REL::RelocationID(75694, 77503).address()) {
		logger::error("Deferred post-water camera call was modified; retaining existing composition.");
		return;
	}
	stl::write_thunk_call<PostWater_SetCamera>(callSite);
	globals::deferred->postWaterHookInstalled = true;
}

void Deferred::DrawSettings()
{
	const char* items[] = {
		T("menu.advanced.deferred_debug_view.off", "Off"),
		T("menu.advanced.deferred_debug_view.albedo", "Albedo"),
		T("menu.advanced.deferred_debug_view.reflectance", "Reflectance"),
		T("menu.advanced.deferred_debug_view.normal", "Normal"),
		T("menu.advanced.deferred_debug_view.roughness", "Roughness"),
		T("menu.advanced.deferred_debug_view.mask", "Mask"),
		T("menu.advanced.deferred_debug_view.diffuse", "Diffuse"),
		T("menu.advanced.deferred_debug_view.specular", "Specular"),
		T("menu.advanced.deferred_debug_view.ssgi", "SSGI"),
		T("menu.advanced.deferred_debug_view.ssr", "SSR"),
		T("menu.advanced.deferred_debug_view.dynamic_cubemaps", "Dynamic Cubemaps")
	};

	int current = static_cast<int>(debugView);
	if (ImGui::Combo(T("menu.advanced.deferred_debug_view", "Deferred Debug View"), &current, items, IM_ARRAYSIZE(items))) {
		DebugView next = static_cast<DebugView>(current);
		bool wasOff = debugView == DebugView::Off;
		bool nowOff = next == DebugView::Off;
		debugView = next;
		if (wasOff != nowOff)
			ClearShaderCache();
	}
}

ID3D11ComputeShader* Deferred::GetComputeMainComposite()
{
	if (!mainCompositeCS) {
		logger::debug("Compiling DeferredCompositeCS");

		std::vector<std::pair<const char*, const char*>> defines;

		if (debugView != DebugView::Off)
			defines.push_back({ "DEBUG_VIEW", nullptr });

		if (globals::features::dynamicCubemaps.loaded)
			defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

		if (globals::features::skylighting.loaded)
			defines.push_back({ "SKYLIGHTING", nullptr });

		if (globals::features::screenSpaceGI.loaded) {
			defines.push_back({ "SSGI", nullptr });
			if (globals::features::screenSpaceGI.settings.EnableSH && globals::features::screenSpaceGI.settings.EnableGI)
				defines.push_back({ "SSGI_SH", nullptr });
		}

		if (globals::features::screenSpaceReflections.loaded && globals::features::screenSpaceReflections.settings.Enabled)
			defines.push_back({ "SSR", nullptr });

		if (globals::features::ibl.loaded)
			defines.push_back({ "IBL", nullptr });

		if (globals::features::physicalSky.loaded)
			defines.push_back({ "PHYSICAL_SKY", nullptr });

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

		if (debugView != DebugView::Off)
			defines.push_back({ "DEBUG_VIEW", nullptr });

		if (globals::features::dynamicCubemaps.loaded)
			defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

		if (globals::features::screenSpaceGI.loaded) {
			defines.push_back({ "SSGI", nullptr });
			if (globals::features::screenSpaceGI.settings.EnableSH && globals::features::screenSpaceGI.settings.EnableGI)
				defines.push_back({ "SSGI_SH", nullptr });
		}

		if (globals::features::screenSpaceReflections.loaded && globals::features::screenSpaceReflections.settings.Enabled)
			defines.push_back({ "SSR", nullptr });

		if (globals::features::ibl.loaded)
			defines.push_back({ "IBL", nullptr });

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

	// Copy depth from before water, unless DeferredPasses already did
	if (!deferred->sceneDepthFinal) {
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;

		auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

		context->CopyResource(depthCopy.texture, depth.texture);
	}

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
	VanillaBinding::BindSharedConstantBuffers();
}
