#include "ColorGrading.h"

#include "State.h"
#include "Util.h"

#include "ColourSpace.h"
#include "Features/PostProcessing.h"

#include "IconsFontAwesome5.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ColorGrading::ColorProfile,
    params)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ColorGrading::Settings,
    useToDInterior,
    skipLDR,
    profiles,
    currentTonemapper,
    tonemapParams,
    gameCinematicBlend,
    gameFadeBlend,
    gameTintBlend,
    useLog,
    logType,
    invertLog,
    enableTonemap,
    enableColorSpaceTransform,
    inputColorSpace,
    processColorSpace,
    outputColorSpace
)

template <int num = 3>
bool shiftSlider(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
	static_assert(num > 1 && num < 5);

	if (ImGui::GetIO().KeyShift) {
		auto changed = ImGui::SliderFloat(label, v, v_min, v_max, format, flags);
		if (changed)
			for (int i = 1; i < num; i++)
				v[i] = v[0];
		return changed;
	} else {
		if constexpr (num == 2)
			return ImGui::SliderFloat2(label, v, v_min, v_max, format, flags);
		else if constexpr (num == 3)
			return ImGui::SliderFloat3(label, v, v_min, v_max, format, flags);
		else if constexpr (num == 4)
			return ImGui::SliderFloat4(label, v, v_min, v_max, format, flags);
	}
}

template <int num = 1>
bool exposureSlider(float* val)
{
	float tempVal[num];
	for (int i = 0; i < num; i++)
		tempVal[i] = log2(val[i]);

	bool retval;
	if constexpr (num == 1)
		retval = ImGui::SliderFloat("Exposure", tempVal, -4.f, 4.f, "%+.2f EV");
	else if constexpr (num == 2)
		retval = shiftSlider<2>("Exposure", tempVal, -4.f, 4.f, "%+.2f EV");
	else if constexpr (num == 3)
		retval = shiftSlider<3>("Exposure", tempVal, -4.f, 4.f, "%+.2f EV");
	else if constexpr (num == 4)
		retval = shiftSlider<4>("Exposure", tempVal, -4.f, 4.f, "%+.2f EV");

	for (int i = 0; i < num; i++)
		val[i] = exp2(tempVal[i]);

	return retval;
}

// Profjack Design
struct TonemapperInfo
{
	std::string_view name;
	std::string_view func_name;
	std::string_view desc;

	using CTP = std::array<float4, 2>;
	std::function<void(CTP&)> draw_settings_func;
	CTP default_settings;

	CTP cached_settings;

    static auto& GetTonemappers()
	{
		using f4 = float4;
		constexpr auto shiftHint = []() { ImGui::TextWrapped("Press Shift to control all channels at the same time."); };

        static std::vector<TonemapperInfo> tonemappers = {
			{ "Reinhard"sv, "Reinhard"sv,
				"Mapping proposed in \"Photographic Tone Reproduction for Digital Images\" by Reinhard et al. 2002."sv,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 0.f, 0.f, 0.f, 0.f } } },

			{ "Reinhard Extended"sv, "ReinhardExt"sv,
				"Extended mapping proposed in \"Photographic Tone Reproduction for Digital Images\" by Reinhard et al. 2002. "
				"An additional user parameter specifies the smallest luminance that is mapped to 1, which allows high luminances to burn out."sv,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat("White Point", &params[0].y, 0.f, 10.f, "%.2f"); },
				{ f4{ 0.f, 2.f, 0.f, 0.f } } },

			{ "Hejl Burgess-Dawson Filmic"sv, "HejlBurgessDawsonFilmic"sv,
				"Variation of the Hejl and Burgess-Dawson filmic curve done by Graham Aldridge. "
				"See his blog post about \"Approximating Film with Tonemapping\"."sv,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 0.f, 0.f, 0.f, 0.f } } },

			{ "Aldridge Filmic"sv, "AldridgeFilmic"sv,
				"Variation of the Hejl and Burgess-Dawson filmic curve done by Graham Aldridge. "
				"See his blog post about \"Approximating Film with Tonemapping\"."sv,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat("Cutoff", &params[0].y, 0.f, .5f, "%.2f"); },
				{ f4{ 0.f, .19f, 0.f, 0.f } } },

			{ "Lottes Filmic/AMD Curve"sv, "LottesFilmic"sv,
				"Filmic curve by Timothy Lottes, described in his GDC talk \"Advanced Techniques and Optimization of HDR Color Pipelines\". "
				"Also known as the \"AMD curve\"."sv,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat("Contrast", &params[0].y, 1.f, 2.f, "%.2f");
					ImGui::SliderFloat("Shoulder", &params[0].z, 0.01f, 2.f, "%.2f");
					ImGui::SliderFloat("Maximum HDR Value", &params[0].w, 1.f, 10.f, "%.2f");
					ImGui::SliderFloat("Input Mid-Level", &params[1].x, 0.f, 1.f, "%.2f");
					ImGui::SliderFloat("Output Mid-Level", &params[1].y, 0.f, 1.f, "%.2f"); },
				{ f4{ 0.f, 1.6f, 0.977f, 8.f }, f4{ 0.18f, 0.267f, 0.f, 0.f } } },

			{ "Day Filmic/Insomniac Curve"sv, "DayFilmic"sv,
				"Filmic curve by Mike Day, described in his document \"An efficient and user-friendly tone mapping operator\". "
				"Also known as the \"Insomniac curve\"."sv,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat("Black Point", &params[0].y, 0.f, 5.f, "%.2f");
					ImGui::SliderFloat("White Point", &params[0].z, 0.f, 5.f, "%.2f");

					ImGui::SliderFloat("Cross-over Point", &params[0].w, 0.f, 5.f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("Point where the toe and shoulder are pieced together into a single curve.");
					ImGui::SliderFloat("Shoulder Strength", &params[1].x, 0.f, 1.f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("Amount of blending between a straight-line curve and a purely asymptotic curve for the shoulder.");
					ImGui::SliderFloat("Toe Strength", &params[1].y, 0.f, 1.f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("Amount of blending between a straight-line curve and a purely asymptotic curve for the toe."); },
				{ f4{ 0.f, 0.f, 2.f, 0.3f }, f4{ 0.8f, 0.7f, 0.f, 0.f } } },

			{ "Uchimura/Grand Turismo Curve"sv, "UchimuraFilmic"sv,
				"Filmic curve by Hajime Uchimura, described in his CEDEC talk \"HDR Theory and Practice\". Characterised by its middle linear section. "
				"Also known as the \"Gran Turismo curve\"."sv,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat("Max Brightness", &params[0].y, 0.01f, 2.f, "%.2f");
					ImGui::SliderFloat("Contrast", &params[0].z, 0.f, 5.f, "%.2f");
					ImGui::SliderFloat("Linear Section Start", &params[0].w, 0.f, 1.f, "%.2f");
					ImGui::SliderFloat("Linear Section Length", &params[1].x, .01f, .99f, "%.2f");
					ImGui::SliderFloat("Black Tightness Shape", &params[1].y, 1.f, 3.f, "%.2f");
					ImGui::SliderFloat("Black Tightness Offset", &params[1].z, 0.f, 1.f, "%.2f"); },
				{ f4{ 0.f, 1.f, 1.f, .22f }, f4{ 0.4f, 1.33f, 0.f, 0.f } } },

			{ "ACES (Hill)"sv, "AcesHill"sv,
				"ACES curve fit by Stephen Hill."sv,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 0.f, 0.f, 0.f, 0.f } } },

			{ "ACES (Narkowicz)"sv, "AcesNarkowicz"sv,
				"ACES curve fit by Krzysztof Narkowicz. See his blog post \"ACES Filmic Tone Mapping Curve\"."sv,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 0.f, 0.f, 0.f, 0.f } } },

			{ "ACES (Guy)"sv, "AcesGuy"sv,
				"Curve from Unreal 3 adapted by to close to the ACES curve by Romain Guy."sv,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 0.f, 0.f, 0.f, 0.f } } },

			{ "AgX Minimal"sv, "AgxMinimal"sv,
				"Minimal version of Troy Sobotka's AgX using a 6th order polynomial approximation. "
				"Originally created by bwrensch, and improved by Troy Sobotka."sv,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat("Slope", &params[0].y, 0.f, 2.f, "%.2f");
					ImGui::SliderFloat("Power", &params[0].z, 0.f, 2.f, "%.2f");
					ImGui::SliderFloat("Offset", &params[0].w, -1.f, 1.f, "%.2f");
					ImGui::SliderFloat("Saturation", &params[1].x, 0.f, 2.f, "%.2f"); },
				{ f4{ 0.f, 1.f, 1.f, 0.f }, f4{ 1.f, 0.f, 0.f, 0.f } } },

			{ "Melon"sv, "MelonTonemap"sv,
				"Tonemapper designed by TripleMelon to fix the ACES issue of intense colour being shifted."sv,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 0.f, 0.f, 0.f, 0.f } } },

			{ "Kajiya"sv, "KajiyaTonemap"sv,
				"Tonemapper designed by Tomasz Stachowiak/Embark for their real time ray tracing engine Kajiya."sv,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 0.f, 0.f, 0.f, 0.f } } },

			{ "GT7"sv, "GT7ToneMapping"sv,
				"Tonemapper designed for Gran Turismo 7."sv,
				[](CTP& params) {
                    exposureSlider(&params[0].x);
                    ImGui::Checkbox("HDR Output (not working for now)", (bool*)&params[0].y);
                    if (params[0].y)
                        ImGui::InputFloat("HDR Max Brightness", &params[0].z, 0.f, 0.f, "%1.f nits");
                },
				{ f4{ 0.f, 0.f, 400.f, 0.f } } }
		};

		static std::once_flag flag;
		std::call_once(flag,
			[&]() {
				for (auto& t : tonemappers)
					t.cached_settings = t.default_settings;
			});

		return tonemappers;
	}

    static void GetDefaultParams(int& tonemapperType, CTP& params)
	{
		auto& tonemappers = GetTonemappers();
		if (auto it = std::ranges::find_if(tonemappers, [&](TonemapperInfo& x) { return "Reinhard"sv == x.name; });
			it != tonemappers.end()) {
			tonemapperType = (int)(it - tonemappers.begin());
			params = it->default_settings;
		} else
			logger::error("Somehow, the default settings are invalid. Please contact the author.");
	}
};

void ColorGrading::DrawSettings()
{
    static int page = 0;
    ImGui::Checkbox("Use ToD and Interior Settings", &settings.useToDInterior);
    ImGui::SameLine();
    ImGui::Checkbox("Skip LDR Color Grading", &settings.skipLDR);
    if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Skip color grading after tonemapping. This includes Lift Gamma Gain and Oklch adjustments.");

    ImGui::Checkbox("Convert Linear to Log Before HDR Color Grading", &settings.useLog);
    if (settings.useLog) {
        ImGui::Checkbox("Convert Log to Linear After HDR Color Grading", &settings.invertLog);
        ImGui::Combo("Log Type", (int*)&settings.logType, "ACEScct\0ARRILogC4\0SonySLog3\0");
    }

    if (settings.useToDInterior) {
        ImGui::Combo("Profile Page", &page, "Dawn\0Sunrise\0Day\0Sunset\0Dusk\0Night\0Interior\0");
    }
    int realPage = settings.useToDInterior ? page + 1 : 0;
    auto& profile = settings.profiles[realPage];

    ImGui::SeparatorText("Color Grading");
    ImGui::PushID(realPage);
    {
		ImGui::Text("Profile: %s", profileNames[realPage].data());
        ImGui::SliderFloat("Input Gamma", &profile.params[6].z, 0.f, 3.f, "%.3f");
        ImGui::SliderFloat("Output Gamma", &profile.params[6].w, 0.f, 3.f, "%.3f");

        if (ImGui::TreeNode("ASC CDL")) {
            shiftSlider("Slope", &profile.params[0].x, 0.f, 2.f, "%.2f");
            shiftSlider("Power", &profile.params[1].x, 0.f, 2.f, "%.2f");
            shiftSlider("Offset", &profile.params[2].x, -1.f, 1.f, "%.2f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Saturation/Hue")) {
            ImGui::SliderFloat("Saturation", &profile.params[6].x, 0.f, 3.f, "%.3f");
            ImGui::SliderFloat("Hue Shift", &profile.params[6].y, -1.f, 1.f, "%.3f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Lift Gamma Gain")) {
            ImGui::DragFloat4("Lift", &profile.params[3].x, 1e-3f, -1.f, 1.f, "%.3f");
            ImGui::DragFloat4("Gamma", &profile.params[4].x, 1e-3f, -1.5f, 1.5f, "%.3f");
            ImGui::DragFloat4("Gain", &profile.params[5].x, 1e-3f, 0.f, 2.f, "%.3f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("OKLCH Saturation")) {
            ImGui::SliderFloat("Saturation", &profile.params[7].x, 0.f, 2.f, "%.3f");
            ImGui::SliderFloat("Vibrance", &profile.params[7].y, 0.f, 3.f, "%.3f");
            ImGui::SliderFloat("Hue Shift", &profile.params[7].z, -1.f, 1.f, "%.3f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("OKLCH Color Mixer")) {
            ImGui::Text("Adjust brightness, vibrance and hue shift of specific hues in the perceptually uniform OKLCH space.");
            constexpr std::array<ImColor, 7> hues = { {
                { 255, 0, 0 },
                { 182, 124, 1 },
                { 87, 159, 0 },
                { 0, 161, 145 },
                { 0, 149, 217 },
                { 133, 100, 255 },
                { 255, 35, 189 },
            } };
            static int hueId = 0;
            if (ImGui::BeginTable("##HueTable", 7)) {
                for (int i = 0; i < 7; i++) {
                    ImGui::TableNextColumn();

                    ImGui::PushID(i);
                    ImGui::PushStyleColor(ImGuiCol_Text, hues[i].Value);
                    ImGui::RadioButton(ICON_FA_SQUARE, &hueId, i);
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::SliderFloat("Hue Shift", &profile.params[8 + hueId].x, -1.f, 1.f, "%.3f");
            ImGui::SliderFloat("Vibrance", &profile.params[8 + hueId].y, 0.f, 3.f, "%.3f");
            ImGui::SliderFloat("Brightness", &profile.params[8 + hueId].z, -1.f, 1.f, "%.3f");
            ImGui::TreePop();
        }
    }

    ImGui::SeparatorText("Tonemapping");
    ImGui::Checkbox("Enable Tonemapping", &settings.enableTonemap);
    if (settings.enableTonemap) {
        auto& tonemappers = TonemapperInfo::GetTonemappers();

        if (ImGui::BeginCombo("Tonemapper", tonemappers[tonemapperType].name.data(), ImGuiComboFlags_HeightLargest)) {
            for (int i = 0; i < tonemappers.size(); ++i) {
                if (ImGui::Selectable(tonemappers[i].name.data(), i == tonemapperType)) {
                    tonemappers[tonemapperType].cached_settings = settings.tonemapParams;
                    settings.tonemapParams = tonemappers[i].cached_settings;
                    tonemapperType = i;
                    recompileFlag = true;
                }

                if (auto _tt = Util::HoverTooltipWrapper())
                    ImGui::Text(tonemappers[i].desc.data());
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();
        ImGui::TextWrapped(tonemappers[tonemapperType].desc.data());
        ImGui::Spacing();
        if (ImGui::Button("Reset", { -1, 0 }))
            settings.tonemapParams = tonemappers[tonemapperType].default_settings;
        ImGui::Spacing();

        ImGui::PushID(tonemapperType);
        tonemappers[tonemapperType].draw_settings_func(settings.tonemapParams);
        ImGui::PopID();
    }

    ImGui::SeparatorText("Game Color Grading");
    ImGui::SliderFloat3("Cinematic Blend", &settings.gameCinematicBlend.x, 0.f, 1.f, "%.3f");
    if (auto _tt = Util::HoverTooltipWrapper())
        ImGui::Text("Saturation, Brightness and Contrast.");
    ImGui::SliderFloat("Fade Blend", &settings.gameFadeBlend, 0.f, 1.f, "%.3f");
    ImGui::SliderFloat("Tint Blend", &settings.gameTintBlend, 0.f, 1.f, "%.3f");
    ImGui::SeparatorText("Color Space Transform");
    ImGui::Checkbox("Enable Color Space Transform", &settings.enableColorSpaceTransform);
    if (settings.enableColorSpaceTransform) {
        auto& spaces = getAvailableColourSpaces();
        ImGui::Combo("Input Color Space", &settings.inputColorSpace, spaces.data(), (int)spaces.size());
        ImGui::Combo("Process Color Space", &settings.processColorSpace, spaces.data(), (int)spaces.size());
        ImGui::Combo("Output Color Space", &settings.outputColorSpace, spaces.data(), (int)spaces.size());

        auto colorSpaceTransformMatrix = getRGBMatrix(spaces[settings.inputColorSpace], spaces[settings.processColorSpace]);
        auto invColorSpaceTransformMatrix = getRGBMatrix(spaces[settings.processColorSpace], spaces[settings.outputColorSpace]);

        settings.colorSpaceTransform = {
            float3{ colorSpaceTransformMatrix(0, 0), colorSpaceTransformMatrix(0, 1), colorSpaceTransformMatrix(0, 2) },
            float3{ colorSpaceTransformMatrix(1, 0), colorSpaceTransformMatrix(1, 1), colorSpaceTransformMatrix(1, 2) },
            float3{ colorSpaceTransformMatrix(2, 0), colorSpaceTransformMatrix(2, 1), colorSpaceTransformMatrix(2, 2) }
        };
    }
}

void ColorGrading::RestoreDefaultSettings()
{
    settings = {};
	TonemapperInfo::GetDefaultParams(tonemapperType, settings.tonemapParams);
	recompileFlag = true;
}

void ColorGrading::LoadSettings(json& o_json)
{
    settings = o_json;
    
    auto& tonemappers = TonemapperInfo::GetTonemappers();
    if (auto it = std::ranges::find_if(tonemappers, [&](TonemapperInfo& x) { return settings.currentTonemapper == x.name; });
		it != tonemappers.end()) {
		tonemapperType = (int)(it - tonemappers.begin());
		settings.tonemapParams = it->default_settings;
	} else {
		TonemapperInfo::GetDefaultParams(tonemapperType, settings.tonemapParams);
	}

    recompileFlag = true;
}

void ColorGrading::SaveSettings(json& o_json)
{
    auto& tonemappers = TonemapperInfo::GetTonemappers();
    settings.currentTonemapper = tonemappers[tonemapperType].name.data();
    o_json = settings;
}

void ColorGrading::SetupResources()
{
	auto renderer = globals::game::renderer;

	logger::debug("Creating buffers...");
	{
		colorCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<ColorCB>());
	}

	logger::debug("Creating 2D textures...");
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

		texColor = std::make_unique<Texture2D>(texDesc);
		texColor->CreateSRV(srvDesc);
		texColor->CreateUAV(uavDesc);
	}

	CompileComputeShaders();
}

void ColorGrading::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&colorgradingCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void ColorGrading::CompileComputeShaders()
{
	const auto& tonemappers = TonemapperInfo::GetTonemappers();

	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		std::string entry = "main";
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &colorgradingCS, "colorgrading.cs.hlsl", { { "TONEMAP_FUNC", tonemappers[tonemapperType].func_name.data() } } },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\ColorGrading") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}

	recompileFlag = false;
}

void ColorGrading::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;

	if (recompileFlag)
		ClearShaderCache();

    auto& pp = globals::features::postProcessing;

    RE::ImageSpaceData imageSpaceData = pp.imageSpaceManager->gameISData;
	bool isInInterior = pp.imageSpaceManager->inInterior;

    auto profile = settings.profiles[0];
    if (settings.useToDInterior) {
        if (isInInterior) {
            profile = settings.profiles[7];
        } else {
            for (int i = 0; i < 6; i++) {
                for (int j = 0; j < 15; j++) {
                    profile.params[j] = (i == 0 ? float4{ 0.f, 0.f, 0.f, 0.f } : profile.params[j]) + settings.profiles[i + 1].params[j] * pp.imageSpaceManager->timeOfDay[i];
                }
            }
        }
    }

    ColorCB colorCBData = {
        .asccdl = {
            profile.params[0],
            profile.params[1],
            profile.params[2]
        },
        .liftgammagain = {
            profile.params[3],
            profile.params[4],
            profile.params[5]
        },
        .saturationHueInOutGamma = profile.params[6],
        .oklchSaturation = profile.params[7],
        .oklchColorMixer = {
            profile.params[8],
            profile.params[9],
            profile.params[10],
            profile.params[11],
            profile.params[12],
            profile.params[13],
            profile.params[14]
        },
        .tonemapParams = {
            settings.tonemapParams[0],
            settings.tonemapParams[1]
        },
        .colorSpaceTransform = {
            float4{ settings.colorSpaceTransform[0].x, settings.colorSpaceTransform[0].y, settings.colorSpaceTransform[0].z, 0.f },
            float4{ settings.colorSpaceTransform[1].x, settings.colorSpaceTransform[1].y, settings.colorSpaceTransform[1].z, 0.f },
            float4{ settings.colorSpaceTransform[2].x, settings.colorSpaceTransform[2].y, settings.colorSpaceTransform[2].z, 0.f }
        },
        .invColorSpaceTransform = {
            float4{ settings.invColorSpaceTransform[0].x, settings.invColorSpaceTransform[0].y, settings.invColorSpaceTransform[0].z, 0.f },
            float4{ settings.invColorSpaceTransform[1].x, settings.invColorSpaceTransform[1].y, settings.invColorSpaceTransform[1].z, 0.f },
            float4{ settings.invColorSpaceTransform[2].x, settings.invColorSpaceTransform[2].y, settings.invColorSpaceTransform[2].z, 0.f }
        },
        .cinematic = float4{
            std::lerp(1.f, imageSpaceData.baseData.cinematic.saturation, settings.gameCinematicBlend.x),
            std::lerp(1.f, imageSpaceData.baseData.cinematic.brightness, settings.gameCinematicBlend.y),
            std::lerp(1.f, imageSpaceData.baseData.cinematic.contrast, settings.gameCinematicBlend.z),
            imageSpaceData.baseAmount
        },
		.fade = float4{
			imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeR],
			imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeG],
			imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeB],
			imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeAmount]
		},
		.tint = float4{
			imageSpaceData.baseData.tint.color.red,
			imageSpaceData.baseData.tint.color.green,
			imageSpaceData.baseData.tint.color.blue,
			imageSpaceData.baseData.tint.amount
		},
		.logType = settings.useLog ? ((1u << settings.logType) | (settings.invertLog ? (1u << 3u) : 0u)) : 0u,
		.skipLDR = settings.skipLDR,
		.enableTonemap = settings.enableTonemap,
		.enableColorSpaceTransform = settings.enableColorSpaceTransform
	};
	colorCB->Update(colorCBData);

	ID3D11ShaderResourceView* srv = inout_tex.srv;
	ID3D11UnorderedAccessView* uav = texColor->uav.get();
	ID3D11Buffer* cb = colorCB->CB();

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetShader(colorgradingCS.get(), nullptr, 0);

	context->Dispatch((texColor->desc.Width + 7) >> 3, (texColor->desc.Height + 7) >> 3, 1);

	// clean up
	srv = nullptr;
	uav = nullptr;
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texColor->resource.get(), texColor->srv.get() };
}