#include "LocalExposure.h"

#include "Features/PostProcessing.h"
#include "HistogramAutoExposure.h"
#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LocalExposure::Settings,
	Exposure,
	Strength,
	HighlightContrast,
	ShadowContrast,
	DetailStrength,
	BaseBlend,
	BlurredLuminanceKernelSize,
	MiddleGreyBias,
	HighlightThreshold,
	ShadowThreshold,
	HighlightThresholdStrength,
	ShadowThresholdStrength)

void LocalExposure::DrawSettings()
{
	auto* exposure = owner ? owner->GetPipelineFeature<HistogramAutoExposure>(PostProcessing::FeaturePipelineIndex::AutoExposure) : nullptr;
	if (!exposure || !exposure->enabled) {
		ImGui::SliderFloat(T("feature.post_processing.local_exposure.exposure", "Exposure"), &settings.Exposure, 0.f, 4.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.local_exposure.manual_brightness_normalization_used_when_histogram_auto_exposure", "Manual brightness normalization used when Histogram Auto Exposure is disabled. Higher values make the scene behave brighter."));
	}

	ImGui::SliderFloat(T("feature.post_processing.local_exposure.strength", "Strength"), &settings.Strength, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.local_exposure.strength_tooltip", "Blends the local adjustment with the globally exposed image."));

	ImGui::SliderFloat(T("feature.post_processing.local_exposure.highlight_contrast", "Highlight Contrast"), &settings.HighlightContrast, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.local_exposure.highlight_contrast_tooltip", "Controls base-layer contrast above middle grey. Lower values recover more highlight range."));

	ImGui::SliderFloat(T("feature.post_processing.local_exposure.shadow_contrast", "Shadow Contrast"), &settings.ShadowContrast, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.local_exposure.shadow_contrast_tooltip", "Controls base-layer contrast below middle grey. Lower values lift shadows more strongly."));

	ImGui::SliderFloat(T("feature.post_processing.local_exposure.detail_strength", "Detail Strength"), &settings.DetailStrength, 0.f, 2.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.local_exposure.detail_strength_tooltip", "Preserves or boosts fine luminance detail separated from the base layer. 1.0 keeps the original detail contrast."));

	ImGui::SliderFloat(T("feature.post_processing.local_exposure.base_blend", "Soft Base Blend"), &settings.BaseBlend, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.local_exposure.base_blend_tooltip", "Blends the edge-aware base with a broad smooth base. Higher values reduce halos and keep large highlights natural."));

	ImGui::SliderFloat(T("feature.post_processing.local_exposure.blurred_luminance_kernel_size", "Blurred Luminance Kernel Size"), &settings.BlurredLuminanceKernelSize, 0.f, 100.f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.local_exposure.blurred_luminance_kernel_size_tooltip", "Sets the broad luminance blur diameter as a percentage of screen width."));

	if (ImGui::TreeNodeEx(T("feature.post_processing.local_exposure.advanced", "Advanced"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T("feature.post_processing.local_exposure.middle_grey_bias", "Middle Grey Bias"), &settings.MiddleGreyBias, -4.f, 4.f, "%+.2f EV");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.local_exposure.middle_grey_bias_tooltip", "Moves the tonal pivot used to separate highlight and shadow adjustments."));

		ImGui::SliderFloat(T("feature.post_processing.local_exposure.highlight_threshold", "Highlight Threshold"), &settings.HighlightThreshold, 0.f, 4.f, "%.2f EV");
		ImGui::SliderFloat(T("feature.post_processing.local_exposure.highlight_threshold_strength", "Highlight Threshold Strength"), &settings.HighlightThresholdStrength, 0.f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.local_exposure.highlight_threshold_tooltip", "Protects tones near middle grey before highlight compression begins."));

		ImGui::SliderFloat(T("feature.post_processing.local_exposure.shadow_threshold", "Shadow Threshold"), &settings.ShadowThreshold, 0.f, 4.f, "%.2f EV");
		ImGui::SliderFloat(T("feature.post_processing.local_exposure.shadow_threshold_strength", "Shadow Threshold Strength"), &settings.ShadowThresholdStrength, 0.f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.local_exposure.shadow_threshold_tooltip", "Protects tones near middle grey before shadow lifting begins."));

		ImGui::TreePop();
	}

	if (ImGui::CollapsingHeader(T("feature.post_processing.local_exposure.debug", "Debug"))) {
		static float debugRescale = .3f;
		ImGui::SliderFloat(T("feature.post_processing.local_exposure.view_resize", "View Resize"), &debugRescale, 0.f, 1.f);
		BUFFER_VIEWER_NODE_TITLE(texBaseLuminance, "Edge-aware Base Log Luminance", debugRescale);
		BUFFER_VIEWER_NODE_TITLE(texLogLuminance, "Scene Log Luminance", debugRescale);
	}
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
	// Calculate mip count for the log-luminance pyramid.
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

	// Create the log-luminance pyramid.
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

		texLogLuminance = eastl::make_unique<Texture2D>(texDesc, "LocalExposure Log Luminance");

		D3D11_SHADER_RESOURCE_VIEW_DESC fullSrvDesc = {};
		fullSrvDesc.Format = texDesc.Format;
		fullSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		fullSrvDesc.Texture2D.MostDetailedMip = 0;
		fullSrvDesc.Texture2D.MipLevels = numMips;
		texLogLuminance->CreateSRV(fullSrvDesc);

		createMipViews(*texLogLuminance, DXGI_FORMAT_R16_FLOAT, numMips, logLuminanceMipSRVs, logLuminanceMipUAVs);
	}

	// Create the edge-aware luminance grid.
	{
		D3D11_TEXTURE3D_DESC texDesc = {};
		texDesc.Width = (fullW + s_GridTileSize - 1) / s_GridTileSize;
		texDesc.Height = (fullH + s_GridTileSize - 1) / s_GridTileSize;
		texDesc.Depth = s_GridDepth;
		texDesc.MipLevels = 1;
		texDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		texLuminanceGrid = eastl::make_unique<Texture3D>(texDesc, "LocalExposure Luminance Grid");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Texture3D.MostDetailedMip = 0;
		srvDesc.Texture3D.MipLevels = 1;
		texLuminanceGrid->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		uavDesc.Texture3D.MipSlice = 0;
		uavDesc.Texture3D.FirstWSlice = 0;
		uavDesc.Texture3D.WSize = texDesc.Depth;
		texLuminanceGrid->CreateUAV(uavDesc);
	}

	// Create low-resolution textures for the broad luminance blur.
	{
		const uint blurMip = std::min(s_BlurMip, numMips - 1);
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = std::max(1u, fullW >> blurMip);
		texDesc.Height = std::max(1u, fullH >> blurMip);
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		auto createBlurTexture = [&](eastl::unique_ptr<Texture2D>& texture, const char* debugName) {
			texture = eastl::make_unique<Texture2D>(texDesc, debugName);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			texture->CreateSRV(srvDesc);

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			texture->CreateUAV(uavDesc);
		};

		createBlurTexture(texBlurTemp, "LocalExposure Blur Temp");
		createBlurTexture(texBlurredLuminance, "LocalExposure Blurred Luminance");
	}

	// Create output base-luminance texture (full resolution, R16F)
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

		texBaseLuminance = eastl::make_unique<Texture2D>(texDesc, "LocalExposure Base Luminance");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		texBaseLuminance->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		texBaseLuminance->CreateUAV(uavDesc);
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

		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
		DX::ThrowIfFailed(device->CreateSamplerState(&sampDesc, mirrorSampler.put()));
	}

	// Create constant buffer
	localExposureCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<LocalExposureCB>());

	CompileComputeShaders();
}

void LocalExposure::ClearShaderCache()
{
	BumpShaderGeneration();
	const auto shaderPtrs = std::array{
		&setupCS, &downsampleCS, &blurHorizontalCS, &blurVerticalCS, &gridCS, &resolveCS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/LocalExposure");
	CompileComputeShaders();
}

void LocalExposure::CompileComputeShaders()
{
	const std::vector<ComputeShaderCompileInfo> shaderInfos = {
		{ &setupCS, "localexposure.cs.hlsl", {}, "CSSetupLogLuminance" },
		{ &downsampleCS, "localexposure.cs.hlsl", {}, "CSDownsampleLogLuminance" },
		{ &blurHorizontalCS, "localexposure.cs.hlsl", {}, "CSBlurHorizontal" },
		{ &blurVerticalCS, "localexposure.cs.hlsl", {}, "CSBlurVertical" },
		{ &gridCS, "localexposure.cs.hlsl", {}, "CSBuildLuminanceGrid" },
		{ &resolveCS, "localexposure.cs.hlsl", {}, "CSResolveBaseLuminance" },
	};

	CompileComputeShadersAsync(L"Data\\Shaders\\PostProcessing\\LocalExposure", shaderInfos);
}

void LocalExposure::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	auto state = globals::state;

	if (!AllShadersReady({ &setupCS, &downsampleCS, &blurHorizontalCS, &blurVerticalCS, &gridCS, &resolveCS }))
		return;

	state->BeginPerfEvent("Local Exposure");

	// Get dimensions
	D3D11_TEXTURE2D_DESC mainDesc;
	inout_tex.tex->GetDesc(&mainDesc);
	uint fullW = mainDesc.Width;
	uint fullH = mainDesc.Height;
	const uint blurMip = std::min(s_BlurMip, numMips - 1);
	const uint blurWidth = texBlurredLuminance->desc.Width;
	const uint blurHeight = texBlurredLuminance->desc.Height;
	const float blurRadius = std::min(
		blurWidth * std::clamp(settings.BlurredLuminanceKernelSize, 0.f, 100.f) * 0.005f,
		(float)s_MaxBlurRadius);

	// Update constant buffer
	LocalExposureCB cbData = {
		.ManualExposure = settings.Exposure,
		.Strength = std::clamp(settings.Strength, 0.f, 1.f),
		.HighlightContrast = std::clamp(settings.HighlightContrast, 0.f, 1.f),
		.ShadowContrast = std::clamp(settings.ShadowContrast, 0.f, 1.f),
		.DetailStrength = std::clamp(settings.DetailStrength, 0.f, 2.f),
		.BaseBlend = std::clamp(settings.BaseBlend, 0.f, 1.f),
		.BlurRadius = blurRadius,
		.MiddleGreyBias = settings.MiddleGreyBias,
		.HighlightThreshold = std::max(settings.HighlightThreshold, 0.f),
		.ShadowThreshold = std::max(settings.ShadowThreshold, 0.f),
		.HighlightThresholdStrength = std::clamp(settings.HighlightThresholdStrength, 0.f, 1.f),
		.ShadowThresholdStrength = std::clamp(settings.ShadowThresholdStrength, 0.f, 1.f),
		.InputWidth = fullW,
		.InputHeight = fullH,
		.BlurredWidth = blurWidth,
		.BlurredHeight = blurHeight,
		.LogLuminanceMin = -13.f,
		.LogLuminanceMax = 18.f,
		.Padding1 = {},
	};
	localExposureCB->Update(cbData);

	ID3D11Buffer* cb = localExposureCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	std::array<ID3D11SamplerState*, 2> samplers = { linearSampler.get(), mirrorSampler.get() };
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	std::array<ID3D11ShaderResourceView*, 4> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { nullptr };

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	auto mipDim = [](uint dim, uint mip) {
		return std::max(1u, (dim + ((1u << mip) - 1u)) >> mip);
	};

	// === Pass 1: Set up scene log luminance ===
	{
		globals::profiler->BeginPass("PostProcessing::LocalExposure::Setup");
		state->BeginPerfEvent("Setup Log Luminance");

		srvs[0] = inout_tex.srv;
		uavs[0] = logLuminanceMipUAVs[0].get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(setupCS.get(), nullptr, 0);
		context->Dispatch((fullW + 7) >> 3, (fullH + 7) >> 3, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// === Pass 2: Build the broad-base mip chain ===
	{
		globals::profiler->BeginPass("PostProcessing::LocalExposure::Pyramid");
		state->BeginPerfEvent("Build Luminance Pyramid");

		for (uint i = 1; i <= blurMip; i++) {
			srvs[1] = logLuminanceMipSRVs[i - 1].get();
			uavs[0] = logLuminanceMipUAVs[i].get();
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(downsampleCS.get(), nullptr, 0);

			uint mipW = mipDim(fullW, i);
			uint mipH = mipDim(fullH, i);
			context->Dispatch((mipW + 7) >> 3, (mipH + 7) >> 3, 1);
			resetViews();
		}

		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// === Pass 3: Blur broad luminance ===
	{
		globals::profiler->BeginPass("PostProcessing::LocalExposure::Blur");
		state->BeginPerfEvent("Blur Broad Luminance");

		srvs[1] = logLuminanceMipSRVs[blurMip].get();
		uavs[0] = texBlurTemp->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(blurHorizontalCS.get(), nullptr, 0);
		context->Dispatch((blurWidth + 7) >> 3, (blurHeight + 7) >> 3, 1);
		resetViews();

		srvs[1] = texBlurTemp->srv.get();
		uavs[0] = texBlurredLuminance->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(blurVerticalCS.get(), nullptr, 0);
		context->Dispatch((blurWidth + 7) >> 3, (blurHeight + 7) >> 3, 1);
		resetViews();

		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// === Pass 4: Build the edge-aware luminance grid ===
	{
		globals::profiler->BeginPass("PostProcessing::LocalExposure::Grid");
		state->BeginPerfEvent("Build Edge-aware Grid");

		srvs[1] = logLuminanceMipSRVs[0].get();
		uavs[1] = texLuminanceGrid->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(gridCS.get(), nullptr, 0);
		context->Dispatch(texLuminanceGrid->desc.Width, texLuminanceGrid->desc.Height, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// === Pass 5: Resolve the base layer ===
	{
		globals::profiler->BeginPass("PostProcessing::LocalExposure::Resolve");
		state->BeginPerfEvent("Resolve Base Luminance");

		srvs[1] = texLogLuminance->srv.get();
		srvs[2] = texLuminanceGrid->srv.get();
		srvs[3] = texBlurredLuminance->srv.get();
		uavs[0] = texBaseLuminance->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(resolveCS.get(), nullptr, 0);
		context->Dispatch((fullW + 7) >> 3, (fullH + 7) >> 3, 1);

		resetViews();
		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	// Cleanup
	context->CSSetShader(nullptr, nullptr, 0);
	cb = nullptr;
	context->CSSetConstantBuffers(1, 1, &cb);
	samplers.fill(nullptr);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// NOTE: We do not modify inout_tex. Composite consumes the base luminance map.
	state->EndPerfEvent();
}
