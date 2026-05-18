#include "HistogramAutoExposure.h"

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
	ImGui::SliderFloat("Exposure Compensation", &settings.ExposureCompensation, -5.f, 5.f, "%+.2f EV");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Applying additional exposure adjustment to the image.");

	ImGui::SliderFloat("Adaptation Speed", &settings.AdaptSpeed, 0.1f, 5.f, "%.2f");
	ImGui::SliderFloat2("Focus Area", &settings.AdaptArea.x, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Specifies the proportion of the area [width, height] that auto exposure will adapt to.");

	ImGui::SliderFloat2("Adaptation Range", &settings.AdaptationRange.x, -10.f, 21.f, "%.2f EV");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"[Min, Max] The average scene luminance will be clamped between them when doing auto exposure."
			"Turning up the minimum, for example, makes it adapt less to darkness and therefore prevents over-brightening of dark scenes.");

	if (ImGui::TreeNodeEx("Purkinje Effect", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"The Purkinje effect simulates the blue shift of human vision under low light.\n"
			"If you don't like the effect, you can set the strength to zero.");

		ImGui::SliderFloat("Max Strength", &settings.PurkinjeStrength, 0.f, 5.f, "%.2f");
		ImGui::SliderFloat("Fade In EV", &settings.PurkinjeStartEV, -10.f, 0.f, "%.2f EV");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("The Purkinje effect will start to take place when the average scene luminance falls lower than this.");
		ImGui::SliderFloat("Max Effect EV", &settings.PurkinjeMaxEV, -10.f, 0.f, "%.2f EV");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("From this point onward, the Purkinje effect remains the greatest.");

		ImGui::TreePop();
	}

	// Debug: Histogram visualization
	if (ImGui::CollapsingHeader("Debug")) {
		debugReadbackRequested = true;

		// Show current adaptation value as EV
		float currentEV = (adaptationValue > 0.f) ? log2(adaptationValue) : -20.f;
		ImGui::Text("Current Adaptation: %.3f (%.2f EV)", adaptationValue, currentEV);

		// Histogram plot
		// The histogram covers a log-luminance range of approximately [-8, 13] EV (256 bins)
		constexpr float kMinLogLum = -8.f;
		constexpr float kMaxLogLum = 13.f;

		// Convert uint histogram to float for ImGui
		float histogramFloat[256];
		float maxBin = 1.f;
		for (int i = 0; i < 256; i++) {
			histogramFloat[i] = (float)histogramData[i];
			if (histogramFloat[i] > maxBin)
				maxBin = histogramFloat[i];
		}

		ImGui::Text("Luminance Histogram (%.0f - %.0f EV)", kMinLogLum, kMaxLogLum);
		ImGui::PlotHistogram("##histogram", histogramFloat, 256, 0, nullptr, 0.f, maxBin, ImVec2(ImGui::GetContentRegionAvail().x, 120));

		// Draw adaptation range markers below the histogram
		{
			float rangeMinNorm = (settings.AdaptationRange.x - kMinLogLum) / (kMaxLogLum - kMinLogLum);
			float rangeMaxNorm = (settings.AdaptationRange.y - kMinLogLum) / (kMaxLogLum - kMinLogLum);
			float currentNorm = (currentEV - kMinLogLum) / (kMaxLogLum - kMinLogLum);

			float plotWidth = ImGui::GetContentRegionAvail().x;
			ImVec2 plotPos = ImGui::GetCursorScreenPos();

			auto* drawList = ImGui::GetWindowDrawList();

			// Adaptation range lines (yellow)
			float xMin = plotPos.x + rangeMinNorm * plotWidth;
			float xMax = plotPos.x + rangeMaxNorm * plotWidth;
			drawList->AddLine(ImVec2(xMin, plotPos.y), ImVec2(xMin, plotPos.y + 16), IM_COL32(255, 200, 0, 255), 2.f);
			drawList->AddLine(ImVec2(xMax, plotPos.y), ImVec2(xMax, plotPos.y + 16), IM_COL32(255, 200, 0, 255), 2.f);

			// Current adaptation line (green)
			float xCur = plotPos.x + currentNorm * plotWidth;
			drawList->AddLine(ImVec2(xCur, plotPos.y), ImVec2(xCur, plotPos.y + 16), IM_COL32(0, 255, 0, 255), 2.f);

			ImGui::Dummy(ImVec2(plotWidth, 20));
			ImGui::TextColored(ImVec4(0, 1, 0, 1), "Green: Current EV");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Yellow: Adaptation Range");
		}
	} else {
		debugReadbackRequested = false;
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

	// Create staging buffers for debug readback
	{
		auto device = globals::d3d::device;

		D3D11_BUFFER_DESC stagingDesc{};
		stagingDesc.ByteWidth = sizeof(uint32_t) * 256;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.StructureByteStride = sizeof(uint32_t);
		stagingDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		device->CreateBuffer(&stagingDesc, nullptr, histogramStagingBuffer.put());
	}

	CompileComputeShaders();
}

void HistogramAutoExposure::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&histogramCS, &histogramAvgCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void HistogramAutoExposure::CompileComputeShaders()
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
			{ &histogramCS, "histogram.cs.hlsl", {}, "CS_Histogram" },
			{ &histogramAvgCS, "histogram.cs.hlsl", {}, "CS_Average" },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\HistogramAutoExposure") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
}

void HistogramAutoExposure::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	auto state = globals::state;

	float exposureCompensation = settings.ExposureCompensation;
	float2 adaptationRange = settings.AdaptationRange;

	AutoExposureCB cbData = {
		.AdaptArea = settings.AdaptArea,
		.AdaptationRange = { exp2(adaptationRange.x), exp2(adaptationRange.y) },
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
	state->BeginPerfEvent("Histogram Auto Exposure");

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

	// Debug readback: copy histogram and adaptation data to CPU when debug panel is open
	if (debugReadbackRequested && histogramStagingBuffer) {
		// Get underlying resource from the UAV
		ID3D11Resource* histResource = nullptr;
		histogramSB->UAV()->GetResource(&histResource);
		context->CopyResource(histogramStagingBuffer.get(), histResource);
		histResource->Release();

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(context->Map(histogramStagingBuffer.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
			memcpy(histogramData.data(), mapped.pData, sizeof(uint32_t) * 256);
			context->Unmap(histogramStagingBuffer.get(), 0);
		}

		// Read adaptation value via a small staging buffer
		D3D11_BUFFER_DESC adaptDesc{};
		adaptDesc.ByteWidth = sizeof(float);
		adaptDesc.Usage = D3D11_USAGE_STAGING;
		adaptDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		adaptDesc.StructureByteStride = sizeof(float);
		adaptDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		winrt::com_ptr<ID3D11Buffer> adaptStaging;
		auto device = globals::d3d::device;
		if (SUCCEEDED(device->CreateBuffer(&adaptDesc, nullptr, adaptStaging.put()))) {
			ID3D11Resource* adaptResource = nullptr;
			adaptationSB->SRV()->GetResource(&adaptResource);
			context->CopyResource(adaptStaging.get(), adaptResource);
			adaptResource->Release();

			D3D11_MAPPED_SUBRESOURCE adaptMapped{};
			if (SUCCEEDED(context->Map(adaptStaging.get(), 0, D3D11_MAP_READ, 0, &adaptMapped))) {
				adaptationValue = *reinterpret_cast<float*>(adaptMapped.pData);
				context->Unmap(adaptStaging.get(), 0);
			}
		}
	}
}
