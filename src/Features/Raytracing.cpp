#include "Raytracing.h"

#include "Globals.h"
#include "State.h"

// Microsoft Pix
#include <filesystem>
#include <shlobj.h>
#include <windows.h>

#include "DX12Interop.h"

#include "Deferred.h"
#include "Features/Upscaling.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Raytracing::Settings,
	PerfOverlay,
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

template <typename T>
	requires std::is_enum_v<T>
static bool DrawEnumRadio(const char* label, T& variable, const char* tooltip = nullptr, const char* const* tooltips = nullptr)
{
	ImGui::PushID(label);

	auto variablePrev = variable;

	int denoiser = static_cast<int32_t>(variable);
	ImGui::TextUnformatted(label);

	if (tooltip != nullptr)
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", tooltip);

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(25, 0));

	auto i = 0;

	for (auto& [value, name] : magic_enum::enum_entries<T>()) {
		ImGui::SameLine();
		ImGui::RadioButton(name.data(), &denoiser, static_cast<int32_t>(value));

		if (tooltips != nullptr)
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", tooltips[i]);

		i++;
	}

	ImGui::PopID();

	variable = static_cast<T>(denoiser);

	return variable != variablePrev;
}

template <typename T>
	requires std::is_enum_v<T>
static bool DrawEnumCombo(const char* label, T& variable, const char* tooltip = nullptr, const char* const* tooltips = nullptr)
{
	ImGui::PushID(label);

	auto variablePrev = variable;

	if (ImGui::BeginCombo(label, magic_enum::enum_name(variable).data())) {
		auto i = 0;

		for (auto& value : magic_enum::enum_values<T>()) {
			bool isSelected = (variable == value);

			if (ImGui::Selectable(magic_enum::enum_name(value).data(), isSelected))
				variable = value;

			if (tooltips != nullptr)
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("%s", tooltips[i]);

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

void Raytracing::DrawSettings()
{
	bool forcedDisabledReason = disableReason != DisableReason::None;

	if (forcedDisabledReason)
		ImGui::BeginDisabled();

	auto ceRTSettingsBefore = settings.CreationEngineRaytracingSettings;

	ImGui::Checkbox("Enabled", &settings.CreationEngineRaytracingSettings.Enabled);

	DrawEnumRadio("Mode", settings.CreationEngineRaytracingSettings.GeneralSettings.Mode);

	DrawEnumRadio("Denoiser", settings.CreationEngineRaytracingSettings.GeneralSettings.Denoiser);

	// Show DLSS RR availability status
	if (settings.CreationEngineRaytracingSettings.GeneralSettings.Denoiser == CreationEngineRaytracing::Denoiser::DLSS_RR) {
		if (!(Upscaling::streamline.loadedFeatures & Streamline::Features::kDLSS_RR)) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DLSS Ray Reconstruction is not available on this system.");
		} else if (globals::features::upscaling.settings.upscaleMethod != Upscaling::UpscaleMethod::kDLSS) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Set Upscaling method to DLSS to enable Ray Reconstruction.");
		}
	}

	bool ptMode = settings.CreationEngineRaytracingSettings.GeneralSettings.Mode == CreationEngineRaytracing::Mode::PathTracing;

	if (ptMode)
		ImGui::BeginDisabled();

	ImGui::Checkbox("Raytraced Shadows", &settings.CreationEngineRaytracingSettings.GeneralSettings.RaytracedShadows);

	if (ptMode)
		ImGui::EndDisabled();

	if (forcedDisabledReason)
		ImGui::EndDisabled();

	if (forcedDisabledReason) {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Ray tracing is disabled: %s", [&]() {
			switch (disableReason) {
			case DisableReason::UnsupportedGPU:
				return "Unsupported GPU.";
			case DisableReason::OutdatedDrivers:
				return "Outdated Drivers.";
			case DisableReason::MissingPlugin:
				return "Missing 'CreationEngineRaytracing.dll', check your mod manager.";
			case DisableReason::InitFailed:
				return "Initialization Failed, check CreationEngineRaytracing.txt log";
			default:
				return "Unknown Reason";
			}
		}());
	}

	if (ImGui::BeginTabBar("Settings")) {
		DrawGeneralSettings();
		DrawAdvancedSettings();
		DrawReSTIRGISettings();
		DrawExperimentalSettings();
		DrawDebugSettings();

		ImGui::EndTabBar();
	}

	if (ceRTSettingsBefore != settings.CreationEngineRaytracingSettings)
		UpdateSettings();
}

CreationEngineRaytracing::Settings Raytracing::GetSettings() const
{
	auto certSettings = settings.CreationEngineRaytracingSettings;

	certSettings.DebugSettings.Markers = globals::features::dx12Interop.settings.EnablePIXCapture;
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
	if (!ImGui::BeginTabItem("General"))
		return;

	ImGui::PushID("GeneralSettings");

	auto& ceRTSettings = settings.CreationEngineRaytracingSettings;

	// RT
	{
		auto& rtSettings = ceRTSettings.RaytracingSettings;

		// Bounces
		if (ImGui::SliderInt("Bounces", &rtSettings.Bounces, 1, 32))
			rtSettings.Bounces = std::clamp(rtSettings.Bounces, 1, 32);

		// Samples Per Pixel
		if (ImGui::SliderInt("Samples Per Pixel", &rtSettings.SamplesPerPixel, 1, 32))
			rtSettings.SamplesPerPixel = std::clamp(rtSettings.SamplesPerPixel, 1, 32);
	}

	if (ceRTSettings.GeneralSettings.Denoiser == CreationEngineRaytracing::Denoiser::NRD_REBLUR)
		DrawReblurSettings();

	DrawSHaRCSettings();

	// Material
	DrawFloat2("Roughness", ceRTSettings.MaterialSettings.Roughness);
	DrawFloat2("Metalness", ceRTSettings.MaterialSettings.Metalness);

	if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
		auto& lightingSettings = ceRTSettings.LightingSettings;

		if (ImGui::DragFloat("Directional Strength", &lightingSettings.Directional, 0.001f))
			lightingSettings.Directional = std::max(0.0f, lightingSettings.Directional);

		if (ImGui::DragFloat("Point Strength", &lightingSettings.Point, 0.001f))
			lightingSettings.Point = std::max(0.0f, lightingSettings.Point);

		ImGui::Checkbox("Lod Dimmer", &lightingSettings.LodDimmer);

		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Vanilla behaviour of dimming lights that are far enough.\n");

		if (ImGui::DragFloat("Emissive Strength", &lightingSettings.Emissive, 0.001f))
			lightingSettings.Emissive = std::max(0.0f, lightingSettings.Emissive);

		if (ImGui::DragFloat("Effect Strength", &lightingSettings.Effect, 0.001f))
			lightingSettings.Effect = std::max(0.0f, lightingSettings.Effect);

		if (ImGui::DragFloat("Sky Strength", &lightingSettings.Sky, 0.001f))
			lightingSettings.Sky = std::max(0.0f, lightingSettings.Sky);
	}

	if (ImGui::CollapsingHeader("Water")) {
		auto& waterSettings = ceRTSettings.WaterSettings;

		if (ImGui::DragFloat("Absorption Scale", &waterSettings.AbsorptionScale, 0.01f, 0.01f, 10.0f, "%.2f"))
			waterSettings.AbsorptionScale = std::clamp(waterSettings.AbsorptionScale, 0.01f, 10.0f);
	}

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawReblurSettings()
{
	if (!ImGui::CollapsingHeader("Reblur")) {
		return;
	}

	auto& reblurSettings = settings.CreationEngineRaytracingSettings.ReblurSettings;

	if (ImGui::InputScalar("Max Accumulated Frames", ImGuiDataType_U32, &reblurSettings.maxAccumulatedFrameNum))
		ClampSetting(reblurSettings.maxAccumulatedFrameNum, 0u, 63u);

	if (ImGui::InputScalar("Max Fast Accumulated Frames", ImGuiDataType_U32, &reblurSettings.maxFastAccumulatedFrameNum))
		ClampSetting(reblurSettings.maxFastAccumulatedFrameNum, 0u, reblurSettings.maxAccumulatedFrameNum);

	if (ImGui::InputScalar("Max Stabilized Frames", ImGuiDataType_U32, &reblurSettings.maxStabilizedFrameNum))
		ClampSetting(reblurSettings.maxStabilizedFrameNum, 0u, reblurSettings.maxAccumulatedFrameNum);

	if (ImGui::InputScalar("History Fix Frames", ImGuiDataType_U32, &reblurSettings.historyFixFrameNum))
		ClampSetting(reblurSettings.historyFixFrameNum, 0u, reblurSettings.maxFastAccumulatedFrameNum);

	if (ImGui::InputScalar("History Fix Base Pixel Stride", ImGuiDataType_U32, &reblurSettings.historyFixBasePixelStride))
		ClampSetting(reblurSettings.historyFixBasePixelStride, 1u, 64u);

	if (ImGui::InputScalar("History Fix Alternate Pixel Stride", ImGuiDataType_U32, &reblurSettings.historyFixAlternatePixelStride))
		ClampSetting(reblurSettings.historyFixAlternatePixelStride, 1u, 64u);

	if (ImGui::SliderFloat("Fast History Clamping Sigma Scale", &reblurSettings.fastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f"))
		ClampSetting(reblurSettings.fastHistoryClampingSigmaScale, 1.0f, 3.0f);

	if (ImGui::SliderFloat("Diffuse Prepass Blur Radius", &reblurSettings.diffusePrepassBlurRadius, 0.0f, 100.0f, "%.1f"))
		ClampSetting(reblurSettings.diffusePrepassBlurRadius, 0.0f, 100.0f);

	if (ImGui::SliderFloat("Specular Prepass Blur Radius", &reblurSettings.specularPrepassBlurRadius, 0.0f, 100.0f, "%.1f"))
		ClampSetting(reblurSettings.specularPrepassBlurRadius, 0.0f, 100.0f);

	if (ImGui::SliderFloat("Min Hit Distance Weight", &reblurSettings.minHitDistanceWeight, 0.001f, 0.2f, "%.3f"))
		ClampSetting(reblurSettings.minHitDistanceWeight, 0.001f, 0.2f);

	if (ImGui::SliderFloat("Min Blur Radius", &reblurSettings.minBlurRadius, 0.0f, 10.0f, "%.2f"))
		ClampSetting(reblurSettings.minBlurRadius, 0.0f, 10.0f);

	if (ImGui::SliderFloat("Max Blur Radius", &reblurSettings.maxBlurRadius, 0.0f, 100.0f, "%.1f"))
		ClampSetting(reblurSettings.maxBlurRadius, 0.0f, 100.0f);

	if (ImGui::SliderFloat("Lobe Angle Fraction", &reblurSettings.lobeAngleFraction, 0.0f, 1.0f, "%.3f"))
		ClampSetting(reblurSettings.lobeAngleFraction, 0.0f, 1.0f);

	if (ImGui::SliderFloat("Roughness Fraction", &reblurSettings.roughnessFraction, 0.0f, 1.0f, "%.3f"))
		ClampSetting(reblurSettings.roughnessFraction, 0.0f, 1.0f);

	if (ImGui::SliderFloat("Plane Distance Sensitivity", &reblurSettings.planeDistanceSensitivity, 0.0f, 1.0f, "%.3f"))
		ClampSetting(reblurSettings.planeDistanceSensitivity, 0.0f, 1.0f);

	if (ImGui::SliderFloat2("Specular Probability Thresholds For MV Modification", reblurSettings.specularProbabilityThresholdsForMvModification.data(), 0.0f, 1.0f, "%.2f")) {
		ClampSetting(reblurSettings.specularProbabilityThresholdsForMvModification[0], 0.0f, 1.0f);
		ClampSetting(reblurSettings.specularProbabilityThresholdsForMvModification[1], reblurSettings.specularProbabilityThresholdsForMvModification[0], 1.0f);
	}

	if (ImGui::SliderFloat("Firefly Suppressor Min Relative Scale", &reblurSettings.fireflySuppressorMinRelativeScale, 1.0f, 3.0f, "%.2f"))
		ClampSetting(reblurSettings.fireflySuppressorMinRelativeScale, 1.0f, 3.0f);

	ImGui::Checkbox("Enable Anti Firefly", &reblurSettings.enableAntiFirefly);
	ImGui::Checkbox("Use Prepass Only For Specular Motion Estimation", &reblurSettings.usePrepassOnlyForSpecularMotionEstimation);
	ImGui::Checkbox("Return History Length Instead Of Occlusion", &reblurSettings.returnHistoryLengthInsteadOfOcclusion);
}

void Raytracing::DrawSHaRCSettings()
{
	if (ImGui::CollapsingHeader("SHaRC")) {
		auto& sharcSettings = settings.CreationEngineRaytracingSettings.SHaRCSettings;

		ImGui::Checkbox("Enabled", &sharcSettings.Enabled);

		if (!sharcSettings.Enabled)
			ImGui::BeginDisabled();

		ImGui::DragFloat("Scale", &sharcSettings.SceneScale, 0.001f, 0.1f, 10.0f);
		sharcSettings.SceneScale = std::clamp(sharcSettings.SceneScale, 0.1f, 10.0f);

		ImGui::InputInt("Accumulation Frames", &sharcSettings.AccumFrameNum);
		sharcSettings.AccumFrameNum = std::clamp(sharcSettings.AccumFrameNum, 5, 100);

		ImGui::InputInt("Stale Frames", &sharcSettings.StaleFrameNum);
		sharcSettings.StaleFrameNum = std::clamp(sharcSettings.StaleFrameNum, 8, 128);

		if (!sharcSettings.Enabled)
			ImGui::EndDisabled();
	}
}

void Raytracing::DrawSSSSettings()
{
	auto& sssSettings = settings.CreationEngineRaytracingSettings.AdvancedSettings.SSSSettings;

	ImGui::Checkbox("Enable Subsurface Scattering", &sssSettings.Enabled);

	if (!sssSettings.Enabled)
		return;

	if (ImGui::CollapsingHeader("Subsurface Scattering")) {
		if (sssSettings.Enabled) {
			ImGui::SliderInt("Sample Count", &sssSettings.SampleCount, 1, 16);
			ImGui::SliderFloat("Max Sample Radius", &sssSettings.MaxSampleRadius, 0.01f, 64.0f, "%.2f");
			ImGui::Checkbox("Enable Transmission", &sssSettings.EnableTransmission);
			ImGui::Checkbox("Material Override", &sssSettings.MaterialOverride);

			if (sssSettings.MaterialOverride) {
				if (ImGui::TreeNodeEx("Subsurface Scattering", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::ColorEdit3("Override Transmission Color", reinterpret_cast<float*>(&sssSettings.OverrideTransmissionColor), ImGuiColorEditFlags_Float);
					ImGui::ColorEdit3("Override Scattering Color", reinterpret_cast<float*>(&sssSettings.OverrideScatteringColor), ImGuiColorEditFlags_Float);
					ImGui::SliderFloat("Override Scale", &sssSettings.OverrideScale, 0.01f, 1000.0f, "%.2f");
					ImGui::SliderFloat("Override Anisotropy", &sssSettings.OverrideAnisotropy, -0.99f, 0.99f);

					ImGui::TreePop();
				}
			}
		}
	}
}

void Raytracing::DrawAdvancedSettings()
{
	if (!ImGui::BeginTabItem("Advanced"))
		return;

	ImGui::PushID("AdvancedSettings");

	auto& advSettings = settings.CreationEngineRaytracingSettings.AdvancedSettings;

	ImGui::SliderFloat("Texture LOD Bias", &advSettings.TexLODBias, -4.0f, 4.0f, "%.1f");

	ImGui::Checkbox("Variable Update Rate", &advSettings.VariableUpdateRate);

	ImGui::Checkbox("GGX Energy Conservation", &advSettings.GGXEnergyConservation);

	ImGui::Checkbox("Per Light Top-Level Acceleration Structures", &advSettings.PerLightTLAS);

	ImGui::Checkbox("Resampled Importance Sampling", &advSettings.RIS.Enabled);

	ImGui::SliderInt("RIS Max Candidates", &advSettings.RIS.MaxCandidates, 2, 16);

	DrawEnumCombo("Hair BSDF", advSettings.HairBSDF);

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Best with hair specular feature enabled.\n");
	}

	DrawSSSSettings();

	DrawEnumCombo("Diffuse BRDF", advSettings.DiffuseBRDF);

	ImGui::Checkbox("Enable Water", &settings.CreationEngineRaytracingSettings.AdvancedSettings.EnableWater);

	ImGui::Checkbox("Stable Planes", &settings.CreationEngineRaytracingSettings.AdvancedSettings.StablePlanes);

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawReSTIRGISettings()
{
	if (!ImGui::BeginTabItem("ReSTIR GI"))
		return;

	ImGui::PushID("ReSTIRGISettings");

	auto& giSettings = settings.CreationEngineRaytracingSettings.ReSTIRGI;

	ImGui::Checkbox("Enabled", &giSettings.Enabled);

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Enable ReSTIR GI indirect lighting resampling.");

	DrawEnumCombo("Resampling Mode", giSettings.ResamplingMode);

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("None: Disabled\nTemporal: Reuse from previous frames\nSpatial: Reuse from nearby pixels\nTemporalAndSpatial: Both (recommended)\nFusedSpatiotemporal: Combined pass");

	ImGui::Separator();
	ImGui::Text("Temporal Resampling");

	if (ImGui::SliderFloat("Temporal Depth Threshold", &giSettings.TemporalDepthThreshold, 0.0f, 1.0f, "%.2f"))
		giSettings.TemporalDepthThreshold = std::clamp(giSettings.TemporalDepthThreshold, 0.0f, 1.0f);

	if (ImGui::SliderFloat("Temporal Normal Threshold", &giSettings.TemporalNormalThreshold, 0.0f, 1.0f, "%.2f"))
		giSettings.TemporalNormalThreshold = std::clamp(giSettings.TemporalNormalThreshold, 0.0f, 1.0f);

	if (ImGui::SliderInt("Max History Length", &giSettings.MaxHistoryLength, 1, 50))
		giSettings.MaxHistoryLength = std::clamp(giSettings.MaxHistoryLength, 1, 50);

	if (ImGui::SliderInt("Max Reservoir Age", &giSettings.MaxReservoirAge, 1, 200))
		giSettings.MaxReservoirAge = std::clamp(giSettings.MaxReservoirAge, 1, 200);

	ImGui::Checkbox("Permutation Sampling", &giSettings.EnablePermutationSampling);
	ImGui::Checkbox("Fallback Sampling", &giSettings.EnableFallbackSampling);

	DrawEnumCombo("Temporal Bias Correction", giSettings.TemporalBiasCorrection);

	ImGui::Separator();
	ImGui::Text("Spatial Resampling");

	if (ImGui::SliderFloat("Spatial Depth Threshold", &giSettings.SpatialDepthThreshold, 0.0f, 1.0f, "%.2f"))
		giSettings.SpatialDepthThreshold = std::clamp(giSettings.SpatialDepthThreshold, 0.0f, 1.0f);

	if (ImGui::SliderFloat("Spatial Normal Threshold", &giSettings.SpatialNormalThreshold, 0.0f, 1.0f, "%.2f"))
		giSettings.SpatialNormalThreshold = std::clamp(giSettings.SpatialNormalThreshold, 0.0f, 1.0f);

	if (ImGui::SliderInt("Spatial Samples", &giSettings.SpatialNumSamples, 0, 8))
		giSettings.SpatialNumSamples = std::clamp(giSettings.SpatialNumSamples, 0, 8);

	if (ImGui::SliderFloat("Spatial Sampling Radius", &giSettings.SpatialSamplingRadius, 1.0f, 64.0f, "%.1f"))
		giSettings.SpatialSamplingRadius = std::clamp(giSettings.SpatialSamplingRadius, 1.0f, 64.0f);

	DrawEnumCombo("Spatial Bias Correction", giSettings.SpatialBiasCorrection);

	ImGui::Separator();
	ImGui::Text("Boiling Filter");

	ImGui::Checkbox("Enable Boiling Filter", &giSettings.EnableBoilingFilter);

	if (giSettings.EnableBoilingFilter) {
		if (ImGui::SliderFloat("Boiling Filter Strength", &giSettings.BoilingFilterStrength, 0.0f, 1.0f, "%.2f"))
			giSettings.BoilingFilterStrength = std::clamp(giSettings.BoilingFilterStrength, 0.0f, 1.0f);
	}

	ImGui::Separator();
	ImGui::Text("Final Shading");

	ImGui::Checkbox("Final Visibility", &giSettings.EnableFinalVisibility);

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Trace a visibility ray for the final GI sample to remove shadows.");

	ImGui::Checkbox("Final MIS", &giSettings.EnableFinalMIS);

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Apply multiple importance sampling with the initial sample.");

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawExperimentalSettings()
{
	if (!ImGui::BeginTabItem("Experimental"))
		return;

	ImGui::PushID("ExperimentalSettings");

	auto& experimentalSettings = settings.CreationEngineRaytracingSettings.ExperimentalSettings;

	ImGui::Checkbox("Path Tracing Cull", &experimentalSettings.PathTracingCull);

	DrawEnumRadio("Texture Mode", experimentalSettings.TextureMode);

	if (experimentalSettings.TextureMode == CreationEngineRaytracing::TextureMode::Exclusive) {
		auto label = experimentalSettings.TextureCutOff == 0 ? "Never Share" : std::format("Share smaller than {}", 1 << (experimentalSettings.TextureCutOff + 7));
		ImGui::SliderInt("Exclusive Mode Cutoff", reinterpret_cast<int*>(&experimentalSettings.TextureCutOff), 0, 13, label.c_str());
	}

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawDebugSettings()
{
	if (!ImGui::BeginTabItem("Debug"))
		return;

	ImGui::PushID("DebugSettings");

	DrawEnumRadio("Performance Overlay", settings.PerfOverlay);

	ImGui::Checkbox("Show Main Texture", &settings.ShowMainTexture);

	if (settings.ShowMainTexture && mainTexture)
		ImGui::Image(mainTexture->srv, { 1280, 720 });

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

	ImGui::Begin("Raytracing Overlay", NULL, windowFlags);

	auto DrawRow = [](const char* label, float gpums) {
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::Text(label);

		ImGui::TableNextColumn();
		ImGui::Text("%g ms", gpums);
	};

	if (ImGui::BeginTable("Passes", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Pass");
		ImGui::TableSetupColumn("GPU");
		ImGui::TableHeadersRow();

		CreationEngineRaytracing::PassTiming* passTimings = nullptr;
		uint32_t numPasses = 0;

		creationEngineRaytracing->GetFrameTime(passTimings, numPasses);

		if (settings.PerfOverlay == OverlayMode::Simple) {
			float totalTime = 0.0f;

			for (size_t i = 0; i < numPasses; i++)
				totalTime += passTimings[i].timing;

			DrawRow("Total", totalTime);
		} else {
			float totalTime = 0.0f;

			for (size_t i = 0; i < numPasses; i++) {
				auto& passTiming = passTimings[i];
				DrawRow(passTiming.name, passTiming.timing);
				totalTime += passTiming.timing;
			}

			DrawRow("Total", totalTime);
		}

		ImGui::EndTable();
	}

	/*ImGui::Text("Textures %zu", instances);
	ImGui::Text("Meshes %zu", instances);
	ImGui::Text("Instances %zu", instances);*/

	ImGui::End();
}

bool Raytracing::Active() const
{
	if (!loaded)
		return false;

	if (!settings.CreationEngineRaytracingSettings.Enabled)
		return false;

	if (!initialized)
		return false;

	return true;
}

void Raytracing::Load()
{
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
}

void Raytracing::InitializeCERaytracing(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
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
	uint2 resolution = { static_cast<uint32_t>(globals::state->screenSize.x), static_cast<uint32_t>(globals::state->screenSize.y) };

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

	DX::ThrowIfFailed(globals::features::dx12Interop.d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(d3d12Resource)));

	CloseHandle(sharedHandle);
}

void Raytracing::SetupResources()
{
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

		// Diffuse Albedo Texture
		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		diffuseAlbedoTexture = eastl::make_unique<WrappedResource>(texDesc);
	}

	if (initialized) {
		creationEngineRaytracing->Initialize(GetSettings());

		creationEngineRaytracing->SetResolution(mainDesc.Width, mainDesc.Height);
		creationEngineRaytracing->SetSharedTextures(albedoTexture.get(), normalRoughnessTexture->resource.get(), gnmaoTexture.get(), diffuseAlbedoTexture->resource.get());
	}

	auto& d3d11Device = globals::features::dx12Interop.d3d11Device;

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
		DX::ThrowIfFailed(skyHemisphere->resource->SetName(L"Sky Hemisphere"));

		// Setup TESWaterReflections
		waterReflections = RE::NiPointer(new RE::TESWaterReflections());

		waterReflections->flags.set(true, RE::TESWaterReflections::Flags::kDirty, RE::TESWaterReflections::Flags::kDynamicCubemap, RE::TESWaterReflections::Flags::kWorldOrigin);

		for (uint i = 0; i < 6; i++) {
			waterReflections->cubeMapSides[i] = RE::TESWaterReflections::CubeMapSide(i, 0.0f);
		}

		creationEngineRaytracing->SetSkyHemisphere(skyHemisphere->resource.get());
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
		.Albedo = settings.CreationEngineRaytracingSettings.Enabled,
		.PathTracing = pathTracingEnabled ? 1u : 0u,
	};
}

void Raytracing::UpdateFeatureData()
{
	auto wetnessEffect = globals::features::wetnessEffects.GetCommonBufferData();
	auto linearLighting = globals::features::linearLighting.GetCommonBufferData();

	std::memcpy(&featureData->ExtendedMaterials, &globals::features::extendedMaterials.settings, sizeof(ExtendedMaterials::Settings));
	std::memcpy(&featureData->WetnessEffects, &wetnessEffect, sizeof(WetnessEffects::PerFrame));
	std::memcpy(&featureData->CloudShadows, &globals::features::cloudShadows.settings, sizeof(CloudShadows::Settings));
	std::memcpy(&featureData->HairSpecular, &globals::features::hairSpecular.settings, sizeof(HairSpecular::Settings));
	std::memcpy(&featureData->ExtendedTranslucency, &globals::features::extendedTranslucency.GetCommonBufferData(), sizeof(ExtendedTranslucency::PerFrame));
	std::memcpy(&featureData->LinearLighting, &linearLighting, sizeof(LinearLighting::PerFrameData));

	static_assert(sizeof(FeatureData::ExtendedMaterials) == sizeof(ExtendedMaterials::Settings));
	static_assert(sizeof(FeatureData::WetnessEffects) == sizeof(WetnessEffects::PerFrame));
	static_assert(sizeof(FeatureData::CloudShadows) == sizeof(CloudShadows::Settings));
	static_assert(sizeof(FeatureData::HairSpecular) == sizeof(HairSpecular::Settings));
	static_assert(sizeof(FeatureData::ExtendedTranslucency) == sizeof(ExtendedTranslucency::PerFrame));
	static_assert(sizeof(FeatureData::LinearLighting) == sizeof(LinearLighting::PerFrameData));

	creationEngineRaytracing->UpdateFeatureData(featureData.get(), sizeof(FeatureData));
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
	if (!settings.CreationEngineRaytracingSettings.Enabled)
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

	if (!mainTexture || resolutionChanged) {
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		mainTexture = eastl::make_unique<WrappedResource>(desc);
		creationEngineRaytracing->SetCopyTarget(mainTexture->resource.get());
	}

	if (Mode() == CreationEngineRaytracing::Mode::PathTracing && creationEngineRaytracing->SetPTOutputTargets) {
		if (!ptDepthTexture || resolutionChanged) {
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			ptDepthTexture = eastl::make_unique<WrappedResource>(desc);
		}
		if (!ptMotionVectorsTexture || resolutionChanged) {
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			ptMotionVectorsTexture = eastl::make_unique<WrappedResource>(desc);
		}
		creationEngineRaytracing->SetPTOutputTargets(ptDepthTexture->resource.get(), ptMotionVectorsTexture->resource.get());
	} else {
		ptDepthTexture.reset();
		ptMotionVectorsTexture.reset();
	}

	if (Mode() == CreationEngineRaytracing::Mode::GlobalIllumination) {
		ConvertTextures();

		globals::features::dx12Interop.Fence([&]() {
			// Executes the render graph for Global Illumination, depends on gbuffer render targets so we call it late
			creationEngineRaytracing->Execute();
		});

		// Fence function already has built-in wait, so we just call post execution for metrics and cleanup
		creationEngineRaytracing->PostExecution();
	} else {
		// Waits for path tracing execution to finish
		creationEngineRaytracing->WaitExecution();
	}

	auto screenSize = globals::state->screenSize;
	auto dynamicScreenSize = Util::ConvertToDynamic(screenSize);

	screenData->Resolution = { static_cast<uint>(screenSize.x), static_cast<uint>(screenSize.y) };
	screenData->DynamicResolution = { static_cast<uint>(dynamicScreenSize.x), static_cast<uint>(dynamicScreenSize.y) };

	screenCB->Update(screenData.get(), sizeof(ScreenData));

	auto mode = settings.CreationEngineRaytracingSettings.GeneralSettings.Mode;

	auto renderer = globals::game::renderer;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	if (mode == CreationEngineRaytracing::Mode::GlobalIllumination) {
		// Add GI result to kMain
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
	} else if (mode == CreationEngineRaytracing::Mode::PathTracing) {
		// Blend PT and Sky
		{
			context->CSSetShader(ptCompositeCS.get(), nullptr, 0);

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

		// Clear Specular RT
		{
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			context->ClearRenderTargetView(renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV, clearColor);
		}
	}

	auto& dx12Interop = globals::features::dx12Interop;

	if (dx12Interop.pixCapture && dx12Interop.pixCaptureStarted) {
		dx12Interop.ga->EndCapture();

		dx12Interop.pixCapture = false;
		dx12Interop.pixCaptureStarted = false;
	}

	if (dx12Interop.pixCapture && !dx12Interop.pixCaptureStarted) {
		dx12Interop.pixCaptureStarted = true;

		dx12Interop.ga->BeginCapture();
	}
}

void Raytracing::GetRayReconstructionInputs(ID3D12Resource*& diffuseAlbedo, ID3D12Resource*& specularAlbedo, ID3D12Resource*& normalRoughness, ID3D12Resource*& specHitDistance)
{
	if (Mode() != CreationEngineRaytracing::Mode::GlobalIllumination && Mode() != CreationEngineRaytracing::Mode::PathTracing)
		return;

	diffuseAlbedo = diffuseAlbedoTexture->resource.get();
	normalRoughness = normalRoughnessTexture->resource.get();

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
