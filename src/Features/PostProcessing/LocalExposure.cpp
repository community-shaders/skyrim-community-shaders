#include "LocalExposure.h"

#include "Features/PostProcessing.h"
#include "HistogramAutoExposure.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LocalExposure::Settings,
	Exposure,
	Shadows,
	Highlights,
	ExposurePreferenceSigma,
	Mip,
	DisplayMip,
	BoostLocalContrast)

void LocalExposure::DrawSettings()
{
	ImGui::SliderFloat("Exposure", &settings.Exposure, 0.f, 4.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Manual brightness normalization used when Histogram Auto Exposure is disabled. Higher values make the scene behave brighter.");

	ImGui::SliderFloat("Shadow Recovery", &settings.Shadows, 0.f, 4.f, "%.1f EV");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("How strongly darker areas are lifted. Higher values recover more shadow detail.");

	ImGui::SliderFloat("Highlight Recovery", &settings.Highlights, 0.f, 4.f, "%.1f EV");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("How strongly bright areas are compressed. Higher values preserve more highlight detail.");

	ImGui::SliderFloat("Exposure Preference", &settings.ExposurePreferenceSigma, 0.f, 10.f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("How selectively each area chooses its best exposure. Higher values create stronger local adaptation; lower values blend more softly.");

	int mipVal = (int)settings.Mip;
	ImGui::SliderInt("Coarse Scale (Mip)", &mipVal, 0, (int)s_MaxMips - 1);
	settings.Mip = (uint)std::clamp(mipVal, 0, (int)s_MaxMips - 1);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Largest image scale used by the effect. Higher values affect broader lighting regions.");

	int displayMipVal = (int)settings.DisplayMip;
	ImGui::SliderInt("Detail Scale (Display Mip)", &displayMipVal, 0, (int)s_MaxMips - 1);
	settings.DisplayMip = (uint)std::clamp(displayMipVal, 0, (int)settings.Mip);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Finest pyramid level reconstructed before full-resolution upsampling. Lower values keep smaller local details.");

	ImGui::Checkbox("Boost Local Contrast", &settings.BoostLocalContrast);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Gives high-contrast local details more influence during pyramid reconstruction.");
}

void LocalExposure::RestoreDefaultSettings()
{
	settings = {};
}

void LocalExposure::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LocalExposure::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LocalExposure::SetupResources()
{
	auto renderer = globals::game::renderer;

	// Get screen dimensions from game render target
	auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];
	D3D11_TEXTURE2D_DESC mainDesc;
	gameTexMainCopy.texture->GetDesc(&mainDesc);

	uint fullW = mainDesc.Width;
	uint fullH = mainDesc.Height;
	// Calculate mip count for the full-resolution exposure-fusion pyramids.
	numMips = 1;
	{
		uint w = fullW, h = fullH;
		while (w > 1 && h > 1 && numMips < s_MaxMips) {
			w = (w + 1) / 2;
			h = (h + 1) / 2;
			numMips++;
		}
	}

	auto createMipViews = [](Texture2D& texture,
							  DXGI_FORMAT format,
							  uint mipCount,
							  std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_MaxMips>& srvs,
							  std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_MaxMips>& uavs) {
		auto device = globals::d3d::device;
		for (uint i = 0; i < mipCount; i++) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = i;
			srvDesc.Texture2D.MipLevels = 1;
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture.resource.get(), &srvDesc, srvs[i].put()));

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = i;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture.resource.get(), &uavDesc, uavs[i].put()));
		}
	};

	// Create exposure luminance and weight pyramids (RGB = highlight, midtone, shadow).
	{
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = fullW;
		texDesc.Height = fullH;
		texDesc.MipLevels = numMips;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		texExposures = eastl::make_unique<Texture2D>(texDesc, "LocalExposure Exposures");
		texWeights = eastl::make_unique<Texture2D>(texDesc, "LocalExposure Weights");

		createMipViews(*texExposures, DXGI_FORMAT_R16G16B16A16_FLOAT, numMips, exposureMipSRVs, exposureMipUAVs);
		createMipViews(*texWeights, DXGI_FORMAT_R16G16B16A16_FLOAT, numMips, weightMipSRVs, weightMipUAVs);
	}

	// Create assembled fusion result pyramid.
	{
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = fullW;
		texDesc.Height = fullH;
		texDesc.MipLevels = numMips;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		texAssemble = eastl::make_unique<Texture2D>(texDesc, "LocalExposure Assemble");
		createMipViews(*texAssemble, DXGI_FORMAT_R16_FLOAT, numMips, assembleMipSRVs, assembleMipUAVs);
	}

	// Create output exposure texture (full resolution, R16F)
	{
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = fullW;
		texDesc.Height = fullH;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		texExposure = eastl::make_unique<Texture2D>(texDesc, "LocalExposure Output");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		texExposure->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		texExposure->CreateUAV(uavDesc);
	}

	// Create linear sampler
	{
		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateSamplerState(&sampDesc, linearSampler.put()));
	}

	// Create constant buffer
	localExposureCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<LocalExposureCB>());

	CompileComputeShaders();
}

void LocalExposure::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&setupCS, &downsampleCS, &blendCS, &computeExpCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void LocalExposure::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry;
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &setupCS, "localexposure.cs.hlsl", {}, "CSSetup" },
		{ &downsampleCS, "localexposure.cs.hlsl", {}, "CSDownsample" },
		{ &blendCS, "localexposure.cs.hlsl", {}, "CSBlend" },
		{ &computeExpCS, "localexposure.cs.hlsl", {}, "CSComputeExposure" },
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\LocalExposure") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
}

void LocalExposure::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	auto state = globals::state;

	state->BeginPerfEvent("Local Exposure");

	// Get dimensions
	D3D11_TEXTURE2D_DESC mainDesc;
	inout_tex.tex->GetDesc(&mainDesc);
	uint fullW = mainDesc.Width;
	uint fullH = mainDesc.Height;

	uint mipLevel = std::min(settings.Mip, numMips - 1);
	uint displayMip = std::min(settings.DisplayMip, mipLevel);
	auto* exposure = owner ? owner->GetPipelineFeature<HistogramAutoExposure>(PostProcessing::FeaturePipelineIndex::AutoExposure) : nullptr;
	bool useGlobalExposure = exposure && exposure->enabled && exposure->GetAdaptationSRV();
	float exposureCompensation = useGlobalExposure ? exp2(exposure->settings.ExposureCompensation) : 1.f;
	float2 adaptationRange = useGlobalExposure ?
	                             float2{ exp2(exposure->settings.AdaptationRange.x), exp2(exposure->settings.AdaptationRange.y) } :
	                             float2{ 1.f, 1.f };
	// Update constant buffer
	LocalExposureCB cbData = {
		.ManualExposure = settings.Exposure,
		.HighlightExposure = exp2(-settings.Highlights),
		.ShadowExposure = exp2(settings.Shadows),
		.ExposurePreferenceSigmaSq = settings.ExposurePreferenceSigma * settings.ExposurePreferenceSigma,
		.InputWidth = fullW,
		.InputHeight = fullH,
		.MipLevel = mipLevel,
		.DisplayMip = displayMip,
		.CurrentMip = 0,
		.HasCoarserMip = 0,
		.BoostLocalContrast = settings.BoostLocalContrast ? 1u : 0u,
		.UseGlobalExposure = useGlobalExposure ? 1u : 0u,
		.ExposureCompensation = exposureCompensation,
		.AdaptationMin = adaptationRange.x,
		.AdaptationMax = adaptationRange.y,
		.DarkThreshold = 0.007f,
	};

	ID3D11Buffer* cb = localExposureCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	ID3D11SamplerState* sampler = linearSampler.get();
	context->CSSetSamplers(0, 1, &sampler);

	auto updateCB = [&]() {
		localExposureCB->Update(cbData);
	};

	auto mipDim = [](uint dim, uint mip) {
		return std::max(1u, (dim + ((1u << mip) - 1u)) >> mip);
	};

	// === Pass 1: Compute synthetic exposure luminances and weights ===
	{
		state->BeginPerfEvent("Fusion Setup");

		updateCB();

		std::array<ID3D11ShaderResourceView*, 5> srvs = { inout_tex.srv, nullptr, nullptr, nullptr, useGlobalExposure ? exposure->GetAdaptationSRV() : nullptr };
		std::array<ID3D11UnorderedAccessView*, 2> uavs = { exposureMipUAVs[0].get(), weightMipUAVs[0].get() };

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(setupCS.get(), nullptr, 0);
		context->Dispatch((fullW + 7) >> 3, (fullH + 7) >> 3, 1);

		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		state->EndPerfEvent();
	}

	// === Pass 2: Build exposure and weight mip chains ===
	{
		state->BeginPerfEvent("Mip Chain");

		for (uint i = 1; i <= mipLevel; i++) {
			std::array<ID3D11ShaderResourceView*, 2> srvs = { exposureMipSRVs[i - 1].get(), weightMipSRVs[i - 1].get() };
			std::array<ID3D11UnorderedAccessView*, 2> uavs = { exposureMipUAVs[i].get(), weightMipUAVs[i].get() };

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(downsampleCS.get(), nullptr, 0);

			uint mipW = mipDim(fullW, i);
			uint mipH = mipDim(fullH, i);
			context->Dispatch((mipW + 7) >> 3, (mipH + 7) >> 3, 1);

			srvs.fill(nullptr);
			uavs.fill(nullptr);
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		}

		state->EndPerfEvent();
	}

	// === Pass 3: Reconstruct Gaussian/Laplacian exposure-fusion result ===
	{
		state->BeginPerfEvent("Fusion Blend");

		for (int i = (int)mipLevel; i >= (int)displayMip; i--) {
			cbData.CurrentMip = (uint)i;
			cbData.HasCoarserMip = i < (int)mipLevel ? 1u : 0u;
			updateCB();

			std::array<ID3D11ShaderResourceView*, 4> srvs = {
				exposureMipSRVs[i].get(),
				weightMipSRVs[i].get(),
				cbData.HasCoarserMip ? exposureMipSRVs[i + 1].get() : nullptr,
				cbData.HasCoarserMip ? assembleMipSRVs[i + 1].get() : nullptr,
			};
			std::array<ID3D11UnorderedAccessView*, 3> uavs = { nullptr, nullptr, assembleMipUAVs[i].get() };

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(blendCS.get(), nullptr, 0);

			uint mipW = mipDim(fullW, (uint)i);
			uint mipH = mipDim(fullH, (uint)i);
			context->Dispatch((mipW + 7) >> 3, (mipH + 7) >> 3, 1);

			srvs.fill(nullptr);
			uavs.fill(nullptr);
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		}

		state->EndPerfEvent();
	}

	// === Pass 4: Guided upsample and output raw-HDR exposure multiplier ===
	{
		state->BeginPerfEvent("Compute Exposure");

		cbData.CurrentMip = 0;
		cbData.HasCoarserMip = 0;
		updateCB();

		// t0 = full-res raw scene color
		// t1 = exposure luminance texture at display_mip
		// t2 = assembled fusion result at display_mip
		// u2 = output exposure map
		std::array<ID3D11ShaderResourceView*, 5> srvs = {
			inout_tex.srv,
			exposureMipSRVs[displayMip].get(),
			assembleMipSRVs[displayMip].get(),
			nullptr,
			useGlobalExposure ? exposure->GetAdaptationSRV() : nullptr,
		};
		std::array<ID3D11UnorderedAccessView*, 3> uavs = { nullptr, nullptr, texExposure->uav.get() };

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(computeExpCS.get(), nullptr, 0);
		context->Dispatch((fullW + 7) >> 3, (fullH + 7) >> 3, 1);

		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);

		state->EndPerfEvent();
	}

	// Cleanup
	context->CSSetShader(nullptr, nullptr, 0);
	cb = nullptr;
	context->CSSetConstantBuffers(1, 1, &cb);
	sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);

	// NOTE: We do NOT modify inout_tex. The exposure map is consumed by Composite.
	state->EndPerfEvent();
}
