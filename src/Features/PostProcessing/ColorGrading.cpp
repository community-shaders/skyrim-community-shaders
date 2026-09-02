#include "ColorGrading.h"

#include "RasterPass.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

#include "ColorSpace.h"
#include "Features/HDRDisplay.h"
#include "Features/LinearLighting.h"
#include "Features/PostProcessing.h"
#include "Menu.h"
#include "OpenDRTIo.h"

#include <DDSTextureLoader.h>
#include <DirectXPackedVector.h>
#include <DirectXTex.h>

#include "IconsFontAwesome5.h"

#define I18N_KEY_PREFIX "feature.post_processing.color_grading."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ColorGrading::Settings,
	skipLDR,
	skipLUT,
	slope,
	power,
	cdlOffset,
	lift,
	gamma,
	gain,
	inOutGamma,
	oklchSaturation,
	oklchColorMixer,
	contrast,
	pivot,
	exposureTemperatureTint,
	shadowsGain,
	midtonesGain,
	highlightsGain,
	shadowsHighlightsRange,
	shadowsOffset,
	midtonesOffset,
	highlightsOffset,
	useOpenDrt,
	currentTonemapper,
	tonemapParams,
	gameCinematicBlend,
	gameFadeBlend,
	gameTintBlend,
	useLog,
	logType,
	invertLog,
	enableTonemap,
	processColorSpace)

template <int num = 1>
bool exposureSlider(float* val)
{
	float tempVal[num];
	for (int i = 0; i < num; i++)
		tempVal[i] = log2(val[i]);

	bool retval;
	if constexpr (num == 1)
		retval = ImGui::SliderFloat(T(TKEY("exposure"), "Exposure"), tempVal, -4.f, 4.f, "%+.2f EV");
	else if constexpr (num == 2)
		retval = Util::ShiftSlider<2>(T(TKEY("exposure"), "Exposure"), tempVal, -4.f, 4.f, "%+.2f EV");
	else if constexpr (num == 3)
		retval = Util::ShiftSlider<3>(T(TKEY("exposure"), "Exposure"), tempVal, -4.f, 4.f, "%+.2f EV");
	else if constexpr (num == 4)
		retval = Util::ShiftSlider<4>(T(TKEY("exposure"), "Exposure"), tempVal, -4.f, 4.f, "%+.2f EV");

	for (int i = 0; i < num; i++)
		val[i] = exp2(tempVal[i]);

	return retval;
}

void drawHDRStatus()
{
	auto& hdr = globals::features::hdrDisplay;
	if (hdr.loaded && hdr.settings.enableHDR) {
		auto hdrOutputActive = std::format("{} {}", ICON_FA_CHECK, T("feature.post_processing.color_grading.hdr_output_active", "HDR Output Active"));
		ImGui::TextColored(Util::Colors::GetSuccess(), "%s", hdrOutputActive.c_str());
		ImGui::Text(T("feature.post_processing.color_grading.paper_white_nits_from_hdr_settings", "Paper White: %.0f nits (from HDR settings)"), static_cast<float>(hdr.settings.hdrPaperWhite));
		ImGui::Text(T("feature.post_processing.color_grading.peak_brightness_nits_from_hdr_settings", "Peak Brightness: %.0f nits (from HDR settings)"), static_cast<float>(hdr.settings.hdrPeakNits));
	} else {
		ImGui::TextColored(Util::Colors::GetDisabled(), "%s", T("feature.post_processing.color_grading.sdr_output_hdr_display_not_enabled", "SDR Output (HDR Display not enabled)"));
	}
}

// Profjack Design
struct TonemapperInfo
{
	std::string_view name;
	std::string_view func_name;
	std::string_view desc;
	int nativeInputSpace;      // color space the tonemapper expects as input
	int nativeOutputSpace;     // color space the tonemapper produces as output
	bool supportsHDR;          // whether this tonemapper supports HDR output
	int nativeInputSpaceHDR;   // input color space index when HDR is active
	int nativeOutputSpaceHDR;  // output color space index when HDR is active

	using CTP = std::array<float4, 2>;
	std::function<void(CTP&)> draw_settings_func;
	CTP default_settings;

	CTP cached_settings;

	static auto& GetTonemappers()
	{
		using f4 = float4;
		static std::vector<TonemapperInfo> tonemappers = {
			{ "Reinhard"sv, "Reinhard"sv,
				T(TKEY("tonemapper.reinhard.description"), "Mapping proposed in \"Photographic Tone Reproduction for Digital Images\" by Reinhard et al. 2002."), 0, 0, false, 0, 0,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 1.f, 0.f, 0.f, 0.f } } },

			{ "Reinhard Extended"sv, "ReinhardExt"sv,
				T(TKEY("tonemapper.reinhard_extended.description"),
					"Extended mapping proposed in \"Photographic Tone Reproduction for Digital Images\" by Reinhard et al. 2002. "
					"An additional user parameter specifies the smallest luminance that is mapped to 1, which allows high luminances to burn out."),
				0, 0, false, 0, 0,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("white_point"), "White Point"), &params[0].y, 0.f, 10.f, "%.2f"); },
				{ f4{ 1.f, 2.f, 0.f, 0.f } } },

			{ "Hejl Burgess-Dawson Filmic"sv, "HejlBurgessDawsonFilmic"sv,
				T(TKEY("tonemapper.hejl_burgess_dawson_filmic.description"),
					"Variation of the Hejl and Burgess-Dawson filmic curve done by Graham Aldridge. "
					"See his blog post about \"Approximating Film with Tonemapping\"."),
				0, 0, false, 0, 0,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 1.f, 0.f, 0.f, 0.f } } },

			{ "Aldridge Filmic"sv, "AldridgeFilmic"sv,
				T(TKEY("tonemapper.aldridge_filmic.description"),
					"Variation of the Hejl and Burgess-Dawson filmic curve done by Graham Aldridge. "
					"See his blog post about \"Approximating Film with Tonemapping\"."),
				0, 0, false, 0, 0,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("cutoff"), "Cutoff"), &params[0].y, 0.f, .5f, "%.2f"); },
				{ f4{ 1.f, .19f, 0.f, 0.f } } },

			{ "Lottes Filmic/AMD Curve"sv, "LottesFilmic"sv,
				T(TKEY("tonemapper.lottes_filmic.description"),
					"Filmic curve by Timothy Lottes, described in his GDC talk \"Advanced Techniques and Optimization of HDR Color Pipelines\". "
					"Also known as the \"AMD curve\"."),
				0, 0, true, 0, 0,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("contrast"), "Contrast"), &params[0].y, 1.f, 2.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("shoulder"), "Shoulder"), &params[0].z, 0.01f, 2.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("maximum_hdr_value"), "Maximum HDR Value"), &params[0].w, 1.f, 10.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("input_mid_level"), "Input Mid-Level"), &params[1].x, 0.f, 1.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("output_mid_level"), "Output Mid-Level"), &params[1].y, 0.f, 1.f, "%.2f");
					drawHDRStatus(); },
				{ f4{ 1.f, 1.6f, 0.977f, 8.f }, f4{ 0.18f, 0.267f, 0.f, 0.f } } },

			{ "Day Filmic/Insomniac Curve"sv, "DayFilmic"sv,
				T(TKEY("tonemapper.day_filmic.description"),
					"Filmic curve by Mike Day, described in his document \"An efficient and user-friendly tone mapping operator\". "
					"Also known as the \"Insomniac curve\"."),
				0, 0, false, 0, 0,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("black_point"), "Black Point"), &params[0].y, 0.f, 5.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("white_point"), "White Point"), &params[0].z, 0.f, 5.f, "%.2f");

					ImGui::SliderFloat(T(TKEY("cross_over_point"), "Cross-over Point"), &params[0].w, 0.f, 5.f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text(T(TKEY("cross_over_point_tooltip"), "Point where the toe and shoulder are pieced together into a single curve."));
					ImGui::SliderFloat(T(TKEY("shoulder_strength"), "Shoulder Strength"), &params[1].x, 0.f, 1.f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text(T(TKEY("shoulder_strength_tooltip"), "Amount of blending between a straight-line curve and a purely asymptotic curve for the shoulder."));
					ImGui::SliderFloat(T(TKEY("toe_strength"), "Toe Strength"), &params[1].y, 0.f, 1.f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text(T(TKEY("toe_strength_tooltip"), "Amount of blending between a straight-line curve and a purely asymptotic curve for the toe.")); },
				{ f4{ 1.f, 0.f, 2.f, 0.3f }, f4{ 0.8f, 0.7f, 0.f, 0.f } } },

			{ "Uchimura/Grand Turismo Curve"sv, "UchimuraFilmic"sv,
				T(TKEY("tonemapper.uchimura_filmic.description"),
					"Filmic curve by Hajime Uchimura, described in his CEDEC talk \"HDR Theory and Practice\". Characterised by its middle linear section. "
					"Also known as the \"Gran Turismo curve\"."),
				0, 0, true, 0, 0,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("max_brightness"), "Max Brightness"), &params[0].y, 0.01f, 2.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("contrast"), "Contrast"), &params[0].z, 0.f, 5.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("linear_section_start"), "Linear Section Start"), &params[0].w, 0.f, 1.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("linear_section_length"), "Linear Section Length"), &params[1].x, .01f, .99f, "%.2f");
					ImGui::SliderFloat(T(TKEY("black_tightness_shape"), "Black Tightness Shape"), &params[1].y, 1.f, 3.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("black_tightness_offset"), "Black Tightness Offset"), &params[1].z, 0.f, 1.f, "%.2f");
					drawHDRStatus(); },
				{ f4{ 1.f, 1.f, 1.f, .22f }, f4{ 0.4f, 1.33f, 0.f, 0.f } } },

			{ "AgX Minimal"sv, "AgxMinimal"sv,
				T(TKEY("tonemapper.agx_minimal.description"),
					"Minimal version of Troy Sobotka's AgX using a 6th order polynomial approximation. "
					"Originally created by bwrensch, and improved by Troy Sobotka. Internally uses AgX input transform."),
				0, 0, false, 0, 0,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("slope"), "Slope"), &params[0].y, 0.f, 2.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("power"), "Power"), &params[0].z, 0.f, 2.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("offset"), "Offset"), &params[0].w, -1.f, 1.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("saturation"), "Saturation"), &params[1].x, 0.f, 2.f, "%.2f"); },
				{ f4{ 1.f, 1.f, 1.f, 0.f }, f4{ 1.f, 0.f, 0.f, 0.f } } },

			{ "Melon"sv, "MelonTonemap"sv,
				T(TKEY("tonemapper.melon.description"), "Tonemapper designed by TripleMelon to fix the ACES issue of intense colour being shifted."), 0, 0, false, 0, 0,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 1.f, 0.f, 0.f, 0.f } } },

			{ "Kajiya"sv, "KajiyaTonemap"sv,
				T(TKEY("tonemapper.kajiya.description"), "Tonemapper designed by Tomasz Stachowiak/Embark for their real time ray tracing engine Kajiya."), 0, 0, false, 0, 0,
				[](CTP& params) { exposureSlider(&params[0].x); },
				{ f4{ 1.f, 0.f, 0.f, 0.f } } },

			{ "GT7"sv, "GT7ToneMapping"sv,
				T(TKEY("tonemapper.gt7.description"), "Tonemapper designed for Gran Turismo 7."), 2, 2, true, 2, 2,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					drawHDRStatus();
				},
				{ f4{ 1.f, 0.f, 1000.f, 0.f } } },

			{ "PsychoV"sv, "PsychoVTonemap"sv,
				T(TKEY("tonemapper.psychov.description"), "PsychoV 17 tonemapper by Carlos Lopez, from RenoDX."),
				0, 0, true, 0, 0,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					drawHDRStatus();
				},
				{ f4{ 1.f, 0.f, 0.f, 0.f } } },

			{ "Neutwo"sv, "NeutwoTonemap"sv,
				T(TKEY("tonemapper.neutwo.description"), "Neutwo tonemapper by Carlos Lopez, from RenoDX."),
				0, 0, true, 2, 2,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("clip_point"), "Clip Point"), &params[0].y, 1.f, 100.f, "%.2f");
					drawHDRStatus();
				},
				{ f4{ 1.f, 100.f, 0.f, 0.f } } },

			{ "ACES"sv, "ACESTonemap"sv,
				T(TKEY("tonemapper.aces.description"), "ACES RRT+ODT tonemapper implementation from RenoDX."),
				0, 0, true, 2, 2,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("min_luminance"), "Min Luminance"), &params[0].y, 0.0001f, 1.f, "%.4f");
					drawHDRStatus();
				},
				{ f4{ 1.f, 0.0001f, 0.f, 0.f } } },

			{ "Frostbite"sv, "FrostbiteTonemap"sv,
				T(TKEY("tonemapper.frostbite.description"), "Frostbite HDR display mapping implementation from RenoDX, based on EA's Frostbite color grading and display presentation work."),
				0, 0, true, 2, 2,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("rolloff_start"), "Rolloff Start"), &params[0].y, 0.f, 1.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("saturation_boost"), "Saturation Boost"), &params[0].z, 0.f, 1.f, "%.2f");
					ImGui::SliderFloat(T(TKEY("hue_correction"), "Hue Correction"), &params[0].w, 0.f, 1.f, "%.2f");
					drawHDRStatus();
				},
				{ f4{ 1.f, 0.25f, 0.3f, 0.6f } } },

			{ "Hermite Spline"sv, "HermiteSplineTonemap"sv,
				T(TKEY("tonemapper.hermite_spline.description"), "Hermite spline tonemapper by Musa, from RenoDX."),
				0, 0, true, 2, 2,
				[](CTP& params) {
					exposureSlider(&params[0].x);
					ImGui::SliderFloat(T(TKEY("white_clip"), "White Clip"), &params[0].y, 1.f, 500.f, "%.2f");
					drawHDRStatus();
				},
				{ f4{ 1.f, 100.f, 0.f, 0.f } } }
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
		if (auto it = std::ranges::find_if(tonemappers, [&](TonemapperInfo& x) { return "GT7"sv == x.name; });
			it != tonemappers.end()) {
			tonemapperType = (int)(it - tonemappers.begin());
			params = it->default_settings;
		} else
			logger::error("Somehow, the default settings are invalid. Please contact the author.");
	}
};

void ColorGrading::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("skip_ldr_color_grading"), "Skip LDR Color Grading"), &settings.skipLDR);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("skip_ldr_color_grading_tooltip"), "Skip color grading after tonemapping. This includes Lift Gamma Gain. Will be automatically skipped with HDR on."));

	ImGui::Checkbox(T(TKEY("skip_lut_direct_color_grading"), "Skip LUT (Direct Color Grading)"), &settings.skipLUT);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("skip_lut_direct_color_grading_tooltip"), "Skip baking color grading into a LUT and apply it directly per-pixel. More accurate but slower."));

	ImGui::Checkbox(T(TKEY("convert_linear_to_log_before_hdr_color_grading"), "Convert Linear to Log Before HDR Color Grading"), &settings.useLog);
	if (settings.useLog) {
		ImGui::Checkbox(T(TKEY("convert_log_to_linear_after_hdr_color_grading"), "Convert Log to Linear After HDR Color Grading"), &settings.invertLog);
		ImGui::Combo(T(TKEY("log_type"), "Log Type"), (int*)&settings.logType, "ACEScct\0ARRILogC4\0SonySLog3\0");
	}

	ImGui::SeparatorText(T(TKEY("color_grading"), "Color Grading"));
	{
		ImGui::SliderFloat(T(TKEY("input_gamma"), "Input Gamma"), &settings.inOutGamma.z, 0.f, 3.f, "%.3f");
		ImGui::SliderFloat(T(TKEY("output_gamma"), "Output Gamma"), &settings.inOutGamma.w, 0.f, 3.f, "%.3f");

		ImGui::Text(T(TKEY("pre_tonemapping_settings"), "Pre-Tonemapping Settings"));
		if (ImGui::TreeNode(T(TKEY("exposure_temperature_tint"), "Exposure/Temperature/Tint"))) {
			exposureSlider(&settings.exposureTemperatureTint.x);
			ImGui::SliderFloat(T(TKEY("temperature"), "Temperature"), &settings.exposureTemperatureTint.y, 10.f, 150.f, "%1.f00K");
			ImGui::SliderFloat(T(TKEY("tint"), "Tint"), &settings.exposureTemperatureTint.z, -1.f, 1.f, "%.3f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("asc_cdl"), "ASC CDL"))) {
			Util::ShiftSlider(T(TKEY("slope"), "Slope"), &settings.slope.x, 0.f, 2.f, "%.2f");
			Util::ShiftSlider(T(TKEY("power"), "Power"), &settings.power.x, 0.f, 2.f, "%.2f");
			Util::ShiftSlider(T(TKEY("offset"), "Offset"), &settings.cdlOffset.x, -1.f, 1.f, "%.2f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("oklch_saturation"), "OKLCH Saturation"))) {
			ImGui::SliderFloat(T(TKEY("saturation"), "Saturation"), &settings.oklchSaturation.x, 0.f, 2.f, "%.3f");
			ImGui::SliderFloat(T(TKEY("vibrance"), "Vibrance"), &settings.oklchSaturation.y, 0.f, 3.f, "%.3f");
			ImGui::SliderFloat(T(TKEY("hue_shift"), "Hue Shift"), &settings.oklchSaturation.z, -1.f, 1.f, "%.3f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("oklch_color_mixer"), "OKLCH Color Mixer"))) {
			ImGui::Text(T(TKEY("oklch_color_mixer_tooltip"), "Adjust brightness, vibrance and hue shift of specific hues in the perceptually uniform OKLCH space."));
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
			ImGui::SliderFloat(T(TKEY("hue_shift"), "Hue Shift"), &settings.oklchColorMixer[hueId].x, -1.f, 1.f, "%.3f");
			ImGui::SliderFloat(T(TKEY("vibrance"), "Vibrance"), &settings.oklchColorMixer[hueId].y, 0.f, 3.f, "%.3f");
			ImGui::SliderFloat(T(TKEY("brightness"), "Brightness"), &settings.oklchColorMixer[hueId].z, -1.f, 1.f, "%.3f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("shadows_midtones_highlights"), "Shadows/Midtones/Highlights"))) {
			Util::ShiftSlider(T(TKEY("shadows_gain"), "Shadows Gain"), &settings.shadowsGain.x, 0.f, 2.f, "%.3f");
			Util::ShiftSlider(T(TKEY("shadows_offset"), "Shadows Offset"), &settings.shadowsOffset.x, -0.5f, 0.5f, "%.3f");
			Util::ShiftSlider(T(TKEY("midtones_gain"), "Midtones Gain"), &settings.midtonesGain.x, 0.f, 2.f, "%.3f");
			Util::ShiftSlider(T(TKEY("midtones_offset"), "Midtones Offset"), &settings.midtonesOffset.x, -0.5f, 0.5f, "%.3f");
			Util::ShiftSlider(T(TKEY("highlights_gain"), "Highlights Gain"), &settings.highlightsGain.x, 0.f, 2.f, "%.3f");
			Util::ShiftSlider(T(TKEY("highlights_offset"), "Highlights Offset"), &settings.highlightsOffset.x, -0.5f, 0.5f, "%.3f");
			ImGui::InputFloat2(T(TKEY("shadows_start_end"), "Shadows Start/End"), &settings.shadowsHighlightsRange.x, "%.3f");
			ImGui::InputFloat2(T(TKEY("highlights_start_end"), "Highlights Start/End"), &settings.shadowsHighlightsRange.z, "%.3f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("contrast"), "Contrast"))) {
			Util::ShiftSlider(T(TKEY("contrast"), "Contrast"), &settings.contrast.x, 0.f, 2.f, "%.3f");
			Util::ShiftSlider(T(TKEY("pivot"), "Pivot"), &settings.pivot.x, 0.f, 1.f, "%.3f");
			ImGui::TreePop();
		}

		ImGui::Text(T(TKEY("post_tonemapping_settings"), "Post-Tonemapping Settings"));
		if (ImGui::TreeNode(T(TKEY("lift_gamma_gain"), "Lift Gamma Gain"))) {
			ImGui::DragFloat4(T(TKEY("lift"), "Lift"), &settings.lift.x, 1e-3f, -1.f, 1.f, "%.3f");
			ImGui::DragFloat4(T(TKEY("gamma"), "Gamma"), &settings.gamma.x, 1e-3f, -1.5f, 1.5f, "%.3f");
			ImGui::DragFloat4(T(TKEY("gain"), "Gain"), &settings.gain.x, 1e-3f, 0.f, 2.f, "%.3f");
			ImGui::TreePop();
		}
	}

	ImGui::SeparatorText(T(TKEY("tonemapping"), "Tonemapping"));
	ImGui::Checkbox(T(TKEY("enable_tonemapping"), "Enable Tonemapping"), &settings.enableTonemap);
	if (settings.enableTonemap) {
		auto& hdrRef = globals::features::hdrDisplay;
		const bool hdrActive = hdrRef.loaded && hdrRef.settings.enableHDR;

		if (ImGui::Checkbox(T(TKEY("use_open_drt"), "Use OpenDRT"), &settings.useOpenDrt))
			recompileFlag = true;

		if (settings.useOpenDrt) {
			ImGui::PushID("OpenDRT");
			OpenDRTDrawSettings(
				settings.odrtConfig,
				hdrActive,
				static_cast<float>(hdrRef.settings.hdrPaperWhite),
				static_cast<float>(hdrRef.settings.hdrPeakNits));
			ImGui::PopID();
		} else {
			auto& tonemappers = TonemapperInfo::GetTonemappers();

			if (ImGui::BeginCombo(T(TKEY("tonemapper"), "Tonemapper"), tonemappers[tonemapperType].name.data(), ImGuiComboFlags_HeightLargest)) {
				for (int i = 0; i < (int)tonemappers.size(); ++i) {
					// Hide non-HDR tonemappers when HDR is active
					if (hdrActive && !tonemappers[i].supportsHDR)
						continue;

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
			if (ImGui::Button(T(TKEY("reset"), "Reset"), { -1, 0 }))
				settings.tonemapParams = tonemappers[tonemapperType].default_settings;
			ImGui::Spacing();

			ImGui::PushID(tonemapperType);
			tonemappers[tonemapperType].draw_settings_func(settings.tonemapParams);
			ImGui::PopID();
		}

		// Tonemapping curve visualization (GPU-evaluated, RGB overlay)
		if (ImGui::TreeNode(T(TKEY("curve_preview"), "Curve Preview"))) {
			curveReadbackRequested = true;
			curveReadbackRequestFrame = ImGui::GetFrameCount();

			if (settings.skipLUT) {
				ImGui::TextDisabled(T(TKEY("curve_preview_requires_lut"), "Enable LUT generation to see curve preview (uncheck 'Skip LUT')"));
			} else {
				// Determine Y-axis max from data
				float yMax = 1.f;
				if (hdrActive) {
					for (int i = 0; i < CurveSamples; i++) {
						yMax = std::max({ yMax, curveR[i], curveG[i], curveB[i] });
					}
					yMax = std::ceil(yMax * 2.f) / 2.f;  // round up to nearest 0.5
					yMax = std::max(yMax, 1.f);
				}

				// Plot area
				float plotW = ImGui::GetContentRegionAvail().x;
				float plotH = 180.f;
				ImVec2 canvasPos = ImGui::GetCursorScreenPos();
				ImVec2 canvasSize = { plotW, plotH };
				ImGui::InvisibleButton("##curve_canvas", canvasSize);
				bool hovered = ImGui::IsItemHovered();

				auto* dl = ImGui::GetWindowDrawList();

				// Background
				dl->AddRectFilled(canvasPos, { canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y }, IM_COL32(20, 20, 20, 255));
				dl->AddRect(canvasPos, { canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y }, IM_COL32(80, 80, 80, 255));

				// Grid lines
				auto gridColor = IM_COL32(50, 50, 50, 255);
				for (int g = 1; g <= 3; g++) {
					float gy = canvasPos.y + canvasSize.y * (1.f - (float)g / 4.f);
					dl->AddLine({ canvasPos.x, gy }, { canvasPos.x + canvasSize.x, gy }, gridColor);
				}
				// Vertical grid at input = 1.0
				{
					float gx = canvasPos.x + canvasSize.x * (1.f / CurveMaxInput);
					dl->AddLine({ gx, canvasPos.y }, { gx, canvasPos.y + canvasSize.y }, gridColor);
				}

				// Identity line (input = output, clamped to plot range)
				{
					float identityEndX = std::min(1.f, yMax) / CurveMaxInput;  // where identity line hits yMax
					float x0 = canvasPos.x;
					float y0 = canvasPos.y + canvasSize.y;
					float x1 = canvasPos.x + canvasSize.x * identityEndX;
					float y1 = canvasPos.y + canvasSize.y * (1.f - std::min(1.f, yMax) / yMax);
					dl->AddLine({ x0, y0 }, { x1, y1 }, IM_COL32(80, 80, 80, 128));
				}

				// Draw RGB curves
				auto drawCurve = [&](const std::array<float, CurveSamples>& data, ImU32 color) {
					for (int i = 0; i < CurveSamples - 1; i++) {
						float x0 = canvasPos.x + canvasSize.x * ((float)i / (CurveSamples - 1));
						float x1 = canvasPos.x + canvasSize.x * ((float)(i + 1) / (CurveSamples - 1));
						float y0 = canvasPos.y + canvasSize.y * (1.f - std::clamp(data[i] / yMax, 0.f, 1.f));
						float y1 = canvasPos.y + canvasSize.y * (1.f - std::clamp(data[i + 1] / yMax, 0.f, 1.f));
						dl->AddLine({ x0, y0 }, { x1, y1 }, color, 1.5f);
					}
				};

				drawCurve(curveR, IM_COL32(220, 60, 60, 255));
				drawCurve(curveG, IM_COL32(60, 200, 60, 255));
				drawCurve(curveB, IM_COL32(80, 80, 240, 255));

				// Hover tooltip with Pre/Post values
				if (hovered) {
					ImVec2 mousePos = ImGui::GetMousePos();
					float t = std::clamp((mousePos.x - canvasPos.x) / canvasSize.x, 0.f, 1.f);
					int idx = std::clamp((int)(t * (CurveSamples - 1)), 0, CurveSamples - 1);
					float preValue = t * CurveMaxInput;

					// Vertical cursor line
					dl->AddLine({ mousePos.x, canvasPos.y }, { mousePos.x, canvasPos.y + canvasSize.y }, IM_COL32(200, 200, 200, 100));

					ImGui::BeginTooltip();
					ImGui::Text(T(TKEY("curve_preview_pre"), "Pre:  %.3f"), preValue);
					ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.25f, 1), T(TKEY("curve_preview_post_r"), "Post R: %.3f"), curveR[idx]);
					ImGui::TextColored(ImVec4(0.25f, 0.8f, 0.25f, 1), T(TKEY("curve_preview_post_g"), "Post G: %.3f"), curveG[idx]);
					ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.95f, 1), T(TKEY("curve_preview_post_b"), "Post B: %.3f"), curveB[idx]);
					ImGui::EndTooltip();
				}

				// Axis labels
				ImGui::TextDisabled(T(TKEY("curve_preview_axis"), "Pre: 0 - %.1f (HDR linear)  |  Post: 0 - %.1f%s"), CurveMaxInput, yMax, hdrActive ? T(TKEY("curve_preview_hdr_suffix"), " (HDR)") : "");
			}
			ImGui::TreePop();
		} else {
			curveReadbackRequested = false;
		}
	}

	ImGui::SeparatorText(T(TKEY("game_color_grading"), "Game Color Grading"));
	ImGui::SliderFloat3(T(TKEY("cinematic_blend"), "Cinematic Blend"), &settings.gameCinematicBlend.x, 0.f, 1.f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("cinematic_blend_tooltip"), "Saturation, Brightness and Contrast."));
	ImGui::SliderFloat(T(TKEY("fade_blend"), "Fade Blend"), &settings.gameFadeBlend, 0.f, 1.f, "%.3f");
	ImGui::SliderFloat(T(TKEY("tint_blend"), "Tint Blend"), &settings.gameTintBlend, 0.f, 1.f, "%.3f");
	ImGui::SeparatorText(T(TKEY("color_space_transform"), "Color Space Transform"));
	{
		auto& spaces = getAvailableColorSpaces();
		auto& hdr = globals::features::hdrDisplay;
		const bool hdrEnabled = hdr.loaded && hdr.settings.enableHDR;

		constexpr int kHDRColorSpace = 2;  // BT2020
		constexpr int kSDRColorSpace = 0;  // sRGB / BT709 gamut
		const int outputColorSpace = hdrEnabled ? kHDRColorSpace : kSDRColorSpace;

		auto& llSettings = globals::features::linearLighting.settings;
		const bool wideGamutActive = llSettings.enableACEScg && llSettings.enableLinearLighting;
		const char* inputSpaceName = wideGamutActive ? spaces[5] : spaces[0];
		ImGui::TextDisabled(T(TKEY("input_color_space"), "Input Color Space: %s (%s)"), inputSpaceName, wideGamutActive ? T(TKEY("input_color_space_auto_detected"), "auto-detected from Linear Lighting ACEScg") : T(TKEY("input_color_space_fixed"), "fixed"));
		ImGui::Combo(T(TKEY("working_color_space"), "Working Color Space"), &settings.processColorSpace, spaces.data(), (int)spaces.size());
		ImGui::TextDisabled(T(TKEY("output_color_space"), "Output Color Space: %s (auto from HDR Display)"), spaces[outputColorSpace]);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(T(TKEY("output_color_space_tooltip"), "Output switches automatically: SDR -> sRGB, HDR -> BT2020."));

		UpdateColorSpaceTransforms(hdrEnabled);
	}

	if (ImGui::Button(T(TKEY("save_lut_and_output_image"), "Save LUT and Output Image"))) {
		saveImagesFlag = true;
	}
	ImGui::SameLine();
	ImGui::Text(T(TKEY("output_saved_to"), "Output will be saved to: %s"), outputPath.c_str());
}

void ColorGrading::RestoreDefaultSettings()
{
	settings = {};
	TonemapperInfo::GetDefaultParams(tonemapperType, settings.tonemapParams);
	recompileFlag = true;
}

void ColorGrading::LoadSettings(json& o_json)
{
	try {
		settings = o_json;
		auto& spaces = getAvailableColorSpaces();
		settings.processColorSpace = std::clamp(settings.processColorSpace, 0, static_cast<int>(spaces.size()) - 1);

		auto& tonemappers = TonemapperInfo::GetTonemappers();
		if (auto it = std::ranges::find_if(tonemappers, [&](TonemapperInfo& x) { return settings.currentTonemapper == x.name; });
			it != tonemappers.end()) {
			tonemapperType = (int)(it - tonemappers.begin());
		} else {
			TonemapperInfo::GetDefaultParams(tonemapperType, settings.tonemapParams);
		}
	} catch (const json::exception& e) {
		logger::error("Failed to load Color Grading settings: {}", e.what());
		RestoreDefaultSettings();
	}

	try {
		auto part1 = o_json["ODRT1"].get<OpenDRTSettingsPart1>();
		auto part2 = o_json["ODRT2"].get<OpenDRTSettingsPart2>();
		std::memcpy(&settings.odrtConfig, &part1, sizeof(OpenDRTSettingsPart1));
		std::memcpy(
			reinterpret_cast<char*>(&settings.odrtConfig) + sizeof(OpenDRTSettingsPart1),
			&part2,
			sizeof(OpenDRTSettingsPart2));
	} catch (const json::exception&) {
		settings.odrtConfig = {};
	}

	recompileFlag = true;
}

void ColorGrading::SaveSettings(json& o_json)
{
	auto& tonemappers = TonemapperInfo::GetTonemappers();
	settings.currentTonemapper = tonemappers[tonemapperType].name.data();
	o_json = settings;

	OpenDRTSettingsPart1 part1;
	OpenDRTSettingsPart2 part2;
	std::memcpy(&part1, &settings.odrtConfig, sizeof(part1));
	std::memcpy(
		&part2,
		reinterpret_cast<const char*>(&settings.odrtConfig) + sizeof(part1),
		sizeof(part2));
	o_json["ODRT1"] = part1;
	o_json["ODRT2"] = part2;
}

void ColorGrading::UpdateColorSpaceTransforms(bool hdrEnabled)
{
	auto& spaces = getAvailableColorSpaces();
	settings.processColorSpace = std::clamp(settings.processColorSpace, 0, static_cast<int>(spaces.size()) - 1);

	auto& tonemappers = TonemapperInfo::GetTonemappers();

	// Auto-detect input color space: ACEScg when wide gamut mode is active, otherwise sRGB
	auto& llSettings = globals::features::linearLighting.settings;
	const bool wideGamutActive = llSettings.enableACEScg && llSettings.enableLinearLighting;
	const int kInputColorSpace = wideGamutActive ? 5 : 0;  // 5 = ACEScg, 0 = sRGB
	constexpr int kHDRColorSpace = 2;                      // BT2020
	constexpr int kSDRColorSpace = 0;                      // sRGB / BT709 gamut
	const int outputColorSpace = hdrEnabled ? kHDRColorSpace : kSDRColorSpace;
	const int tonemapInputSpace =
		settings.useOpenDrt ? 4 :
							  ((hdrEnabled && tonemappers[tonemapperType].supportsHDR) ?
									  tonemappers[tonemapperType].nativeInputSpaceHDR :
									  tonemappers[tonemapperType].nativeInputSpace);
	const int tonemapOutputSpace =
		settings.useOpenDrt ? 2 :
							  ((hdrEnabled && tonemappers[tonemapperType].supportsHDR) ?
									  tonemappers[tonemapperType].nativeOutputSpaceHDR :
									  tonemappers[tonemapperType].nativeOutputSpace);

	auto storeMatrix = [](const DirectX::SimpleMath::Matrix& mat, std::array<float3, 3>& out) {
		out = {
			float3{ mat(0, 0), mat(0, 1), mat(0, 2) },
			float3{ mat(1, 0), mat(1, 1), mat(1, 2) },
			float3{ mat(2, 0), mat(2, 1), mat(2, 2) }
		};
	};

	storeMatrix(getRGBMatrix(spaces[kInputColorSpace], spaces[settings.processColorSpace]), inputToWorkingMatrix);
	storeMatrix(getRGBMatrix(spaces[settings.processColorSpace], spaces[tonemapInputSpace]), workingToTonemapMatrix);
	storeMatrix(getRGBMatrix(spaces[tonemapOutputSpace], spaces[outputColorSpace]), tonemapToOutputMatrix);
}

void ColorGrading::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

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

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags = 0;

		texColor = std::make_unique<Texture2D>(texDesc);
		texColor->CreateSRV(srvDesc);
		texColor->CreateRTV(rtvDesc);

		D3D11_TEXTURE3D_DESC lutTexDesc = {
			.Width = LUTDim,
			.Height = LUTDim,
			.Depth = LUTDim,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		D3D11_SHADER_RESOURCE_VIEW_DESC lutSrvDesc = {
			.Format = lutTexDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = lutTexDesc.MipLevels }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC lutUavDesc = {
			.Format = lutTexDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = lutTexDesc.Depth }
		};

		texLUT = std::make_unique<Texture3D>(lutTexDesc);
		texLUT->CreateSRV(lutSrvDesc);
		texLUT->CreateUAV(lutUavDesc);
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));
	}

	// Curve preview textures: 256x1 ramp for GPU-based curve evaluation
	{
		D3D11_TEXTURE2D_DESC curveTexDesc = {
			.Width = CurveSamples,
			.Height = 1,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { .Count = 1 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
		};

		D3D11_SHADER_RESOURCE_VIEW_DESC curveSrvDesc = {
			.Format = curveTexDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_RENDER_TARGET_VIEW_DESC curveRtvDesc = {
			.Format = curveTexDesc.Format,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texCurveInput = eastl::make_unique<Texture2D>(curveTexDesc);
		texCurveInput->CreateSRV(curveSrvDesc);

		texCurveOutput = eastl::make_unique<Texture2D>(curveTexDesc);
		texCurveOutput->CreateSRV(curveSrvDesc);
		texCurveOutput->CreateRTV(curveRtvDesc);

		// Fill input with linear ramp [0, CurveMaxInput] in R=G=B
		std::array<DirectX::PackedVector::XMHALF4, CurveSamples> rampData;
		for (int i = 0; i < CurveSamples; i++) {
			float v = (float)i / (float)(CurveSamples - 1) * CurveMaxInput;
			rampData[i] = {
				DirectX::PackedVector::XMConvertFloatToHalf(v),
				DirectX::PackedVector::XMConvertFloatToHalf(v),
				DirectX::PackedVector::XMConvertFloatToHalf(v),
				DirectX::PackedVector::XMConvertFloatToHalf(1.f)
			};
		}
		context->UpdateSubresource(texCurveInput->resource.get(), 0, nullptr, rampData.data(), CurveSamples * sizeof(DirectX::PackedVector::XMHALF4), 0);

		// Staging texture for readback
		D3D11_TEXTURE2D_DESC stagingDesc = curveTexDesc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		device->CreateTexture2D(&stagingDesc, nullptr, curveStaging.put());
	}

	CompileShaders();
}

void ColorGrading::ClearShaderCache()
{
	BumpShaderGeneration();
	const auto shaderPtrs = std::array{ &colorgradingPS };
	const auto computeShaderPtrs = std::array{ &lutgenCS };

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
		for (auto shader : computeShaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/ColorGrading");
	CompileShaders();
}

void ColorGrading::CompileShaders()
{
	const auto& tonemappers = TonemapperInfo::GetTonemappers();

	auto tonemapFuncName = settings.useOpenDrt ? "OpenDRTTransform" : tonemappers[tonemapperType].func_name.data();

	const std::vector<ComputeShaderCompileInfo> shaderInfos = {
		{ &lutgenCS, "colorgrading.hlsl", { { "TONEMAP_FUNC", tonemapFuncName } }, "CSLUTGen" },
	};

	CompileComputeShadersAsync(L"Data\\Shaders\\PostProcessing\\ColorGrading", shaderInfos);

	const std::vector<PixelShaderCompileInfo> rasterInfos = {
		{ &colorgradingPS, "colorgrading.hlsl", { { "TONEMAP_FUNC", tonemapFuncName } }, "PSColorGrading" },
	};

	CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\ColorGrading", {}, rasterInfos);

	recompileFlag = false;
	curveNeedsUpdate = true;  // shader changed, curve must update
}

void ColorGrading::Draw(TextureInfo& inout_tex)
{
	auto context = globals::d3d::context;
	auto state = globals::state;

	// Auto-switch to an HDR-capable tonemapper if current one doesn't support HDR.
	// This runs every frame so the switch happens immediately when HDR is toggled,
	// regardless of which settings page the user is viewing.
	{
		auto& hdrRef = globals::features::hdrDisplay;
		const bool hdrActive = hdrRef.loaded && hdrRef.settings.enableHDR;
		auto& tonemappers = TonemapperInfo::GetTonemappers();

		if (hdrActive && !tonemappers[tonemapperType].supportsHDR) {
			for (int i = 0; i < (int)tonemappers.size(); ++i) {
				if (tonemappers[i].supportsHDR) {
					tonemappers[tonemapperType].cached_settings = settings.tonemapParams;
					settings.tonemapParams = tonemappers[i].cached_settings;
					tonemapperType = i;
					recompileFlag = true;
					break;
				}
			}
		}
	}

	if (recompileFlag)
		ClearShaderCache();

	if (!AllShadersReady({ &colorgradingPS }) || !AllShadersReady({ &lutgenCS }))
		return;
	if (!owner || !owner->GetFullscreenVS())
		return;

	globals::profiler->BeginPass("PostProcessing::ColorGrading");
	state->BeginPerfEvent("Color Grading and Tonemapping");

	auto& pp = globals::features::postProcessing;

	RE::ImageSpaceData imageSpaceData = pp.imageSpaceManager->gameISData;
	auto& hdr = globals::features::hdrDisplay;
	const bool hdrEnabled = hdr.loaded && hdr.settings.enableHDR;
	UpdateColorSpaceTransforms(hdrEnabled);

	// Always compute XYZ matrices for white balance
	{
		auto& spaces = getAvailableColorSpaces();
		int wsIdx = std::clamp(settings.processColorSpace, 0, static_cast<int>(spaces.size()) - 1);
		auto storeMatrix = [](const DirectX::SimpleMath::Matrix& mat, std::array<float3, 3>& out) {
			out = {
				float3{ mat(0, 0), mat(0, 1), mat(0, 2) },
				float3{ mat(1, 0), mat(1, 1), mat(1, 2) },
				float3{ mat(2, 0), mat(2, 1), mat(2, 2) }
			};
		};
		storeMatrix(getRGBMatrix(spaces[wsIdx], "XYZ"), workingToXYZMatrix);
		storeMatrix(getRGBMatrix("XYZ", spaces[wsIdx]), xyzToWorkingMatrix);
	}

	ColorCB colorCBData = {
		.asccdl = { settings.slope, settings.power, settings.cdlOffset },
		.liftgammagain = { settings.lift, settings.gamma, settings.gain },
		.inOutGamma = settings.inOutGamma,
		.oklchSaturation = settings.oklchSaturation,
		.oklchColorMixer = { settings.oklchColorMixer[0], settings.oklchColorMixer[1], settings.oklchColorMixer[2], settings.oklchColorMixer[3], settings.oklchColorMixer[4], settings.oklchColorMixer[5], settings.oklchColorMixer[6] },
		.contrast = settings.contrast,
		.pivot = settings.pivot,
		.exposureTemperatureTint = settings.exposureTemperatureTint,
		.shadows = settings.shadowsGain,
		.midtones = settings.midtonesGain,
		.highlights = settings.highlightsGain,
		.shadowsHighlightsRange = settings.shadowsHighlightsRange,
		.tonemapParams = { settings.tonemapParams[0], settings.tonemapParams[1] },
		.inputToWorking = { float4{ inputToWorkingMatrix[0].x, inputToWorkingMatrix[0].y, inputToWorkingMatrix[0].z, 0.f }, float4{ inputToWorkingMatrix[1].x, inputToWorkingMatrix[1].y, inputToWorkingMatrix[1].z, 0.f }, float4{ inputToWorkingMatrix[2].x, inputToWorkingMatrix[2].y, inputToWorkingMatrix[2].z, 0.f } },
		.workingToTonemap = { float4{ workingToTonemapMatrix[0].x, workingToTonemapMatrix[0].y, workingToTonemapMatrix[0].z, 0.f }, float4{ workingToTonemapMatrix[1].x, workingToTonemapMatrix[1].y, workingToTonemapMatrix[1].z, 0.f }, float4{ workingToTonemapMatrix[2].x, workingToTonemapMatrix[2].y, workingToTonemapMatrix[2].z, 0.f } },
		.tonemapToOutput = { float4{ tonemapToOutputMatrix[0].x, tonemapToOutputMatrix[0].y, tonemapToOutputMatrix[0].z, 0.f }, float4{ tonemapToOutputMatrix[1].x, tonemapToOutputMatrix[1].y, tonemapToOutputMatrix[1].z, 0.f }, float4{ tonemapToOutputMatrix[2].x, tonemapToOutputMatrix[2].y, tonemapToOutputMatrix[2].z, 0.f } },
		.workingToXYZ = { float4{ workingToXYZMatrix[0].x, workingToXYZMatrix[0].y, workingToXYZMatrix[0].z, 0.f }, float4{ workingToXYZMatrix[1].x, workingToXYZMatrix[1].y, workingToXYZMatrix[1].z, 0.f }, float4{ workingToXYZMatrix[2].x, workingToXYZMatrix[2].y, workingToXYZMatrix[2].z, 0.f } },
		.xyzToWorking = { float4{ xyzToWorkingMatrix[0].x, xyzToWorkingMatrix[0].y, xyzToWorkingMatrix[0].z, 0.f }, float4{ xyzToWorkingMatrix[1].x, xyzToWorkingMatrix[1].y, xyzToWorkingMatrix[1].z, 0.f }, float4{ xyzToWorkingMatrix[2].x, xyzToWorkingMatrix[2].y, xyzToWorkingMatrix[2].z, 0.f } },
		.workingWhitePoint = [&]() {
			auto& spaces = getAvailableColorSpaces();
			int wsIdx = std::clamp(settings.processColorSpace, 0, static_cast<int>(spaces.size()) - 1);
			auto wp = getWhitePoint(spaces[wsIdx]);
			return float4{ wp.x, wp.y, 0.f, 0.f }; }(),
		.shadowsOffset = settings.shadowsOffset,
		.midtonesOffset = settings.midtonesOffset,
		.highlightsOffset = settings.highlightsOffset,
		.cinematic = float4{ std::lerp(1.f, imageSpaceData.baseData.cinematic.saturation, settings.gameCinematicBlend.x), std::lerp(1.f, imageSpaceData.baseData.cinematic.brightness, settings.gameCinematicBlend.y), std::lerp(1.f, imageSpaceData.baseData.cinematic.contrast, settings.gameCinematicBlend.z), imageSpaceData.baseAmount },
		.fade = float4{ imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeR], imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeG], imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeB], imageSpaceData.modData.data[RE::ImageSpaceModData::kFadeAmount] * settings.gameFadeBlend },
		.tint = float4{ imageSpaceData.baseData.tint.color.red, imageSpaceData.baseData.tint.color.green, imageSpaceData.baseData.tint.color.blue, imageSpaceData.baseData.tint.amount * settings.gameTintBlend },
		.logType = settings.useLog ? ((1u << settings.logType) | (settings.invertLog ? (1u << 3u) : 0u)) : 0u,
		.skipLDR = settings.skipLDR,
		.skipLUT = settings.skipLUT,
		.enableTonemap = settings.enableTonemap,
		.enableColorSpaceTransform = true,
		// Auto-populate HDR settings from HDR feature
		.enableHDR = [&]() -> uint {
			return hdrEnabled ? 1u : 0u;
		}(),
		.hdrPeakNits = [&]() -> float {
			return hdrEnabled ? static_cast<float>(hdr.settings.hdrPeakNits) : 1000.f;
		}(),
		.hdrPaperWhiteNits = [&]() -> float {
			return hdrEnabled ? static_cast<float>(hdr.settings.hdrPaperWhite) : 203.f;
		}(),
		.odrtConfig = settings.odrtConfig,
	};
	colorCB->Update(colorCBData);

	// Check if curve needs update (CB changed = settings changed)
	if (memcmp(&colorCBData, prevCurveCB.data(), sizeof(ColorCB)) != 0) {
		curveNeedsUpdate = true;
		memcpy(prevCurveCB.data(), &colorCBData, sizeof(ColorCB));
	}

	ID3D11Buffer* cb = colorCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);

	std::array<ID3D11SamplerState*, 1> samplers = { linearSampler.get() };
	context->CSSetSamplers(0, 1, samplers.data());
	ID3D11UnorderedAccessView* uav = nullptr;

	if (!settings.skipLUT) {
		// LUT Gen
		uav = texLUT->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(lutgenCS.get(), nullptr, 0);
		context->Dispatch(LUTDim >> 3, LUTDim >> 3, LUTDim >> 3);

		uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Apply Color Grading (via LUT or direct)
	{
		PostProcessingRaster::RasterPass pass(context);

		std::array<ID3D11ShaderResourceView*, 2> srvs = { inout_tex.srv, texLUT->srv.get() };

		ID3D11Buffer* psCB = cb;
		std::array<ID3D11SamplerState*, 1> psSamplers = { linearSampler.get() };
		context->PSSetConstantBuffers(1, 1, &psCB);
		context->PSSetSamplers(0, 1, psSamplers.data());
		context->PSSetShaderResources(0, (UINT)(settings.skipLUT ? 1 : 2), srvs.data());
		pass.SetTargets({ texColor->rtv.get() }, (float)texColor->desc.Width, (float)texColor->desc.Height);
		pass.SetShaders(owner->GetFullscreenVS(), colorgradingPS.get());
		pass.Draw();

		// clean up
		srvs.fill(nullptr);
		psCB = nullptr;
		psSamplers.fill(nullptr);
		context->PSSetShaderResources(0, 2, srvs.data());
		context->PSSetConstantBuffers(1, 1, &psCB);
		context->PSSetSamplers(0, 1, psSamplers.data());
	}

	// clean up CS stage (CB/sampler stayed bound for LUT generation)
	cb = nullptr;
	samplers.fill(nullptr);
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, 1, samplers.data());

	if (saveImagesFlag) {
		saveImagesFlag = false;
		OutputTextures();
	}

	inout_tex = { texColor->resource.get(), texColor->srv.get() };
	globals::profiler->EndPass();

	const bool curveReadbackActive =
		Menu::GetSingleton()->IsEnabled &&
		curveReadbackRequested &&
		ImGui::GetCurrentContext() &&
		curveReadbackRequestFrame >= ImGui::GetFrameCount() - 1;
	if (!curveReadbackActive)
		curveReadbackRequested = false;

	// Debug: evaluate color grading pipeline on a neutral ramp for curve preview
	if (curveReadbackActive && curveNeedsUpdate && texCurveInput && texCurveOutput && colorgradingPS) {
		curveNeedsUpdate = false;
		// Render the curve with the same PS on the 256x1 ramp input (same CB, same LUT)
		PostProcessingRaster::RasterPass pass(context);

		ID3D11Buffer* curveCB = colorCB->CB();
		std::array<ID3D11SamplerState*, 1> curveSamplers = { linearSampler.get() };
		std::array<ID3D11ShaderResourceView*, 2> curveSRVs = { texCurveInput->srv.get(), texLUT ? texLUT->srv.get() : nullptr };

		context->PSSetConstantBuffers(1, 1, &curveCB);
		context->PSSetSamplers(0, 1, curveSamplers.data());
		context->PSSetShaderResources(0, (UINT)(settings.skipLUT ? 1 : 2), curveSRVs.data());
		pass.SetTargets({ texCurveOutput->rtv.get() }, (float)CurveSamples, 1.f);
		pass.SetShaders(owner->GetFullscreenVS(), colorgradingPS.get());
		pass.Draw();

		// Clean up
		curveSRVs.fill(nullptr);
		curveCB = nullptr;
		curveSamplers.fill(nullptr);
		context->PSSetShaderResources(0, 2, curveSRVs.data());
		context->PSSetConstantBuffers(1, 1, &curveCB);
		context->PSSetSamplers(0, 1, curveSamplers.data());

		// Readback
		if (curveStaging) {
			context->CopyResource(curveStaging.get(), texCurveOutput->resource.get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(context->Map(curveStaging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
				auto* pixels = reinterpret_cast<const uint16_t*>(mapped.pData);
				for (int i = 0; i < CurveSamples; i++) {
					curveR[i] = DirectX::PackedVector::XMConvertHalfToFloat(pixels[i * 4 + 0]);
					curveG[i] = DirectX::PackedVector::XMConvertHalfToFloat(pixels[i * 4 + 1]);
					curveB[i] = DirectX::PackedVector::XMConvertHalfToFloat(pixels[i * 4 + 2]);
				}
				context->Unmap(curveStaging.get(), 0);
			}
		}
	}

	state->EndPerfEvent();
}

void ColorGrading::OutputTextures()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	DirectX::ScratchImage lutImage;
	DirectX::ScratchImage colorImage;

	if (texLUT->resource) {
		DirectX::CaptureTexture(device, context, texLUT->resource.get(), lutImage);
	}

	if (texColor->resource) {
		DirectX::CaptureTexture(device, context, texColor->resource.get(), colorImage);
	}

	if (std::filesystem::create_directories(outputPath)) {
		logger::info("Missing pp directory created: {}", outputPath);
	}

	std::filesystem::path savePath = outputPath;

	std::filesystem::path lutPath = savePath / "PP_ColorGrading_BakedLUT.dds";
	std::filesystem::path colorPath = savePath / "PP_ColorGrading_ColorOutput.dds";

	DX::ThrowIfFailed(SaveToDDSFile(lutImage.GetImages(), lutImage.GetImageCount(), lutImage.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, lutPath.c_str()));
	DX::ThrowIfFailed(SaveToDDSFile(colorImage.GetImages(), colorImage.GetImageCount(), colorImage.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, colorPath.c_str()));
}

#undef I18N_KEY_PREFIX
