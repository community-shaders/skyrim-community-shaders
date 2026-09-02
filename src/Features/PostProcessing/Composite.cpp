#include "Composite.h"

#include "CODBloom.h"
#include "Features/PostProcessing.h"
#include "HistogramAutoExposure.h"
#include "LensFlare.h"
#include "LocalExposure.h"
#include "PhysicalGlare.h"

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
	auto* glare = owner->GetPipelineFeature<PhysicalGlare>(PostProcessing::FeaturePipelineIndex::PhysicalGlare);
	auto* exposure = owner->GetPipelineFeature<HistogramAutoExposure>(PostProcessing::FeaturePipelineIndex::AutoExposure);
	auto* localExposure = owner->GetPipelineFeature<LocalExposure>(PostProcessing::FeaturePipelineIndex::LocalExposure);

	enabled = (bloom && bloom->enabled) || (flare && flare->enabled) || (glare && glare->enabled) || (exposure && exposure->enabled) || (localExposure && localExposure->enabled);
}

void Composite::SetupResources()
{
	auto renderer = globals::game::renderer;

	{
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

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

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateRTV(rtvDesc);
	}

	CompileRasterShaders();
}

void Composite::ClearShaderCache()
{
	BumpShaderGeneration();
	{
		std::lock_guard lock(shaderMutex);
		for (auto& shader : compositeShaders) {
			if (shader) {
				shader->Release();
				shader.detach();
			}
		}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/Composite");
	CompileRasterShaders();
}

void Composite::CompileRasterShaders()
{
	std::vector<PixelShaderCompileInfo> shaderInfos;

	// Compile all non-empty flag combinations (1..31)
	for (uint flags = 1; flags < CompositeFlags::FLAG_COUNT; flags++) {
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

		shaderInfos.push_back({ &compositeShaders[flags], "composite.ps.hlsl", std::move(defines), "PSComposite" });
	}

	CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\Composite", {}, shaderInfos);
}

void Composite::Draw(TextureInfo& inout_tex)
{
	if (!owner)
		return;

	auto* bloom = owner->GetPipelineFeature<CODBloom>(PostProcessing::FeaturePipelineIndex::CODBloom);
	auto* flare = owner->GetPipelineFeature<LensFlare>(PostProcessing::FeaturePipelineIndex::LensFlare);
	auto* glare = owner->GetPipelineFeature<PhysicalGlare>(PostProcessing::FeaturePipelineIndex::PhysicalGlare);
	auto* exposure = owner->GetPipelineFeature<HistogramAutoExposure>(PostProcessing::FeaturePipelineIndex::AutoExposure);
	auto* localExposure = owner->GetPipelineFeature<LocalExposure>(PostProcessing::FeaturePipelineIndex::LocalExposure);

	bool hasBloom = bloom && bloom->enabled;
	bool hasFlare = flare && flare->enabled;
	bool hasGlare = glare && glare->enabled;
	bool hasExposure = exposure && exposure->enabled;
	bool hasLocalExposure = localExposure && localExposure->enabled;

	uint flags = (hasBloom ? BLOOM : 0) | (hasFlare ? FLARE : 0) | (hasGlare ? GLARE : 0) | (hasExposure ? EXPOSURE : 0) | (hasLocalExposure ? LOCAL_EXPOSURE : 0);
	if (flags == NONE)
		return;

	if (!AllShadersReady({ &compositeShaders[flags] }))
		return;

	if (!owner->GetFullscreenVS())
		return;

	globals::profiler->BeginPass("PostProcessing::Composite");
	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("Composite");

	ID3D11PixelShader* shader = compositeShaders[flags].get();

	// Bind resources:
	//   t0 = main color (inout_tex)
	//   t1 = bloom texture (if available)
	//   t2 = flare texture (if available)
	//   t3 = glare texture (if available)
	//   t4 = adaptation buffer (if exposure enabled)
	//   t5 = local exposure base luminance (if local exposure enabled)
	//   rtv0 = output
	//   b1 = auto exposure constant buffer (if exposure enabled)
	//   b2 = local exposure constant buffer (if local exposure enabled)
	std::array<ID3D11ShaderResourceView*, 6> srvs = { nullptr };

	srvs[0] = inout_tex.srv;

	if (hasBloom) {
		auto bloomOutput = bloom->GetBloomOutput();
		srvs[1] = bloomOutput.srv;
	}
	if (hasFlare) {
		auto flareOutput = flare->GetFlareOutput();
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
		srvs[5] = localExposure->GetBaseLuminanceSRV();
	}

	uint width = texOutput->desc.Width;
	uint height = texOutput->desc.Height;

	{
		PostProcessingRaster::RasterPass pass(context);

		if (hasExposure) {
			ID3D11Buffer* cb = exposure->GetConstantBuffer();
			context->PSSetConstantBuffers(1, 1, &cb);
		}
		if (hasLocalExposure) {
			ID3D11Buffer* cb = localExposure->GetConstantBuffer();
			context->PSSetConstantBuffers(2, 1, &cb);
		}
		context->PSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		pass.SetTargets({ texOutput->rtv.get() }, (float)width, (float)height);
		pass.SetShaders(owner->GetFullscreenVS(), shader);
		pass.Draw();

		// cleanup
		srvs.fill(nullptr);
		context->PSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	}

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	state->EndPerfEvent();
	globals::profiler->EndPass();
}
