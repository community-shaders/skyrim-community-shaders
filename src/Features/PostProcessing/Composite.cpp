#include "Composite.h"

#include "CODBloom.h"
#include "ColorGrading.h"
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
	auto renderer = globals::game::renderer;

	{
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);
		// Exposure is resolved here. Preserve overexposed HDR values until the
		// following tone mapper instead of clipping them back to scene FP16.
		texDesc.Format = globals::features::linearLighting.IsLinearLightingActive() ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc, "Composite Output");
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateRTV(rtvDesc);
	}

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

void Composite::CompileRasterShaders(uint firstFlags, uint endFlags, Output output)
{
	std::vector<PixelShaderCompileInfo> shaderInfos;

	for (uint flags = firstFlags; flags < endFlags; flags++) {
		const uint index = ShaderIndex(flags, output);
		{
			std::lock_guard lock(shaderMutex);
			if (shaderRequested[index])
				continue;
			shaderRequested[index] = true;
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
		if (output == Output::Graded)
			defines.push_back({ "COMPOSITE_APPLY_LUT", "" });
		if (output == Output::Converted)
			defines.push_back({ "COMPOSITE_CONVERT_OUTPUT", "" });

		shaderInfos.push_back({ &compositeShaders[index], "composite.ps.hlsl", std::move(defines), "PSComposite" });
	}

	if (!shaderInfos.empty())
		CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\Composite", {}, shaderInfos);
}

void Composite::Draw(TextureInfo& inout_tex)
{
	Draw(inout_tex, nullptr, {});
}

Composite::Output Composite::Draw(TextureInfo& inout_tex, ColorGrading* grading, ConversionTarget conversion)
{
	if (!owner)
		return Output::Composite;

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
		return Output::Composite;
	if (!AllShadersReady({ &compositeShaders[flags] }))
		return Output::Composite;

	if (!owner->GetFullscreenVS())
		return Output::Composite;
	if (hasPhysical && !emptyWaveCB) {
		emptyWaveCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<PhysicalLensImaging::DiffractionParameters>(), "Composite::InactiveDiffraction");
		emptyWaveCB->Update(PhysicalLensImaging::DiffractionParameters{});
	}

	Output output = Output::Composite;
	auto* target = texOutput.get();
	const char* passName = "PostProcessing::Composite";
	if (texOutput->desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
		if (grading && !grading->settings.skipLUT) {
			CompileRasterShaders(flags, flags + 1, Output::Graded);
			if (AllShadersReady({ &compositeShaders[ShaderIndex(flags, Output::Graded)] }) && grading->PrepareDraw()) {
				output = Output::Graded;
				target = grading->texColor.get();
				passName = "PostProcessing::CompositeAndColorGrading";
			}
		} else if (conversion.texture && conversion.texture->rtv && conversion.constants &&
				   conversion.texture->desc.Width == texOutput->desc.Width &&
				   conversion.texture->desc.Height == texOutput->desc.Height) {
			CompileRasterShaders(flags, flags + 1, Output::Converted);
			if (AllShadersReady({ &compositeShaders[ShaderIndex(flags, Output::Converted)] })) {
				output = Output::Converted;
				target = conversion.texture;
				passName = "PostProcessing::CompositeAndConvertToTarget";
			}
		}
	}

	const bool applyLUT = output == Output::Graded;
	const bool convertOutput = output == Output::Converted;
	auto state = globals::state;
	auto context = globals::d3d::context;

	globals::profiler->BeginPass(passName);
	state->BeginPerfEvent("Composite");
	if (applyLUT)
		grading->UpdateConstantsAndLUT();

	ID3D11PixelShader* shader = compositeShaders[ShaderIndex(flags, output)].get();

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
	//   t15/b0/s2 = fused color grading LUT, constants and sampler
	//   b0 = final output conversion constants on the conversion variant
	//   b4 = physical diffraction sampling (zeroed when inactive)
	//   rtv0 = output
	//   b1 = auto exposure constant buffer (if exposure enabled)
	//   b2 = local exposure constant buffer (if local exposure enabled)
	//   b3 = final bloom/flare sampling parameters
	//   s0 = linear clamp (local exposure and flare), s1 = linear border (bloom)
	std::array<ID3D11ShaderResourceView*, 16> srvs = {};
	const uint srvCount = applyLUT ? 16 : (hasPhysical ? 15 : 8);

	srvs[0] = inout_tex.srv;
	if (applyLUT)
		srvs[15] = grading->texLUT->srv.get();
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
		PostProcessingRaster::RasterPass pass(context, srvCount, applyLUT ? 3 : 2);

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
		if (applyLUT) {
			cb = grading->colorCB->CB();
			context->PSSetConstantBuffers(0, 1, &cb);
			ID3D11SamplerState* sampler = grading->linearSampler.get();
			context->PSSetSamplers(2, 1, &sampler);
		} else if (convertOutput) {
			context->PSSetConstantBuffers(0, 1, &conversion.constants);
		}
		context->PSSetShaderResources(0, srvCount, srvs.data());
		pass.SetTargets({ target->rtv.get() }, (float)width, (float)height);
		pass.SetShaders(owner->GetFullscreenVS(), shader);
		pass.Draw();

		// cleanup
		srvs.fill(nullptr);
		context->PSSetShaderResources(0, srvCount, srvs.data());
	}

	inout_tex = { target->resource.get(), target->srv.get() };
	state->EndPerfEvent();
	globals::profiler->EndPass();
	if (applyLUT)
		grading->FinishDraw();
	return output;
}
