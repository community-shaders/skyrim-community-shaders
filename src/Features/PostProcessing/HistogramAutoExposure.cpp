#include "HistogramAutoExposure.h"

#include "I18n/I18n.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HistogramAutoExposure::Settings,
	ExposureCompensation,
	AdaptationRange,
	AdaptArea,
	AdaptSpeed,
	PurkinjeStartEV,
	PurkinjeMaxEV,
	PurkinjeStrength)

void HistogramAutoExposure::DrawSettings()
{
	ImGui::SliderFloat(T("feature.post_processing.histogram_auto_exposure.exposure_compensation", "Exposure Compensation"), &settings.ExposureCompensation, -5.f, 5.f, "%+.2f EV");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.histogram_auto_exposure.applying_additional_exposure_adjustment_to_the_image", "Applying additional exposure adjustment to the image."));

	ImGui::SliderFloat(T("feature.post_processing.histogram_auto_exposure.adaptation_speed", "Adaptation Speed"), &settings.AdaptSpeed, 0.1f, 5.f, "%.2f");
	ImGui::SliderFloat2(T("feature.post_processing.histogram_auto_exposure.focus_area", "Focus Area"), &settings.AdaptArea.x, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T("feature.post_processing.histogram_auto_exposure.specifies_the_proportion_of_the_area_width_height", "Specifies the proportion of the area [width, height] that auto exposure will adapt to."));

	ImGui::SliderFloat2(T("feature.post_processing.histogram_auto_exposure.adaptation_range", "Adaptation Range"), &settings.AdaptationRange.x, -10.f, 21.f, "%.2f EV100");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			T("feature.post_processing.histogram_auto_exposure.min_max_the_average_scene_luminance_will_be",
				"[Min, Max] The average scene luminance will be clamped between them when doing auto exposure."
				"Turning up the minimum, for example, makes it adapt less to darkness and therefore prevents over-brightening of dark scenes."));

	if (ImGui::TreeNodeEx(T("feature.post_processing.histogram_auto_exposure.purkinje_effect", "Purkinje Effect"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			T("feature.post_processing.histogram_auto_exposure.the_purkinje_effect_simulates_the_blue_shift_of",
				"The Purkinje effect simulates the blue shift of human vision under low light.\n"
				"If you don't like the effect, you can set the strength to zero."));

		ImGui::SliderFloat(T("feature.post_processing.histogram_auto_exposure.max_strength", "Max Strength"), &settings.PurkinjeStrength, 0.f, 5.f, "%.2f");
		ImGui::SliderFloat(T("feature.post_processing.histogram_auto_exposure.fade_in_ev", "Fade In EV100"), &settings.PurkinjeStartEV, -10.f, 3.f, "%.2f EV100");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.histogram_auto_exposure.the_purkinje_effect_will_start_to_take_place", "The Purkinje effect will start to take place when the average scene luminance falls lower than this."));
		ImGui::SliderFloat(T("feature.post_processing.histogram_auto_exposure.max_effect_ev", "Max Effect EV100"), &settings.PurkinjeMaxEV, -7.f, 3.f, "%.2f EV100");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T("feature.post_processing.histogram_auto_exposure.from_this_point_onward_the_purkinje_effect_remains", "From this point onward, the Purkinje effect remains the greatest."));

		ImGui::TreePop();
	}

	if (ImGui::CollapsingHeader(T("feature.post_processing.histogram_auto_exposure.histogram", "Histogram"), ImGuiTreeNodeFlags_DefaultOpen)) {
		histogramReadbackRequested = true;
		histogramReadbackRequestFrame = ImGui::GetFrameCount();

		constexpr float kMinEV100 = -10.f;
		constexpr float kMaxEV100 = 21.f;
		constexpr int kHistogramBins = 256;
		constexpr int kFirstLuminanceBin = 1;
		constexpr int kLastLuminanceBin = kHistogramBins - 1;
		constexpr float kMiddleGray = 0.18f;

		const float adaptedLum = std::max(adaptationValue, 1e-5f);
		const float adaptedEV100 = log2(adaptedLum) + 3.0f;
		const float compensationEV = settings.ExposureCompensation;
		const float compensationScale = exp2(compensationEV);
		const float clampedAdaptedLum = std::clamp(adaptedLum, exp2(settings.AdaptationRange.x - 3.0f), exp2(settings.AdaptationRange.y - 3.0f));
		const float compensatedTargetLum = clampedAdaptedLum / std::max(compensationScale, 1e-5f);
		const float compensatedTargetEV100 = log2(compensatedTargetLum) + 3.0f;
		const float finalExposure = kMiddleGray * compensationScale / clampedAdaptedLum;
		const float finalExposureEV = log2(std::max(finalExposure, 1e-5f));

		ImGui::Text(T("feature.post_processing.histogram_auto_exposure.adapted_luminance_ev", "Adapted Luminance: %.6g (%.2f EV100)"), adaptedLum, adaptedEV100);
		ImGui::Text(T("feature.post_processing.histogram_auto_exposure.compensated_target_ev", "Compensated Target: %.6g (%.2f EV100)"), compensatedTargetLum, compensatedTargetEV100);
		ImGui::Text(T("feature.post_processing.histogram_auto_exposure.final_global_exposure_ev", "Final Global Exposure: %.6g (%+.2f EV)"), finalExposure, finalExposureEV);

		float maxBin = 1.f;
		for (int i = 0; i < kHistogramBins; i++) {
			maxBin = std::max(maxBin, static_cast<float>(histogramData[i]));
		}

		ImGui::Text(T("feature.post_processing.histogram_auto_exposure.luminance_histogram_ev", "Luminance Histogram (%.0f - %.0f EV100)"), kMinEV100, kMaxEV100);
		const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		const ImVec2 canvasSize = ImVec2(ImGui::GetContentRegionAvail().x, 120.f);
		ImGui::InvisibleButton("##histogram_canvas", canvasSize);

		auto* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(18, 18, 18, 255));
		drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(80, 80, 80, 255));

		const float binWidth = canvasSize.x / static_cast<float>(kHistogramBins);
		for (int i = 0; i < kHistogramBins; i++) {
			const float binValue = static_cast<float>(histogramData[i]);
			const float barHeight = canvasSize.y * std::clamp(binValue / maxBin, 0.f, 1.f);
			const float x0 = canvasPos.x + static_cast<float>(i) * binWidth;
			const float x1 = canvasPos.x + static_cast<float>(i + 1) * binWidth;
			const float y0 = canvasPos.y + canvasSize.y - barHeight;
			const ImU32 color = i == 0 ? IM_COL32(90, 90, 90, 180) : IM_COL32(90, 150, 220, 220);
			drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, canvasPos.y + canvasSize.y), color);
		}

		auto evToX = [&](float ev) {
			const float norm = std::clamp((ev - kMinEV100) / (kMaxEV100 - kMinEV100), 0.f, 1.f);
			const float bin = static_cast<float>(kFirstLuminanceBin) + norm * static_cast<float>(kLastLuminanceBin - kFirstLuminanceBin);
			return canvasPos.x + (bin + 0.5f) * binWidth;
		};

		auto drawMarker = [&](float ev, ImU32 color) {
			const float x = evToX(ev);
			drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y), color, 2.f);
		};

		drawMarker(settings.AdaptationRange.x, IM_COL32(255, 200, 0, 255));
		drawMarker(settings.AdaptationRange.y, IM_COL32(255, 200, 0, 255));
		drawMarker(adaptedEV100, IM_COL32(0, 255, 0, 255));
		drawMarker(compensatedTargetEV100, IM_COL32(0, 220, 255, 255));

		if (ImGui::IsItemHovered()) {
			const float mouseX = ImGui::GetIO().MousePos.x;
			const int bin = std::clamp(static_cast<int>((mouseX - canvasPos.x) / binWidth), 0, kHistogramBins - 1);
			ImGui::BeginTooltip();
			if (bin == 0) {
				ImGui::Text(T("feature.post_processing.histogram_auto_exposure.bin_0_below_luminance_threshold", "Bin 0: below luminance threshold"));
			} else {
				const float histogramPos = static_cast<float>(bin - kFirstLuminanceBin) / static_cast<float>(kLastLuminanceBin - kFirstLuminanceBin);
				const float ev = histogramPos * (kMaxEV100 - kMinEV100) + kMinEV100;
				ImGui::Text(T("feature.post_processing.histogram_auto_exposure.bin", "Bin: %d"), bin);
				ImGui::Text(T("feature.post_processing.histogram_auto_exposure.luminance", "Luminance: %.6g"), exp2(ev - 3.0f));
				ImGui::Text(T("feature.post_processing.histogram_auto_exposure.ev", "EV100: %.2f"), ev);
			}
			ImGui::Text(T("feature.post_processing.histogram_auto_exposure.samples", "Samples: %u"), histogramData[bin]);
			ImGui::EndTooltip();
		}

		ImGui::TextColored(ImVec4(0, 1, 0, 1), T("feature.post_processing.histogram_auto_exposure.green_adapted_ev", "Green: Adapted EV100"));
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0, 0.86f, 1, 1), T("feature.post_processing.histogram_auto_exposure.cyan_compensation_target", "Cyan: Compensation Target"));
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), T("feature.post_processing.histogram_auto_exposure.yellow_adaptation_range", "Yellow: Adaptation Range"));
	} else {
		histogramReadbackRequested = false;
	}
}

void HistogramAutoExposure::RestoreDefaultSettings()
{
	settings = {};
}

void HistogramAutoExposure::LoadSettings(json& o_json)
{
	settings = o_json;
}

void HistogramAutoExposure::SaveSettings(json& o_json)
{
	o_json = settings;
}

void HistogramAutoExposure::SetupResources()
{
	logger::debug("Creating buffers...");
	{
		autoExposureCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<AutoExposureCB>());

		histogramSB = std::make_unique<StructuredBuffer>(StructuredBufferDesc<uint>(256u, false), 256);
		histogramSB->CreateUAV();

		adaptationSB = std::make_unique<StructuredBuffer>(StructuredBufferDesc<float>(1u, false), 1);
		adaptationSB->CreateSRV();
		adaptationSB->CreateUAV();
	}

	// Create staging buffers for histogram readback
	{
		auto device = globals::d3d::device;

		D3D11_BUFFER_DESC stagingDesc{};
		stagingDesc.ByteWidth = sizeof(uint32_t) * 256;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.StructureByteStride = sizeof(uint32_t);
		stagingDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		device->CreateBuffer(&stagingDesc, nullptr, histogramStagingBuffer.put());

		D3D11_BUFFER_DESC adaptDesc{};
		adaptDesc.ByteWidth = sizeof(float);
		adaptDesc.Usage = D3D11_USAGE_STAGING;
		adaptDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		adaptDesc.StructureByteStride = sizeof(float);
		adaptDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		device->CreateBuffer(&adaptDesc, nullptr, adaptationStagingBuffer.put());
	}

	CompileComputeShaders();
}

void HistogramAutoExposure::ClearShaderCache()
{
	BumpShaderGeneration();
	const auto shaderPtrs = std::array{
		&histogramCS, &histogramAvgCS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/HistogramAutoExposure");
	CompileComputeShaders();
}

void HistogramAutoExposure::CompileComputeShaders()
{
	const std::vector<ComputeShaderCompileInfo> shaderInfos = {
		{ &histogramCS, "histogram.cs.hlsl", {}, "CS_Histogram" },
		{ &histogramAvgCS, "histogram.cs.hlsl", {}, "CS_Average" },
	};

	CompileComputeShadersAsync(L"Data\\Shaders\\PostProcessing\\HistogramAutoExposure", shaderInfos);
}

void HistogramAutoExposure::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	auto state = globals::state;

	if (!AllShadersReady({ &histogramCS, &histogramAvgCS }))
		return;

	float exposureCompensation = settings.ExposureCompensation;
	float2 adaptationRange = settings.AdaptationRange;

	AutoExposureCB cbData = {
		.AdaptArea = settings.AdaptArea,
		.AdaptationRange = { exp2(adaptationRange.x - 3.0f), exp2(adaptationRange.y - 3.0f) },
		.AdaptLerp = std::clamp(1.f - exp(-RE::BSTimer::GetSingleton()->realTimeDelta * settings.AdaptSpeed), 0.f, 1.f),
		.ExposureCompensation = exp2(exposureCompensation),
		.PurkinjeStartEV = settings.PurkinjeStartEV,
		.PurkinjeMaxEV = settings.PurkinjeMaxEV,
		.PurkinjeStrength = settings.PurkinjeStrength,
	};
	autoExposureCB->Update(cbData);

	std::array<ID3D11ShaderResourceView*, 2> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { nullptr };
	ID3D11Buffer* cb = autoExposureCB->CB();

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	context->CSSetConstantBuffers(1, 1, &cb);
	globals::profiler->BeginPass("PostProcessing::HistogramAutoExposure");
	state->BeginPerfEvent("Histogram Auto Exposure");

	const bool histogramReadbackActive =
		Menu::GetSingleton()->IsEnabled &&
		histogramReadbackRequested &&
		ImGui::GetCurrentContext() &&
		histogramReadbackRequestFrame >= ImGui::GetFrameCount() - 1;
	if (!histogramReadbackActive)
		histogramReadbackRequested = false;

	{
		state->BeginPerfEvent("Calculate Histogram");
		srvs[0] = inout_tex.srv;
		uavs[0] = histogramSB->UAV();
		uavs[1] = adaptationSB->UAV();

		context->CSSetShaderResources(0, (UINT)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);

		// Calculate histogram
		context->CSSetShader(histogramCS.get(), nullptr, 0);
		uint texWidth = 0;
		uint texHeight = 0;
		{
			D3D11_TEXTURE2D_DESC desc;
			inout_tex.tex->GetDesc(&desc);
			texWidth = desc.Width;
			texHeight = desc.Height;
		}
		uint32_t dispatchX = ((texWidth - 1) >> 5) + 1;
		uint32_t dispatchY = ((texHeight - 1) >> 5) + 1;
		dispatchX = (dispatchX + 7) / 8;
		dispatchY = (dispatchY + 7) / 8;

		context->Dispatch(dispatchX, dispatchY, 1);

		if (histogramReadbackActive && histogramStagingBuffer) {
			uavs.fill(nullptr);
			context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);

			ID3D11Resource* histResource = nullptr;
			histogramSB->UAV()->GetResource(&histResource);
			context->CopyResource(histogramStagingBuffer.get(), histResource);
			histResource->Release();

			uavs[0] = histogramSB->UAV();
			uavs[1] = adaptationSB->UAV();
			context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);
		}

		// Calculate average
		context->CSSetShader(histogramAvgCS.get(), nullptr, 0);
		context->Dispatch(1, 1, 1);
		state->EndPerfEvent();
	}

	// Clean up
	resetViews();
	cb = nullptr;
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	// NOTE: We intentionally do NOT modify inout_tex here.
	// The adaptation result is stored in adaptationSB and will be consumed
	// by the Composite pass which applies exposure before color grading.
	state->EndPerfEvent();
	globals::profiler->EndPass();

	// Readback histogram and adaptation data when the histogram panel is open.
	// histogramStagingBuffer was copied before CS_Average cleared the GPU histogram.
	if (histogramReadbackActive && histogramStagingBuffer) {
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(context->Map(histogramStagingBuffer.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
			memcpy(histogramData.data(), mapped.pData, sizeof(uint32_t) * 256);
			context->Unmap(histogramStagingBuffer.get(), 0);
		}

		if (adaptationStagingBuffer) {
			ID3D11Resource* adaptResource = nullptr;
			adaptationSB->SRV()->GetResource(&adaptResource);
			context->CopyResource(adaptationStagingBuffer.get(), adaptResource);
			adaptResource->Release();

			D3D11_MAPPED_SUBRESOURCE adaptMapped{};
			if (SUCCEEDED(context->Map(adaptationStagingBuffer.get(), 0, D3D11_MAP_READ, 0, &adaptMapped))) {
				adaptationValue = *reinterpret_cast<float*>(adaptMapped.pData);
				context->Unmap(adaptationStagingBuffer.get(), 0);
			}
		}
	}
}
