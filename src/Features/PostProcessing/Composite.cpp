#include "Composite.h"

#include "CODBloom.h"
#include "Features/PostProcessing.h"
#include "HistogramAutoExposure.h"
#include "LensFlare.h"
#include "PhysicalGlare.h"

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

	enabled = (bloom && bloom->enabled) || (flare && flare->enabled) || (glare && glare->enabled) || (exposure && exposure->enabled);
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

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateUAV(uavDesc);
	}

	CompileComputeShaders();
}

void Composite::ClearShaderCache()
{
	for (auto& shader : compositeShaders) {
		if (shader) {
			shader->Release();
			shader.detach();
		}
	}

	CompileComputeShaders();
}

void Composite::CompileComputeShaders()
{
	auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\Composite\\composite.cs.hlsl");

	// Compile all non-empty flag combinations (1..15)
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

		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), defines, "cs_5_0", "CSComposite")))
			compositeShaders[flags].attach(rawPtr);
	}
}

void Composite::Draw(TextureInfo& inout_tex)
{
	if (!owner)
		return;

	auto* bloom = owner->GetPipelineFeature<CODBloom>(PostProcessing::FeaturePipelineIndex::CODBloom);
	auto* flare = owner->GetPipelineFeature<LensFlare>(PostProcessing::FeaturePipelineIndex::LensFlare);
	auto* glare = owner->GetPipelineFeature<PhysicalGlare>(PostProcessing::FeaturePipelineIndex::PhysicalGlare);
	auto* exposure = owner->GetPipelineFeature<HistogramAutoExposure>(PostProcessing::FeaturePipelineIndex::AutoExposure);

	bool hasBloom = bloom && bloom->enabled;
	bool hasFlare = flare && flare->enabled;
	bool hasGlare = glare && glare->enabled;
	bool hasExposure = exposure && exposure->enabled;

	uint flags = (hasBloom ? BLOOM : 0) | (hasFlare ? FLARE : 0) | (hasGlare ? GLARE : 0) | (hasExposure ? EXPOSURE : 0);
	if (flags == NONE)
		return;

	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("Composite");

	ID3D11ComputeShader* shader = compositeShaders[flags].get();
	if (!shader) {
		state->EndPerfEvent();
		return;
	}

	// Bind resources:
	//   t0 = main color (inout_tex)
	//   t1 = bloom texture (if available)
	//   t2 = flare texture (if available)
	//   t3 = glare texture (if available)
	//   t4 = adaptation buffer (if exposure enabled)
	//   u0 = output
	//   b1 = auto exposure constant buffer (if exposure enabled)
	std::array<ID3D11ShaderResourceView*, 5> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };

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

		// Bind the auto exposure constant buffer at b1
		ID3D11Buffer* cb = exposure->GetConstantBuffer();
		context->CSSetConstantBuffers(1, 1, &cb);
	}

	uavs[0] = texOutput->uav.get();

	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShader(shader, nullptr, 0);

	uint width = texOutput->desc.Width;
	uint height = texOutput->desc.Height;
	context->Dispatch((width + 7) >> 3, (height + 7) >> 3, 1);

	// cleanup
	srvs.fill(nullptr);
	uavs.fill(nullptr);

	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	if (hasExposure) {
		ID3D11Buffer* nullCB = nullptr;
		context->CSSetConstantBuffers(1, 1, &nullCB);
	}

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	state->EndPerfEvent();
}
