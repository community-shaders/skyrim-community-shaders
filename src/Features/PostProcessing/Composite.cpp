#include "Composite.h"

#include "CODBloom.h"
#include "Features/LinearLighting.h"
#include "Features/PostProcessing.h"
#include "HistogramAutoExposure.h"
#include "LensFlare.h"
#include "LocalExposure.h"
#include "PhysicalGlare.h"
#include "PhysicalLens.h"

#include "RasterPass.h"

#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

void Composite::UpdateAutoEnabled()
{
	if (!owner)
		return;

	auto* bloom = owner->GetPipelineFeature<CODBloom>(PostProcessing::FeaturePipelineIndex::CODBloom);
	auto* flare = owner->GetPipelineFeature<LensFlare>(PostProcessing::FeaturePipelineIndex::LensFlare);
	auto* physicalLens = owner->GetPipelineFeature<PhysicalLens>(PostProcessing::FeaturePipelineIndex::PhysicalLens);
	auto* glare = owner->GetPipelineFeature<PhysicalGlare>(PostProcessing::FeaturePipelineIndex::PhysicalGlare);
	auto* exposure = owner->GetPipelineFeature<HistogramAutoExposure>(PostProcessing::FeaturePipelineIndex::AutoExposure);
	auto* localExposure = owner->GetPipelineFeature<LocalExposure>(PostProcessing::FeaturePipelineIndex::LocalExposure);

	enabled = globals::state->preExposure != 1.0f || (bloom && bloom->enabled) || (flare && flare->enabled) || (glare && glare->enabled) || (physicalLens && physicalLens->enabled) || (exposure && exposure->IsActive()) || (localExposure && localExposure->enabled);
}

void Composite::SetupResources()
{
	compositeCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<CompositeCB>(), "Composite Parameters");
	CompileRasterShaders();
}

void Composite::ClearShaderCache()
{
	BumpShaderGeneration();
	{
		std::lock_guard lock(shaderMutex);
		shaderRequested.fill(false);
		for (auto& shader : compositeShaders) {
			if (shader) {
				shader->Release();
				shader.detach();
			}
		}
	}

	CompileRasterShaders();
}

void Composite::CompileRasterShaders(uint firstFlags, uint endFlags)
{
	std::vector<PixelShaderCompileInfo> shaderInfos;

	for (uint flags = firstFlags; flags < endFlags; flags++) {
		{
			std::lock_guard lock(shaderMutex);
			if (shaderRequested[flags])
				continue;
			shaderRequested[flags] = true;
		}
		std::vector<std::pair<const char*, const char*>> defines;
		if (flags & BLOOM)
			defines.push_back({ "HAS_BLOOM", "" });
		if (flags & FLARE)
			defines.push_back({ "HAS_LENS_FLARE", "" });
		if (flags & GLARE)
			defines.push_back({ "HAS_GLARE", "" });
		if (flags & EXPOSURE)
			defines.push_back({ "HAS_EXPOSURE", "" });
		if (flags & LOCAL_EXPOSURE)
			defines.push_back({ "HAS_LOCAL_EXPOSURE", "" });
		if (flags & PHYSICAL_LENS)
			defines.push_back({ "HAS_PHYSICAL_LENS", "" });

		shaderInfos.push_back({ &compositeShaders[flags], "composite.ps.hlsl", std::move(defines), "PSComposite" });
	}

	if (!shaderInfos.empty())
		CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\Composite", {}, shaderInfos);
}

void Composite::Draw(TextureInfo& inout_tex)
{
	if (!owner)
		return;

	auto* bloom = owner->GetPipelineFeature<CODBloom>(PostProcessing::FeaturePipelineIndex::CODBloom);
	auto* flare = owner->GetPipelineFeature<LensFlare>(PostProcessing::FeaturePipelineIndex::LensFlare);
	auto* physicalLens = owner->GetPipelineFeature<PhysicalLens>(PostProcessing::FeaturePipelineIndex::PhysicalLens);
	auto* glare = owner->GetPipelineFeature<PhysicalGlare>(PostProcessing::FeaturePipelineIndex::PhysicalGlare);
	auto* exposure = owner->GetPipelineFeature<HistogramAutoExposure>(PostProcessing::FeaturePipelineIndex::AutoExposure);
	auto* localExposure = owner->GetPipelineFeature<LocalExposure>(PostProcessing::FeaturePipelineIndex::LocalExposure);

	bool hasBloom = bloom && bloom->enabled && bloom->GetBloomOutput().srv;
	bool hasFlare = flare && flare->enabled && flare->GetGhostHaloOutput().srv;
	bool hasPhysicalFlare = physicalLens && physicalLens->enabled && physicalLens->GetOutput().srv;
	bool hasPhysicalLens = physicalLens && physicalLens->enabled && physicalLens->GetCameraMapSRV();
	bool hasPhysicalWave = physicalLens && physicalLens->enabled && physicalLens->GetDiffractionCB();
	bool hasGlare = glare && glare->enabled && glare->GetGlareOutput().srv;
	bool hasExposure = exposure && exposure->IsActive() && exposure->GetAdaptationSRV();
	bool hasLocalExposure = localExposure && localExposure->enabled && localExposure->GetLogLuminanceSRV();
	bool hasPhysical = hasPhysicalFlare || hasPhysicalLens || hasPhysicalWave;

	uint flags = (hasBloom ? BLOOM : 0) | (hasFlare ? FLARE : 0) | (hasGlare ? GLARE : 0) | (hasExposure ? EXPOSURE : 0) | (hasLocalExposure ? LOCAL_EXPOSURE : 0) | (hasPhysical ? PHYSICAL_LENS : 0);

	if (hasPhysical) {
		CompileRasterShaders(flags, flags + 1);
		if (!AllShadersReady({ &compositeShaders[flags] })) {
			// Keep established effects while the on-demand lens variant compiles.
			hasPhysicalFlare = hasPhysicalLens = hasPhysicalWave = hasPhysical = false;
			flags &= ~PHYSICAL_LENS;
		}
	}
	if (flags == NONE && globals::state->preExposure == 1.0f)
		return;
	if (!AllShadersReady({ &compositeShaders[flags] }))
		return;

	if (!owner->GetFullscreenVS())
		return;
	if (hasPhysical && !emptyWaveCB) {
		emptyWaveCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<PhysicalLensImaging::DiffractionParameters>(), "Composite::InactiveDiffraction");
		emptyWaveCB->Update(PhysicalLensImaging::DiffractionParameters{});
	}

	// Keep exposed HDR values in FP32 until the tone mapper.
	const auto format = globals::features::linearLighting.IsLinearLightingActive() ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
	auto* texOutput = owner->GetColorTarget(inout_tex.tex, format);
	if (!texOutput)
		return;

	globals::profiler->BeginPass("PostProcessing::Composite");
	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("Composite");

	ID3D11PixelShader* shader = compositeShaders[flags].get();

	// Bind resources:
	//   t0 = main color (inout_tex)
	//   t1 = accumulated bloom mip 1 (if available)
	//   t2 = half-resolution ghosts and halo (if available)
	//   t3 = glare texture (if available)
	//   t4 = adaptation buffer (if exposure enabled)
	//   t5 = original scene log luminance (if local exposure enabled)
	//   t6 = edge-aware luminance grid
	//   t7 = blurred luminance
	//   t8 = half-resolution physical lens ghosts (independent of t2)
	//   t9 = spectral image mapping and transmission
	//   t10-t14 = diffraction core, ownership, sources, tile masks, PSF
	//   b4 = physical diffraction sampling (zeroed when inactive)
	//   rtv0 = output
	//   b1 = auto exposure constant buffer (if exposure enabled)
	//   b2 = local exposure constant buffer (if local exposure enabled)
	//   b3 = final bloom/flare sampling parameters
	//   s0 = linear clamp (local exposure and flare), s1 = linear border (bloom)
	std::array<ID3D11ShaderResourceView*, 15> srvs = { nullptr };
	const uint srvCount = hasPhysical ? (uint)srvs.size() : 8;

	srvs[0] = inout_tex.srv;
	if (hasPhysicalWave || hasPhysicalFlare) {
		auto wave = physicalLens->GetDiffractionSRVs();
		std::copy(wave.begin(), wave.end(), srvs.begin() + 10);
	}
	if (hasPhysicalLens)
		srvs[9] = physicalLens->GetCameraMapSRV();
	if (hasPhysicalFlare)
		srvs[8] = physicalLens->GetOutput().srv;

	if (hasBloom) {
		auto bloomOutput = bloom->GetBloomOutput();
		srvs[1] = bloomOutput.srv;
	}
	if (hasFlare) {
		auto flareOutput = flare->GetGhostHaloOutput();
		srvs[2] = flareOutput.srv;
	}
	if (hasGlare) {
		auto glareOutput = glare->GetGlareOutput();
		srvs[3] = glareOutput.srv;
	}
	if (hasExposure) {
		srvs[4] = exposure->GetAdaptationSRV();
	}
	if (hasLocalExposure) {
		srvs[5] = localExposure->GetLogLuminanceSRV();
		srvs[6] = localExposure->GetLuminanceGridSRV();
		srvs[7] = localExposure->GetBlurredLuminanceSRV();
	}

	uint width = texOutput->desc.Width;
	uint height = texOutput->desc.Height;

	CompositeCB cbData = {};
	cbData.OutputSize[0] = (float)width;
	cbData.OutputSize[1] = (float)height;
	if (hasBloom) {
		cbData.BloomRadius = bloom->settings.UpsampleRadius;
		cbData.BloomIntensity = bloom->settings.BlendFactor;
	}
	if (hasFlare) {
		cbData.FlareSize[0] = (float)flare->texGhostHalo->desc.Width;
		cbData.FlareSize[1] = (float)flare->texGhostHalo->desc.Height;
		std::copy(flare->settings.Tint.begin(), flare->settings.Tint.end(), cbData.FlareTint);
		cbData.FlareIntensity = flare->settings.Intensity;
	}
	cbData.HasPhysicalFlare = hasPhysicalFlare ? 1.f : 0.f;
	cbData.HasPhysicalLens = hasPhysicalLens ? 1.f : 0.f;
	compositeCB->Update(cbData);
	std::array<ID3D11SamplerState*, 2> samplers = {
		hasLocalExposure ? localExposure->linearSampler.get() : (hasFlare ? flare->colorSampler.get() : ((hasPhysicalFlare || hasPhysicalLens || hasPhysicalWave) ? physicalLens->sampler.get() : nullptr)),
		hasBloom ? bloom->colorSampler.get() : nullptr
	};

	{
		PostProcessingRaster::RasterPass pass(context, srvCount);

		if (hasExposure) {
			ID3D11Buffer* cb = exposure->GetConstantBuffer();
			context->PSSetConstantBuffers(1, 1, &cb);
		}
		if (hasLocalExposure) {
			ID3D11Buffer* cb = localExposure->GetConstantBuffer();
			context->PSSetConstantBuffers(2, 1, &cb);
		}
		ID3D11Buffer* cb = compositeCB->CB();
		context->PSSetConstantBuffers(3, 1, &cb);
		if (hasPhysical) {
			cb = hasPhysicalWave ? physicalLens->GetDiffractionCB() : emptyWaveCB->CB();
			context->PSSetConstantBuffers(4, 1, &cb);
		}
		context->PSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->PSSetShaderResources(0, srvCount, srvs.data());
		pass.SetTargets({ texOutput->rtv.get() }, (float)width, (float)height);
		pass.SetShaders(owner->GetFullscreenVS(), shader);
		pass.Draw();

		// cleanup
		srvs.fill(nullptr);
		context->PSSetShaderResources(0, srvCount, srvs.data());
	}

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	state->EndPerfEvent();
	globals::profiler->EndPass();
}
