#include "Raytracing.h"

#include "Globals.h"
#include "State.h"

// Microsoft Pix
#include <filesystem>
#include <shlobj.h>
#include <windows.h>

#include "DX12Interop.h"

#include "../I18n/I18n.h"

#include "Deferred.h"
#include "Features/Upscaling.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Raytracing::Settings,
	PerfOverlay,
	DisplaySceneGraphCounters,
	DisableVanillaFogPT,
	CreationEngineRaytracingSettings)

////////////////////////////////////////////////////////////////////////////////////

void Raytracing::RestoreDefaultSettings()
{
	settings = {};
}

void Raytracing::LoadSettings(json& o_json)
{
	settings = o_json;

	UpdateSettings();
}

void Raytracing::SaveSettings(json& o_json)
{
	o_json = settings;
}

static void DrawFloat2(const char* label, float2& v, float min = 0.0f, float max = 1.0f)
{
	float floats[2] = { v.x, v.y };
	if (ImGui::SliderFloat2(label, floats, min, max)) {
		v = { floats[0], floats[1] };
		v.Clamp({ min, min }, { max, max });
	}
}

static std::string StableLabel(const char* label, std::string_view id)
{
	return std::format("{}###{}", label, id);
}

template <typename T, size_t N>
	requires std::is_enum_v<T>
static bool DrawEnumRadio(const char* label, const char* id, T& variable, const std::array<const char*, N>& labels, const char* tooltip = nullptr)
{
	static_assert(N == magic_enum::enum_count<T>(), "Enum label count must match enum count.");

	ImGui::PushID(id);

	auto variablePrev = variable;

	int enumValue = static_cast<int32_t>(variable);
	ImGui::TextUnformatted(label);

	if (tooltip != nullptr)
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", tooltip);

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(25, 0));

	auto i = 0;

	for (auto& [value, name] : magic_enum::enum_entries<T>()) {
		ImGui::SameLine();
		const auto itemLabel = StableLabel(labels[i], name);
		ImGui::RadioButton(itemLabel.c_str(), &enumValue, static_cast<int32_t>(value));

		i++;
	}

	ImGui::PopID();

	variable = static_cast<T>(enumValue);

	return variable != variablePrev;
}

template <typename T, size_t N>
	requires std::is_enum_v<T>
static bool DrawEnumCombo(const char* label, const char* id, T& variable, const std::array<const char*, N>& labels, const char* tooltip = nullptr)
{
	static_assert(N == magic_enum::enum_count<T>(), "Enum label count must match enum count.");

	ImGui::PushID(id);

	auto variablePrev = variable;

	const auto currentIndex = magic_enum::enum_index(variable);
	const auto comboLabel = StableLabel(label, "Combo");

	if (ImGui::BeginCombo(comboLabel.c_str(), currentIndex ? labels[*currentIndex] : "")) {
		auto i = 0;

		for (auto& value : magic_enum::enum_values<T>()) {
			bool isSelected = (variable == value);
			const auto itemLabel = StableLabel(labels[i], magic_enum::enum_name(value));

			if (ImGui::Selectable(itemLabel.c_str(), isSelected))
				variable = value;

			if (isSelected)
				ImGui::SetItemDefaultFocus();

			i++;
		}

		ImGui::EndCombo();
	} else if (tooltip != nullptr) {
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", tooltip);
	}

	ImGui::PopID();

	return variable != variablePrev;
}

template <class T>
static void ClampSetting(T& value, T min, T max)
{
	value = std::clamp(value, min, max);
}

#define I18N_KEY_PREFIX "feature.raytracing."

void Raytracing::DrawSettings()
{
	bool forcedDisabledReason = disableReason != DisableReason::None;

	if (forcedDisabledReason) {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), T(TKEY("ray_tracing_disabled"), "Ray tracing is disabled: %s"), [&]() {
			switch (disableReason) {
			case DisableReason::UnsupportedGPU:
				return T(TKEY("disable_reason_unsupported_gpu"), "Unsupported GPU.");
			case DisableReason::OutdatedDrivers:
				return T(TKEY("disable_reason_outdated_drivers"), "Outdated Drivers.");
			case DisableReason::MissingPlugin:
				return T(TKEY("disable_reason_missing_plugin"), "Missing 'CreationEngineRaytracing.dll', check your mod manager.");
			case DisableReason::InitFailed:
				return T(TKEY("disable_reason_init_failed"), "Initialization Failed, check CreationEngineRaytracing.txt log");
			default:
				return T(TKEY("disable_reason_unknown"), "Unknown Reason");
			}
		}());
	}

	if (forcedDisabledReason)
		ImGui::BeginDisabled();

	auto ceRTSettingsBefore = settings.CreationEngineRaytracingSettings;

	ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &settings.CreationEngineRaytracingSettings.Enabled);

	DrawEnumRadio(
		T(TKEY("mode"), "Mode"),
		"Mode",
		settings.CreationEngineRaytracingSettings.GeneralSettings.Mode,
		std::array{
			T(TKEY("mode_none"), "None"),
			T(TKEY("mode_global_illumination"), "Global Illumination"),
			T(TKEY("mode_path_tracing"), "Path Tracing"),
		});

	DrawEnumRadio(
		T(TKEY("denoiser"), "Denoiser"),
		"Denoiser",
		settings.CreationEngineRaytracingSettings.GeneralSettings.Denoiser,
		std::array{
			T(TKEY("denoiser_none"), "None"),
			T(TKEY("denoiser_nrd"), "NRD"),
			T(TKEY("denoiser_dlss_rr"), "DLSS RR"),
			T(TKEY("denoiser_accumulation"), "Accumulation"),
		});

	// Show DLSS RR availability status
	if (settings.CreationEngineRaytracingSettings.GeneralSettings.Denoiser == CreationEngineRaytracing::Denoiser::DLSS_RR) {
		if (!(Upscaling::streamline.loadedFeatures & Streamline::Features::kDLSS_RR)) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", T(TKEY("dlss_rr_not_available"), "DLSS Ray Reconstruction is not available on this system."));
		} else if (globals::features::upscaling.settings.upscaleMethod != Upscaling::UpscaleMethod::kDLSS) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", T(TKEY("set_upscaling_to_dlss"), "Set Upscaling method to DLSS to enable Ray Reconstruction."));
		}
	}

	// Accumulation only works in Path Tracing mode
	if (settings.CreationEngineRaytracingSettings.GeneralSettings.Denoiser == CreationEngineRaytracing::Denoiser::Accumulation) {
		if (settings.CreationEngineRaytracingSettings.GeneralSettings.Mode != CreationEngineRaytracing::Mode::PathTracing) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", T(TKEY("accumulation_pt_only"), "Accumulation is only available in Path Tracing mode."));
		}
	}

	/*bool ptMode = settings.CreationEngineRaytracingSettings.GeneralSettings.Mode == CreationEngineRaytracing::Mode::PathTracing;

	if (ptMode)
		ImGui::BeginDisabled();

	ImGui::Checkbox(T(TKEY("raytraced_shadows"), "Raytraced Shadows"), &settings.CreationEngineRaytracingSettings.GeneralSettings.RaytracedShadows);

	if (ptMode)
		ImGui::EndDisabled();*/

	if (ImGui::BeginTabBar("Settings")) {
		DrawGeneralSettings();
		DrawAdvancedSettings();
		DrawReSTIRGISettings();
		DrawExperimentalSettings();
		DrawDebugSettings();

		ImGui::EndTabBar();
	}

	if (forcedDisabledReason)
		ImGui::EndDisabled();

	if (ceRTSettingsBefore != settings.CreationEngineRaytracingSettings)
		UpdateSettings();
}

CreationEngineRaytracing::Settings Raytracing::GetSettings() const
{
	auto certSettings = settings.CreationEngineRaytracingSettings;

	// Only if PIX is enabled (globals::dx12Interop.enablePIXCapture)
	certSettings.DebugSettings.Markers = false;
	certSettings.DebugSettings.Timings = settings.PerfOverlay != OverlayMode::None;

	return certSettings;
}

void Raytracing::UpdateSettings()
{
	if (!initialized)
		return;

	creationEngineRaytracing->UpdateSettings(GetSettings());
}

void Raytracing::DrawGeneralSettings()
{
	if (!ImGui::BeginTabItem(T(TKEY("tab_general"), "General")))
		return;

	ImGui::PushID("GeneralSettings");

	auto& ceRTSettings = settings.CreationEngineRaytracingSettings;

	// RT
	{
		auto& rtSettings = ceRTSettings.RaytracingSettings;

		// Bounces
		if (ImGui::SliderInt(T(TKEY("bounces"), "Bounces"), &rtSettings.Bounces, 1, 32))
			rtSettings.Bounces = std::clamp(rtSettings.Bounces, 1, 32);

		// Samples Per Pixel
		if (ImGui::SliderInt(T(TKEY("samples_per_pixel"), "Samples Per Pixel"), &rtSettings.SamplesPerPixel, 1, 32))
			rtSettings.SamplesPerPixel = std::clamp(rtSettings.SamplesPerPixel, 1, 32);
	}

	if (ceRTSettings.GeneralSettings.Denoiser == CreationEngineRaytracing::Denoiser::NRD)
		DrawReblurSettings();

	DrawSHaRCSettings();

	// Material
	DrawFloat2(T(TKEY("roughness"), "Roughness"), ceRTSettings.MaterialSettings.Roughness);
	DrawFloat2(T(TKEY("metalness"), "Metalness"), ceRTSettings.MaterialSettings.Metalness);

	if (ImGui::CollapsingHeader(T(TKEY("lighting"), "Lighting"), ImGuiTreeNodeFlags_DefaultOpen)) {
		auto& lightingSettings = ceRTSettings.LightingSettings;

		if (ImGui::DragFloat(T(TKEY("directional_strength"), "Directional Strength"), &lightingSettings.Directional, 0.001f))
			lightingSettings.Directional = std::max(0.0f, lightingSettings.Directional);

		if (ImGui::DragFloat(T(TKEY("point_strength"), "Point Strength"), &lightingSettings.Point, 0.001f))
			lightingSettings.Point = std::max(0.0f, lightingSettings.Point);

		ImGui::Checkbox(T(TKEY("lod_dimmer"), "Lod Dimmer"), &lightingSettings.LodDimmer);

		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("lod_dimmer_desc"), "Vanilla behaviour of dimming lights that are far enough.\n"));

		if (ImGui::DragFloat(T(TKEY("emissive_strength"), "Emissive Strength"), &lightingSettings.Emissive, 0.001f))
			lightingSettings.Emissive = std::max(0.0f, lightingSettings.Emissive);

		if (ImGui::DragFloat(T(TKEY("effect_strength"), "Effect Strength"), &lightingSettings.Effect, 0.001f))
			lightingSettings.Effect = std::max(0.0f, lightingSettings.Effect);

		if (ImGui::DragFloat(T(TKEY("sky_strength"), "Sky Strength"), &lightingSettings.Sky, 0.001f))
			lightingSettings.Sky = std::max(0.0f, lightingSettings.Sky);
	}

	if (ImGui::CollapsingHeader(T(TKEY("water"), "Water"))) {
		auto& waterSettings = ceRTSettings.WaterSettings;

		if (ImGui::DragFloat(T(TKEY("absorption_scale"), "Absorption Scale"), &waterSettings.AbsorptionScale, 0.01f, 0.01f, 10.0f, "%.2f"))
			waterSettings.AbsorptionScale = std::clamp(waterSettings.AbsorptionScale, 0.01f, 10.0f);
	}

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawReblurSettings()
{
	if (!ImGui::CollapsingHeader(T(TKEY("reblur"), "Reblur"))) {
		return;
	}

	auto& reblurSettings = settings.CreationEngineRaytracingSettings.ReblurSettings;

	if (ImGui::InputScalar(T(TKEY("max_accumulated_frames"), "Max Accumulated Frames"), ImGuiDataType_U32, &reblurSettings.maxAccumulatedFrameNum))
		ClampSetting(reblurSettings.maxAccumulatedFrameNum, 0u, 63u);

	if (ImGui::InputScalar(T(TKEY("max_fast_accumulated_frames"), "Max Fast Accumulated Frames"), ImGuiDataType_U32, &reblurSettings.maxFastAccumulatedFrameNum))
		ClampSetting(reblurSettings.maxFastAccumulatedFrameNum, 0u, reblurSettings.maxAccumulatedFrameNum);

	if (ImGui::InputScalar(T(TKEY("max_stabilized_frames"), "Max Stabilized Frames"), ImGuiDataType_U32, &reblurSettings.maxStabilizedFrameNum))
		ClampSetting(reblurSettings.maxStabilizedFrameNum, 0u, reblurSettings.maxAccumulatedFrameNum);

	if (ImGui::InputScalar(T(TKEY("history_fix_frames"), "History Fix Frames"), ImGuiDataType_U32, &reblurSettings.historyFixFrameNum))
		ClampSetting(reblurSettings.historyFixFrameNum, 0u, reblurSettings.maxFastAccumulatedFrameNum);

	if (ImGui::InputScalar(T(TKEY("history_fix_base_pixel_stride"), "History Fix Base Pixel Stride"), ImGuiDataType_U32, &reblurSettings.historyFixBasePixelStride))
		ClampSetting(reblurSettings.historyFixBasePixelStride, 1u, 64u);

	if (ImGui::InputScalar(T(TKEY("history_fix_alternate_pixel_stride"), "History Fix Alternate Pixel Stride"), ImGuiDataType_U32, &reblurSettings.historyFixAlternatePixelStride))
		ClampSetting(reblurSettings.historyFixAlternatePixelStride, 1u, 64u);

	if (ImGui::SliderFloat(T(TKEY("fast_history_clamping_sigma_scale"), "Fast History Clamping Sigma Scale"), &reblurSettings.fastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f"))
		ClampSetting(reblurSettings.fastHistoryClampingSigmaScale, 1.0f, 3.0f);

	if (ImGui::SliderFloat(T(TKEY("diffuse_prepass_blur_radius"), "Diffuse Prepass Blur Radius"), &reblurSettings.diffusePrepassBlurRadius, 0.0f, 100.0f, "%.1f"))
		ClampSetting(reblurSettings.diffusePrepassBlurRadius, 0.0f, 100.0f);

	if (ImGui::SliderFloat(T(TKEY("specular_prepass_blur_radius"), "Specular Prepass Blur Radius"), &reblurSettings.specularPrepassBlurRadius, 0.0f, 100.0f, "%.1f"))
		ClampSetting(reblurSettings.specularPrepassBlurRadius, 0.0f, 100.0f);

	if (ImGui::SliderFloat(T(TKEY("min_hit_distance_weight"), "Min Hit Distance Weight"), &reblurSettings.minHitDistanceWeight, 0.001f, 0.2f, "%.3f"))
		ClampSetting(reblurSettings.minHitDistanceWeight, 0.001f, 0.2f);

	if (ImGui::SliderFloat(T(TKEY("min_blur_radius"), "Min Blur Radius"), &reblurSettings.minBlurRadius, 0.0f, 10.0f, "%.2f"))
		ClampSetting(reblurSettings.minBlurRadius, 0.0f, 10.0f);

	if (ImGui::SliderFloat(T(TKEY("max_blur_radius"), "Max Blur Radius"), &reblurSettings.maxBlurRadius, 0.0f, 100.0f, "%.1f"))
		ClampSetting(reblurSettings.maxBlurRadius, 0.0f, 100.0f);

	if (ImGui::SliderFloat(T(TKEY("lobe_angle_fraction"), "Lobe Angle Fraction"), &reblurSettings.lobeAngleFraction, 0.0f, 1.0f, "%.3f"))
		ClampSetting(reblurSettings.lobeAngleFraction, 0.0f, 1.0f);

	if (ImGui::SliderFloat(T(TKEY("roughness_fraction"), "Roughness Fraction"), &reblurSettings.roughnessFraction, 0.0f, 1.0f, "%.3f"))
		ClampSetting(reblurSettings.roughnessFraction, 0.0f, 1.0f);

	if (ImGui::SliderFloat(T(TKEY("plane_distance_sensitivity"), "Plane Distance Sensitivity"), &reblurSettings.planeDistanceSensitivity, 0.0f, 1.0f, "%.3f"))
		ClampSetting(reblurSettings.planeDistanceSensitivity, 0.0f, 1.0f);

	if (ImGui::SliderFloat2(T(TKEY("specular_probability_thresholds_for_mv_modification"), "Specular Probability Thresholds For MV Modification"), reblurSettings.specularProbabilityThresholdsForMvModification.data(), 0.0f, 1.0f, "%.2f")) {
		ClampSetting(reblurSettings.specularProbabilityThresholdsForMvModification[0], 0.0f, 1.0f);
		ClampSetting(reblurSettings.specularProbabilityThresholdsForMvModification[1], reblurSettings.specularProbabilityThresholdsForMvModification[0], 1.0f);
	}

	if (ImGui::SliderFloat(T(TKEY("firefly_suppressor_min_relative_scale"), "Firefly Suppressor Min Relative Scale"), &reblurSettings.fireflySuppressorMinRelativeScale, 1.0f, 3.0f, "%.2f"))
		ClampSetting(reblurSettings.fireflySuppressorMinRelativeScale, 1.0f, 3.0f);

	ImGui::Checkbox(T(TKEY("enable_anti_firefly"), "Enable Anti Firefly"), &reblurSettings.enableAntiFirefly);
	ImGui::Checkbox(T(TKEY("use_prepass_only_for_specular_motion_estimation"), "Use Prepass Only For Specular Motion Estimation"), &reblurSettings.usePrepassOnlyForSpecularMotionEstimation);
	ImGui::Checkbox(T(TKEY("return_history_length_instead_of_occlusion"), "Return History Length Instead Of Occlusion"), &reblurSettings.returnHistoryLengthInsteadOfOcclusion);
}

void Raytracing::DrawSHaRCSettings()
{
	if (ImGui::CollapsingHeader(T(TKEY("sharc"), "SHaRC"))) {
		auto& sharcSettings = settings.CreationEngineRaytracingSettings.SHaRCSettings;

		ImGui::Checkbox(T(TKEY("sharc_enabled"), "Enabled"), &sharcSettings.Enabled);

		if (!sharcSettings.Enabled)
			ImGui::BeginDisabled();

		ImGui::DragFloat(T(TKEY("sharc_scale"), "Scale"), &sharcSettings.SceneScale, 0.001f, 0.1f, 10.0f);
		sharcSettings.SceneScale = std::clamp(sharcSettings.SceneScale, 0.1f, 10.0f);

		ImGui::InputInt(T(TKEY("sharc_accumulation_frames"), "Accumulation Frames"), &sharcSettings.AccumFrameNum);
		sharcSettings.AccumFrameNum = std::clamp(sharcSettings.AccumFrameNum, 5, 100);

		ImGui::InputInt(T(TKEY("sharc_stale_frames"), "Stale Frames"), &sharcSettings.StaleFrameNum);
		sharcSettings.StaleFrameNum = std::clamp(sharcSettings.StaleFrameNum, 8, 128);

		if (!sharcSettings.Enabled)
			ImGui::EndDisabled();
	}
}

void Raytracing::DrawSSSSettings()
{
	auto& sssSettings = settings.CreationEngineRaytracingSettings.AdvancedSettings.SSSSettings;

	ImGui::Checkbox(T(TKEY("sss_enabled"), "Enable Subsurface Scattering"), &sssSettings.Enabled);

	if (!sssSettings.Enabled)
		return;

	if (ImGui::CollapsingHeader(T(TKEY("subsurface_scattering"), "Subsurface Scattering"))) {
		if (sssSettings.Enabled) {
			ImGui::SliderInt(T(TKEY("sss_sample_count"), "Sample Count"), &sssSettings.SampleCount, 1, 16);
			ImGui::SliderFloat(T(TKEY("sss_max_sample_radius"), "Max Sample Radius"), &sssSettings.MaxSampleRadius, 0.01f, 64.0f, "%.2f");
			ImGui::Checkbox(T(TKEY("sss_enable_transmission"), "Enable Transmission"), &sssSettings.EnableTransmission);
			ImGui::Checkbox(T(TKEY("sss_material_override"), "Material Override"), &sssSettings.MaterialOverride);

			if (sssSettings.MaterialOverride) {
				if (ImGui::TreeNodeEx(T(TKEY("sss_overrides"), "Subsurface Scattering"), ImGuiTreeNodeFlags_DefaultOpen)) {
					const auto overrideTransmissionLabel = StableLabel(T(TKEY("sss_override_transmission_color"), "Override Transmission Color"), "OverrideTransmissionColor");
					const auto overrideScatteringLabel = StableLabel(T(TKEY("sss_override_scattering_color"), "Override Scattering Color"), "OverrideScatteringColor");
					ImGui::ColorEdit3(overrideTransmissionLabel.c_str(), reinterpret_cast<float*>(&sssSettings.OverrideTransmissionColor), ImGuiColorEditFlags_Float);
					ImGui::ColorEdit3(overrideScatteringLabel.c_str(), reinterpret_cast<float*>(&sssSettings.OverrideScatteringColor), ImGuiColorEditFlags_Float);
					ImGui::SliderFloat(T(TKEY("sss_override_scale"), "Override Scale"), &sssSettings.OverrideScale, 0.01f, 1000.0f, "%.2f");
					ImGui::SliderFloat(T(TKEY("sss_override_anisotropy"), "Override Anisotropy"), &sssSettings.OverrideAnisotropy, -0.99f, 0.99f);

					ImGui::TreePop();
				}
			}
		}
	}
}

void Raytracing::DrawAdvancedSettings()
{
	if (!ImGui::BeginTabItem(T(TKEY("tab_advanced"), "Advanced")))
		return;

	ImGui::PushID("AdvancedSettings");

	auto& advSettings = settings.CreationEngineRaytracingSettings.AdvancedSettings;

	ImGui::SliderFloat(T(TKEY("texture_lod_bias"), "Texture LOD Bias"), &advSettings.TexLODBias, -4.0f, 4.0f, "%.1f");

	ImGui::Checkbox(T(TKEY("variable_update_rate"), "Variable Update Rate"), &advSettings.VariableUpdateRate);

	ImGui::Checkbox(T(TKEY("ggx_energy_conservation"), "GGX Energy Conservation"), &advSettings.GGXEnergyConservation);

	ImGui::Checkbox(T(TKEY("per_light_tlas"), "Per Light Top-Level Acceleration Structures"), &advSettings.PerLightTLAS);

	ImGui::Checkbox(T(TKEY("ris_enabled"), "Resampled Importance Sampling"), &advSettings.RIS.Enabled);

	ImGui::SliderInt(T(TKEY("ris_max_candidates"), "RIS Max Candidates"), &advSettings.RIS.MaxCandidates, 2, 16);

	DrawEnumCombo(
		T(TKEY("hair_bsdf"), "Hair BSDF"),
		"HairBSDF",
		advSettings.HairBSDF,
		std::array{
			T(TKEY("hair_bsdf_none"), "None"),
			T(TKEY("hair_bsdf_chiang"), "Chiang BSDF"),
			T(TKEY("hair_bsdf_far_field"), "Far Field BCSDF"),
		});

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(T(TKEY("hair_bsdf_tooltip"), "Best with hair specular feature enabled.\n"));
	}

	DrawSSSSettings();

	DrawEnumCombo(
		T(TKEY("diffuse_brdf"), "Diffuse BRDF"),
		"DiffuseBRDF",
		advSettings.DiffuseBRDF,
		std::array{
			T(TKEY("diffuse_brdf_lambert"), "Lambert"),
			T(TKEY("diffuse_brdf_burley"), "Burley"),
			T(TKEY("diffuse_brdf_oren_nayar"), "Oren Nayar"),
			T(TKEY("diffuse_brdf_gotanda"), "Gotanda"),
			T(TKEY("diffuse_brdf_chan"), "Chan"),
		});

	ImGui::Checkbox(T(TKEY("stable_planes"), "Stable Planes"), &advSettings.StablePlanes);

	ImGui::Checkbox(T(TKEY("disable_vanilla_fog_pt"), "Disable vanilla fog when pathtracing"), &settings.DisableVanillaFogPT);

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawReSTIRGISettings()
{
	if (!ImGui::BeginTabItem(T(TKEY("tab_restir_gi"), "ReSTIR GI")))
		return;

	ImGui::PushID("ReSTIRGISettings");

	auto& giSettings = settings.CreationEngineRaytracingSettings.ReSTIRGI;

	ImGui::Checkbox(T(TKEY("restir_enabled"), "Enabled"), &giSettings.Enabled);

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("restir_enabled_tooltip"), "Enable ReSTIR GI indirect lighting resampling."));

	DrawEnumCombo(
		T(TKEY("restir_resampling_mode"), "Resampling Mode"),
		"ReSTIRGIResamplingMode",
		giSettings.ResamplingMode,
		std::array{
			T(TKEY("restir_resampling_mode_none"), "None"),
			T(TKEY("restir_resampling_mode_temporal"), "Temporal"),
			T(TKEY("restir_resampling_mode_spatial"), "Spatial"),
			T(TKEY("restir_resampling_mode_temporal_and_spatial"), "Temporal and Spatial"),
			T(TKEY("restir_resampling_mode_fused_spatiotemporal"), "Fused Spatiotemporal"),
		});

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("restir_resampling_mode_tooltip"),
							  "None: Disabled\n"
							  "Temporal: Reuse from previous frames\n"
							  "Spatial: Reuse from nearby pixels\n"
							  "Temporal and Spatial: Both (recommended)\n"
							  "Fused Spatiotemporal: Combined pass"));

	ImGui::Separator();
	ImGui::Text(T(TKEY("temporal_resampling"), "Temporal Resampling"));

	if (ImGui::SliderFloat(T(TKEY("restir_temporal_depth_threshold"), "Temporal Depth Threshold"), &giSettings.TemporalDepthThreshold, 0.0f, 1.0f, "%.2f"))
		giSettings.TemporalDepthThreshold = std::clamp(giSettings.TemporalDepthThreshold, 0.0f, 1.0f);

	if (ImGui::SliderFloat(T(TKEY("restir_temporal_normal_threshold"), "Temporal Normal Threshold"), &giSettings.TemporalNormalThreshold, 0.0f, 1.0f, "%.2f"))
		giSettings.TemporalNormalThreshold = std::clamp(giSettings.TemporalNormalThreshold, 0.0f, 1.0f);

	if (ImGui::SliderInt(T(TKEY("restir_max_history_length"), "Max History Length"), &giSettings.MaxHistoryLength, 1, 50))
		giSettings.MaxHistoryLength = std::clamp(giSettings.MaxHistoryLength, 1, 50);

	if (ImGui::SliderInt(T(TKEY("restir_max_reservoir_age"), "Max Reservoir Age"), &giSettings.MaxReservoirAge, 1, 200))
		giSettings.MaxReservoirAge = std::clamp(giSettings.MaxReservoirAge, 1, 200);

	ImGui::Checkbox(T(TKEY("restir_permutation_sampling"), "Permutation Sampling"), &giSettings.EnablePermutationSampling);
	ImGui::Checkbox(T(TKEY("restir_fallback_sampling"), "Fallback Sampling"), &giSettings.EnableFallbackSampling);

	DrawEnumCombo(
		T(TKEY("temporal_bias_correction"), "Temporal Bias Correction"),
		"TemporalBiasCorrection",
		giSettings.TemporalBiasCorrection,
		std::array{
			T(TKEY("bias_correction_off"), "Off"),
			T(TKEY("bias_correction_basic"), "Basic"),
			T(TKEY("bias_correction_raytraced"), "Raytraced"),
		});

	ImGui::Separator();
	ImGui::Text(T(TKEY("spatial_resampling"), "Spatial Resampling"));

	if (ImGui::SliderFloat(T(TKEY("restir_spatial_depth_threshold"), "Spatial Depth Threshold"), &giSettings.SpatialDepthThreshold, 0.0f, 1.0f, "%.2f"))
		giSettings.SpatialDepthThreshold = std::clamp(giSettings.SpatialDepthThreshold, 0.0f, 1.0f);

	if (ImGui::SliderFloat(T(TKEY("restir_spatial_normal_threshold"), "Spatial Normal Threshold"), &giSettings.SpatialNormalThreshold, 0.0f, 1.0f, "%.2f"))
		giSettings.SpatialNormalThreshold = std::clamp(giSettings.SpatialNormalThreshold, 0.0f, 1.0f);

	if (ImGui::SliderInt(T(TKEY("restir_spatial_samples"), "Spatial Samples"), &giSettings.SpatialNumSamples, 0, 8))
		giSettings.SpatialNumSamples = std::clamp(giSettings.SpatialNumSamples, 0, 8);

	if (ImGui::SliderFloat(T(TKEY("restir_spatial_sampling_radius"), "Spatial Sampling Radius"), &giSettings.SpatialSamplingRadius, 1.0f, 64.0f, "%.1f"))
		giSettings.SpatialSamplingRadius = std::clamp(giSettings.SpatialSamplingRadius, 1.0f, 64.0f);

	DrawEnumCombo(
		T(TKEY("spatial_bias_correction"), "Spatial Bias Correction"),
		"SpatialBiasCorrection",
		giSettings.SpatialBiasCorrection,
		std::array{
			T(TKEY("bias_correction_off"), "Off"),
			T(TKEY("bias_correction_basic"), "Basic"),
			T(TKEY("bias_correction_raytraced"), "Raytraced"),
		});

	ImGui::Separator();
	ImGui::Text(T(TKEY("boiling_filter"), "Boiling Filter"));

	ImGui::Checkbox(T(TKEY("restir_enable_boiling_filter"), "Enable Boiling Filter"), &giSettings.EnableBoilingFilter);

	if (giSettings.EnableBoilingFilter) {
		if (ImGui::SliderFloat(T(TKEY("restir_boiling_filter_strength"), "Boiling Filter Strength"), &giSettings.BoilingFilterStrength, 0.0f, 1.0f, "%.2f"))
			giSettings.BoilingFilterStrength = std::clamp(giSettings.BoilingFilterStrength, 0.0f, 1.0f);
	}

	ImGui::Separator();
	ImGui::Text(T(TKEY("final_shading"), "Final Shading"));

	ImGui::Checkbox(T(TKEY("restir_final_visibility"), "Final Visibility"), &giSettings.EnableFinalVisibility);

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("restir_final_visibility_tooltip"), "Trace a visibility ray for the final GI sample to remove shadows."));

	ImGui::Checkbox(T(TKEY("restir_final_mis"), "Final MIS"), &giSettings.EnableFinalMIS);

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("restir_final_mis_tooltip"), "Apply multiple importance sampling with the initial sample."));

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawExperimentalSettings()
{
	if (!ImGui::BeginTabItem(T(TKEY("tab_experimental"), "Experimental")))
		return;

	ImGui::PushID("ExperimentalSettings");

	auto& experimentalSettings = settings.CreationEngineRaytracingSettings.ExperimentalSettings;

	ImGui::Checkbox(T(TKEY("path_tracing_cull"), "Path Tracing Cull"), &experimentalSettings.PathTracingCull);

	DrawEnumRadio(
		T(TKEY("texture_mode"), "Texture Mode"),
		"TextureMode",
		experimentalSettings.TextureMode,
		std::array{
			T(TKEY("texture_mode_share"), "Share"),
			T(TKEY("texture_mode_exclusive"), "Exclusive"),
		});

	if (experimentalSettings.TextureMode == CreationEngineRaytracing::TextureMode::Exclusive) {
		auto neverShare = std::string(T(TKEY("never_share"), "Never Share"));
		auto shareSmaller = std::string_view(T(TKEY("share_smaller_than"), "Share smaller than {}"));
		auto condition = std::to_string(1 << (experimentalSettings.TextureCutOff + 7));

		auto shareConditionLabel = std::vformat(shareSmaller, std::make_format_args(condition));

		auto label = (experimentalSettings.TextureCutOff == 0) ? neverShare : shareConditionLabel;
		ImGui::SliderInt(T(TKEY("exclusive_mode_cutoff"), "Exclusive Mode Cutoff"), reinterpret_cast<int*>(&experimentalSettings.TextureCutOff), 0, 6, label.c_str());
	}

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawDebugSettings()
{
	if (!ImGui::BeginTabItem(T(TKEY("tab_debug"), "Debug")))
		return;

	ImGui::PushID("DebugSettings");

	DrawEnumRadio(
		T(TKEY("performance_overlay"), "Performance Overlay"),
		"PerformanceOverlay",
		settings.PerfOverlay,
		std::array{
			T(TKEY("performance_overlay_none"), "None"),
			T(TKEY("performance_overlay_simple"), "Simple"),
			T(TKEY("performance_overlay_complete"), "Complete"),
		});

	ImGui::Checkbox(T(TKEY("display_scenegraph_counters"), "Display SceneGraph Counters"), &settings.DisplaySceneGraphCounters);

	const auto bufferViewerLabel = StableLabel(T(TKEY("buffer_viewer"), "Buffer Viewer"), "BufferViewer");
	if (ImGui::TreeNode(bufferViewerLabel.c_str())) {
		static float debugRescale = .3f;
		ImGui::SliderFloat(T(TKEY("debug_view_resize"), "View Resize"), &debugRescale, 0.f, 1.f);

		const auto depthLabel = StableLabel(T(TKEY("debug_depth"), "Depth"), "Depth");
		if (ImGui::TreeNode(depthLabel.c_str())) {
			D3D11_TEXTURE2D_DESC desc;
			ID3D11ShaderResourceView* srv = nullptr;

			if (Mode() == CreationEngineRaytracing::Mode::PathTracing) {
				ptDepthTexture->resource11->GetDesc(&desc);
				srv = ptDepthTexture->srv;
			} else {
				const auto& mainDepth = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
				mainDepth.texture->GetDesc(&desc);
				srv = mainDepth.depthSRV;
			}

			ImGui::Image(srv, { desc.Width * debugRescale, desc.Height * debugRescale });
			ImGui::TreePop();
		}

		const auto mainLabel = StableLabel(T(TKEY("debug_main"), "Main"), "Main");
		if (ImGui::TreeNode(mainLabel.c_str())) {
			D3D11_TEXTURE2D_DESC desc;
			mainTexture->resource11->GetDesc(&desc);

			ImGui::Image(mainTexture->srv, { desc.Width * debugRescale, desc.Height * debugRescale });
			ImGui::TreePop();
		}

		const auto normalRoughnessLabel = StableLabel(T(TKEY("debug_normal_roughness"), "NormalRoughness"), "NormalRoughness");
		if (ImGui::TreeNode(normalRoughnessLabel.c_str())) {
			D3D11_TEXTURE2D_DESC desc;
			normalRoughnessTexture->resource11->GetDesc(&desc);

			ImGui::Image(normalRoughnessTexture->srv, { desc.Width * debugRescale, desc.Height * debugRescale });
			ImGui::TreePop();
		}

		const auto diffuseAlbedoLabel = StableLabel(T(TKEY("debug_diffuse_albedo"), "Diffuse Albedo"), "DiffuseAlbedo");
		if (ImGui::TreeNode(diffuseAlbedoLabel.c_str())) {
			D3D11_TEXTURE2D_DESC desc;
			diffuseAlbedoTexture->resource11->GetDesc(&desc);

			ImGui::Image(diffuseAlbedoTexture->srv, { desc.Width * debugRescale, desc.Height * debugRescale });
			ImGui::TreePop();
		}

		const auto masks2Label = StableLabel(T(TKEY("debug_masks2"), "Masks 2"), "Masks2");
		if (ImGui::TreeNode(masks2Label.c_str())) {
			auto renderer = globals::game::renderer;
			auto masks2 = renderer->GetRuntimeData().renderTargets[MASKS2];

			D3D11_TEXTURE2D_DESC desc;
			masks2.texture->GetDesc(&desc);

			ImGui::ImageWithBg(masks2.SRV, { desc.Width * debugRescale, desc.Height * debugRescale }, { 0, 0 }, { 1, 1 }, { 0, 0, 0, 1 });
			ImGui::TreePop();
		}

		const auto flowMapLabel = StableLabel(T(TKEY("debug_flowmap"), "FlowMap"), "FlowMap");
		if (ImGui::TreeNode(flowMapLabel.c_str())) {
			D3D11_TEXTURE2D_DESC desc;
			waterFlowMap->resource11->GetDesc(&desc);

			ImGui::ImageWithBg(waterFlowMap->srv, { desc.Width * debugRescale, desc.Height * debugRescale }, { 0, 0 }, { 1, 1 }, { 0, 0, 0, 1 });
			ImGui::TreePop();
		}

		const auto skyHemisphereLabel = StableLabel(T(TKEY("debug_sky_hemisphere"), "Sky Hemisphere"), "SkyHemisphere");
		if (ImGui::TreeNode(skyHemisphereLabel.c_str())) {
			D3D11_TEXTURE2D_DESC desc;
			skyHemisphere->resource11->GetDesc(&desc);

			ImGui::ImageWithBg(skyHemisphere->srv, { desc.Width * debugRescale, desc.Height * debugRescale }, { 0, 0 }, { 1, 1 }, { 0, 0, 0, 1 });
			ImGui::TreePop();
		}

		ImGui::TreePop();
	}

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawOverlay()
{
	if (!IsOverlayVisible())
		return;

	auto* menu = Menu::GetSingleton();

	if (!globals::state || !menu)
		return;

	// Set window flags - no decoration and only movable when ShowBorder is true
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;

	// Only allow mouse interaction when the main menu is open
	if (!menu->IsEnabled) {
		windowFlags |= ImGuiWindowFlags_NoInputs;
	}

	windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;

	if (!PositionSet) {
		Position = ImVec2(10, 10);
		ImGui::SetNextWindowPos(Position);
		PositionSet = true;
	} else {
		ImGui::SetNextWindowPos(Position, ImGuiCond_FirstUseEver);
	}

	const auto overlayTitle = StableLabel(T(TKEY("overlay_title"), "Raytracing Overlay"), "RaytracingOverlay");
	ImGui::Begin(overlayTitle.c_str(), NULL, windowFlags);

	auto DrawRow = [](const char* label, float gpums) {
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label);

		ImGui::TableNextColumn();
		ImGui::Text(T(TKEY("overlay_gpu_ms"), "%g ms"), gpums);
	};

	if (ImGui::BeginTable("Passes", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn(T(TKEY("overlay_pass"), "Pass"));
		ImGui::TableSetupColumn(T(TKEY("overlay_gpu"), "GPU"));
		ImGui::TableHeadersRow();

		float totalTime = 0.0f;

		for (const auto& passTiming : passTimings) {
			if (settings.PerfOverlay == OverlayMode::Complete)
				DrawRow(passTiming.name.c_str(), passTiming.timing);

			totalTime += passTiming.timing;
		}

		DrawRow(T(TKEY("overlay_total"), "Total"), totalTime);

		ImGui::EndTable();
	}

	// Display accumulated frame count when using Accumulation denoiser
	if (settings.CreationEngineRaytracingSettings.GeneralSettings.Denoiser == CreationEngineRaytracing::Denoiser::Accumulation &&
		creationEngineRaytracing && creationEngineRaytracing->GetAccumulatedFrameCount) {
		uint32_t accumulatedFrames = creationEngineRaytracing->GetAccumulatedFrameCount();
		ImGui::Text(T(TKEY("overlay_accumulated_frames"), "Accumulated Frames: %u"), accumulatedFrames);
	}

	if (settings.DisplaySceneGraphCounters && creationEngineRaytracing) {
		uint32_t textures = 0;
		uint32_t models = 0;
		uint32_t instances = 0;

		creationEngineRaytracing->GetSceneGraphCounters(textures, models, instances);

		ImGui::Text(T(TKEY("overlay_textures"), "Textures %zu"), textures);
		ImGui::Text(T(TKEY("overlay_models"), "Models %zu"), models);
		ImGui::Text(T(TKEY("overlay_instances"), "Instances %zu"), instances);
	}

	ImGui::End();
}

#undef I18N_KEY_PREFIX

bool Raytracing::Available(bool a_initialized) const
{
	if (!loaded)
		return false;

	if (forcedDisabled)
		return false;

	if (!settings.CreationEngineRaytracingSettings.Enabled)
		return false;

	if (a_initialized && !initialized)
		return false;

	return true;
}

void Raytracing::Load()
{
	if (forcedDisabled)
		return;

	Hooks::Install();
}

void Raytracing::PostPostLoad()
{
	creationEngineRaytracing = eastl::make_unique<CreationEngineRaytracing>();

	if (!creationEngineRaytracing->handle) {
		settings.CreationEngineRaytracingSettings.Enabled = false;
		forcedDisabled = true;
		disableReason = DisableReason::MissingPlugin;
		return;
	}

	RE::GetINISetting("bReflectLODLand:Water")->data.b = false;
	RE::GetINISetting("bReflectLODObjects:Water")->data.b = false;
	RE::GetINISetting("bReflectLODTrees:Water")->data.b = false;
	RE::GetINISetting("bReflectSky:Water")->data.b = true;
}

void Raytracing::DataLoaded()
{
	if (forcedDisabled)
		return;

	BGSActorCellEventHandler::Register();
}

void Raytracing::CompileShaders()
{
	const auto skyHemiSize = std::to_string(SKY_HEMI_SIZE);
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CubeToHemiCS.hlsl", { { "RESOLUTION", skyHemiSize.c_str() } }, "cs_5_0")); rawPtr)
		cubeToHemiCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\PTCompositeCS.hlsl", {}, "cs_5_0")); rawPtr)
		ptCompositeCS.attach(rawPtr);

	auto compileConvertTexturesCS = [&](bool rayReconstruction) {
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\ConvertTexturesCS.hlsl", { { "DLSS_RR", rayReconstruction ? "1" : "0" } }, "cs_5_0")); rawPtr)
			convertTexturesCS[rayReconstruction ? 1 : 0].attach(rawPtr);
	};

	compileConvertTexturesCS(false);
	compileConvertTexturesCS(true);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\GICompositeCS.hlsl", {}, "cs_5_0")); rawPtr)
		giCompositeCS.attach(rawPtr);

	// Depth copy
	{
		if (auto rawPtr = reinterpret_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CopyDepth.hlsl", {}, "vs_5_0", "MainVS")); rawPtr)
			copyDepthVS.attach(rawPtr);

		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CopyDepth.hlsl", {}, "ps_5_0", "MainPS")); rawPtr)
			copyDepthPS.attach(rawPtr);
	}
}

void Raytracing::InitializeCERaytracing(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
	if (forcedDisabled)
		return;

	if (initialized)
		return;

	bool result = creationEngineRaytracing->InitializeRenderer(d3d11Device, d3d12Device, commandQueue, computeCommandQueue, copyCommandQueue);

	if (!result) {
		settings.CreationEngineRaytracingSettings.Enabled = false;
		initialized = false;
		forcedDisabled = true;
		disableReason = DisableReason::InitFailed;

		logger::error("[Raytracing] Failed to initialize Creation Engine ray tracing.");
		return;
	}

	initialized = true;

	UpdateResolution();

	logger::info("[Raytracing] Successfully initialized Creation Engine ray tracing.");
}

bool Raytracing::UpdateResolution()
{
	uint2 resolution = { globals::game::graphicsState->screenWidth, globals::game::graphicsState->screenHeight };

	if (resolution == m_Resolution)
		return false;

	m_Resolution = resolution;

	creationEngineRaytracing->SetResolution(m_Resolution.x, m_Resolution.y);

	return true;
}

void Raytracing::UpdateJitter(float2 jitter)
{
	creationEngineRaytracing->UpdateJitter(jitter);
}

void ShareTexture(ID3D11Texture2D* d3d11Texture, ID3D12Resource** d3d12Resource, bool nt = false, uint accessFlags = DXGI_SHARED_RESOURCE_READ)  // DXGI_SHARED_RESOURCE_WRITE
{
	D3D11_TEXTURE2D_DESC desc;
	d3d11Texture->GetDesc(&desc);

	IDXGIResource1* dxgiResource;
	DX::ThrowIfFailed(d3d11Texture->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

	HANDLE sharedHandle = nullptr;

	if (nt)
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, accessFlags, nullptr, &sharedHandle));
	else
		DX::ThrowIfFailed(dxgiResource->GetSharedHandle(&sharedHandle));

	DX::ThrowIfFailed(globals::dx12Interop->d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(d3d12Resource)));

	// Only close handle if it was created here
	if (nt)
		CloseHandle(sharedHandle);
}

void Raytracing::SetupResources()
{
	if (forcedDisabled)
		return;

	auto renderer = globals::game::renderer;

	D3D11_TEXTURE2D_DESC mainDesc;
	auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	mainTex.texture->GetDesc(&mainDesc);

	// Gbuffer Textures
	ShareTexture(renderer->GetRuntimeData().renderTargets[ALBEDO].texture, albedoTexture.put());
	ShareTexture(renderer->GetRuntimeData().renderTargets[MASKS2].texture, gnmaoTexture.put());

	// Shared Textures
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = mainDesc.Width;
		texDesc.Height = mainDesc.Height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		// Normal Roughness Texture
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
		normalRoughnessTexture = eastl::make_unique<WrappedResource>(texDesc);
	}

	if (initialized) {
		creationEngineRaytracing->Initialize(GetSettings());

		creationEngineRaytracing->SetResolution(mainDesc.Width, mainDesc.Height);
		creationEngineRaytracing->SetSharedTextures(albedoTexture.get(), normalRoughnessTexture->GetResource(), gnmaoTexture.get());

		// Diffuse Albedo Texture
		{
			CreationEngineRaytracing::SharedTexture main;
			CreationEngineRaytracing::SharedTexture diffuseAlbedo;
			creationEngineRaytracing->GetSharedTextures(main, diffuseAlbedo);

			mainTexture = eastl::make_unique<WrappedResource>(main.native, main.shared);
			diffuseAlbedoTexture = eastl::make_unique<WrappedResource>(diffuseAlbedo.native, diffuseAlbedo.shared);
		}
	}

	auto& d3d11Device = globals::dx12Interop->d3d11Device;

	featureData = eastl::make_unique<FeatureData>();

	screenCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<ScreenData>());

	screenData = eastl::make_unique<ScreenData>();

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
		DX::ThrowIfFailed(d3d11Device->CreateSamplerState(&samplerDesc, samplerState.put()));
	}

	// PT Depth/MV Copy
	{
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		blendDesc.RenderTarget[0].BlendEnable = false;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, copyBlendState.put()));

		// Create rasterizer state for fullscreen rendering
		D3D11_RASTERIZER_DESC rasterizerDesc = {};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.FrontCounterClockwise = false;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.0f;
		rasterizerDesc.SlopeScaledDepthBias = 0.0f;
		rasterizerDesc.DepthClipEnable = false;
		rasterizerDesc.ScissorEnable = false;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = false;
		DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rasterizerDesc, copyRasterizerState.put()));

		D3D11_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		dsDesc.StencilEnable = false;  // Disable stencil testing
		d3d11Device->CreateDepthStencilState(&dsDesc, depthStencilState.put());
	}

	// Sky Hemisphere
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = SKY_HEMI_SIZE;
		texDesc.Height = SKY_HEMI_SIZE;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		skyHemisphere = eastl::make_unique<WrappedResource>(texDesc);
		DX::ThrowIfFailed(skyHemisphere->GetResource()->SetName(L"Sky Hemisphere"));

		// Setup TESWaterReflections
		waterReflections = RE::NiPointer(new RE::TESWaterReflections());

		waterReflections->flags.set(true, RE::TESWaterReflections::Flags::kDirty, RE::TESWaterReflections::Flags::kDynamicCubemap, RE::TESWaterReflections::Flags::kWorldOrigin);

		for (uint i = 0; i < 6; i++) {
			waterReflections->cubeMapSides[i] = RE::TESWaterReflections::CubeMapSide(i, 0.0f);
		}

		creationEngineRaytracing->SetSkyHemisphere(skyHemisphere->GetResource());
	}

	// Water FlowMap
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = WATER_FLOWMAP_SIZE;
		texDesc.Height = WATER_FLOWMAP_SIZE;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

		waterFlowMap = eastl::make_unique<WrappedResource>(texDesc);
		DX::ThrowIfFailed(waterFlowMap->GetResource()->SetName(L"Water FlowMap"));

		creationEngineRaytracing->SetWaterFlowMap(waterFlowMap->GetResource());
	}

	CompileShaders();
}

Raytracing::SharedData Raytracing::GetCommonBufferData() const
{
	const bool pathTracingEnabled = settings.CreationEngineRaytracingSettings.Enabled &&
	                                settings.CreationEngineRaytracingSettings.GeneralSettings.Mode == CreationEngineRaytracing::Mode::PathTracing;

	return {
		.InteriorDirectional = settings.CreationEngineRaytracingSettings.Enabled ? 0.0f : 1.0f,
		.Ambient = settings.CreationEngineRaytracingSettings.Enabled ? 0.0f : 1.0f,
		.EnvMap = settings.CreationEngineRaytracingSettings.Enabled ? 0.0f : 1.0f,
		.Albedo = settings.CreationEngineRaytracingSettings.Enabled ? 1u : 0u,
		.PathTracing = pathTracingEnabled ? 1u : 0u,
	};
}

void Raytracing::UpdateFeatureData()
{
	auto wetnessEffect = globals::features::wetnessEffects.GetCommonBufferData();
	auto linearLighting = globals::features::linearLighting.GetCommonBufferData();
	auto skinData = globals::features::skin.GetCommonBufferData();

	std::memcpy(&featureData->ExtendedMaterials, &globals::features::extendedMaterials.settings, sizeof(ExtendedMaterials::Settings));
	std::memcpy(&featureData->WetnessEffects, &wetnessEffect, sizeof(WetnessEffects::PerFrame));
	std::memcpy(&featureData->CloudShadows, &globals::features::cloudShadows.settings, sizeof(CloudShadows::Settings));
	std::memcpy(&featureData->HairSpecular, &globals::features::hairSpecular.settings, sizeof(HairSpecular::Settings));
	std::memcpy(&featureData->ExtendedTranslucency, &globals::features::extendedTranslucency.GetCommonBufferData(), sizeof(ExtendedTranslucency::PerFrame));
	std::memcpy(&featureData->LinearLighting, &linearLighting, sizeof(LinearLighting::PerFrameData));
	std::memcpy(&featureData->ExponentialHeightFog, &globals::features::exponentialHeightFog.settings, sizeof(ExponentialHeightFog::Settings));
	std::memcpy(&featureData->LODBlending, &globals::features::lodBlending.settings, sizeof(LODBlending::Settings));
	std::memcpy(&featureData->Skin, &skinData, sizeof(Skin::SkinData));

	static_assert(sizeof(FeatureData::ExtendedMaterials) == sizeof(ExtendedMaterials::Settings));
	static_assert(sizeof(FeatureData::WetnessEffects) == sizeof(WetnessEffects::PerFrame));
	static_assert(sizeof(FeatureData::CloudShadows) == sizeof(CloudShadows::Settings));
	static_assert(sizeof(FeatureData::HairSpecular) == sizeof(HairSpecular::Settings));
	static_assert(sizeof(FeatureData::ExtendedTranslucency) == sizeof(ExtendedTranslucency::PerFrame));
	static_assert(sizeof(FeatureData::LinearLighting) == sizeof(LinearLighting::PerFrameData));
	static_assert(sizeof(FeatureData::ExponentialHeightFog) == sizeof(ExponentialHeightFog::Settings));
	static_assert(sizeof(FeatureData::LODBlending) == sizeof(LODBlending::Settings));
	static_assert(sizeof(FeatureData::Skin) == sizeof(Skin::SkinData));

	creationEngineRaytracing->UpdateFeatureData(featureData.get(), sizeof(FeatureData));
}

void Raytracing::UpdateSkinDetailNormal(ID3D11Texture2D* skinDetailNormalTextureD3D11)
{
	if (!initialized || !skinDetailNormalTextureD3D11 || !creationEngineRaytracing->SetSkinDetailNormal)
		return;

	D3D11_TEXTURE2D_DESC desc;
	skinDetailNormalTextureD3D11->GetDesc(&desc);

	ShareTexture(skinDetailNormalTextureD3D11, skinDetailNormalTexture.put());

	creationEngineRaytracing->SetSkinDetailNormal(skinDetailNormalTexture.get());
}

void Raytracing::SkyCubeToHemi() const
{
	auto context = globals::d3d::context;

	context->CSSetShader(cubeToHemiCS.get(), nullptr, 0);

	auto reflections = globals::game::renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];
	auto reflectionOcc = globals::features::cloudShadows.loaded ? globals::features::cloudShadows.texCubemapCloudOccCopy->srv.get() : nullptr;

	ID3D11ShaderResourceView* srvs[] = {
		reflections.SRV,
		reflectionOcc
	};
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	auto sampler = samplerState.get();
	context->CSSetSamplers(0, 1, &sampler);

	ID3D11UnorderedAccessView* uav = skyHemisphere->uav;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	uint dispatch = (uint)std::ceil(SKY_HEMI_SIZE / 8.0f);
	context->Dispatch(dispatch, dispatch, 1);

	uav = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
}

void Raytracing::ConvertTextures()
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	ID3D11Buffer* cb = screenCB->CB();
	context->CSSetConstantBuffers(0, 1, &cb);

	auto* frameBufferCB = *globals::game::perFrame.get();
	context->CSSetConstantBuffers(12, 1, &frameBufferCB);

	bool isRayReconstruction = globals::features::upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS_RR;

	uint shaderIndex = isRayReconstruction ? 1 : 0;
	context->CSSetShader(convertTexturesCS[shaderIndex].get(), nullptr, 0);

	auto normalSmoothness = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
	auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
	auto gnmao = renderer->GetRuntimeData().renderTargets[MASKS2];

	ID3D11ShaderResourceView* srvs[] = {
		normalSmoothness.SRV,
		albedo.SRV,
		gnmao.SRV
	};

	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	auto sampler = samplerState.get();
	context->CSSetSamplers(0, 1, &sampler);

	ID3D11UnorderedAccessView* uavs[] = {
		normalRoughnessTexture->uav,
		diffuseAlbedoTexture->uav
	};

	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	auto dispatchCount = Util::GetScreenDispatchCount(true);
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

	uavs[0] = nullptr;
	uavs[1] = nullptr;

	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
}

void Raytracing::DeferredPasses()
{
	if (!settings.CreationEngineRaytracingSettings.Enabled || Mode() == CreationEngineRaytracing::Mode::None)
		return;

	auto* context = globals::d3d::context;

	bool resolutionChanged = UpdateResolution();

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = m_Resolution.x;
	desc.Height = m_Resolution.y;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	if (Mode() == CreationEngineRaytracing::Mode::PathTracing) {
		if (!ptDepthTexture || resolutionChanged) {
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			ptDepthTexture = eastl::make_unique<WrappedResource>(desc);
		}
		if (!ptMotionVectorsTexture || resolutionChanged) {
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			ptMotionVectorsTexture = eastl::make_unique<WrappedResource>(desc);
		}
		creationEngineRaytracing->SetPTOutputTargets(ptDepthTexture->GetResource(), ptMotionVectorsTexture->GetResource());
	} else {
		ptDepthTexture.reset();
		ptMotionVectorsTexture.reset();
	}

	if (Mode() == CreationEngineRaytracing::Mode::GlobalIllumination) {
		ConvertTextures();

		globals::dx12Interop->Fence([&]() {
			// Executes the render graph for Global Illumination, depends on gbuffer render targets so we call it late
			creationEngineRaytracing->Execute();
		});
	}

	// Waits for execution to finish (blocks CPU)
	// TODO: Implement double buffering to avoid stalling the CPU while waiting for GPU results
	creationEngineRaytracing->WaitExecution();

	if (settings.PerfOverlay != OverlayMode::None)
		creationEngineRaytracing->GetPassTimings(passTimings);

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto dynamicScreenSize = Util::ConvertToDynamic(screenSize);

	screenData->Resolution = { static_cast<uint>(screenSize.x), static_cast<uint>(screenSize.y) };
	screenData->DynamicResolution = { static_cast<uint>(dynamicScreenSize.x), static_cast<uint>(dynamicScreenSize.y) };

	screenCB->Update(screenData.get(), sizeof(ScreenData));

	auto mode = settings.CreationEngineRaytracingSettings.GeneralSettings.Mode;

	auto renderer = globals::game::renderer;

	const auto& renderTargets = renderer->GetRuntimeData().renderTargets;

	auto& main = renderTargets[RE::RENDER_TARGETS::kMAIN];

	const bool globalIllumation = (mode == CreationEngineRaytracing::Mode::GlobalIllumination);
	const bool pathtracing = (mode == CreationEngineRaytracing::Mode::PathTracing);

	// Fog management
	static auto& enableFog = (*(bool*)REL::RelocationID(528125, 415070).address());
	if (enableFog)
		fogEnabled = true;

	enableFog = settings.DisableVanillaFogPT && pathtracing ? false : fogEnabled;

	if (globalIllumation) {
		// Add global illumination result to kMain
		{
			context->CSSetShader(giCompositeCS.get(), nullptr, 0);

			ID3D11Buffer* cb = screenCB->CB();
			context->CSSetConstantBuffers(0, 1, &cb);

			context->CSSetShaderResources(0, 1, &mainTexture->srv);

			ID3D11UnorderedAccessView* uav = main.UAV;
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

			auto dispatchCount = Util::GetScreenDispatchCount(true);
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

			uav = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		}
	} else if (pathtracing) {
		// Blend pathtracing and sky (colors and motion vectors)
		{
			auto& mv = renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

			context->CSSetShader(ptCompositeCS.get(), nullptr, 0);

			ID3D11Buffer* cb = screenCB->CB();
			context->CSSetConstantBuffers(0, 1, &cb);

			ID3D11ShaderResourceView* srvs[] = {
				mainTexture->srv,
				ptMotionVectorsTexture->srv
			};
			context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

			ID3D11UnorderedAccessView* uavs[] = {
				main.UAV,
				mv.UAV
			};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			auto dispatchCount = Util::GetScreenDispatchCount(true);
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

			uavs[0] = nullptr;
			uavs[1] = nullptr;
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
		}

		// Copy Depth buffer
		{
			auto depthStencils = renderer->GetDepthStencilData().depthStencils;

			auto& mainDepth = depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			auto& mainDepthCopy = depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
			auto& zPrePassCopy = depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

			context->ClearDepthStencilView(mainDepth.views[0], D3D11_CLEAR_DEPTH, 1.0f, 0u);
			context->ClearDepthStencilView(mainDepthCopy.views[0], D3D11_CLEAR_DEPTH, 1.0f, 0u);
			context->ClearDepthStencilView(zPrePassCopy.views[0], D3D11_CLEAR_DEPTH, 1.0f, 0u);

			ID3D11DepthStencilState* oldDSS = nullptr;
			UINT oldRef = 0;

			context->OMGetDepthStencilState(&oldDSS, &oldRef);

			ID3D11RenderTargetView* oldRTV;
			ID3D11DepthStencilView* oldDSV;
			context->OMGetRenderTargets(1, &oldRTV, &oldDSV);

			UINT numViewports = 1;
			D3D11_VIEWPORT oldViewport = {};
			context->RSGetViewports(&numViewports, &oldViewport);

			D3D11_VIEWPORT viewport = {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = screenSize.x;
			viewport.Height = screenSize.y;
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			context->RSSetViewports(1, &viewport);

			context->IASetInputLayout(nullptr);
			context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
			context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			context->OMSetRenderTargets(0, nullptr, mainDepth.views[0]);

			context->OMSetDepthStencilState(depthStencilState.get(), 0);

			// Set up rasterizer and blend states
			context->RSSetState(copyRasterizerState.get());
			context->OMSetBlendState(copyBlendState.get(), nullptr, 0xffffffff);

			// Set up vertex shader
			context->VSSetShader(copyDepthVS.get(), nullptr, 0);

			// Set up pixel shader
			context->PSSetShader(copyDepthPS.get(), nullptr, 0);

			ID3D11ShaderResourceView* srvs[] = { ptDepthTexture->srv };

			context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

			context->Draw(3, 0);

			context->OMSetDepthStencilState(oldDSS, oldRef);

			context->OMSetRenderTargets(1,
				&oldRTV,
				oldDSV);

			context->RSSetViewports(1, &oldViewport);

			if (oldDSS) {
				oldDSS->Release();
				oldDSS = nullptr;
			}

			if (oldRTV) {
				oldRTV->Release();
				oldRTV = nullptr;
			}

			if (oldDSV) {
				oldDSV->Release();
				oldDSV = nullptr;
			}

			context->PSSetShader(nullptr, nullptr, 0);
			context->VSSetShader(nullptr, nullptr, 0);

			context->CopyResource(mainDepthCopy.texture, mainDepth.texture);
			context->CopyResource(zPrePassCopy.texture, mainDepth.texture);
		}

		// Clear Specular render target
		{
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			context->ClearRenderTargetView(renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV, clearColor);
		}
	}
}

void Raytracing::GetRayReconstructionInputs(ID3D12Resource*& diffuseAlbedo, ID3D12Resource*& specularAlbedo, ID3D12Resource*& normalRoughness, ID3D12Resource*& specHitDistance)
{
	if (Mode() != CreationEngineRaytracing::Mode::GlobalIllumination && Mode() != CreationEngineRaytracing::Mode::PathTracing)
		return;

	diffuseAlbedo = diffuseAlbedoTexture->GetResource();
	normalRoughness = normalRoughnessTexture->GetResource();

	creationEngineRaytracing->GetRRInput(specularAlbedo, specHitDistance);
}

RE::BSEventNotifyControl Raytracing::BGSActorCellEventHandler::ProcessEvent(const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*)
{
	if (a_event->flags.underlying() != static_cast<uint32_t>(RE::BGSActorCellEvent::CellFlag::kEnter))
		return RE::BSEventNotifyControl::kContinue;

	auto* tesWaterSystem = RE::TESWaterSystem::GetSingleton();

	if (tesWaterSystem->waterReflections.empty()) {
		tesWaterSystem->waterReflections.push_back(globals::features::raytracing.waterReflections);
	}

	tesWaterSystem->Enable();

	return RE::BSEventNotifyControl::kContinue;
}
