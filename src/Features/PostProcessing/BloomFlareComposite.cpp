#include "BloomFlareComposite.h"

#include "CODBloom.h"
#include "Features/PostProcessing.h"
#include "LensFlare.h"

#include "State.h"
#include "Util.h"

void BloomFlareComposite::UpdateAutoEnabled()
{
	if (!owner)
		return;

	auto* bloom = owner->GetPipelineFeature<CODBloom>(PostProcessing::FeaturePipelineIndex::CODBloom);
	auto* flare = owner->GetPipelineFeature<LensFlare>(PostProcessing::FeaturePipelineIndex::LensFlare);

	enabled = (bloom && bloom->enabled) || (flare && flare->enabled);
}

void BloomFlareComposite::SetupResources()
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

void BloomFlareComposite::ClearShaderCache()
{
	auto const shaderPtrs = std::array{
		&compositeCS, &compositeBloomOnlyCS, &compositeFlareOnlyCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void BloomFlareComposite::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry = "main";
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &compositeCS, "composite.cs.hlsl", { { "HAS_BLOOM", "" }, { "HAS_LENS_FLARE", "" } }, "CSComposite" },
			{ &compositeBloomOnlyCS, "composite.cs.hlsl", { { "HAS_BLOOM", "" } }, "CSComposite" },
			{ &compositeFlareOnlyCS, "composite.cs.hlsl", { { "HAS_LENS_FLARE", "" } }, "CSComposite" },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\BloomFlareComposite") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
}

void BloomFlareComposite::Draw(TextureInfo& inout_tex)
{
	if (!owner)
		return;

	auto* bloom = owner->GetPipelineFeature<CODBloom>(PostProcessing::FeaturePipelineIndex::CODBloom);
	auto* flare = owner->GetPipelineFeature<LensFlare>(PostProcessing::FeaturePipelineIndex::LensFlare);

	bool hasBloom = bloom && bloom->enabled;
	bool hasFlare = flare && flare->enabled;

	if (!hasBloom && !hasFlare)
		return;

	auto state = globals::state;
	auto context = globals::d3d::context;

	state->BeginPerfEvent("Bloom/Flare Composite");

	// Select the appropriate shader permutation
	ID3D11ComputeShader* shader = nullptr;
	if (hasBloom && hasFlare)
		shader = compositeCS.get();
	else if (hasBloom)
		shader = compositeBloomOnlyCS.get();
	else
		shader = compositeFlareOnlyCS.get();

	if (!shader) {
		state->EndPerfEvent();
		return;
	}

	// Bind resources:
	//   t0 = main color (inout_tex)
	//   t1 = bloom texture (if available)
	//   t2 = flare texture (if available)
	//   u0 = output
	std::array<ID3D11ShaderResourceView*, 3> srvs = { nullptr };
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

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	state->EndPerfEvent();
}
