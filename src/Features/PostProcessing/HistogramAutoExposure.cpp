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
	ImGui::SliderFloat2("Adaptation Range", &settings.AdaptationRange.x, -6.f, 21.f, "%.2f EV");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"[Min, Max] The average scene luminance will be clamped between them when doing auto exposure."
			"Turning up the minimum, for example, makes it adapt less to darkness and therefore prevents over-brightening of dark scenes.");

	if (ImGui::TreeNodeEx("Purkinje Effect", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"The Purkinje effect simulates the blue shift of human vision under low light.\n"
			"If you don't like the effect, you can set the strength to zero.");

		ImGui::SliderFloat("Max Strength", &settings.PurkinjeStrength, 0.f, 5.f, "%.2f");
		ImGui::SliderFloat("Fade In EV", &settings.PurkinjeStartEV, -6.f, 21.f, "%.2f EV");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("The Purkinje effect will start to take place when the average scene luminance falls lower than this.");
		ImGui::SliderFloat("Max Effect EV", &settings.PurkinjeMaxEV, -6.f, 21.f, "%.2f EV");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("From this point onward, the Purkinje effect remains the greatest.");

		ImGui::TreePop();
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
	auto renderer = globals::game::renderer;

	logger::debug("Creating buffers...");
	{
		autoExposureCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<AutoExposureCB>());

		histogramSB = std::make_unique<StructuredBuffer>(StructuredBufferDesc<uint>(256u, false), 256);
		histogramSB->CreateUAV();

		adaptationSB = std::make_unique<StructuredBuffer>(StructuredBufferDesc<float>(1u, false), 1);
		adaptationSB->CreateSRV();
		adaptationSB->CreateUAV();
	}

	logger::debug("Creating 2D textures...");
	{
		// texAdapt for adaptation
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

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

		texAdapt = std::make_unique<Texture2D>(texDesc);
		texAdapt->CreateSRV(srvDesc);
		texAdapt->CreateUAV(uavDesc);
	}

	CompileComputeShaders();
}

void HistogramAutoExposure::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&histogramCS, &histogramAvgCS, &adaptCS
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
			{ &adaptCS, "adapt.cs.hlsl", {} },
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

	AutoExposureCB cbData = {
		.AdaptArea = settings.AdaptArea,
		.AdaptationRange = { exp2(settings.AdaptationRange.x) * 0.125f, exp2(settings.AdaptationRange.y) * 0.125f },
		.AdaptLerp = std::clamp(1.f - exp(-RE::BSTimer::GetSingleton()->realTimeDelta * settings.AdaptSpeed), 0.f, 1.f),
		.ExposureCompensation = exp2(settings.ExposureCompensation),
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

		// Calculate histogram - optimized for 32x32 threads
		// Reduced number of dispatches due to increased thread count and sampling optimization
		context->CSSetShader(histogramCS.get(), nullptr, 0);
		uint32_t dispatchX = ((texAdapt->desc.Width - 1) >> 5) + 1;
		uint32_t dispatchY = ((texAdapt->desc.Height - 1) >> 5) + 1;

		// Further reduce dispatches based on our sampling pattern
		// Since we're sampling at 8x spacing, we can reduce dispatches by 8x
		dispatchX = (dispatchX + 7) / 8;
		dispatchY = (dispatchY + 7) / 8;

		context->Dispatch(dispatchX, dispatchY, 1);

		// Calculate average
		context->CSSetShader(histogramAvgCS.get(), nullptr, 0);
		context->Dispatch(1, 1, 1);
		state->EndPerfEvent();
	}

	// Adapt
	{
		resetViews();
		state->BeginPerfEvent("Adapt Exposure");

		srvs[0] = inout_tex.srv;
		srvs[1] = adaptationSB->SRV();
		uavs[0] = texAdapt->uav.get();

		context->CSSetShaderResources(0, (UINT)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(adaptCS.get(), nullptr, 0);

		// Maintain the same number of threads for the adapt shader
		// Since we're not changing the sampling pattern of the adapt shader
		context->Dispatch(((texAdapt->desc.Width - 1) >> 5) + 1, ((texAdapt->desc.Height - 1) >> 5) + 1, 1);
		state->EndPerfEvent();
	}

	// Clean up
	resetViews();
	cb = nullptr;
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texAdapt->resource.get(), texAdapt->srv.get() };
	state->EndPerfEvent();
}
