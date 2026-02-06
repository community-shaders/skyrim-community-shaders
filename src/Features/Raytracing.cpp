#include "Raytracing.h"
#include "InverseSquareLighting.h"
#include "TerrainBlending.h"

#include "Globals.h"
#include "Raytracing/ShaderUtils.h"
#include "ShaderCache.h"
#include "State.h"

#include "Deferred.h"
#include <filesystem>
#include <shlobj.h>
#include <windows.h>

#include "Utils/PerfUtils.h"

#include "Menu.h"

#include "Features/CloudShadows.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/HairSpecular.h"
#include "Features/LinearLighting.h"
#include "Features/WetnessEffects.h"

#include <imgui_stdlib.h>

// WhiteFurnace here just so 'else' RAYTRACING_EXTRA_FIELDS is not empty
#ifdef DLSS_RR
#	define RAYTRACING_EXTRA_FIELDS WhiteFurnace, DLSSRR
#else
#	define RAYTRACING_EXTRA_FIELDS WhiteFurnace
#endif

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Raytracing::Settings,
	Enabled,
	GlobalIllumination,
	AdvancedSettings,
	TraceMode,
	Denoiser,
	Resolution,
	Bounces,
	SamplesPerPixel,
	Roughness,
	Metalness,
	Emissive,
	Effect,
	Sky,
	Directional,
	Point,
	TexLODBias,
	LodDimmer,
	RaytracedShadows,
	PathTracing,
	CullShadows,
	RussianRoulette,
	ConvertToGamma,
	PerformanceOverlay,
	DebugOutput,
	EnablePIXCapture,
	PIXCaptureLocation,
	EnableDebugDevice,
	SHaRC,
	SVGFDiffuse,
	SVGFSpecular,
	DisableSkinned,
	InteriorSun,
	RAYTRACING_EXTRA_FIELDS)

////////////////////////////////////////////////////////////////////////////////////

void Raytracing::RestoreDefaultSettings()
{
	settings = {};

	recompileReason |= RecompileReason::RestoreDefaultsSettings;
}

void Raytracing::LoadSettings(json& o_json)
{
	settings = o_json;

	recompileReason |= RecompileReason::LoadSettings;
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

void Raytracing::DrawSettings()
{
	if (ImGui::BeginTabBar("Settings")) {
		DrawGeneralSettings();
		DrawAdvancedSettings();
		DrawDebugSettings();

		ImGui::EndTabBar();
	}

	if (recompileReason != RecompileReason::None) {
		CompileRTGIShaders();
		CompileCompositeShader();
		recompileReason = RecompileReason::None;
		accumulatedFrames = 0; // Reset accumulation on recompile/settings change
	}
}

void Raytracing::DrawSHaRCSettings()
{
	if (settings.TraceMode != TraceMode::SHaRC)
		return;

	if (ImGui::CollapsingHeader("SHaRC")) {
		auto& sharcSettings = settings.SHaRC;

		ImGui::DragFloat("Scale", &sharcSettings.SceneScale, 0.001f, 0.1f, 10.0f);
		sharcSettings.SceneScale = std::clamp(sharcSettings.SceneScale, 0.1f, 10.0f);

		ImGui::InputInt("Accumulation Frames", &sharcSettings.AccumFrameNum);
		sharcSettings.AccumFrameNum = std::clamp(sharcSettings.AccumFrameNum, 5, 100);

		ImGui::InputInt("Stale Frames", &sharcSettings.StaleFrameNum);
		sharcSettings.StaleFrameNum = std::clamp(sharcSettings.StaleFrameNum, 8, 128);

		ImGui::Checkbox("Antifirefly Filter", &sharcSettings.AntifireflyFilter);
	}
}
//SVGFDiffuse

void Raytracing::DrawSVGFSettings()
{
	if (settings.Denoiser != Denoiser::SVGF)
		return;

	// Shameless word by word copy of jiaye's settings
	if (ImGui::CollapsingHeader("SVGF")) {
		if (ImGui::BeginTabBar("svgf_tabbar")) {
			DrawSVGFInternalSettings("Diffuse", settings.SVGFDiffuse);
			DrawSVGFInternalSettings("Specular", settings.SVGFSpecular);

			ImGui::EndTabBar();
		}
	}
}

void Raytracing::DrawSVGFInternalSettings(const char* name, SVGFPipeline::Settings& svgfSettings)
{
	if (ImGui::BeginTabItem(name)) {
		ImGui::SliderInt("Alpha Frames", (int*)&svgfSettings.AlphaFrames, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Temporal feedback frames for color.");

		ImGui::SliderInt("Moments Alpha Frames", (int*)&svgfSettings.MomentsAlphaFrames, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Temporal feedback frames for moments.");

		ImGui::SliderInt("À Trous Iterations", (int*)&svgfSettings.AtrousIterations, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Number of À Trous wavelet filter iterations. More iterations yield smoother results but may blur details and have a higher computational cost.");

		ImGui::SliderFloat("Color Phi", &svgfSettings.ColorPhi, 0.01f, 32.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Controls sensitivity to color differences in the À Trous filter. Lower values preserve more detail but may retain noise.");

		ImGui::SliderFloat("Normal Phi", &svgfSettings.NormalPhi, 1.0f, 1024.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Controls sensitivity to normal differences in the À Trous filter. Higher values preserve more detail but may retain noise.");

		ImGui::SliderFloat("Depth Phi", &svgfSettings.DepthPhi, 0.001f, 0.2f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Controls sensitivity to depth differences in the À Trous filter. Higher values preserve more detail but may retain noise.");

		ImGui::SliderFloat("Depth Threshold", &svgfSettings.DepthThreshold, 0.0f, 1.0f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Depth rejection difference. Lower values are more agressive.");

		ImGui::SliderInt("Normal Threshold", (int*)&svgfSettings.NormalThreshold, 0, 90, "%dº", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Normal rejection difference in dregrees. Lower values are more agressive.");

		ImGui::SliderInt("History Threshold", (int*)&svgfSettings.HistoryThreshold, 0, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Minimal accumulated frames before applying variance filter. Lower is faster but has less filtering.");

		ImGui::Checkbox("Variance", &svgfSettings.Variance);

		ImGui::Checkbox("Spatial", &svgfSettings.Spatial);

		ImGui::EndTabItem();
	}
}

#ifdef DLSS_RR
void Raytracing::DrawDLSSRRSettings()
{
	if (settings.Denoiser != Denoiser::DLSSRR)
		return;

	if (ImGui::CollapsingHeader("DLSS RR")) {
		auto& dlssrrSettings = settings.DLSSRR;

		DrawEnumCombo("Quality Mode", dlssrrSettings.QualityMode);
		DrawEnumRadio("Preset", dlssrrSettings.Preset);
	}
}
#endif

void Raytracing::DrawDenoiserSettings()
{
	DrawSVGFSettings();
#ifdef DLSS_RR
	DrawDLSSRRSettings();
#endif
	if (settings.Denoiser == Denoiser::Accumulation && settings.PathTracing) {
		if (ImGui::CollapsingHeader("Accumulation")) {
			ImGui::Text("Accumulated Frames: %d", accumulatedFrames);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Number of frames accumulated for denoising.\nAccumulation resets when camera moves.");
			}
			
			if (cameraHasMoved) {
				ImGui::TextColored({ 1.0f, 0.5f, 0.0f, 1.0f }, "Camera is moving - resetting accumulation");
			}
		}
	}
}
//ResolutionMode

void Raytracing::DrawResolutionSettings()
{
	bool disabled = false;

	// DLSS RR manages RT resolution itself
#ifdef DLSS_RR
	if (settings.Denoiser == Denoiser::DLSSRR) {
		ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.0f }, "*DLSS Ray Reconstruction manages resolution via the 'Quality Mode' setting*");
		disabled |= true;
	}
#endif

	if (disabled)
		ImGui::BeginDisabled();

	DrawEnumRadio("Resolution", settings.Resolution, "Controls the Ray Tracing resolution. Lower resolutions cast fewer rays, which improves performance but greatly reduces detail and clarity.");

	if (disabled)
		ImGui::EndDisabled();
}

void Raytracing::DrawLightingSettings()
{
	if (ImGui::CollapsingHeader("Lighting")) {
		if (ImGui::DragFloat("Emissive Strength", &settings.Emissive, 0.001f))
			settings.Emissive = std::max(0.0f, settings.Emissive);

		if (ImGui::DragFloat("Effect Strength", &settings.Effect, 0.001f))
			settings.Effect = std::max(0.0f, settings.Effect);

		if (ImGui::DragFloat("Sky Strength", &settings.Sky, 0.001f))
			settings.Sky = std::max(0.0f, settings.Sky);

		DrawLightSettings();
	}
}

void Raytracing::DrawLightSettings()
{
	if (ImGui::CollapsingHeader("Lights")) {
		if (ImGui::TreeNodeEx("Direct Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::DragFloat("Directional Strength", &settings.Directional, 0.001f))
				settings.Directional = std::max(0.0f, settings.Directional);

			if (ImGui::DragFloat("Point Strength", &settings.Point, 0.001f))
				settings.Point = std::max(0.0f, settings.Point);

			ImGui::Checkbox("Lod Dimmer", &settings.LodDimmer);

			ImGui::Checkbox("Raytraced Shadows", &settings.RaytracedShadows);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Replaces directional light shadowmaps.\n");
			}

			ImGui::Checkbox("Cull Shadows", &settings.CullShadows);

			ImGui::TreePop();
		}
	}
}

void Raytracing::DrawGeneralSettings()
{
	if (!ImGui::BeginTabItem("General"))
		return;

	ImGui::PushID("GeneralSettings");

	ImGui::Checkbox("Enabled", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable Ray-Traced Global Illumination.");
	}

	ImGui::Checkbox("Global Illumination", &settings.GlobalIllumination);

	if (DrawEnumRadio("TraceMode", settings.TraceMode, nullptr, TraceModeTooltips))
		recompileReason |= RecompileReason::General;

	if (DrawEnumRadio("Denoiser", settings.Denoiser))
		recompileReason |= RecompileReason::General;

	DrawResolutionSettings();

	// Bounces
	{
		int bounces = settings.Bounces;

		if (ImGui::SliderInt("Bounces", &settings.Bounces, 1, 32))
			settings.Bounces = std::clamp(settings.Bounces, 1, 32);

		if (bounces != settings.Bounces)
			recompileReason |= RecompileReason::General;
	}

	// Samples Per Pixel
	{
		int samples = settings.SamplesPerPixel;

		if (ImGui::SliderInt("Samples Per Pixel", &settings.SamplesPerPixel, 1, 32))
			settings.SamplesPerPixel = std::clamp(settings.SamplesPerPixel, 1, 32);

		if (samples != settings.SamplesPerPixel)
			recompileReason |= RecompileReason::General;
	}

	DrawFloat2("Roughness", settings.Roughness);
	DrawFloat2("Metalness", settings.Metalness);

	DrawSHaRCSettings();

	DrawDenoiserSettings();

	DrawLightingSettings();

	if (ImGui::Checkbox("Path Tracing", &settings.PathTracing)) {
		recompileReason |= RecompileReason::General;
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Experimental Path Tracing mode.\n");
	}

	/*ImGui::Checkbox("Recompress Textures", &settings.DebugShare);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Some texture formats cannot be shared between APIs, enabling this option ensures they'll be recompressed in a lower quality yet compatible format.\n");
	}*/

	ImGui::Checkbox("Russian Roulette", &settings.RussianRoulette);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable Russian Roulette termination for ray paths to improve performance at the cost of some variance.\n");
	}

	ImGui::Checkbox("Convert To Gamma", &settings.ConvertToGamma);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Convert the final raytraced output to gamma space.\n");
	}

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawAdvancedSettings()
{
	if (!ImGui::BeginTabItem("Advanced"))
		return;

	ImGui::PushID("AdvancedSettings");

	auto& advSettings = settings.AdvancedSettings;

	if (ImGui::TreeNodeEx("Culling", ImGuiTreeNodeFlags_DefaultOpen)) {
		DrawEnumRadio("Culling", advSettings.Culling.Mode, nullptr, CullingModeTooltips);

		ImGui::SliderInt("Minimal Radius", &advSettings.Culling.MinRadius, 0, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Nodes with a radius lower than this value are culled when outside the view.\n");
		}

		DrawEnumRadio("Distance Culling Mode", advSettings.Culling.DistanceMode, nullptr, CullingDistanceModeTooltips);

		if (advSettings.Culling.DistanceMode == CullingDistanceMode::Minimal) {
			ImGui::InputInt("Minimal Distance", &advSettings.Culling.MinDistance);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Distance to cull when outside the view regardless of radius.\n");
			}
		} else {
			ImGui::InputInt("Starting Distance", &advSettings.Culling.StartDistance);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Minimal distance to start modulating radius.\n");
			}

			ImGui::SliderFloat("Distance Ratio", &advSettings.Culling.DistanceRatio, 0.1f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		}

		ImGui::TreePop();
	}

	ImGui::Checkbox("Variable Update Rate", &advSettings.VariableUpdateRate);


	if (ImGui::Checkbox("Resampled Importance Sampling", &advSettings.RIS.Enabled))
		recompileReason |= RecompileReason::Advanced;

	ImGui::SliderInt("RIS Max Candidates", &advSettings.RIS.MaxCandidates, 2, 16);

	ImGui::SliderFloat("Texture LOD Bias", &settings.TexLODBias, -4.0f, 4.0f, "%.1f");

	if (ImGui::Checkbox("GGX Energy Conservation", &advSettings.GGXEnergyConservation))
		recompileReason |= RecompileReason::Advanced;

	if (ImGui::Checkbox("Use Hair Chiang BSDF", &advSettings.UseHairChiangBSDF))
		recompileReason |= RecompileReason::Advanced;

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Best with hair specular feature enabled.\n");
	}

	if (ImGui::TreeNodeEx("Subsurface Scattering", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox("Enable Subsurface Scattering", &advSettings.EnableSubsurfaceScattering))
			recompileReason |= RecompileReason::Advanced;

		if (advSettings.EnableSubsurfaceScattering) {
			ImGui::SliderInt("SSS Sample Count", &advSettings.SSSSampleCount, 1, 16);
			ImGui::SliderFloat("Max Sample Radius", &advSettings.SSSMaxSampleRadius, 0.01f, 64.0f, "%.2f");
			ImGui::Checkbox("Enable SSS Transmission", &advSettings.EnableSssTransmission);
			ImGui::Checkbox("SSS Material Override", &advSettings.SSSMaterialOverride);

			if (advSettings.SSSMaterialOverride) {
				ImGui::ColorEdit3("Override SSS Transmission Color", reinterpret_cast<float*>(&advSettings.OverrideSSSTransmissionColor), ImGuiColorEditFlags_Float);
				ImGui::ColorEdit3("Override SSS Scattering Color", reinterpret_cast<float*>(&advSettings.OverrideSSSScatteringColor), ImGuiColorEditFlags_Float);
				ImGui::SliderFloat("Override SSS Scale", &advSettings.OverrideSSSScale, 0.01f, 1000.0f, "%.2f");
				ImGui::SliderFloat("Override SSS Anisotropy", &advSettings.OverrideSSSAnisotropy, -0.99f, 0.99f);
			}
		}
		ImGui::TreePop();
	}

	if (DrawEnumCombo("Diffuse BRDF", advSettings.DiffuseBRDF))
		recompileReason |= RecompileReason::Advanced;

	if (DrawEnumRadio("Light Evaluation Mode", advSettings.LightEvalMode, nullptr, LightEvalModeTooltips))
		recompileReason |= RecompileReason::Advanced;

	if (DrawEnumRadio("Lighting Mode", advSettings.LightingMode, nullptr, LightingModeTooltips))
		recompileReason |= RecompileReason::Advanced;

	ImGui::Checkbox("Interior Sun", &settings.InteriorSun);

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawDebugSettings()
{
	if (!ImGui::BeginTabItem("Debug"))
		return;

	ImGui::PushID("DebugSettings");

	if (ImGui::TreeNodeEx("Skinning and DynamicTriShapes", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Disable Skinning", &settings.DisableSkinned);

		if (ImGui::Checkbox("Use Optimized Mapping", &skinningPipeline->settings.OptimizedMapping))
			skinningPipeline->recompile = true;

		if (ImGui::SliderInt("Thread Group Size", (int*)&skinningPipeline->settings.ThreadGroupSize,
				SkinningPipeline::MIN_THREAD_GROUP_SIZE, SkinningPipeline::MAX_THREAD_GROUP_SIZE, "%d", ImGuiSliderFlags_AlwaysClamp))
			skinningPipeline->recompile = true;

		ImGui::TreePop();
	}

	ImGui::Checkbox("Disable Texture Sharing", &debugDisableTextureSharing);
	
	ImGui::InputText("Shader Defines", &debugDefines);

	ImGui::SameLine();

	if (ImGui::Button("Recompile"))
		recompileReason |= RecompileReason::Debug;

	if (ImGui::Checkbox("White Furnace", &settings.WhiteFurnace))
		recompileReason |= RecompileReason::Debug;

	// Debug display mode
	if (ImGui::BeginCombo("Debug Output", magic_enum::enum_name(settings.DebugOutput).data())) {
		for (auto& value : magic_enum::enum_values<DebugOutput>()) {
			bool isSelected = (settings.DebugOutput == value);

			if (ImGui::Selectable(magic_enum::enum_name(value).data(), isSelected))
				settings.DebugOutput = value;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}

		ImGui::EndCombo();
	}

	ImGui::Checkbox("Enable PIX Capture", &settings.EnablePIXCapture);
	{
		if (settings.EnablePIXCapture) {
			if (ImGui::TreeNodeEx("Pix Capture", ImGuiTreeNodeFlags_DefaultOpen)) {
				//Pix Capture Location
				{
					int pixCapLocation = static_cast<int32_t>(settings.PIXCaptureLocation);
					ImGui::TextUnformatted("PIX Capture");

					ImGui::SameLine();
					ImGui::Dummy(ImVec2(25, 0));

					for (auto& [value, name] : magic_enum::enum_entries<PIXCaptureLocation>()) {
						ImGui::SameLine();
						ImGui::RadioButton(name.data(), &pixCapLocation, static_cast<int32_t>(value));
					}

					settings.PIXCaptureLocation = static_cast<PIXCaptureLocation>(pixCapLocation);
				}

				if (ImGui::Button("Single Frame Capture")) {
					pixCapture = true;
					pixCaptureStarted = false;
				}

				if (ImGui::Button("Start MultiFrame Capture")) {
					pixCapture = true;
					pixCaptureStarted = false;
					pixMultiFrame = true;
				}

				if (pixCapture && pixCaptureStarted && pixMultiFrame && ImGui::Button("End MultiFrame Capture")) {
					pixMultiFrame = false;
				}

				if (ImGui::Button("Start TRD Capture")) {
					pixCapture = true;
					pixCaptureStarted = false;
					pixTDR = true;
				}

				ImGui::TreePop();
			}
		}
	}

	ImGui::Checkbox("Enabled Debug Device", &settings.EnableDebugDevice);

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Lights: {}", lights.size()).c_str());

		ImGui::Text(std::format("Used Textures: {}, Shared: {}", textureRegisters.UsedCount(), textures.size()).c_str());
		ImGui::Text(std::format("Used Shapes: {}", shapeRegisters.UsedCount()).c_str());
		ImGui::Text(std::format("Models: {}", models.size()).c_str());

		auto instanceCount = instances.size();

		if (ImGui::TreeNodeEx(std::format("Instances: {}", instanceCount).c_str())) {
			for (auto& [root, instance]: instances) {
				ImGui::Text(std::format("{}, Detached: {}", std::string_view{ instance.filename }, instance.IsDetached()).c_str());
			}
		}

		if (settings.GlobalIllumination) {
			auto blasInstancesCount = blasInstances.size();
			ImGui::Text(std::format("GI Unculled: {}, Culled: {}", blasInstancesCount, instanceCount - blasInstancesCount).c_str());
		}

		if (RaytracedShadows()) {
			auto blasInstancesCount = blasShadowInstances.size();
			ImGui::Text(std::format("Shadow Unculled: {}, Culled: {}", blasInstancesCount, instanceCount - blasInstancesCount).c_str());
		}

		ImGui::TreePop();
	}

	// Debug Draw Original and Converted Normal Maps
//#if defined(DEBUG_MSNCONVERSION)
	if (normalMaps.empty()) {
		ImGui::Text("No normal maps converted.");
	} else {
		eastl::vector<std::pair<ID3D11Resource*, ConvertedNormalMap*>> normalMapVector;

		for (auto& [msNormal, convertedNormal] : normalMaps) {
			normalMapVector.emplace_back(msNormal, convertedNormal.get());
		}

		auto normalMapsCount = static_cast<uint>(normalMapVector.size());
		debugNormalMap = std::min(debugNormalMap, normalMapsCount);

		if (ImGui::BeginCombo("NormalMap", std::to_string(debugNormalMap).c_str())) {
			for (uint i = 0; i < normalMapsCount; i++) {
				bool isSelected = debugNormalMap == i;

				auto& [msNormal, convertedNormal] = normalMapVector.at(i);

				if (!convertedNormal->OriginalSRV)
					continue;

				if (!convertedNormal)
					continue;

				if (!convertedNormal->converted)
					continue;

				if (!convertedNormal->Texture || !convertedNormal->Texture->srv || !convertedNormal->Texture->srv.get())
					continue;

				if (ImGui::Selectable(std::to_string(i).c_str(), isSelected))
					debugNormalMap = i;

				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}

		auto& [msNormal, convertedNormal] = normalMapVector.at(debugNormalMap);

		if (convertedNormal && convertedNormal->converted && convertedNormal->OriginalSRV && convertedNormal->Texture && convertedNormal->Texture->srv && convertedNormal->Texture->srv.get()) {
			ImGui::Image(convertedNormal->OriginalSRV, ImVec2(256, 256));
			ImGui::SameLine();
			ImGui::Image(convertedNormal->Texture->srv.get(), ImVec2(256, 256));
		}
	}
//#endif

	ImGui::Image(skyHemisphere->srv, ImVec2(512, 512));

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawOverlay()
{
	auto* menu = Menu::GetSingleton();

	if (!globals::state || !menu)
		return;

	// Set window flags - no decoration and only movable when ShowBorder is true
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;

	// Only allow mouse interaction when the main menu is open
	if (!menu->IsEnabled) {
		windowFlags |= ImGuiWindowFlags_NoInputs;
	}

	ImGui::Begin("Raytracing Overlay", NULL, windowFlags);

	auto DrawRow = [](const char* label, size_t instances, float cpums, float gpums, [[maybe_unused]] double frameTime = 0.0f) {
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::Text(label);

		ImGui::TableNextColumn();
		ImGui::Text("%zu", instances);

		ImGui::TableNextColumn();
		ImGui::Text("%g ms", cpums);

		ImGui::TableNextColumn();
		ImGui::Text("%g ms", gpums);
	};

	if (ImGui::BeginTable("Effects", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Effect");
		ImGui::TableSetupColumn("Instances");
		ImGui::TableSetupColumn("CPU");
		ImGui::TableSetupColumn("GPU");
		ImGui::TableHeadersRow();

		if (RaytracedShadows())
			DrawRow("Shadows", blasShadowInstances.size(), shadowsCPUTime, shadowsGPUTime);

		// GI/PT
		DrawRow(settings.PathTracing ? "Path Tracing" : "Global Illumination", blasInstances.size(), mainCPUTime, mainGPUTime);

		// Denoiser
		//DrawRow(settings.PathTracing ? "Denoiser", blasInstances.size(), 0);

		ImGui::EndTable();
	}

	if (settings.PathTracing && settings.Denoiser == Denoiser::Accumulation) {
		ImGui::Separator();
		ImGui::Text("Accumulation Frames: %d", accumulatedFrames);
	}

	ImGui::End();
}

void Raytracing::SetupOutputRT()
{
	logger::info("[RT] SetupOutputRT - RenderSize: {}x{}", renderSize.x, renderSize.y);

	auto createRT = [&](eastl::unique_ptr<DX12::Texture2D>& texture, DXGI_FORMAT format, GIHeapDef::Slot slot, LPCWSTR name) {
		if (texture)
			texture.reset();

		texture = eastl::make_unique<DX12::Texture2D>(d3d12Device.get(), renderSize.x, renderSize.y, format, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		texture->SetName(name);
		texture->CreateUAV(giHeap->CPUHandle(slot));
		texture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	};

	// u0 - Output texture
	createRT(outputTexture, DXGI_FORMAT_R16G16B16A16_FLOAT, GIHeap::Slot::Output, L"Output texture");

	// u1 - Diffuse Albedo Path Tracing texture
	createRT(diffuseAlbedoPathTracingTexture, DXGI_FORMAT_R8G8B8A8_UNORM, GIHeap::Slot::DiffuseAlbedoPathTracing, L"Diffuse Albedo Path Tracing texture");

	// u2 - Normal Roughness Path Tracing texture
	createRT(normalRoughnessPathTracingTexture, DXGI_FORMAT_R16G16B16A16_SNORM, GIHeap::Slot::NormalRoughnessPathTracing, L"Normal Roughness Path Tracing texture");

	// u3 - Reflectance texture
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = renderSize.x;
		texDesc.Height = renderSize.y;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		specularAlbedoTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(specularAlbedoTexture->resource->SetName(L"Specular Albedo texture"));

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Format = texDesc.Format;

		d3d12Device->CreateUnorderedAccessView(specularAlbedoTexture->resource.get(), nullptr, &uavDesc, giHeap->CPUHandle(GIHeap::Slot::Reflectance));

		const auto& barrier = CD3DX12_RESOURCE_BARRIER::Transition(specularAlbedoTexture->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		commandList->ResourceBarrier(1, &barrier);

		//createRT(specularAlbedoTexture, DXGI_FORMAT_R16G16B16A16_FLOAT, GIHeap::Slot::Reflectance, L"Reflectance texture");
	}

	// u4 - Specular Hit Distance texture
	createRT(specularHitDistanceTexture, DXGI_FORMAT_R32_FLOAT, GIHeap::Slot::SpecularHitDist, L"Specular Hit Distance texture");

	// Motion vector
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = renderSize.x;
		texDesc.Height = renderSize.y;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		motionVectorsTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(motionVectorsTexture->resource->SetName(L"Motion Vectors Texture"));
	}

	// Normal Roughness
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = renderSize.x;
		texDesc.Height = renderSize.y;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		normalRoughnessTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(normalRoughnessTexture->resource->SetName(L"Normal Roughness Texture"));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		d3d12Device->CreateShaderResourceView(normalRoughnessTexture->resource.get(), &srvDesc, giHeap->CPUHandle(GIHeap::Slot::NormalRoughness));
	}

	// Diffuse (Metallic modulated albedo)
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = renderSize.x;
		texDesc.Height = renderSize.y;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		diffuseAlbedoTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(diffuseAlbedoTexture->resource->SetName(L"Diffuse Texture Texture"));
	}

	svgfDenoiser->SetupTextureResources(renderSize);

	renderResData->RenderRes = renderSize;
	renderResData->RenderResRcp = float2(1.0f / static_cast<float>(renderSize.x), 1.0f / static_cast<float>(renderSize.y));

	renderResCB->Update(renderResData.get(), sizeof(RenderResData));
}

void Raytracing::SetupResources()
{
#if defined(DLSS_RR)
	InitRR();
#endif

	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	normalMapConverter = eastl::make_unique<ModelSpaceToTangent>();

	auto device12 = d3d12Device.get();

	giHeap = eastl::make_unique<DX12::DescriptorHeap<GIHeap>>(
		device12,
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GIHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	shadowHeap = eastl::make_unique<DX12::DescriptorHeap<ShadowsHeap>>(
		device12,
		D3D12_DESCRIPTOR_HEAP_DESC(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ShadowsHeap::NumDescriptors(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));

	for (auto& pipeline : GetPipelines()) {
		pipeline->Initialize();
		pipeline->CreateRootSignature(device12);
		pipeline->CompileShaders(device12);
		pipeline->SetupResources(device12);
	}

	sharcPipeline->CreateUAVs(
		giHeap->CPUHandle(GIHeap::Slot::SHaRCHashEntries),
		giHeap->CPUHandle(GIHeap::Slot::SHaRCLock),
		giHeap->CPUHandle(GIHeap::Slot::SHaRCAccumulation),
		giHeap->CPUHandle(GIHeap::Slot::SHaRCResolved));

	svgfDenoiser = eastl::make_unique<SVGFPipeline>();
	svgfDenoiser->SetupResources();

	renderResData = eastl::make_unique<RenderResData>();

	// Constant buffers
	auto cbDesc = ConstantBufferDesc<RenderResData>();
	renderResCB = eastl::make_unique<ConstantBuffer>(cbDesc);

	accumulationCBData = eastl::make_unique<AccumulationCBData>();
	auto accCbDesc = ConstantBufferDesc<AccumulationCBData>();
	accumulationCB = eastl::make_unique<ConstantBuffer>(accCbDesc);

	// Setup default textures (this is a bit wordy...)
	{
		uint8_t white[] = { 255u, 255u, 255u, 255u };
		uint8_t gray[] = { 128u, 128u, 128u, 255u };
		uint8_t normal[] = { 128u, 128u, 255u, 255u };
		uint8_t black[] = { 0u, 0u, 0u, 0u };
		uint8_t rmaos[] = { 128u, 0u, 255u, 255u };
		uint8_t detail[] = { 63u, 64u, 63u, 255u };

		defaultWhiteTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());
		defaultGrayTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());
		defaultNormalTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());
		defaultBlackTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());
		defaultRMAOSTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());
		defaultDetailTexture = eastl::make_shared<DefaultTexture>(d3d12Device.get(), textureRegisters.Allocate());

		defaultWhiteTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);
		defaultGrayTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);
		defaultNormalTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);
		defaultBlackTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);
		defaultRMAOSTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);
		defaultDetailTexture->CreateSRV<GIHeap>(giHeap.get(), GIHeapDef::Slot::Textures);

		defaultWhiteTexture->UpdateAndUpload(commandList.get(), white);
		defaultGrayTexture->UpdateAndUpload(commandList.get(), gray);
		defaultNormalTexture->UpdateAndUpload(commandList.get(), normal);
		defaultBlackTexture->UpdateAndUpload(commandList.get(), black);
		defaultRMAOSTexture->UpdateAndUpload(commandList.get(), rmaos);
		defaultDetailTexture->UpdateAndUpload(commandList.get(), detail);
	}

	auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC mainDesc;
	mainTex.texture->GetDesc(&mainDesc);

	// Depth
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = mainDesc.Width;
		texDesc.Height = mainDesc.Height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		depthTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(depthTexture->resource->SetName(L"Depth texture"));

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(depthTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		commandList->ResourceBarrier(1, &barrier);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		d3d12Device->CreateShaderResourceView(depthTexture->resource.get(), &srvDesc, giHeap->CPUHandle(GIHeap::Slot::Depth));
		d3d12Device->CreateShaderResourceView(depthTexture->resource.get(), &srvDesc, shadowHeap->CPUHandle(ShadowsHeap::Slot::Depth));
	}

	// Shadow mask
	{
		auto shadowMask = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kSHADOW_MASK];
		D3D11_TEXTURE2D_DESC shadowMaskDesc;
		shadowMask.texture->GetDesc(&shadowMaskDesc);

		logger::info("[RT] Shadowmask Format: {}", magic_enum::enum_name(shadowMaskDesc.Format));

		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = mainDesc.Width;
		texDesc.Height = mainDesc.Height;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = shadowMaskDesc.Format;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		shadowMaskTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(shadowMaskTexture->resource->SetName(L"Shadow Mask"));

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Format = texDesc.Format;

		d3d12Device->CreateUnorderedAccessView(shadowMaskTexture->resource.get(), nullptr, &uavDesc, shadowHeap->CPUHandle(ShadowsHeap::Slot::ShadowMask));
	}

	if (UpdateRenderSize())
		SetupOutputRT();

	// UAVs
	{
		// u0 - Final texture
		{
			D3D11_TEXTURE2D_DESC texDesc{};
			texDesc.Width = mainDesc.Width;
			texDesc.Height = mainDesc.Height;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			mainTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
			DX::ThrowIfFailed(mainTexture->resource->SetName(L"Main Texture"));

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = texDesc.Format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
			srvDesc.Texture2D.PlaneSlice = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

			d3d12Device->CreateShaderResourceView(mainTexture->resource.get(), &srvDesc, giHeap->CPUHandle(GIHeap::Slot::Main)); 

			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mainTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commandList->ResourceBarrier(1, &barrier);
		}

		// Accumulation buffer for path tracing denoiser
		{
			D3D11_TEXTURE2D_DESC texDesc{};
			texDesc.Width = mainDesc.Width;
			texDesc.Height = mainDesc.Height;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			accumulationTexture = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
			DX::ThrowIfFailed(accumulationTexture->resource->SetName(L"Accumulation Texture"));

			accumulationTextureCopy = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
			DX::ThrowIfFailed(accumulationTextureCopy->resource->SetName(L"Accumulation Texture Copy"));
		}
	}

	// Light buffer
	{
		lightBuffer = eastl::make_unique<DX12::StructuredBufferUpload<Light>>(d3d12Device.get(), RTConstants::MAX_LIGHTS);
		DX::ThrowIfFailed(lightBuffer->resource->SetName(L"Light Buffer"));

		lightBuffer->CreateSRV(giHeap->CPUHandle(GIHeap::Slot::Lights));
	}

	// Shape buffer
	{
		shapeBuffer = eastl::make_unique<DX12::StructuredBufferUpload<ShapeData>>(d3d12Device.get(), RTConstants::MAX_SHAPES);
		DX::ThrowIfFailed(shapeBuffer->resource->SetName(L"Shape Buffer"));

		shapeBuffer->CreateSRV(giHeap->CPUHandle(GIHeap::Slot::Shapes));

		DX::ThrowIfFailed(shapeBuffer->UploadResource()->Map(0, nullptr, reinterpret_cast<void**>(&shapeData)));
	}

	// Instance buffer
	{
		instanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<InstanceData>>(d3d12Device.get(), RTConstants::MAX_INSTANCES);
		DX::ThrowIfFailed(instanceBuffer->resource->SetName(L"Instance Buffer"));

		instanceBuffer->CreateSRV(giHeap->CPUHandle(GIHeap::Slot::Instances));
	}

	// Geometry transform buffer
	{
		transformBuffer = eastl::make_unique<DX12::StructuredBufferUpload<float3x4>>(d3d12Device.get(), RTConstants::MAX_TRANSFORMS);
		DX::ThrowIfFailed(transformBuffer->resource->SetName(L"Transform Buffer"));

		transformBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	// Create instance buffer for BLAS
	{
		blasInstanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>>(d3d12Device.get(), RTConstants::MAX_INSTANCES, false);
		DX::ThrowIfFailed(blasInstanceBuffer->resource->SetName(L"BLAS Instance Buffer"));

		blasInstanceBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	// Create shadow instance buffer for BLAS
	{
		blasShadowInstanceBuffer = eastl::make_unique<DX12::StructuredBufferUpload<D3D12_RAYTRACING_INSTANCE_DESC>>(d3d12Device.get(), RTConstants::MAX_INSTANCES, false);
		DX::ThrowIfFailed(blasShadowInstanceBuffer->resource->SetName(L"BLAS Instance Buffer"));

		blasShadowInstanceBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	logger::debug("Creating constant buffer...");
	{
		frameBuffer = eastl::make_unique<DX12::StructuredBufferUpload<FrameData>>(d3d12Device.get(), 1, false, 2);
		DX::ThrowIfFailed(frameBuffer->resource->SetName(L"Frame Buffer"));

		frameBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

		frameData = eastl::make_unique<FrameData>();

		shadowsCB = eastl::make_unique<DX12::StructuredBufferUpload<ShadowsFrameData>>(d3d12Device.get(), 1, false);
		DX::ThrowIfFailed(shadowsCB->resource->SetName(L"Shadows Constant Buffer"));

		shadowsCB->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

		shadowsCBData = eastl::make_unique<ShadowsFrameData>();
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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, samplerState.put()));
	}

	// Sky Hemisphere
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = RTConstants::SKY_HEMI_SIZE;
		texDesc.Height = RTConstants::SKY_HEMI_SIZE;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		skyHemisphere = eastl::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
		DX::ThrowIfFailed(skyHemisphere->resource->SetName(L"Sky Hemisphere"));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		d3d12Device->CreateShaderResourceView(skyHemisphere->resource.get(), &srvDesc, giHeap->CPUHandle(GIHeap::Slot::SkyHemisphere));

		// Setup TESWaterReflections
		waterReflections = RE::NiPointer(new RE::TESWaterReflections());

		waterReflections->flags.set(true, RE::TESWaterReflections::Flags::kDirty, RE::TESWaterReflections::Flags::kDynamicCubemap, RE::TESWaterReflections::Flags::kWorldOrigin);

		for (uint i = 0; i < 6; i++) {
			waterReflections->cubeMapSides[i] = RE::TESWaterReflections::CubeMapSide(i, 0.0f);
		}
	}

	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	if (fenceEvent == nullptr) {
		DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	CompileShaders();
}

#ifdef DLSS_RR
void Raytracing::InitRR()
{
	std::wstring interposerPath = L"Data\\Shaders\\Upscaling\\Streamline\\sl.interposer.dll";
	interposer = LoadLibraryW(interposerPath.c_str());
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		return;
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS_RR };

	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D12;
	pref.flags = sl::PreferenceFlags::eUseManualHooking;
	//sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	pref.logLevel = sl::LogLevel::eOff;

	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slSetTag = (PFun_slSetTag*)GetProcAddress(interposer, "slSetTag");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		logger::info("[Streamline] Successfully initialized Streamline");
	}

	slSetD3DDevice((void*)d3d12Device.get());

	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDGetOptimalSettings", (void*&)slDLSSDGetOptimalSettings);
	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDGetState", (void*&)slDLSSDGetState);
	slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", (void*&)slDLSSDSetOptions);
}

int32_t Raytracing::GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculate halton number for index and base.
float Raytracing::Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void Raytracing::GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

sl::DLSSMode Raytracing::GetDLSSMode() const
{
	switch (settings.DLSSRR.QualityMode) {
	case DLSSRRQuality::MaxPerformance:
		return sl::DLSSMode::eMaxPerformance;
		break;
	case DLSSRRQuality::MaxQuality:
	case DLSSRRQuality::NativeRes:
		return sl::DLSSMode::eMaxQuality;
		break;
	case DLSSRRQuality::DLAA:
		return sl::DLSSMode::eDLAA;
		break;
	default:
		return sl::DLSSMode::eBalanced;
		break;
	}
}

void Raytracing::GetDLSSRROptimal()
{
	auto dlssdOptionsNew = GetDLSSRROptions();

	if (dlssdOptions.mode != dlssdOptionsNew.mode || dlssdOptions.qualityPreset != dlssdOptionsNew.qualityPreset || dlssdOptions.outputWidth != dlssdOptionsNew.outputWidth || dlssdOptions.outputHeight != dlssdOptionsNew.outputHeight) {
		dlssdOptions = dlssdOptionsNew;

		sl::Result result = slDLSSDGetOptimalSettings(dlssdOptions, optimalSettings);
		if (result != sl::Result::eOk) {
			logger::critical("[RT] Failed to get DLSS RR optimal settings, error code: {}", (int)result);
			return;
		}
	}
}

sl::DLSSDOptions Raytracing::GetDLSSRROptions() const
{
	sl::DLSSDOptions dlssdOptionsOut{};

	dlssdOptionsOut.mode = GetDLSSMode();

	auto screenSize = GetScreenSize();

	dlssdOptionsOut.outputWidth = screenSize.x;
	dlssdOptionsOut.outputHeight = screenSize.y;

	dlssdOptionsOut.colorBuffersHDR = sl::Boolean::eTrue;
	dlssdOptionsOut.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
	dlssdOptionsOut.alphaUpscalingEnabled = sl::Boolean::eFalse;

	auto preset = (settings.DLSSRR.Preset == DLSSRRPreset::D) ? sl::DLSSDPreset::ePresetD : sl::DLSSDPreset::ePresetE;

	dlssdOptionsOut.dlaaPreset = preset;
	dlssdOptionsOut.qualityPreset = preset;
	dlssdOptionsOut.balancedPreset = preset;
	dlssdOptionsOut.performancePreset = preset;
	dlssdOptionsOut.ultraPerformancePreset = preset;

	return dlssdOptionsOut;
}

void Raytracing::SetDLSSRROptions()
{
	auto worldToCameraView = globals::game::frameBufferCached.GetCameraView().Transpose();
	auto cameraViewToWorld = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

	dlssdOptions.worldToCameraView = sl::float4x4{
		sl::float4{ worldToCameraView._11, worldToCameraView._12, worldToCameraView._13, worldToCameraView._14 },
		sl::float4{ worldToCameraView._21, worldToCameraView._22, worldToCameraView._23, worldToCameraView._24 },
		sl::float4{ worldToCameraView._31, worldToCameraView._32, worldToCameraView._33, worldToCameraView._34 },
		sl::float4{ worldToCameraView._41, worldToCameraView._42, worldToCameraView._43, worldToCameraView._44 }
	};

	dlssdOptions.cameraViewToWorld = sl::float4x4{
		sl::float4{ cameraViewToWorld._11, cameraViewToWorld._12, cameraViewToWorld._13, cameraViewToWorld._14 },
		sl::float4{ cameraViewToWorld._21, cameraViewToWorld._22, cameraViewToWorld._23, cameraViewToWorld._24 },
		sl::float4{ cameraViewToWorld._31, cameraViewToWorld._32, cameraViewToWorld._33, cameraViewToWorld._34 },
		sl::float4{ cameraViewToWorld._41, cameraViewToWorld._42, cameraViewToWorld._43, cameraViewToWorld._44 }
	};

	if (SL_FAILED(result, slDLSSDSetOptions(slViewportHandle, dlssdOptions))) {
		logger::critical("[DLSS RR] Could not set DLSS RR options");
		return;
	}
}

void Raytracing::CheckFrameConstants()
{
	if (dlssFrameChecker.IsNewFrame()) {
		slGetNewFrameToken(frameToken, &globals::state->frameCount);

		auto state = globals::state;

		sl::Constants slConstants = {};

		if (globals::game::isVR) {
			slConstants.cameraAspectRatio = (state->screenSize.x * 0.5f) / state->screenSize.y;
		} else {
			slConstants.cameraAspectRatio = state->screenSize.x / state->screenSize.y;
		}

		slConstants.cameraFOV = Util::GetVerticalFOVRad();
		slConstants.cameraNear = *globals::game::cameraNear;
		slConstants.cameraFar = *globals::game::cameraFar;

		auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

		slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
		slConstants.cameraPinholeOffset = { 0.f, 0.f };
		slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
		slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
		slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
		slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust();
		slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
		slConstants.depthInverted = sl::Boolean::eFalse;

		recalculateCameraMatrices(slConstants);

		auto screenSize = GetScreenSize();
		auto phaseCount = GetJitterPhaseCount(renderSize.x, screenSize.x);

		GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);

		slConstants.jitterOffset = { -jitter.x, -jitter.y };
		slConstants.reset = sl::Boolean::eFalse;

		slConstants.mvecScale = { (globals::game::isVR ? 0.5f : 1.0f), 1 };
		slConstants.motionVectors3D = sl::Boolean::eFalse;
		slConstants.motionVectorsInvalidValue = FLT_MIN;
		slConstants.orthographicProjection = sl::Boolean::eFalse;
		slConstants.motionVectorsDilated = sl::Boolean::eFalse;
		slConstants.motionVectorsJittered = sl::Boolean::eFalse;

		if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, slViewportHandle))) {
			logger::error("[Streamline] Could not set constants");
		}
	}
}
#endif

static std::wstring StringViewToWString(std::string_view sv)
{
	std::string str(sv);

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);

	std::wstring wstr(size_needed, 0);

	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);

	return wstr;
}

void Raytracing::ShareRT(ID3D11Texture2D* pTexture2D, const GIHeap::Slot& target, const ShadowsHeap::Slot& cTarget, ID3D12Resource** ppResource) const
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture2D->GetDesc(&desc);

	winrt::com_ptr<IDXGIResource1> dxgiResource;
	DX::ThrowIfFailed(pTexture2D->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

	HANDLE sharedHandle = nullptr;
	DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &sharedHandle));  // DXGI_SHARED_RESOURCE_WRITE

	DX::ThrowIfFailed(d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(ppResource)));
	CloseHandle(sharedHandle);

	/*const auto& barrier = CD3DX12_RESOURCE_BARRIER::Transition(*ppResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
	commandList->ResourceBarrier(1, &barrier);*/

	// Create SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	if (target != GIHeap::Slot::None)
		d3d12Device->CreateShaderResourceView(*ppResource, &srvDesc, giHeap->CPUHandle(target));

	if (cTarget != ShadowsHeap::Slot::None)
		d3d12Device->CreateShaderResourceView(*ppResource, &srvDesc, shadowHeap->CPUHandle(cTarget));
}

void Raytracing::SetupSharedRT()
{
	const auto& rendererRD = globals::game::renderer->GetRuntimeData();

	ShareRT(rendererRD.renderTargets[ALBEDO].texture, GIHeap::Slot::Albedo, ShadowsHeap::Slot::None, albedoTexture.put());
	//ShareRT(rendererRD.renderTargets[REFLECTANCE].texture, GIHeap::Slot::None, ShadowsHeap::Slot::None, gbufferReflectanceTexture.put());
	//ShareRT(rendererRD.renderTargets[NORMALROUGHNESS].texture, HeapSlot::NormalRoughness, ComputeHeapSlot::None, normalRoughnessTexture.put());
	ShareRT(rendererRD.renderTargets[MASKS2].texture, GIHeap::Slot::GNMD, ShadowsHeap::Slot::None, GNMDTexture.put());  // GNMD

	DX::ThrowIfFailed(albedoTexture->SetName(L"Shared Albedo Texture"));
	//DX::ThrowIfFailed(gbufferReflectanceTexture->SetName(L"Shared Reflectance Texture"));
	//DX::ThrowIfFailed(normalRoughnessTexture->SetName(L"Shared NormalRoughness Texture"));
	DX::ThrowIfFailed(GNMDTexture->SetName(L"Shared GNMD Texture"));
}

bool IsValidLight(RE::BSLight* a_light)
{
	return a_light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

eastl::vector<LightLimitFix::LightData> Raytracing::GetPointLights()
{
	eastl::vector<LightLimitFix::LightData> lightsData{};

	auto accumulator = *globals::game::currentAccumulator.get();
	const auto activeShadowSceneNode = accumulator->GetRuntimeData().activeShadowSceneNode;

	auto& isl = globals::features::inverseSquareLighting;

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) {
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightLimitFix::LightData light{};
					light.color = float3(runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue);
					light.lightFlags = std::bit_cast<LightLimitFix::LightFlags>(runtimeData.ambient.red);

					if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;
						light.invRadius = 1.0f / light.radius;
						light.fade = runtimeData.fade;

						if (settings.LodDimmer)
							light.fade *= bsLight->lodDimmer;
					}

					if (!IsGlobalLight(bsLight)) {
						light.lightFlags.set(LightLimitFix::LightFlags::PortalStrict);
					}

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						GET_INSTANCE_MEMBER(maskIndex, shadowLight);
						light.shadowMaskIndex = maskIndex;
						light.lightFlags.set(LightLimitFix::LightFlags::Shadow);
					}

					// Check for inactive shadow light
					if (light.shadowMaskIndex != 255) {
						auto worldPos = niLight->world.translate;

						light.positionWS[0].data = float3(worldPos.x, worldPos.y, worldPos.z);

						if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
							lightsData.push_back(light);
						}
					}
				}
			}
		}
	};

	const auto& activeLights = activeShadowSceneNode->GetRuntimeData().activeLights;
	for (auto& light : activeLights) {
		addLight(light);
	}

	const auto& activeShadowLights = activeShadowSceneNode->GetRuntimeData().activeShadowLights;
	for (auto& light : activeShadowLights) {
		addLight(light);
	}

	return lightsData;
}

void Raytracing::UpdateLights()
{
	if (!renderingWorld || lightsUpdated)
		return;

	// Directional light
	{
		auto accumulator = *globals::game::currentAccumulator.get();
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto direction = Float3(dirLight->GetWorldDirection());
		direction.Normalize();

		auto& diffuse = dirLight->GetLightRuntimeData().diffuse;

		frameData->Directional.Vector = -direction;
		frameData->Directional.Color = float3(diffuse.red, diffuse.green, diffuse.blue) * settings.Directional;
	}

	// Point lights
	{
		lights.clear();
		lights.reserve(RTConstants::MAX_LIGHTS);

		for (auto data : GetPointLights()) {
			if (lights.size() >= RTConstants::MAX_LIGHTS)
				break;

			if (data.lightFlags.any(LightLimitFix::LightFlags::Disabled))
				continue;

			Light light;
			light.Vector = data.positionWS[0].data;
			light.Radius = data.radius;
			light.Color = data.color * settings.Point;
			light.InvRadius = data.invRadius;
			light.FadeZone = data.fadeZone;
			light.SizeBias = data.sizeBias;
			light.Fade = data.fade;
			light.Type = 0;
			light.Flags = 0;

			if (data.lightFlags.any(LightLimitFix::LightFlags::InverseSquare))
				light.Flags |= (1 << 0);

			if (data.lightFlags.any(LightLimitFix::LightFlags::Linear))
				light.Flags |= (1 << 1);

			lights.push_back(light);
		}

		if (!lights.empty())
			lightBuffer->UpdateList(lights.data(), lights.size());
	}

	lightsUpdated = true;
}

static DirectX::XMFLOAT3X4 GetXMF3X4FromNiTransform(const RE::NiTransform& Transform)
{
	const RE::NiMatrix3& m = Transform.rotate;
	const float scale = Transform.scale;

	return {
		m.entry[0][0] * scale, m.entry[1][0] * scale, m.entry[2][0] * scale,
		m.entry[0][1] * scale, m.entry[1][1] * scale, m.entry[2][1] * scale,
		m.entry[0][2] * scale, m.entry[1][2] * scale, m.entry[2][2] * scale,
		Transform.translate.x, Transform.translate.y, Transform.translate.z
	};
}

void Raytracing::CopyDepth()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];  // kMAIN kPOST_ZPREPASS_COPY

	context->CSSetShader(copyDepthCS.get(), nullptr, 0);

	//auto* renderSizeCB = renderResCB->CB();
	//context->CSSetConstantBuffers(0, 1, &renderSizeCB);

	auto* cb = globals::state->sharedDataCB->CB();
	context->CSSetConstantBuffers(5, 1, &cb);

	context->CSSetShaderResources(0, 1, &depth.depthSRV);

	//auto sampler = samplerState.get();
	//context->CSSetSamplers(0, 1, &sampler);

	eastl::array<ID3D11UnorderedAccessView*, 2> uavs = {
		depthTexture->uav,
		svgfDenoiser->depthLinearTexture->uav.get()
	};
	context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);

	uint2 screenSize = GetScreenSize();
	uint2 dispatchCount = { DivideRoundUp(screenSize.x, 8u), DivideRoundUp(screenSize.y, 8u) };
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

	uavs.fill(nullptr);
	context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);
}

void Raytracing::ConvertTextures() const
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->CSSetShader(settings.PathTracing ? convertTexturesPTCS.get() : convertTexturesCS.get(), nullptr, 0);

	auto* renderSizeCB = renderResCB->CB();
	context->CSSetConstantBuffers(0, 1, &renderSizeCB);

	auto* frameBufferCB = *globals::game::perFrame.get();
	context->CSSetConstantBuffers(12, 1, &frameBufferCB);

	ID3D11ShaderResourceView* srvs[4] = {
		renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS].SRV,
		renderer->GetRuntimeData().renderTargets[ALBEDO].SRV,
		renderer->GetRuntimeData().renderTargets[MASKS2].SRV,
		renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV
	};

	context->CSSetShaderResources(0, _countof(srvs), srvs);

	auto sampler = samplerState.get();
	context->CSSetSamplers(0, 1, &sampler);

	ID3D11UnorderedAccessView* uavs[3] = {
		normalRoughnessTexture->uav,
		diffuseAlbedoTexture->uav,
		motionVectorsTexture->uav
	};

	context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);

	uint2 dispatchCount = { DivideRoundUp(renderSize.x, 8u), DivideRoundUp(renderSize.y, 8u) };
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

	uavs[0] = nullptr;
	uavs[1] = nullptr;
	uavs[2] = nullptr;
	context->CSSetUnorderedAccessViews(0, _countof(uavs), uavs, nullptr);
}

void Raytracing::SkyCubeToHemi() const
{
	auto context = globals::d3d::context;

	context->CSSetShader(cubeToHemiCS.get(), nullptr, 0);

	auto reflections = globals::game::renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];
	auto reflectionOcc = globals::features::cloudShadows.loaded ? globals::features::cloudShadows.texCubemapCloudOccCopy->srv.get() : nullptr;

	eastl::array<ID3D11ShaderResourceView*, 2> srvs = {
		reflections.SRV,
		reflectionOcc
	};
	context->CSSetShaderResources(0, (UINT)srvs.size(), srvs.data());

	auto sampler = samplerState.get();
	context->CSSetSamplers(0, 1, &sampler);

	ID3D11UnorderedAccessView* uav = skyHemisphere->uav;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	uint dispatch = (uint)std::ceil(RTConstants::SKY_HEMI_SIZE / 8.0f);
	context->Dispatch(dispatch, dispatch, 1);

	uav = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
}

void Raytracing::ConvertMSN()
{
	while (!msnConvertionQueue.empty()) {
		auto& key = msnConvertionQueue.front();

		if (auto model = models.find(key); model != models.end()) {
			model->second->ConvertMSN();
		}

		msnConvertionQueue.pop_front();
	}
}

void Raytracing::Main_RenderWorld(bool a1)
{
	if (Active()) {
		renderingWorld = true;
		lightsUpdated = false;

		SkyCubeToHemi();
		ConvertMSN();
	}

	Hooks::Main_RenderWorld::func(a1);

	if (Active()) {
		renderingWorld = false;
	}
}

// A custom visit controller built to ignore billboard/particle geometry
static RE::BSVisit::BSVisitControl TraverseScenegraphRTGeometries(RE::NiAVObject* a_object, std::function<RE::BSVisit::BSVisitControl(RE::BSGeometry*)> a_func)
{
	auto result = RE::BSVisit::BSVisitControl::kContinue;

	if (!a_object) {
		return result;
	}

	auto geom = a_object->AsGeometry();
	if (geom) {
		return a_func(geom);
	}

	// Doodlum sez this is faster
	auto rtti = a_object->GetRTTI();

	static REL::Relocation<const RE::NiRTTI*> billboardRTTI{ RE::NiBillboardNode::Ni_RTTI };
	if (rtti == billboardRTTI.get())
		return result;

	// Might break vegetation
	static REL::Relocation<const RE::NiRTTI*> orderedRTTI{ RE::BSOrderedNode::Ni_RTTI };
	if (rtti == orderedRTTI.get())
		return result;

	auto node = a_object->AsNode();
	if (node) {
		for (auto& child : node->GetChildren()) {
			result = TraverseScenegraphRTGeometries(child.get(), a_func);
			if (result == RE::BSVisit::BSVisitControl::kStop) {
				break;
			}
		}
	}

	return result;
}

void Raytracing::CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root)
{
	if (!root) {
		logger::warn("[RT] CreateModel - NULL root object for model: {}", model ? model : "unknown");
		return;
	}

	const REL::Relocation<const RE::NiRTTI*> rtti{ RE::NiMultiTargetTransformController::Ni_RTTI };
	auto* controller = reinterpret_cast<RE::NiMultiTargetTransformController*>(root->GetController(rtti.get()));

	if (controller) {
		eastl::hash_set<RE::NiNode*> parents;
		eastl::hash_set<RE::NiAVObject*> targets;

		for (uint16_t i = 0; i < controller->numInterps; i++) {
			auto* target = controller->targets[i];

			if (!target)
				continue;

			auto [it, emplaced] = targets.emplace(target);
			parents.emplace(target->parent);

			if (!emplaced)
				continue;

			CreateModelInternal(form, std::format("{}_{}", model, target->name.c_str()).c_str(), target);
		}

		for (auto* parent : parents) {
			for (auto& child : parent->GetChildren()) {
				if (targets.find(child.get()) != targets.end())
					continue;

				CreateModelInternal(form, std::format("{}_{}_{}", model, child->name.c_str(), child->parentIndex).c_str(), child.get());
			}
		}

		return;
	}

	CreateModelInternal(form, model, root);
}

void Raytracing::CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* pRoot)
{
	if (!pRoot) {
		logger::error("[RT] CreateModel \"{}\" - nullptr root", path);
		return;
	}

	logger::trace("[RT] CreateModel \"{}\"", typeid(*pRoot).name());

	if (!path) {
		logger::debug("[RT] CreateModel \"{}\" - Invalid Path", pRoot->name);
		return;
	}

	if (strlen(path) == 0) {
		logger::debug("[RT] CreateModel \"{}\" - Empty Path", pRoot->name);
		return;
	}

	if (instances.find(pRoot) != instances.end()) {
		logger::warn("[RT] CreateModel \"{}\" - Instance/Model for 0x{:08X} already present.", path, reinterpret_cast<uintptr_t>(pRoot));
		return;
	}

	const auto* bsxFlags = pRoot->GetExtraData<RE::BSXFlags>("BSX");

	if (bsxFlags) {
		if (static_cast<int32_t>(bsxFlags->value) & static_cast<int32_t>(RE::BSXFlags::Flag::kEditorMarker))
			return;

		logger::debug("[RT] CreateModel - BSX Flags [0x{:x}]: {}", bsxFlags->value, GetFlagsString<RE::BSXFlags::Flag>(bsxFlags->value));
	}

	auto formID = form->GetFormID();

	// We only need one buffer per model
	if (models.find(path) != models.end()) {
		AddInstance(formID, pRoot, path);
		return;
	}

	logger::debug("[RT] CreateModel - Path: {}, FormID [0x{:08X}], NiNode [0x{:08X}]: {}", path, formID, reinterpret_cast<uintptr_t>(pRoot), pRoot->name);

	auto formType = form->GetFormType();

	auto rootWorldInverse = pRoot->world.Invert();

	eastl::vector<eastl::unique_ptr<Shape>> shapes;

	TraverseScenegraphRTGeometries(pRoot, [&](RE::BSGeometry* pGeometry) -> RE::BSVisit::BSVisitControl {
		const char* name = pGeometry->name.c_str();

		logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - {}", name);

		const auto& geometryType = pGeometry->GetType();

		if (geometryType.none(RE::BSGeometry::Type::kTriShape, RE::BSGeometry::Type::kDynamicTriShape)) {
			logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Unsupported Geometry: {} for {}", magic_enum::enum_name(geometryType.get()), name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		const auto& geometryRuntimeData = pGeometry->GetGeometryRuntimeData();

		auto* effect = geometryRuntimeData.properties[RE::BSGeometry::States::kEffect].get();

		if (!effect) {
			logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - No Effect");
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		bool isLightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect) != nullptr;
		bool isEffectShader = netimmerse_cast<RE::BSEffectShaderProperty*>(effect) != nullptr;

		// Only lighting and effect shader for now
		if (!isLightingShader && !isEffectShader) {
			logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Unsupported shader type: {}", effect->GetRTTI()->name);
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		auto shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(effect);
		bool skinned = shaderProperty && shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned);

		auto& geomFlags = pGeometry->GetFlags();

		if (geomFlags.any(RE::NiAVObject::Flag::kHidden) && !skinned) {
			logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Is Hidden");
			return RE::BSVisit::BSVisitControl::kContinue;
		}

		auto flags = Shape::Flags::None;

		// Landscape needs special handling of triangles
		if (formType == RE::FormType::Land)
			flags |= Shape::Flags::Landscape;

		if (geometryType.all(RE::BSGeometry::Type::kDynamicTriShape))
			flags |= Shape::Flags::Dynamic;

		float3x4 localToRoot;
		XMStoreFloat3x4(&localToRoot, GetXMFromNiTransform(rootWorldInverse * pGeometry->world));

		if (auto* triShapeRD = geometryRuntimeData.rendererData) {  // Non-Skinned
			auto* pTriShape = netimmerse_cast<RE::BSTriShape*>(pGeometry);

			const auto& triShapeRuntime = pTriShape->GetTrishapeRuntimeData();

			if (triShapeRuntime.vertexCount == 0) {
				logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Vertex count of 0 for {}: {}", path ? path : "N/A", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			if (triShapeRuntime.triangleCount == 0) {
				logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Triangle count of 0 for {}: {}", path ? path : "N/A", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			auto shape = eastl::make_unique<Shape>(flags, shapeRegisters.Allocate(), pGeometry, localToRoot);

			shape->BuildMesh(triShapeRD, triShapeRuntime.vertexCount, triShapeRuntime.triangleCount, 0);
			shape->BuildMaterial(geometryRuntimeData, name, formID);
			shape->CreateBuffers(ToWide(name));

			shapes.push_back(eastl::move(shape));
		} else if (auto* skinInstance = geometryRuntimeData.skinInstance.get()) {  // Skinned
			auto& skinPartition = skinInstance->skinPartition;

			if (!skinPartition) {
				logger::warn("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Invalid SkinPartition");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			if (skinPartition->vertexCount == 0) {
				logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Vertex count of 0 for {}: {}", path ? path : "N/A", name ? name : "N/A");
				return RE::BSVisit::BSVisitControl::kContinue;
			}

			const auto skinNumPartitions = skinPartition->numPartitions;

			logger::debug("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Partitions: {}, VertexCount: {}, Unk24: [0x{:X}]", skinNumPartitions, skinPartition->vertexCount, skinPartition->unk24);

			// This looks diabolical
			static REL::Relocation<const RE::NiRTTI*> dismemberRTTI{ RE::BSDismemberSkinInstance::Ni_RTTI };

			eastl::vector<RE::BSDismemberSkinInstance::Data> dismemberData(skinNumPartitions, { true, false, 0 });

			decltype(dismemberReferences.begin()) it;
			bool emplacedDismemberRef = false;

			if (skinInstance->GetRTTI() == dismemberRTTI.get()) {
				auto* dismemberSkinInstance = reinterpret_cast<RE::BSDismemberSkinInstance*>(skinInstance);

				auto& dismemberRuntime = dismemberSkinInstance->GetRuntimeData();
		
				const auto dismemberNumPartitions = static_cast<uint32_t>(dismemberRuntime.numPartitions);

				if (skinNumPartitions != dismemberNumPartitions)
					logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Skin and Dismember partition count mismatch");

				std::memcpy(dismemberData.data(), dismemberRuntime.partitions, dismemberNumPartitions * sizeof(RE::BSDismemberSkinInstance::Data));

				eastl::tie(it, emplacedDismemberRef) = dismemberReferences.try_emplace(dismemberSkinInstance, eastl::vector<Shape*>(skinNumPartitions));
			}

			for (size_t i = 0; i < skinPartition->partitions.size(); i++) {
				auto& partition = skinPartition->partitions[i];
				auto& dismemberPartition = dismemberData[i];

				// Fix for modded geometry
				if (partition.triangles == 0) {
					logger::error("\t\t[RT] CreateModel::TraverseScenegraphGeometries - Triangle count of 0 for {}: {}", path ? path : "N/A", name ? name : "N/A");
					continue;
				}

				// Fix for modded geometry
				if (partition.bonesPerVertex > 0)
					flags |= Shape::Flags::Skinned;

				auto shape = eastl::make_unique<Shape>(flags, shapeRegisters.Allocate(), pGeometry, localToRoot, dismemberPartition.editorVisible, dismemberPartition.slot);

				// Diabolical Part II
				if (emplacedDismemberRef)
					it->second[i] = shape.get();

				shape->BuildMesh(partition.buffData, skinPartition->vertexCount, partition.triangles, partition.bonesPerVertex);
				shape->BuildMaterial(geometryRuntimeData, name, formID);
				shape->CreateBuffers(ToWide(name));

				shapes.push_back(eastl::move(shape));
			}
		}

		return RE::BSVisit::BSVisitControl::kContinue;
	});

	if (auto shapeCount = shapes.size(); shapeCount > 0) {
		eastl::string modelKey = path;

		auto model = eastl::make_unique<Model>(shapes);

		// Models with these flags cannot be instanced directly
		if (model->GetShapeFlags().any(Shape::Flags::Dynamic, Shape::Flags::Skinned))
			modelKey.append(Model::KeySuffix(pRoot).c_str());

		auto [it, emplaced] = models.try_emplace(modelKey, eastl::move(model));

		if (emplaced) {
			if (it->second->ShouldQueueMSNConversion())
				msnConvertionQueue.emplace_back(modelKey);

			it->second->BuildBLAS(commandList.get());

			AddInstance(formID, pRoot, modelKey);

			logger::debug("[RT] CreateModel - Commited {} TriShapes to [0x{:08X}]", shapeCount, reinterpret_cast<uintptr_t>(it->second.get()));
		} else {
			logger::warn("[RT] CreateModel - Emplace failed for {} TriShapes", shapeCount);
		}
	} else {
		logger::debug("[RT] CreateModel - No TriShapes to commit");
	}
}

bool Raytracing::RemoveInstance(RE::NiAVObject* pRoot, bool releaseModel)
{
	if (auto instanceIt = instances.find(pRoot); instanceIt != instances.end()) {
		auto& instance = instanceIt->second;

		logger::debug("[RT] RemoveInstance - \"{}\", \"{}\"", pRoot->name, instance.filename);

		if (auto modelIt = models.find(instance.filename); modelIt != models.end()) {
			auto& model = modelIt->second;

			auto refCount = model->Release();

			logger::debug("[RT] RemoveInstance - RefCount: {}", refCount);

			// If this is the last Instance of the model, remove it
			if (refCount <= 0 && releaseModel) {
				// Not sure if its necesary to mutex here, but when the model goes out of scope the buffers are destroyed so I assume it is
				std::lock_guard lock{ renderMutex };

				logger::debug("[RT] RemoveInstance - No refs, erasing from collection");
				models.erase(modelIt);
			}
		}

		instances.erase(instanceIt);

		return true;
	}

	return false;
}

bool Raytracing::RemoveInstance(RE::FormID formID, bool releaseModel)
{
	bool removed = false;

	if (auto nodesIt = formIDNodes.find(formID); nodesIt != formIDNodes.end()) {
		for (auto& rootNode : nodesIt->second) {
			removed = RemoveInstance(rootNode, releaseModel);
		}

		formIDNodes.erase(nodesIt);
	}

	return removed;
}

void Raytracing::SetInstanceDetached(RE::NiAVObject* root, bool detached)
{
	if (auto instanceIt = instances.find(root); instanceIt != instances.end()) {
		instanceIt->second.SetDetached(detached);
	}
}

void Raytracing::SetInstanceDetached(RE::FormID formID, bool detached)
{
	if (auto nodesIt = formIDNodes.find(formID); nodesIt != formIDNodes.end()) {
		for (auto& rootNode : nodesIt->second) {
			SetInstanceDetached(rootNode, detached);
		}
	}
}

eastl::shared_ptr<Allocation> Raytracing::GetTextureRegister(ID3D11Texture2D* dx11Texture, eastl::shared_ptr<Allocation> defaultTexture)
{
	std::lock_guard lock{ textureRegisterMutex };

	if (!dx11Texture)
		return defaultTexture;

	// Texture already placed in heap, return allocation
	if (auto refIt = textures.find(dx11Texture); refIt != textures.end()) {
		return refIt->second->allocation;
	}

	winrt::com_ptr<IDXGIResource> dxgiResource;
	HRESULT hr = dx11Texture->QueryInterface(IID_PPV_ARGS(dxgiResource.put()));

	if (FAILED(hr)) {
		logger::error("[RT] GetTextureRegister - Failed to query interface.");
		return defaultTexture;
	}

	HANDLE sharedHandle = nullptr;
	hr = dxgiResource->GetSharedHandle(&sharedHandle);

	if (FAILED(hr) || !sharedHandle) {
		D3D11_TEXTURE2D_DESC desc;
		dx11Texture->GetDesc(&desc);

		logger::debug("[RT] GetTextureRegister - Failed to get shared handle - [{}, {}] Format: {}, Flags: {}", desc.Width, desc.Height, magic_enum::enum_name(desc.Format), GetFlagsString<D3D11_RESOURCE_MISC_FLAG>(desc.MiscFlags));
		return defaultTexture;
	}

	winrt::com_ptr<ID3D12Resource> dx12Texture;
	hr = d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(dx12Texture.put()));

	CloseHandle(sharedHandle);

	if (FAILED(hr)) {
		logger::error("[RT] GetTextureRegister - Failed to open shared handle.");
		return defaultTexture;
	}

	if (!dx12Texture) {
		logger::error("[RT] GetTextureRegister - Failed to adquire DX12 texture.");
		return defaultTexture;
	}

	D3D12_RESOURCE_DESC texResDesc = dx12Texture->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
	texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	texSrvDesc.Format = texResDesc.Format;
	texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	texSrvDesc.Texture2D.MostDetailedMip = 0;
	texSrvDesc.Texture2D.MipLevels = texResDesc.MipLevels;
	texSrvDesc.Texture2D.PlaneSlice = 0;
	texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	auto [it, emplaced] = textures.try_emplace(dx11Texture, nullptr);

	if (emplaced) {
		it->second = eastl::make_unique<TextureReference>(std::move(dx12Texture), eastl::shared_ptr<Allocation>(textureRegisters.Allocate(), AllocationDeleter()));

		d3d12Device->CreateShaderResourceView(it->second->resource.get(), &texSrvDesc, giHeap->CPUHandle(GIHeap::Slot::Textures, it->second->allocation->GetIndex()));

		return it->second->allocation;
	} else {
		logger::error("[RT] GetTextureRegister - TextureReference emplace failed.");
	}

	logger::debug("[RT] GetTextureRegister - Source texture not found");

	return defaultTexture;
}

eastl::shared_ptr<Allocation> Raytracing::GetMSNormalMapRegister([[maybe_unused]] Shape* shape, RE::BSGraphics::Texture* texture, eastl::shared_ptr<Allocation> defaultTexture)
{
	std::lock_guard lock{ textureRegisterMutex };

	auto* texture2D = reinterpret_cast<ID3D11Texture2D*>(texture->texture);

	if (auto refIt = normalMaps.find(texture2D); refIt != normalMaps.end()) {
		return refIt->second->Reference->allocation;
	} else {
		auto [it, emplaced] = normalMaps.emplace(texture2D, eastl::make_unique<ConvertedNormalMap>());

		if (!emplaced) {
			logger::warn("[RT] GetMSNormalMapRegister - NormalMap emplace failed.");
			return defaultTexture;
		}

		auto* normalMap = it->second.get();

		normalMap->OriginalSRV = texture->resourceView;

		D3D11_TEXTURE2D_DESC desc;
		texture2D->GetDesc(&desc);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

		normalMap->Texture = eastl::make_unique<Texture2D>(desc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;

		normalMap->Texture->CreateSRV(srvDesc);

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = desc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;

		normalMap->Texture->CreateRTV(rtvDesc);

		static float clearColor[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
		globals::d3d::context->ClearRenderTargetView(normalMap->Texture->rtv.get(), clearColor);

		// Share the new texture
		winrt::com_ptr<IDXGIResource> dxgiResource;
		HRESULT hr = normalMap->Texture->resource->QueryInterface(IID_PPV_ARGS(dxgiResource.put()));

		if (FAILED(hr)) {
			logger::error("[RT] GetMSNormalMapRegister - Failed to query interface.");
			return defaultTexture;
		}

		HANDLE sharedHandle = nullptr;
		hr = dxgiResource->GetSharedHandle(&sharedHandle);

		if (FAILED(hr) || !sharedHandle) {
			logger::error("[RT] GetMSNormalMapRegister - Failed to get shared handle.");
			return defaultTexture;
		}

		winrt::com_ptr<ID3D12Resource> dx12Texture;
		hr = d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(dx12Texture.put()));

		CloseHandle(sharedHandle);

		if (FAILED(hr)) {
			logger::error("[RT] GetMSNormalMapRegister - Failed to open shared handle.");
			return defaultTexture;
		}

		if (!dx12Texture) {
			logger::error("[RT] GetMSNormalMapRegister - Failed to adquire DX12 texture.");
			return defaultTexture;
		}

		D3D12_RESOURCE_DESC texResDesc = dx12Texture->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
		texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		texSrvDesc.Format = texResDesc.Format;
		texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		texSrvDesc.Texture2D.MostDetailedMip = 0;
		texSrvDesc.Texture2D.MipLevels = texResDesc.MipLevels;
		texSrvDesc.Texture2D.PlaneSlice = 0;
		texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		normalMap->Reference = eastl::make_unique<TextureReference>(std::move(dx12Texture), eastl::shared_ptr<Allocation>(textureRegisters.Allocate(), AllocationDeleter()));

		d3d12Device->CreateShaderResourceView(normalMap->Reference->resource.get(), &texSrvDesc, giHeap->CPUHandle(GIHeap::Slot::Textures, normalMap->Reference->allocation->GetIndex()));
	
		allocationMSNormalMaps.emplace(normalMap->Reference->allocation->GetIndex(), texture2D);

		return normalMap->Reference->allocation;	
	}
}

void Raytracing::AddInstance(RE::FormID formID, RE::NiAVObject* pNiNode, eastl::string path)
{
	logger::debug("[RT] AddInstance [0x{:08X}] - {}, Path: {}", formID, pNiNode->name, path);

	if (auto instanceIt = instances.find(pNiNode); instanceIt == instances.end()) {
		if (auto modelIt = models.find(path); modelIt != models.end()) {
			auto [it, emplaced] = instances.try_emplace(pNiNode, Instance(formID, path));

			if (emplaced) {
				if (auto nodesIt = formIDNodes.find(formID); nodesIt != formIDNodes.end()) {
					nodesIt->second.push_back(pNiNode);
				} else {
					formIDNodes.try_emplace(formID, eastl::vector<RE::NiAVObject*>{ pNiNode });
				}

				modelIt->second->AddRef();
			}
		}
	}
}

eastl::vector<size_t> Raytracing::GatherInstanceLights(RE::NiAVObject* pNiNode)
{
	eastl::vector<size_t> instanceLights;

	float3 center = Float3(pNiNode->worldBound.center);
	float radius = pNiNode->worldBound.radius;

	for (size_t i = 0; i < lights.size(); i++) {
		const Light& light = lights[i];

		if ((center - light.Vector).Length() <= radius + light.Radius)
			instanceLights.push_back(i);
	}

	return instanceLights;
}

static RE::NiCamera* FindNiCamera(RE::NiAVObject* object)
{
	if (auto* camera = skyrim_cast<RE::NiCamera*>(object))
		return camera;

	auto* node = object->AsNode();
	if (!node)
		return nullptr;

	for (auto& child : node->GetChildren()) {
		if (child) {
			if (auto* res = FindNiCamera(child.get()))
				return res;
		}
	}
	return nullptr;
}

void Raytracing::UpdateInstances()
{
	//std::lock_guard lock{ geometryMutex };

	blasInstances.clear();
	blasInstances.reserve(instances.size());

	const auto& cullingSettings = settings.AdvancedSettings.Culling;

	auto* player = RE::PlayerCharacter::GetSingleton();

	auto* tesCamera = RE::PlayerCamera::GetSingleton()->currentState->camera;
	RE::NiCamera* camera = FindNiCamera(tesCamera->cameraRoot.get());
	RE::NiPoint3 position = camera->world.translate;

	//auto eye = Util::GetAverageEyePosition();
	//float4 cameraPos = globals::game::frameBufferCached.GetCameraPosAdjust();

	uint32_t shapeIndex = 0;
	uint32_t instanceIndex = 0;

	for (auto& [node, instance] : instances) {
		if (instance.IsDetached())
			continue;

		if (blasInstances.size() > RTConstants::MAX_INSTANCES)
			break;

		if (instance.formID == player->formID && !player->Is3rdPersonVisible())
			continue;

		auto it = models.find(instance.filename);

		// Model was erased but not its (this) instance
		if (it == models.end())
			continue;

		auto& model = it->second;

		auto shapeFlags = model->GetShapeFlags();

		const bool dynamic = shapeFlags.any(Shape::Flags::Dynamic);
		const bool skinned = shapeFlags.any(Shape::Flags::Skinned);
		const bool landscape = shapeFlags.any(Shape::Flags::Landscape);

		if (settings.DisableSkinned && (dynamic || skinned))
			continue;

		if (skinned && node->GetFlags().any(RE::NiAVObject::Flag::kHidden))
			continue;

		if (cullingSettings.Mode == CullingMode::Smart) {
			if (landscape && node->GetFlags().any(RE::NiAVObject::Flag::kHidden))
				continue;

			auto worldBound = node->worldBound;

			float worldBoundRadius = Util::Units::GameUnitsToMeters(worldBound.radius);
			float distanceToBounds = Util::Units::GameUnitsToMeters(position.GetDistance(worldBound.center)) - worldBoundRadius;

			auto shaderTypes = model->GetShaderTypes();
			auto features = model->GetFeatures();

			bool frustumCull = false;

			// Culls small models outside of the player's view
			if (cullingSettings.MinRadius > 0) {
				// We'll exclude emissive models from radius frustum culling
				bool frustumCullable = !(shaderTypes & RE::BSShader::Type::Effect) && !(features & static_cast<int>(RE::BSShaderMaterial::Feature::kGlowMap));
				frustumCull |= frustumCullable && (worldBoundRadius < cullingSettings.MinRadius);
			}

			// Culls all models outside of the player's view, must satisfy condition
			if (cullingSettings.DistanceMode == CullingDistanceMode::Minimal) {
				frustumCull |= distanceToBounds > cullingSettings.MinDistance;
			} else if (cullingSettings.DistanceMode == CullingDistanceMode::Ratio) {
				float distanceToStart = std::max(0.0f, distanceToBounds - cullingSettings.StartDistance);
				float adaptativeRadius = distanceToStart * cullingSettings.DistanceRatio;
				frustumCull |= worldBoundRadius < adaptativeRadius;
			}

			// We'll cull small models or very distant ones (that are outside the player view)
			if (frustumCull && !camera->NodeInFrustum(node))
				continue;

		} else if (cullingSettings.Mode == CullingMode::Skyrim) {
			if (node->GetFlags().any(RE::NiAVObject::Flag::kHidden))
				continue;
		}

		instance.Update(node, position, { it->first, model.get() }, skinningPipeline.get());

		// This is temporary while I think of a better place to fit this (probably on instance.Update?)
		auto firstShapeIndex = shapeIndex;

		for (auto& shape : model->shapes) {
			if (shapeIndex >= RTConstants::MAX_SHAPES) {
				logger::critical("[RT] UpdateInstances - Total shape count {} would exceed RTConstants::MAX_SHAPES {}", shapeIndex, RTConstants::MAX_SHAPES);
				break;
			}

			if (model->HideShape(shape.get()))
				continue;

			shapeData[shapeIndex] = shape->GetData();
			shapeIndex++;
		}

		// No visible shape in instance
		if (shapeIndex == firstShapeIndex)
			continue;

		// TODO: split double sided models so only them get the flag
		bool isDoubleSided = model->GetShaderFlags().any(RE::BSShaderProperty::EShaderPropertyFlag::kTwoSided);

		D3D12_RAYTRACING_INSTANCE_DESC blasInstance{};
		blasInstance.InstanceID = 0;
		blasInstance.InstanceMask = 1;
		blasInstance.Flags = isDoubleSided ? D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE : D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		blasInstance.AccelerationStructure = model->blasBuffer->GetResource()->GetGPUVirtualAddress();

		// Copy transform matrix from Instance to DX12 BLAS instance
		memcpy(blasInstance.Transform, instance.transform.m, sizeof(blasInstance.Transform));

		blasInstances.push_back(blasInstance);

		instanceData[instanceIndex] = {
			instance.transform,
			LightData(GatherInstanceLights(node)),
			firstShapeIndex
		};
		instanceIndex++;
	}

	shapeBuffer->Upload(commandList.get(), 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	blasInstanceBuffer->UpdateList(blasInstances.data(), std::min(blasInstances.size(), (size_t)RTConstants::MAX_INSTANCES));
	blasInstanceBuffer->Upload(commandList.get());

	instanceBuffer->UpdateList(instanceData.data(), std::min(instanceIndex, RTConstants::MAX_INSTANCES));
	instanceBuffer->Upload(commandList.get(), 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void Raytracing::UpdateBLASes()
{
	static eastl::vector<CD3DX12_RESOURCE_BARRIER> barriers;
	barriers.clear();

	if (barriers.capacity() < instances.size())
		barriers.reserve(instances.size());

	for (auto& [node, instance] : instances) {
		auto it = models.find(instance.filename);

		auto& model = it->second;

		if (model->UpdateBLAS(commandList.get()))
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(model->blasBuffer->GetResource()));

		//logger::info("[RT] UpdateBLASes {} - {} - 0x{:08X} - {}", instance.filename, model->shapes.size(), reinterpret_cast<uintptr_t>(node), (flags & Model::Flags::BLASRebuild) ? "Rebuild" : "Update");
	}

	const uint blasUpdateCount = (uint)barriers.size();

	if (blasUpdateCount > 0)
		commandList->ResourceBarrier(blasUpdateCount, barriers.data());
}

auto GetFrustumCorners2(const RE::NiFrustum& frustum)
{
	eastl::array<float3, 8> corners;

	// Near plane
	corners[0] = { frustum.fLeft, frustum.fTop, frustum.fNear };      // near top-left
	corners[1] = { frustum.fRight, frustum.fTop, frustum.fNear };     // near top-right
	corners[2] = { frustum.fRight, frustum.fBottom, frustum.fNear };  // near bottom-right
	corners[3] = { frustum.fLeft, frustum.fBottom, frustum.fNear };   // near bottom-left

	// Far plane
	float scale = frustum.fFar / frustum.fNear;
	corners[4] = { frustum.fLeft * scale, frustum.fTop * scale, frustum.fFar };
	corners[5] = { frustum.fRight * scale, frustum.fTop * scale, frustum.fFar };
	corners[6] = { frustum.fRight * scale, frustum.fBottom * scale, frustum.fFar };
	corners[7] = { frustum.fLeft * scale, frustum.fBottom * scale, frustum.fFar };

	return corners;
}

auto GetFrustumCorners(const RE::NiFrustum& frustum)
{
	eastl::array<float3, 8> corners;

	float scale = frustum.fFar / frustum.fNear;

	// Near plane (Y = forward)
	corners[0] = { frustum.fLeft, frustum.fNear, frustum.fTop };      // near top-left
	corners[1] = { frustum.fRight, frustum.fNear, frustum.fTop };     // near top-right
	corners[2] = { frustum.fRight, frustum.fNear, frustum.fBottom };  // near bottom-right
	corners[3] = { frustum.fLeft, frustum.fNear, frustum.fBottom };   // near bottom-left

	// Far plane
	corners[4] = { frustum.fLeft * scale, frustum.fFar, frustum.fTop * scale };
	corners[5] = { frustum.fRight * scale, frustum.fFar, frustum.fTop * scale };
	corners[6] = { frustum.fRight * scale, frustum.fFar, frustum.fBottom * scale };
	corners[7] = { frustum.fLeft * scale, frustum.fFar, frustum.fBottom * scale };

	return corners;
}

void ComputeFrustumAABB(eastl::array<float3, 8> corners, float3& bbMin, float3& bbMax, DirectX::XMMATRIX* transform = nullptr)
{
	if (transform)
		for (int i = 0; i < 8; i++) {
			corners[i] = float3::Transform(corners[i], *transform);
		}

	// Initialize AABB
	bbMin = corners[0];
	bbMax = corners[0];

	// Compute min/max for X, Y, Z
	for (int i = 1; i < 8; i++) {
		bbMin.x = std::min(bbMin.x, corners[i].x);
		bbMin.y = std::min(bbMin.y, corners[i].y);
		bbMin.z = std::min(bbMin.z, corners[i].z);

		bbMax.x = std::max(bbMax.x, corners[i].x);
		bbMax.y = std::max(bbMax.y, corners[i].y);
		bbMax.z = std::max(bbMax.z, corners[i].z);
	}
}

bool SphereCastAABB(const float3& sphereCenter, float sphereRadius, const float3& dir, float maxDistance, const float3& bbMin, const float3& bbMax, float* hitDistance = nullptr)
{
	auto SphereCastAxis = [](float origin, float dir, float min, float max, float& tmin, float& tmax) -> bool {
		if (std::abs(dir) < 1e-6f) {
			if (origin < min || origin > max)
				return false;

			return true;
		}

		float ood = 1.0f / dir;
		float t1 = (min - origin) * ood;
		float t2 = (max - origin) * ood;

		if (t1 > t2)
			std::swap(t1, t2);

		tmin = std::max(tmin, t1);
		tmax = std::min(tmax, t2);

		return tmin <= tmax;
	};

	// Expand AABB by sphere radius
	float3 min = bbMin - float3(sphereRadius, sphereRadius, sphereRadius);
	float3 max = bbMax + float3(sphereRadius, sphereRadius, sphereRadius);

	float tmin = 0.0f;
	float tmax = maxDistance;

	if (!SphereCastAxis(sphereCenter.x, dir.x, min.x, max.x, tmin, tmax))
		return false;

	if (!SphereCastAxis(sphereCenter.y, dir.y, min.y, max.y, tmin, tmax))
		return false;

	if (!SphereCastAxis(sphereCenter.z, dir.z, min.z, max.z, tmin, tmax))
		return false;

	if (hitDistance)
		*hitDistance = tmin;

	return true;
}

auto GetPlanes(float3 corners[8])
{
	eastl::array<DirectX::SimpleMath::Plane, 6> planes;

	// Near plane
	planes[0] = Plane(corners[0], corners[1], corners[2]);

	// Far plane
	planes[1] = Plane(corners[5], corners[4], corners[7]);

	// Left plane
	planes[2] = Plane(corners[4], corners[0], corners[3]);

	// Right plane
	planes[3] = Plane(corners[1], corners[5], corners[6]);

	// Bottom plane
	planes[4] = Plane(corners[0], corners[4], corners[5]);

	// Top plane
	planes[5] = Plane(corners[3], corners[7], corners[6]);

	return planes;
}

bool SphereCastFrustum(const float3& sphereCenter, float radius, const float3& dir, const std::array<Plane, 6>& planes, float maxDistance = FLT_MAX, float* hitDistance = nullptr)
{
	float tmin = 0.0f;
	float tmax = maxDistance;

	for (const auto& plane : planes) {
		float nDotDir = plane.DotNormal(dir);
		float dist = plane.DotCoordinate(sphereCenter);

		if (std::abs(nDotDir) < 1e-6f) {
			// Ray is parallel to plane
			if (dist < -radius)  // sphere completely outside
				return false;
			else
				continue;  // sphere may be intersecting
		}

		float t1 = (-radius - dist) / nDotDir;
		float t2 = (radius - dist) / nDotDir;
		if (t1 > t2)
			std::swap(t1, t2);

		tmin = std::max(tmin, t1);
		tmax = std::min(tmax, t2);

		if (tmin > tmax)  // no intersection along this ray
			return false;
	}

	if (hitDistance)
		*hitDistance = tmin;

	return true;
}

void Raytracing::UpdateShadowInstances()
{
	//std::lock_guard lock{ geometryMutex };

	blasShadowInstances.clear();
	blasShadowInstances.reserve(instances.size());

	DirectX::XMMATRIX transformInverse;
	float3 bbMin, bbMax;
	float3 localLightDirection;

	auto* tesCamera = RE::PlayerCamera::GetSingleton()->currentState->camera;
	RE::NiCamera* camera = FindNiCamera(tesCamera->cameraRoot.get());
	RE::NiPoint3 position = camera->world.translate;

	if (settings.CullShadows) {
		auto transform = GetXMFromNiTransform(camera->world);
		transformInverse = DirectX::XMMatrixInverse(nullptr, transform);

		RE::NiFrustum frustrum = camera->GetRuntimeData2().viewFrustum;

		auto frustumCorners = GetFrustumCorners(frustrum);

		ComputeFrustumAABB(frustumCorners, bbMin, bbMax);  // In local (camera) space

		logger::trace("[RT] UpdateShadowInstances - Min: {}, Max: {}", bbMin, bbMax);

		localLightDirection = float3::TransformNormal(float3(shadowsCBData->Direction), transformInverse);
	}

	for (auto& [pNiNode, instance] : instances) {
		if (instance.IsDetached())
			continue;

		if (blasShadowInstances.size() > RTConstants::MAX_INSTANCES)
			break;

		if (settings.CullShadows) {
			auto worldBound = pNiNode->worldBound;
			float3 localCenter = float3::Transform(Float3(worldBound.center), transformInverse);

			logger::trace("[RT] UpdateShadowInstances - Local Center: {}, Radius: {}", localCenter, worldBound.radius);

			if (!SphereCastAABB(localCenter, worldBound.radius, localLightDirection, FLT_MAX, bbMin, bbMax))
				continue;
		}

		auto it = models.find(instance.filename);

		// Model was erased but not the instance
		if (it == models.end())
			continue;

		auto& model = it->second;

		instance.Update(pNiNode, position, { it->first, model.get() }, skinningPipeline.get());

		D3D12_RAYTRACING_INSTANCE_DESC blasShadowInstance = {
			.InstanceID = static_cast<uint>(blasShadowInstances.size()),
			.InstanceMask = 1,
			.AccelerationStructure = model->blasBuffer->GetResource()->GetGPUVirtualAddress()
		};

		memcpy(blasShadowInstance.Transform, instance.transform.m, sizeof(blasShadowInstance.Transform));

		blasShadowInstances.push_back(blasShadowInstance);
	}

	blasShadowInstanceBuffer->UpdateList(blasShadowInstances.data(), std::min(blasShadowInstances.size(), (size_t)RTConstants::MAX_INSTANCES));
	blasShadowInstanceBuffer->Upload(commandList.get());
}

void Raytracing::PostRaytraceCleanup()
{
	while (!tempGPUData.empty() && tempGPUData.front().fenceValue <= fenceValue) {
		tempGPUData.pop_front();
	}
}

void Raytracing::BSShader_SetupGeometry([[maybe_unused]] RE::BSShader* oThis, [[maybe_unused]] RE::BSRenderPass* pPass, [[maybe_unused]] uint32_t renderFlags)
{
	if (!Active() || !renderingWorld)
		return;

	UpdateLights();
}

void Raytracing::BuildTLAS()
{
	if (tlas)
		return;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = RTConstants::MAX_INSTANCES,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = blasInstanceBuffer->resource->GetGPUVirtualAddress()
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	//logger::info("[RT] Build TLAS - Instances: {}, ResultDataMaxSizeInBytes: {}, ScratchDataSizeInBytes: {}", MAX_INSTANCES, prebuildInfo.ResultDataMaxSizeInBytes, prebuildInfo.ScratchDataSizeInBytes);

	auto desc = BASIC_BUFFER_DESC;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// TLAS
	{
		desc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
		DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&tlas)));
		DX::ThrowIfFailed(tlas->SetName(L"TLAS"));

		// SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc = {};
		tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		tlasDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
		tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, shadowHeap->CPUHandle(ShadowsHeap::Slot::TLAS));
		d3d12Device->CreateShaderResourceView(nullptr, &tlasDesc, giHeap->CPUHandle(GIHeap::Slot::TLAS));
	}

	// TLAS scratch (used for rebuilding)
	desc.Width = std::max(prebuildInfo.ScratchDataSizeInBytes, 8ULL);
	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&tlasScratch)));
	DX::ThrowIfFailed(tlasScratch->SetName(L"TLAS scratch"));

	// TLAS update scratch
	/*desc.Width = std::max(prebuildInfo.UpdateScratchDataSizeInBytes * TLAS_BUFFER_SIZE_MULT, 8ULL);  // WARP bug workaround: use 8 if the required size was reported as less
	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasUpdateScratch)));
	DX::ThrowIfFailed(tlasUpdateScratch->SetName(L"TLAS update scratch"));*/
}

void Raytracing::RebuildTLAS(ID3D12GraphicsCommandList4* pCommandList, size_t numDescs, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = static_cast<uint>(numDescs),
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = instanceDescs
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	//logger::info("[RT] Rebuild TLAS - Instances: {}, ResultDataMaxSizeInBytes: {}, ScratchDataSizeInBytes: {}", numDescs, prebuildInfo.ResultDataMaxSizeInBytes, prebuildInfo.ScratchDataSizeInBytes);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {
		.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress()
	};

	pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	const auto& asBarrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.get());
	pCommandList->ResourceBarrier(1, &asBarrier);
}

uint2 Raytracing::GetScreenSize() const
{
	auto screenSize = Util::ConvertToDynamic(globals::state->screenSize);

	return {
		static_cast<uint>(screenSize.x),
		static_cast<uint>(screenSize.y)
	};
}

uint2 Raytracing::GetRenderSize()
{
	auto renderSizeOut = GetScreenSize();

#if defined(DLSS_RR)
	if (settings.Denoiser == Denoiser::DLSSRR) {
		GetDLSSRROptimal();

		if (settings.DLSSRR.QualityMode != DLSSRRQuality::NativeRes) {
			renderSizeOut = { optimalSettings.optimalRenderWidth, optimalSettings.optimalRenderHeight };
		}
	} else
#endif
	{
		uint resolutionFactor = 1;

		switch (settings.Resolution) {
		case (Resolution::Half):
			resolutionFactor = 2;
			break;
		case (Resolution::Quarter):
			resolutionFactor = 4;
			break;
		case (Resolution::Eighth):
			resolutionFactor = 8;
			break;
		default:
			break;
		}

		renderSizeOut = { renderSizeOut.x / resolutionFactor, renderSizeOut.y / resolutionFactor };
	}

	return renderSizeOut;
}

bool Raytracing::UpdateRenderSize()
{
	uint2 renderSizeNew = GetRenderSize();

	if (renderSize != renderSizeNew) {
		renderSize = renderSizeNew;

		return true;
	}

	return false;
}

void Raytracing::DrawRTGI()
{
	// We mutex here to prevent changes to resources while the command list is in flight, we could just queue everything maybe?
	std::lock_guard lock{ renderMutex };

	if (!d3d11Context) {
		logger::error("d3d11Context is nullptr");
	}

	if (!d3d11Fence) {
		logger::error("d3d11Fence is nullptr");
	}

	auto& rendererRuntimeData = globals::game::renderer->GetRuntimeData();
	auto main = rendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kMAIN];

	d3d11Context->CopyResource(mainTexture->resource11, main.texture);

	if (!RaytracedShadows())
		CopyDepth();

	if (settings.WhiteFurnace) {
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		d3d11Context->ClearRenderTargetView(rendererRuntimeData.renderTargets[ALBEDO].RTV, clearColor);
	}

	ConvertTextures();

	// Wait for D3D11 to finish
	{
		//d3d11Context->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);
		d3d11Context->Flush();
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;
	}

	auto startTime = Util::GetNowSecs();
	UpdateMeasureTime(startTime);

	if (pixCapture && (!pixCaptureStarted || pixTDR) && settings.PIXCaptureLocation == PIXCaptureLocation::GlobalIllumination) {
		pixCaptureStarted = true;

		/*if (pixMultiFrame) {
			PIXGpuCaptureNextFrames(L"I:/Temp/Pix/TDRCap.pix", 60);
		} else {*/
		//PIXBeginCapture(PIX_CAPTURE_GPU, PIXCaptureParameters
		ga->BeginCapture();
		//}
	}

	UpdateInstances();

	skinningPipeline->Dispatch(commandList.get(), d3d12Device.get());

	UpdateBLASes();

	// Upload buffers
	lightBuffer->UploadRegion(commandList.get(), sizeof(Light) * lights.size(), 0, 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	if (UpdateRenderSize())
		SetupOutputRT();

#ifdef DLSS_RR
	if (settings.Denoiser == Denoiser::DLSSRR) {
		//GetDLSSRROptimal();  // TODO: Remove this once we can handle dynamic resolution changes properly
		SetDLSSRROptions();
		CheckFrameConstants();
	}
#endif

	// Update framebuffer
	{
		frameData->ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		frameData->ProjInverse = globals::game::frameBufferCached.GetCameraProjInverse().Transpose();

		float4 position = globals::game::frameBufferCached.GetCameraPosAdjust();
		frameData->Position = float3(position.x, position.y, position.z);

		float4 positionPrev = globals::game::frameBufferCached.GetCameraPreviousPosAdjust();
		frameData->PositionPrev = float3(positionPrev.x, positionPrev.y, positionPrev.z);

		frameData->FrameCount = globals::state->frameCount;

		frameData->CameraData = Util::GetCameraData();

		auto eye = Util::GetCameraData(0);
		float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
		float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));

		frameData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

		frameData->Roughness = settings.Roughness;
		frameData->Metalness = settings.Metalness;

		frameData->Emissive = settings.Emissive;
		frameData->Effect = settings.Effect;
		frameData->Sky = settings.Sky;

		frameData->Lights = static_cast<uint>(lights.size());

		frameData->PixelConeSpreadAngle = std::atan((2.0f / eye.projMat.m[1][1]) / renderSize.y);
		frameData->TexLODBias = settings.TexLODBias;

		frameData->SSSSampleCount = settings.AdvancedSettings.SSSSampleCount;
		frameData->SSSMaxSampleRadius = settings.AdvancedSettings.SSSMaxSampleRadius;
		frameData->EnableSssTransmission = settings.AdvancedSettings.EnableSssTransmission;
		frameData->SSSMaterialOverride = settings.AdvancedSettings.SSSMaterialOverride;
		frameData->OverrideSSSTransmissionColor = settings.AdvancedSettings.OverrideSSSTransmissionColor;
		frameData->OverrideSSSScatteringColor = settings.AdvancedSettings.OverrideSSSScatteringColor;
		frameData->OverrideSSSScale = settings.AdvancedSettings.OverrideSSSScale;
		frameData->OverrideSSSAnisotropy = settings.AdvancedSettings.OverrideSSSAnisotropy;

		frameData->RussianRoulette = settings.RussianRoulette;

		if (Util::IsInterior()) {
			frameData->EmittanceColor = float3::One;
		} else {
			const auto* sky = RE::Sky::GetSingleton();

			if (sky && sky->region)
				frameData->EmittanceColor = Float3(sky->region->emittanceColor);
			else
				frameData->EmittanceColor = float3::One;
		}

		auto& cloudShadows = globals::features::cloudShadows;
		frameData->CloudOpacity = cloudShadows.loaded ? cloudShadows.settings.Opacity : 0.0f;

		frameData->SHaRC = settings.SHaRC.GetFrameData(settings.TraceMode == TraceMode::SHaRC);  // Sets UpdatePass to true if in SHaRC mode

		frameData->DispatchSize = renderSize;

		// Update Features
		{
			auto wetnessEffect = globals::features::wetnessEffects.GetCommonBufferData();
			auto linearLighting = globals::features::linearLighting.GetCommonBufferData();

			frameData->Features.ExtendedMaterial = *reinterpret_cast<CPMSettings*>(&globals::features::extendedMaterials.settings);
			frameData->Features.WetnessEffects = *reinterpret_cast<WetnessEffectsSettings*>(&wetnessEffect);
			frameData->Features.CloudShadows = *reinterpret_cast<CloudShadowsSettings*>(&globals::features::cloudShadows.settings);
			frameData->Features.HairSpecular = *reinterpret_cast<HairSpecularSettings*>(&globals::features::hairSpecular.settings);
			frameData->Features.ExtendedTranslucency = *reinterpret_cast<ExtendedTranslucencySettings*>(&globals::features::extendedTranslucency.settings);
			frameData->Features.LinearLighting = *reinterpret_cast<LinearLightingSettings*>(&linearLighting);
		}

		// Upload buffer 0, for SHaRC resolve pass
		frameBuffer->Update(frameData.get(), sizeof(FrameData), 0, 0);

		if (settings.TraceMode == TraceMode::SHaRC) {
			// Upload buffer 1, for main RT pass
			frameData->SHaRC.UpdatePass = false;
			frameBuffer->Update(frameData.get(), sizeof(FrameData), 0, 1);
		}

		// Upload buffer 0 to GPU
		frameBuffer->Upload(commandList.get());
	}

	BuildTLAS();
	RebuildTLAS(commandList.get(), blasInstances.size(), blasInstanceBuffer->resource->GetGPUVirtualAddress());

	{
		auto setupRTPipeline = [&]() {
			commandList->SetPipelineState1(pipelineRT.get());
			commandList->SetComputeRootSignature(rootSignature.get());

			auto* pHeap = giHeap->Heap();
			commandList->SetDescriptorHeaps(1, &pHeap);

			// Parameter 0: UAV table
			commandList->SetComputeRootDescriptorTable(0, giHeap->TableGPUHandle(GIHeap::Table::UAV));

			// Parameter 1: Fixed SRVs
			commandList->SetComputeRootDescriptorTable(1, giHeap->TableGPUHandle(GIHeap::Table::SRV));

			// Parameter 2: Vertex buffers
			commandList->SetComputeRootDescriptorTable(2, giHeap->TableGPUHandle(GIHeap::Table::VertexBuffer));

			// Parameter 3: Triangle buffers
			commandList->SetComputeRootDescriptorTable(3, giHeap->TableGPUHandle(GIHeap::Table::TriangleBuffer));

			// Parameter 4: Textures
			commandList->SetComputeRootDescriptorTable(4, giHeap->TableGPUHandle(GIHeap::Table::Textures));

			// Parameter 5: Constant buffer
			commandList->SetComputeRootConstantBufferView(5, frameBuffer->resource->GetGPUVirtualAddress());
		};

		// Raytracing
		{
			setupRTPipeline();

			D3D12_DISPATCH_RAYS_DESC dispatchDesc{};
			dispatchDesc.Depth = 1;

			shaderBindingTable->FillDispatchShaderBindingTable(dispatchDesc, shaderBindingTableBuffer->resource->GetGPUVirtualAddress());

			// SHaRC Update pass
			if (settings.TraceMode == TraceMode::SHaRC) {
				dispatchDesc.Width = DivideRoundUp(renderSize.x, 5.0f);
				dispatchDesc.Height = DivideRoundUp(renderSize.y, 5.0f);

				commandList->DispatchRays(&dispatchDesc);

				sharcPipeline->Resolve(commandList.get(), frameBuffer->resource.get());

				// Restore RT pipeline
				commandList->SetPipelineState1(pipelineRT.get());
				commandList->SetComputeRootSignature(rootSignature.get());

				// Restore RT pipeline
				setupRTPipeline();

				// Update Frame Buffer for main RT pass, maybe we should use two buffers?
				// Using one GPU heap buffer with multiple upload buffers felt like a hack (but it works)
				frameBuffer->Upload(commandList.get(), 1);

				// This function uses CopyBufferRegion to upload only the UpdatePass variable, but it failed to work...
				//frameBuffer->UploadRegion(commandList.get(), sizeof(SHaRCFrameData::UpdatePass),  offsetof(FrameData, SHaRC) + offsetof(SHaRCFrameData, UpdatePass), 1);
			}

			// Main pass
			{
				dispatchDesc.Width = renderSize.x;
				dispatchDesc.Height = renderSize.y;

				commandList->DispatchRays(&dispatchDesc);

				CD3DX12_RESOURCE_BARRIER rtUAVBarrier[3] = {
					CD3DX12_RESOURCE_BARRIER::UAV(outputTexture->resource.get()),
					CD3DX12_RESOURCE_BARRIER::UAV(specularAlbedoTexture->resource.get()),
					CD3DX12_RESOURCE_BARRIER::UAV(specularHitDistanceTexture->resource.get())
				};

				commandList->ResourceBarrier(_countof(rtUAVBarrier), rtUAVBarrier);

				if (settings.PathTracing) {
					CD3DX12_RESOURCE_BARRIER ptUAVBarrier[2] = {
						CD3DX12_RESOURCE_BARRIER::UAV(diffuseAlbedoPathTracingTexture->resource.get()),
						CD3DX12_RESOURCE_BARRIER::UAV(normalRoughnessPathTracingTexture->resource.get())
					};

					commandList->ResourceBarrier(_countof(ptUAVBarrier), ptUAVBarrier);
				}
			}
		}

		if (settings.DebugOutput == DebugOutput::None) {
#ifdef DLSS_RR
			if (settings.Denoiser == Denoiser::DLSSRR) {
				{
					auto screenSize = GetScreenSize();

					sl::Extent inputExtent{ 0, 0, renderSize.x, renderSize.y };
					sl::Extent inputNativeExtent{ 0, 0, screenSize.x, screenSize.y };
					sl::Extent outputExtent{ 0, 0, screenSize.x, screenSize.y };

					uint32_t state = settings.PathTracing ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

					sl::Resource colorIn = { sl::ResourceType::eTex2d, outputTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
					sl::Resource colorOut = { sl::ResourceType::eTex2d, mainTexture->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
					sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
					sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectorsTexture->resource.get(), 0 };
					sl::Resource diffuseAlbedo = { sl::ResourceType::eTex2d, settings.PathTracing ? diffuseAlbedoPathTracingTexture->resource.get() : diffuseAlbedoTexture->resource.get(), state };
					sl::Resource specularAlbedo = { sl::ResourceType::eTex2d, specularAlbedoTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
					sl::Resource normalRoughness = { sl::ResourceType::eTex2d, settings.PathTracing ? normalRoughnessPathTracingTexture->resource.get() : normalRoughnessTexture->resource.get(), state };
					sl::Resource specHitDistance = { sl::ResourceType::eTex2d, specularHitDistanceTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };

					sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &inputExtent };
					sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &outputExtent };
					sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &inputNativeExtent };
					sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag diffuseAlbedoTag = sl::ResourceTag{ &diffuseAlbedo, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag specularAlbedoTag = sl::ResourceTag{ &specularAlbedo, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag normalRoughnessTag = sl::ResourceTag{ &normalRoughness, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };
					sl::ResourceTag specHitDistanceTag = sl::ResourceTag{ &specHitDistance, sl::kBufferTypeSpecularHitDistance, sl::ResourceLifecycle::eValidUntilPresent, &inputExtent };

					sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, diffuseAlbedoTag, specularAlbedoTag, normalRoughnessTag, specHitDistanceTag };
					if (SL_FAILED(result, slSetTag(slViewportHandle, resourceTags, _countof(resourceTags), commandList.get()))) {
						logger::error("[DLSS RR] Failed to set DLSS RR tags, error: {}", magic_enum::enum_name(result));
						return;
					}
				}

				const sl::BaseStructure* inputs[] = { &slViewportHandle };

				if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureDLSS_RR, *frameToken, inputs, _countof(inputs), commandList.get()))) {
					logger::error("[DLSS RR] Failed to evaluate DLSS RR feature, error: {}", magic_enum::enum_name(result));
				}
			} else
#endif
			{
				auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mainTexture->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
				commandList->ResourceBarrier(1, &barrier);

				outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(mainTexture->resource.get(), outputTexture->resource.get());
				outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				barrier = CD3DX12_RESOURCE_BARRIER::Transition(mainTexture->resource.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList->ResourceBarrier(1, &barrier);
			}
		} else {
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mainTexture->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
			commandList->ResourceBarrier(1, &barrier);

			if (settings.DebugOutput == DebugOutput::Output) {
				outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(mainTexture->resource.get(), outputTexture->resource.get());
				outputTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			} else if (settings.DebugOutput == DebugOutput::Reflectance) {
				const auto& barrierCopy = CD3DX12_RESOURCE_BARRIER::Transition(specularAlbedoTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->ResourceBarrier(1, &barrierCopy);

				commandList->CopyResource(mainTexture->resource.get(), specularAlbedoTexture->resource.get());

				const auto& barrierUAV = CD3DX12_RESOURCE_BARRIER::Transition(specularAlbedoTexture->resource.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				commandList->ResourceBarrier(1, &barrierUAV);
			} else if (settings.DebugOutput == DebugOutput::SpecularHitDistance) {
				specularHitDistanceTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->CopyResource(mainTexture->resource.get(), specularHitDistanceTexture->resource.get());
				specularHitDistanceTexture->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			} else if (settings.DebugOutput == DebugOutput::NormalRoughnessGbuffer) {
				auto normalRoughnessProxy = settings.PathTracing ? normalRoughnessPathTracingTexture->resource.get() : normalRoughnessTexture->resource.get();
				auto transitionCopy = CD3DX12_RESOURCE_BARRIER::Transition(normalRoughnessProxy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->ResourceBarrier(1, &transitionCopy);

				commandList->CopyResource(mainTexture->resource.get(), normalRoughnessProxy);

				auto transitionNonPixelRes = CD3DX12_RESOURCE_BARRIER::Transition(normalRoughnessProxy, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList->ResourceBarrier(1, &transitionNonPixelRes);
			} else if (settings.DebugOutput == DebugOutput::GeometryNormalMetalness) {
				auto transitionCopy = CD3DX12_RESOURCE_BARRIER::Transition(GNMDTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->ResourceBarrier(1, &transitionCopy);

				commandList->CopyResource(mainTexture->resource.get(), GNMDTexture.get());

				auto transitionNonPixelRes = CD3DX12_RESOURCE_BARRIER::Transition(GNMDTexture.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList->ResourceBarrier(1, &transitionNonPixelRes);
			} else if (settings.DebugOutput == DebugOutput::Albedo) {
				auto transitionCopy = CD3DX12_RESOURCE_BARRIER::Transition(albedoTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->ResourceBarrier(1, &transitionCopy);

				commandList->CopyResource(mainTexture->resource.get(), albedoTexture.get());

				auto transitionNonPixelRes = CD3DX12_RESOURCE_BARRIER::Transition(albedoTexture.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList->ResourceBarrier(1, &transitionNonPixelRes);
			} else if (settings.DebugOutput == DebugOutput::Diffuse) {
				auto diffuseAlbedoProxy = settings.PathTracing ? diffuseAlbedoPathTracingTexture->resource.get() : diffuseAlbedoTexture->resource.get();
				auto transitionCopy = CD3DX12_RESOURCE_BARRIER::Transition(diffuseAlbedoProxy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
				commandList->ResourceBarrier(1, &transitionCopy);

				commandList->CopyResource(mainTexture->resource.get(), diffuseAlbedoProxy);

				auto transitionNonPixelRes = CD3DX12_RESOURCE_BARRIER::Transition(diffuseAlbedoProxy, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList->ResourceBarrier(1, &transitionNonPixelRes);
			}

			barrier = CD3DX12_RESOURCE_BARRIER::Transition(mainTexture->resource.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commandList->ResourceBarrier(1, &barrier);
		}

		DX::ThrowIfFailed(commandList->Close());

		if (canMeasure) {
			mainCPUTime = static_cast<float>((Util::GetNowSecs() - startTime) * 1000.0);
			startTime = Util::GetNowSecs();
		}

		ID3D12CommandList* commandListPtr = commandList.get();
		commandQueue->ExecuteCommandLists(1, &commandListPtr);
	}

	// Wait for D3D12 to finish
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

	// Wait until GPU is done with previous frame
	if (d3d12Fence->GetCompletedValue() < fenceValue) {
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, nullptr));
	}

	if (frameChecker.IsNewFrame()) {
		//logger::info("[RT] Executed Frame: {}", frameIndex);
		frameIndex++;
	}

	if (canMeasure)
		mainGPUTime = static_cast<float>((Util::GetNowSecs() - startTime) * 1000.0);

	if (pixCapture && pixCaptureStarted && !pixTDR && settings.PIXCaptureLocation == PIXCaptureLocation::GlobalIllumination) {
		ga->EndCapture();
		pixCapture = pixMultiFrame;
		pixCaptureStarted = false;
	}

	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
	fenceValue++;

	//New frame, reset
	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));

	/*if (pixCapture) {
		pixCaptureStarted = true;
		ga->BeginCapture();
	}*/

	PostRaytraceCleanup();

	// Check for camera movement for accumulation denoiser
	if (settings.Denoiser == Denoiser::Accumulation && settings.PathTracing) {
		const auto& currentViewProj = globals::game::frameBufferCached.GetCameraViewProjUnjittered();
		const auto& prevViewProj = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered();

		bool matrixChanged = std::memcmp(&currentViewProj, &prevViewProj, sizeof(currentViewProj)) != 0;

		float3 posDelta = frameData->Position - frameData->PositionPrev;
		float movementSq = posDelta.x * posDelta.x + posDelta.y * posDelta.y + posDelta.z * posDelta.z;
		const float posThreshold = 0.01f;

		cameraHasMoved = matrixChanged || (movementSq > posThreshold);
		
		if (cameraHasMoved) {
			accumulatedFrames = 0;
		} else {
			accumulatedFrames++;
		}
	}

	if (settings.DebugOutput == DebugOutput::None) {
		if (settings.Denoiser == Denoiser::SVGF) {
			auto sampler = samplerState.get();
			d3d11Context->CSSetSamplers(0, 1, &sampler);

			auto* renderSizeCB = renderResCB->CB();
			d3d11Context->CSSetConstantBuffers(0, 1, &renderSizeCB);

			auto* frameBufferCB = *globals::game::perFrame.get();
			d3d11Context->CSSetConstantBuffers(12, 1, &frameBufferCB);

			// Diffuse
			svgfDenoiser->Denoise(d3d11Context.get(), renderSize, settings.SVGFDiffuse, normalRoughnessTexture.get(), mainTexture.get(), true);

			// Specular
			svgfDenoiser->Denoise(d3d11Context.get(), renderSize, settings.SVGFSpecular, normalRoughnessTexture.get(), specularAlbedoTexture.get(), false);
		}
	}

	// True Linear to Gamma
	if (settings.ConvertToGamma || !settings.PathTracing && settings.Denoiser == Denoiser::SVGF) {
		d3d11Context->CSSetShader(compositeCS.get(), nullptr, 0);

		d3d11Context->CopyResource(main.textureCopy, main.texture);

		eastl::array<ID3D11ShaderResourceView*, 4> srvs = {
			main.SRVCopy,
			diffuseAlbedoTexture->srv,
			mainTexture->srv,
			specularAlbedoTexture->srv
		};
		d3d11Context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());

		d3d11Context->CSSetUnorderedAccessViews(0, 1, &main.UAV, nullptr);

		auto dispatchCount = Util::GetScreenDispatchCount();
		d3d11Context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	} else if (settings.PathTracing && settings.Denoiser == Denoiser::Accumulation) {
		if (accumulatedFrames == 0 || cameraHasMoved) {
			d3d11Context->CopyResource(accumulationTexture->resource11, mainTexture->resource11);
			d3d11Context->CopyResource(main.texture, mainTexture->resource11);
		} else {
			accumulationCBData->AccumulatedFrames = (uint)accumulatedFrames;
			accumulationCB->Update(accumulationCBData.get(), sizeof(AccumulationCBData));

			auto* accumulationCBPtr = accumulationCB->CB();
			d3d11Context->CSSetConstantBuffers(2, 1, &accumulationCBPtr);

			d3d11Context->CSSetShader(accumulationCS.get(), nullptr, 0);

			d3d11Context->CopyResource(accumulationTextureCopy->resource11, accumulationTexture->resource11);

			eastl::array<ID3D11ShaderResourceView*, 2> srvs = {
				accumulationTextureCopy->srv,
				mainTexture->srv
			};
			d3d11Context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());

			ID3D11UnorderedAccessView* accumulationUAV = accumulationTexture->uav;
			d3d11Context->CSSetUnorderedAccessViews(0, 1, &accumulationUAV, nullptr);

			auto dispatchCount = Util::GetScreenDispatchCount();
			d3d11Context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

			d3d11Context->CopyResource(main.texture, accumulationTexture->resource11);
		}
	} else {
		d3d11Context->CopyResource(main.texture, mainTexture->resource11);
	}

	// Clear specular if Path Tracing is enabled
	if (settings.PathTracing) {
		auto renderer = globals::game::renderer;

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		d3d11Context->ClearRenderTargetView(renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV, clearColor);
	}
}

void Raytracing::UpdateShadowsFrameBuffer()
{
	shadowsCBData->CameraData = Util::GetCameraData();

	auto eye = Util::GetCameraData(0);
	float2 ndcToViewMult = float2(2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1));
	shadowsCBData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

	shadowsCBData->ViewInverse = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

	float4 cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
	shadowsCBData->Position = float4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f);

	if (shadowLight) {
		auto direction = Normalize(Float3(-shadowLight->GetShadowDirectionalLightRuntimeData().sunVector));
		shadowsCBData->Direction = float4(direction.x, direction.y, direction.z, 0.0f);
	}

	shadowsCB->Update(shadowsCBData.get(), sizeof(ShadowsFrameData));
}

void Raytracing::RenderShadows()
{
	//logger::info("[RT] RenderShadows - ShadowLight [0x{:x}], TLAS [0x{:x}]", reinterpret_cast<uintptr_t>(shadowLight), reinterpret_cast<uintptr_t>(tlas.get()));

	if (!shadowLight)
		return;

	if (!shadowFrameChecker.IsNewFrame())
		return;

	//std::lock_guard lock{ renderMutex };

	auto rendererRuntimeData = globals::game::renderer->GetRuntimeData();
	auto shadowMask = rendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kSHADOW_MASK];

	CopyDepth();

	// Tell DX11 to finish and wait
	//d3d11Context->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);
	d3d11Context->Flush();
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
	fenceValue++;

	auto startTime = Util::GetNowSecs();
	UpdateMeasureTime(startTime);

	if (pixCapture && (!pixCaptureStarted || pixTDR) && settings.PIXCaptureLocation == PIXCaptureLocation::Shadows) {
		pixCaptureStarted = true;

		/*if (pixMultiFrame) {
			PIXGpuCaptureNextFrames(L"I:/Temp/Pix/TDRCap.pix", 60);
		} else {*/
		//PIXBeginCapture(PIX_CAPTURE_GPU, PIXCaptureParameters
		ga->BeginCapture();
		//}
	}

	// Do DX12 work...
	UpdateShadowInstances();

	skinningPipeline->Dispatch(commandList.get(), d3d12Device.get());

	//UpdateDynamicSkinning(commandList.get());

	shadowsCB->Upload(commandList.get());

	BuildTLAS();
	RebuildTLAS(commandList.get(), blasShadowInstances.size(), blasShadowInstanceBuffer->resource->GetGPUVirtualAddress());

	commandList->SetPipelineState1(shadowPipeline.get());
	commandList->SetComputeRootSignature(shadowRS.get());

	auto computeHeapPtr = shadowHeap->Heap();
	commandList->SetDescriptorHeaps(1, &computeHeapPtr);

	// UAV table
	commandList->SetComputeRootDescriptorTable(0, shadowHeap->TableGPUHandle(ShadowsHeap::Table::UAV));

	// SRV table
	commandList->SetComputeRootDescriptorTable(1, shadowHeap->TableGPUHandle(ShadowsHeap::Table::SRV));

	// Constant buffer
	commandList->SetComputeRootConstantBufferView(2, shadowsCB->resource->GetGPUVirtualAddress());

	CD3DX12_RESOURCE_BARRIER ctuBarrier[1] = {
		CD3DX12_RESOURCE_BARRIER::Transition(shadowMaskTexture->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	};
	commandList->ResourceBarrier(_countof(ctuBarrier), ctuBarrier);

	// Dispatch
	auto shadowMaskDesc = shadowMaskTexture->resource->GetDesc();

	D3D12_GPU_VIRTUAL_ADDRESS sbtAddr = shadowSBTBuffer->resource->GetGPUVirtualAddress();

	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {
		.RayGenerationShaderRecord = {
			.StartAddress = sbtAddr,
			.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES },
		.MissShaderTable = { .StartAddress = sbtAddr + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES },
		.HitGroupTable = { .StartAddress = sbtAddr + 2 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES },
		.Width = static_cast<UINT>(shadowMaskDesc.Width),
		.Height = shadowMaskDesc.Height,
		.Depth = 1
	};

	commandList->DispatchRays(&dispatchDesc);

	CD3DX12_RESOURCE_BARRIER utcBarrier[1] = {
		CD3DX12_RESOURCE_BARRIER::Transition(shadowMaskTexture->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
	};
	commandList->ResourceBarrier(_countof(utcBarrier), utcBarrier);

	DX::ThrowIfFailed(commandList->Close());

	if (canMeasure) {
		shadowsCPUTime = static_cast<float>((Util::GetNowSecs() - startTime) * 1000.0);
		startTime = Util::GetNowSecs();
	}

	ID3D12CommandList* commandListPtr = commandList.get();
	commandQueue->ExecuteCommandLists(1, &commandListPtr);

	// Wait for D3D12 to finish and signal DX11
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));

	// Wait for GPU
	if (d3d12Fence->GetCompletedValue() < fenceValue) {
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(fenceValue, nullptr));
	}

	if (frameChecker.IsNewFrame())
		frameIndex++;

	if (canMeasure)
		shadowsGPUTime = static_cast<float>((Util::GetNowSecs() - startTime) * 1000.0);

	if (pixCapture && pixCaptureStarted && !pixTDR && settings.PIXCaptureLocation == PIXCaptureLocation::Shadows) {
		ga->EndCapture();
		pixCapture = pixMultiFrame;  // Do not stop capture when doing multiframe
		pixCaptureStarted = false;
	}

	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
	fenceValue++;

	// Reset for next command list usage
	DX::ThrowIfFailed(commandAllocator->Reset());
	DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));

	PostRaytraceCleanup();

	d3d11Context->CopyResource(shadowMask.texture, shadowMaskTexture->resource11);
}

void Raytracing::DataLoaded()
{
	CellAttachDetachEventHandler::Register();
	BGSActorCellEventHandler::Register();
}

void Raytracing::PostPostLoad()
{
	Hooks::Install();
	Initialize();

	//MenuOpenCloseEventHandler::Register();
	//TESLoadGameEventHandler::Register();

	TESObjectLoadedEventHandler::Register();

	//TESCellFullyLoadedEventHandler::Register();
}
static std::wstring GetLatestWinPixGpuCapturerPath()
{
	LPWSTR programFilesPath = nullptr;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

	std::filesystem::path pixInstallationPath = programFilesPath;
	pixInstallationPath /= "Microsoft PIX";

	std::wstring newestVersionFound;

	for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath)) {
		if (directory_entry.is_directory()) {
			if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str()) {
				newestVersionFound = directory_entry.path().filename().c_str();
			}
		}
	}

	if (newestVersionFound.empty()) {
		// TODO: Error, no PIX installation found
	}

	return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
}

void DumpDredBreadcrumbs(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1& breadcrumbsOutput)
{
	const D3D12_AUTO_BREADCRUMB_NODE1* pNode = breadcrumbsOutput.pHeadAutoBreadcrumbNode;

	while (pNode) {
		const UINT32 completedOps = *pNode->pLastBreadcrumbValue;
		const UINT32 totalOps = pNode->BreadcrumbCount;

		logger::error("[RT] Command List: {}", pNode->pCommandListDebugNameA ? pNode->pCommandListDebugNameA : "<unnamed>");
		logger::error("[RT] Queue: {}", pNode->pCommandQueueDebugNameA ? pNode->pCommandQueueDebugNameA : "<unnamed>");
		logger::error("[RT] Completed Ops: {} / {}", completedOps, totalOps);

		if (pNode->pCommandHistory && totalOps > 0) {
			// Last executed command
			UINT32 lastIndex = (completedOps > 0) ? completedOps - 1 : 0;
			auto lastOp = pNode->pCommandHistory[lastIndex];
			logger::error("[RT] Last Executed Command: {}", magic_enum::enum_name(lastOp));

			// Next (likely faulting) command
			if (completedOps < totalOps) {
				auto nextOp = pNode->pCommandHistory[completedOps];
				logger::error("[RT] Next (Likely Faulting) Command: {}", magic_enum::enum_name(nextOp));
			}
		}

		logger::error("");  // empty line for readability
		pNode = pNode->pNext;
	}
}

void Raytracing::DeviceRemovedHandler()
{
	if (settings.EnablePIXCapture) {
		ga->EndCapture();
		pixCapture = false;
		pixCaptureStarted = false;
		pixMultiFrame = false;
		pixTDR = false;
	}

	if (settings.EnableDebugDevice) {
		// 1. Device removed reason
		HRESULT reason = d3d12Device->GetDeviceRemovedReason();
		logger::error("[RT] ============================================================");
		logger::error("[RT] DEVICE REMOVED! HRESULT = 0x{:08X}", reason);

		winrt::com_ptr<ID3D12DeviceRemovedExtendedData1> dred;
		if (FAILED(d3d12Device->QueryInterface(IID_PPV_ARGS(&dred)))) {
			logger::error("[RT] DRED not available on this device.");
			return;
		}

		// ---------------------------------------------------------------------
		// 2. Auto Breadcrumbs
		// ---------------------------------------------------------------------
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bcOutput = {};
		if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&bcOutput)) && bcOutput.pHeadAutoBreadcrumbNode) {
			DumpDredBreadcrumbs(bcOutput);
		} else {
			logger::error("[RT] No breadcrumbs available.");
		}
	}
}

void Raytracing::InitD3D12(ID3D11Device* ppDevice, ID3D11DeviceContext* pImmediateContext, IDXGIAdapter* a_adapter)
{
	Hooks::InstallD3D11Hooks(ppDevice);

	if (settings.EnablePIXCapture) {
		// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
		// This may happen if the application is launched through the PIX UI.
		if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0) {
			auto pixGPUCapturerPath = GetLatestWinPixGpuCapturerPath();

			if (pixGPUCapturerPath.empty()) {
				logger::warn("[RT] PIX capture is enabled but binaries where not found.");
			} else {
				LoadLibrary(pixGPUCapturerPath.c_str());
			}
		}
	}

	logger::info("[RT] Creating D3D12 device");

	// Set Device
	DX::ThrowIfFailed(ppDevice->QueryInterface(IID_PPV_ARGS(&d3d11Device)));

	// Set Context Device
	DX::ThrowIfFailed(pImmediateContext->QueryInterface(IID_PPV_ARGS(&d3d11Context)));

	bool debugDevice = !settings.EnablePIXCapture && settings.EnableDebugDevice;

	// Create debug device
	if (debugDevice) {
		winrt::com_ptr<ID3D12Debug3> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
			debugController->SetEnableGPUBasedValidation(TRUE);
		} else {
			logger::critical("[RT] Debug layer creation failed.");
		}

		winrt::com_ptr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)))) {
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
	}

	if (settings.EnablePIXCapture) {
		DX::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ga)));
	}

	// Create Device
	{
		DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&d3d12Device)));

		// Check hardware raytracing tier
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
			if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
				if (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
					logger::info("[RT] Hardware ray tracing supported! Tier: {}", magic_enum::enum_name(options5.RaytracingTier));
				else
					logger::warn("[RT] Hardware ray tracing not supported.");
			}
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;

		DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandList)));

		DX::ThrowIfFailed(commandQueue->SetName(L"Command Queue"));
		DX::ThrowIfFailed(commandAllocator->SetName(L"Command Allocator"));
		DX::ThrowIfFailed(commandList->SetName(L"Command List"));

		DX::ThrowIfFailed(commandAllocator->Reset());
		DX::ThrowIfFailed(commandList->Reset(commandAllocator.get(), nullptr));
		//DX::ThrowIfFailed(commandList->Close());
	}

	if (debugDevice) {
		winrt::com_ptr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(d3d12Device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
		} else {
			logger::critical("[RT] Debug break creation failed.");
		}
	}

	// Create Interop
	{
		HANDLE sharedFenceHandle;
		DX::ThrowIfFailed(d3d12Device->CreateFence(fenceValue, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
		DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
		DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
		CloseHandle(sharedFenceHandle);
	}

	// D3D12 Memory Allocator
	{
		D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
		allocatorDesc.pDevice = d3d12Device.get();
		allocatorDesc.pAdapter = a_adapter;
		allocatorDesc.Flags = D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS;

		DX::ThrowIfFailed(D3D12MA::CreateAllocator(&allocatorDesc, allocator.put()));
	}

	// D3D12MA Pools
	{
		// Upload pool
		{
			D3D12MA::POOL_DESC poolDesc = {};
			poolDesc.HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
			poolDesc.Flags = D3D12MA_RECOMMENDED_POOL_FLAGS;
			poolDesc.HeapFlags = D3D12MA_RECOMMENDED_HEAP_FLAGS | D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

			DX::ThrowIfFailed(allocator->CreatePool(&poolDesc, uploadPool.put()));
		}

		// Default pools
		{
			D3D12MA::POOL_DESC poolDesc = {};
			poolDesc.HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			poolDesc.Flags = D3D12MA_RECOMMENDED_POOL_FLAGS;
			poolDesc.HeapFlags = D3D12MA_RECOMMENDED_HEAP_FLAGS | D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

			DX::ThrowIfFailed(allocator->CreatePool(&poolDesc, dynamicVertexPool.put()));
			DX::ThrowIfFailed(allocator->CreatePool(&poolDesc, vertexPool.put()));
			DX::ThrowIfFailed(allocator->CreatePool(&poolDesc, vertexCopyPool.put()));
			DX::ThrowIfFailed(allocator->CreatePool(&poolDesc, skinningPool.put()));
			DX::ThrowIfFailed(allocator->CreatePool(&poolDesc, trianglePool.put()));

			DX::ThrowIfFailed(allocator->CreatePool(&poolDesc, blasScratchPool.put()));
			DX::ThrowIfFailed(allocator->CreatePool(&poolDesc, blasPool.put()));
		}
	}

	if (settings.EnableDebugDevice || settings.EnablePIXCapture) {
		HANDLE disconnectEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(UINT64_MAX, disconnectEvent));

		std::thread([this, disconnectEvent]() {
			WaitForSingleObject(disconnectEvent, INFINITE);
			DeviceRemovedHandler();
		}).detach();
	}
}

void Raytracing::CreateRootSignature()
{
	// UAV range
	giHeap->CreateTable(
		GIHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ { GIHeap::Slot::Output, 1 },
			{ GIHeap::Slot::DiffuseAlbedoPathTracing, 1 },
			{ GIHeap::Slot::NormalRoughnessPathTracing, 1 },
			{ GIHeap::Slot::Reflectance, 1 },
			{ GIHeap::Slot::SpecularHitDist, 1 },
			{ GIHeap::Slot::SHaRCHashEntries, 1 },
			{ GIHeap::Slot::SHaRCLock, 1 },
			{ GIHeap::Slot::SHaRCAccumulation, 1 },
			{ GIHeap::Slot::SHaRCResolved, 1 } });

	// Fixed SRV ranges
	giHeap->CreateTable(
		GIHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { GIHeap::Slot::Main, 1, 0 },
			{ GIHeap::Slot::Depth, 1, 0 },
			{ GIHeap::Slot::Albedo, 1, 0 },
			{ GIHeap::Slot::NormalRoughness, 1, 0 },
			{ GIHeap::Slot::GNMD, 1, 0 },
			{ GIHeap::Slot::TLAS, 1, 0 },
			{ GIHeap::Slot::SkyHemisphere, 1, 0 },
			{ GIHeap::Slot::Lights, 1, 0 },
			{ GIHeap::Slot::Shapes, 1, 0 },
			{ GIHeap::Slot::Instances, 1, 0 } });

	// Vertex buffers (unbounded)
	giHeap->CreateTable(
		GIHeap::Table::VertexBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { GIHeap::Slot::Vertices, UINT_MAX, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } });

	// Triangle buffers (unbounded)
	giHeap->CreateTable(
		GIHeap::Table::TriangleBuffer,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { GIHeap::Slot::Triangles, UINT_MAX, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } });

	// Textures (unbounded)
	giHeap->CreateTable(
		GIHeap::Table::Textures,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { GIHeap::Slot::Textures, UINT_MAX, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE } });

	auto rootParameters = giHeap->GetRootParameters();

	CD3DX12_ROOT_PARAMETER1 constantRootParam;
	constantRootParam.InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
	rootParameters.push_back(constantRootParam);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);  // register s0

	auto flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
	             D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
	             D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
	             D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
	             D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

	// Create root signature
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		1,
		&staticSampler,
		flags);

	winrt::com_ptr<ID3DBlob> signature;
	winrt::com_ptr<ID3DBlob> error;

	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, signature.put(), error.put());

	if (FAILED(hr)) {
		if (error) {
			logger::error("[RT] D3DX12SerializeVersionedRootSignature {}", (char*)error->GetBufferPointer());
		}
		DX::ThrowIfFailed(hr);
	}

	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
	DX::ThrowIfFailed(rootSignature->SetName(L"RT Root Signature"));
}

void Raytracing::CreateShadowsRootSignature()
{
	// UAV range
	shadowHeap->CreateTable(
		ShadowsHeap::Table::UAV,
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		{ { ShadowsHeap::Slot::ShadowMask, 1 } });

	// SRV
	shadowHeap->CreateTable(
		ShadowsHeap::Table::SRV,
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		{ { ShadowsHeap::Slot::Depth, 1 },
			{ ShadowsHeap::Slot::TLAS, 1 } });

	auto rootParameters = shadowHeap->GetRootParameters();

	CD3DX12_ROOT_PARAMETER1 constantRootParam;
	constantRootParam.InitAsConstantBufferView(0, 0);
	rootParameters.push_back(constantRootParam);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		static_cast<uint>(rootParameters.size()),
		rootParameters.data(),
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	winrt::com_ptr<ID3DBlob> signature;
	winrt::com_ptr<ID3DBlob> error;

	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, signature.put(), error.put());

	if (FAILED(hr)) {
		if (error) {
			logger::error("[RT] D3DX12SerializeVersionedRootSignature {}", (char*)error->GetBufferPointer());
		}
		DX::ThrowIfFailed(hr);
	}

	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&shadowRS)));
	DX::ThrowIfFailed(shadowRS->SetName(L"Shadow Root Signature"));
}

void Raytracing::Initialize()
{
}

void Raytracing::ClearShaderCache()
{
	copyDepthCS = nullptr;  // This is actually optional
	CompileShaders();
}

void Raytracing::CompileShaders()
{
	if (!rootSignature) {
		CreateRootSignature();
		CompileRTGIShaders();
	}

	if (!shadowRS) {
		CreateShadowsRootSignature();
		CompileRTShadowsShaders();
	}

	CompileComputeShaders();
}

void Raytracing::CompileRTGIShaders()
{
	const auto bouncesWStr = std::to_wstring(settings.Bounces);
	const auto samplesWStr = std::to_wstring(settings.SamplesPerPixel);

	eastl::vector<DxcDefine> defines = {
		{ L"MAX_BOUNCES", bouncesWStr.c_str() },
		{ L"MAX_SAMPLES", samplesWStr.c_str() },
	};

	auto& advSettings = settings.AdvancedSettings;

	if (advSettings.RIS.Enabled)
		defines.emplace_back(L"RIS");

	const auto risMaxCandidates = std::to_wstring(static_cast<uint32_t>(advSettings.RIS.MaxCandidates));
	defines.emplace_back(L"RIS_MAX_CANDIDATES", risMaxCandidates.c_str());

	if (advSettings.GGXEnergyConservation)
		defines.emplace_back(L"GGX_ENERGY_CONSERVATION");

	if (advSettings.UseHairChiangBSDF)
		defines.emplace_back(L"HAIR_CHIANG_BSDF");

	if (advSettings.EnableSubsurfaceScattering)
		defines.emplace_back(L"SUBSURFACE_SCATTERING");

	const auto diffuseMode = std::to_wstring(static_cast<uint32_t>(advSettings.DiffuseBRDF));
	defines.emplace_back(L"DIFFUSE_MODE", diffuseMode.c_str());

	const auto lightEvalMode = std::to_wstring(static_cast<uint32_t>(advSettings.LightEvalMode));
	defines.emplace_back(L"LIGHTEVAL_MODE", lightEvalMode.c_str());

	const auto lightingMode = std::to_wstring(static_cast<uint32_t>(advSettings.LightingMode));
	defines.emplace_back(L"LIGHTING_MODE", lightingMode.c_str());

	if (settings.WhiteFurnace)
		defines.emplace_back(L"DEBUG_WHITE_FURNACE");

	if (settings.TraceMode == TraceMode::SHaRC)
		defines.emplace_back(L"SHARC");

	if (settings.PathTracing)
		defines.emplace_back(L"PATH_TRACING");

	if (settings.Denoiser == Denoiser::SVGF)
		defines.emplace_back(L"RAW_RADIANCE");

	const auto definesWStr = StringViewToWString(std::string_view{ debugDefines });

	if (!debugDefines.empty()) {
		defines.emplace_back(definesWStr.c_str());
	}

	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, L"Data/Shaders/Raytracing/GI/RayGeneration.hlsl", defines);

	winrt::com_ptr<IDxcBlob> missBlob, closestHitBlob, anyHitBlob;
	ShaderUtils::CompileShader(missBlob, L"Data/Shaders/Raytracing/GI/Miss.hlsl", defines);
	ShaderUtils::CompileShader(closestHitBlob, L"Data/Shaders/Raytracing/GI/ClosestHit.hlsl", defines);
	ShaderUtils::CompileShader(anyHitBlob, L"Data/Shaders/Raytracing/GI/AnyHit.hlsl", defines);

	winrt::com_ptr<IDxcBlob> shadowMissBlob, shadowAnyHitBlob;
	ShaderUtils::CompileShader(shadowMissBlob, L"Data/Shaders/Raytracing/GI/ShadowMiss.hlsl");
	ShaderUtils::CompileShader(shadowAnyHitBlob, L"Data/Shaders/Raytracing/GI/ShadowAnyHit.hlsl");

	DX12::RTPipelineBuilder pipelineBuilder;

	// Init pipeline
	{
		// Libraries
		pipelineBuilder.AddRayGenLib(rayGenBlob.get(), L"RayGeneration");

		pipelineBuilder.AddMissLib(missBlob.get(), L"IndirectMiss");
		pipelineBuilder.AddMissLib(shadowMissBlob.get(), L"ShadowMiss");

		pipelineBuilder.AddHitLib(closestHitBlob.get(), L"IndirectClosestHit");

		pipelineBuilder.AddAnyHitLib(anyHitBlob.get(), L"IndirectAnyHit");
		pipelineBuilder.AddAnyHitLib(shadowAnyHitBlob.get(), L"ShadowAnyHit");

		// Hit groups
		pipelineBuilder.AddHitGroup(L"IndirectHitGroup", L"IndirectClosestHit", L"IndirectAnyHit");
		pipelineBuilder.AddHitGroup(L"ShadowHitGroup", L"", L"ShadowAnyHit");

		// Shader + pipeline config
		pipelineBuilder.AddShaderConfig(20, 8);
		pipelineBuilder.AddGlobalRootSignature(rootSignature.get());
		pipelineBuilder.AddPipelineConfig(1);  // Max recursion depth

		auto desc = pipelineBuilder.MakeStateObjectDesc();

		auto createPipeline = [&](winrt::com_ptr<ID3D12StateObject>& pipeline, LPCWSTR name) {
			if (pipeline)
				pipeline = nullptr;

			HRESULT hr = d3d12Device->CreateStateObject(desc, IID_PPV_ARGS(&pipeline));

			if (FAILED(hr)) {
				logger::critical("CreateStateObject failed: {}", hr);
			}

			DX::ThrowIfFailed(hr);

			DX::ThrowIfFailed(pipeline->SetName(std::format(L"{} Pipeline", name).c_str()));
		};

		createPipeline(pipelineRT, L"RT");
	}

	// Init shader tables
	{
		winrt::com_ptr<ID3D12StateObjectProperties> props;
		pipelineRT->QueryInterface(props.put());

		uint64_t shaderBindingTableSizePrev = shaderBindingTable ? shaderBindingTable->GetTotalSize() : 0;

		if (shaderBindingTable)
			shaderBindingTable.reset();

		shaderBindingTable = eastl::make_unique<DX12::ShaderBindingTable>(pipelineBuilder.CreateShaderBindingTable(props.get()));

		auto shaderBindingTableSize = shaderBindingTable->GetTotalSize();
		logger::debug("[RT] GI SBT size: {}", shaderBindingTableSize);

		// Recreate buffer if necessary
		if (!shaderBindingTableBuffer || shaderBindingTableSize > shaderBindingTableSizePrev) {
			if (shaderBindingTableBuffer)
				shaderBindingTableBuffer.reset();

			shaderBindingTableBuffer = eastl::make_unique<DX12::ResourceUpload>(d3d12Device.get(), shaderBindingTableSize);
			shaderBindingTableBuffer->SetName(L"RT Shader Binding Table Buffer");
		}

		std::vector<uint8_t> shaderBindingTableCPU(shaderBindingTableSize);
		shaderBindingTable->Build(shaderBindingTableCPU.data());

		shaderBindingTable->LogShaderBindingTable(shaderBindingTableBuffer->resource->GetGPUVirtualAddress());

		shaderBindingTableBuffer->Update(shaderBindingTableCPU.data(), shaderBindingTableSize);
		shaderBindingTableBuffer->Upload(commandList.get());
		shaderBindingTableBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
}

void Raytracing::CompileRTShadowsShaders()
{
	winrt::com_ptr<IDxcBlob> shadowsRTBlob;
	ShaderUtils::CompileShader(shadowsRTBlob, L"Data/Shaders/Raytracing/ShadowsRT.hlsl");

	// Init pipeline
	{
		D3D12_DXIL_LIBRARY_DESC lib = {
			.DXILLibrary = {
				.pShaderBytecode = shadowsRTBlob->GetBufferPointer(),
				.BytecodeLength = shadowsRTBlob->GetBufferSize() }
		};

		D3D12_HIT_GROUP_DESC hitGroup = {
			.HitGroupExport = L"HitGroup",
			.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
			.ClosestHitShaderImport = L"ClosestHit"
		};

		D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {
			.MaxPayloadSizeInBytes = 4,
			.MaxAttributeSizeInBytes = 8,
		};

		D3D12_GLOBAL_ROOT_SIGNATURE globalSig = { shadowRS.get() };

		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 2 };

		D3D12_STATE_SUBOBJECT subobjects[] = {
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &lib },
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroup },
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderCfg },
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalSig },
			{ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, .pDesc = &pipelineCfg }
		};
		D3D12_STATE_OBJECT_DESC desc = { .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
			.NumSubobjects = std::size(subobjects),
			.pSubobjects = subobjects };

		HRESULT hr = d3d12Device->CreateStateObject(&desc, IID_PPV_ARGS(&shadowPipeline));

		if (FAILED(hr)) {
			logger::error("CreateStateObject failed: {}", hr);
		}

		DX::ThrowIfFailed(hr);

		DX::ThrowIfFailed(shadowPipeline->SetName(L"Shadow Pipeline"));
	}

	// Init shader tables
	{
		winrt::com_ptr<ID3D12StateObjectProperties> props;
		shadowPipeline->QueryInterface(props.put());

		size_t shaderBindingTableSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 3;

		shadowSBTBuffer = eastl::make_unique<DX12::ResourceUpload>(d3d12Device.get(), shaderBindingTableSize);
		shadowSBTBuffer->SetName(L"Shadows SBT");

		std::vector<uint8_t> shaderBindingTableCPU(shaderBindingTableSize);

		void* data = shaderBindingTableCPU.data();
		auto writeId = [&](const wchar_t* name) {
			void* id = props->GetShaderIdentifier(name);
			memcpy(data, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			data = static_cast<char*>(data) +
			       D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		};

		writeId(L"RayGeneration");
		writeId(L"Miss");
		writeId(L"HitGroup");

		shadowSBTBuffer->Update(shaderBindingTableCPU.data(), shaderBindingTableSize);
		shadowSBTBuffer->Upload(commandList.get());
		shadowSBTBuffer->TransitionBarrier(commandList.get(), D3D12_RESOURCE_STATE_GENERIC_READ);
	}
}

void Raytracing::CompileComputeShaders()
{
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CopyDepthCS.hlsl", {}, "cs_5_0")); rawPtr)
		copyDepthCS.attach(rawPtr);

	const auto skyHemiSize = std::to_string(RTConstants::SKY_HEMI_SIZE);
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CubeToHemiCS.hlsl", { { "RESOLUTION", skyHemiSize.c_str() } }, "cs_5_0")); rawPtr)
		cubeToHemiCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\ConvertTexturesCS.hlsl", {}, "cs_5_0")); rawPtr)
		convertTexturesCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\ConvertTexturesCS.hlsl", { { "PT", "" } }, "cs_5_0")); rawPtr)
		convertTexturesPTCS.attach(rawPtr);

	CompileCompositeShader();
}

void Raytracing::CompileCompositeShader()
{
	std::vector<std::pair<const char*, const char*>> defines;

	if (settings.ConvertToGamma) {
		defines.emplace_back("GAMMA_OUTPUT", "");
	}

	if (!settings.PathTracing && settings.Denoiser == Denoiser::SVGF) {
		defines.emplace_back("COMPOSITE", "");
		defines.emplace_back("DIFFUSE", "");
		defines.emplace_back("SPECULAR", "");
	}

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CompositeCS.hlsl", defines, "cs_5_0")); rawPtr)
		compositeCS.attach(rawPtr);

	std::vector<std::pair<const char*, const char*>> accDefines;
	accDefines.emplace_back("ACCUMULATION", "");
	if (settings.ConvertToGamma) {
		accDefines.emplace_back("GAMMA_OUTPUT", "");
	}
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CompositeCS.hlsl", accDefines, "cs_5_0")); rawPtr)
		accumulationCS.attach(rawPtr);
}

RaytracingFD::FeatureData Raytracing::GetCommonBufferData()
{
	RaytracingFD::FeatureData featureData{
		.InteriorDirectional = settings.GlobalIllumination ? 0.0f : 1.0f,
		.Ambient = settings.GlobalIllumination ? 0.0f : 1.0f,
		.EnvMap = settings.GlobalIllumination ? 0.0f : 1.0f,
		.Albedo = settings.GlobalIllumination
	};

	return featureData;
}

RE::BSEventNotifyControl Raytracing::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// When entering a loadscreen
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		logger::debug("MenuOpenCloseEventHandler::ProcessEvent - Opening: {}", a_event->opening);

		if (a_event->opening) {
			//auto& rt = globals::features::raytracing;
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Raytracing::TESLoadGameEventHandler::ProcessEvent(const RE::TESLoadGameEvent* a_event, RE::BSTEventSource<RE::TESLoadGameEvent>*)
{
	logger::debug("TESLoadGameEventHandler::ProcessEvent {}", reinterpret_cast<intptr_t>(a_event));

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Raytracing::TESObjectLoadedEventHandler::ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*)
{
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	auto* eventRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->formID);

	if (a_event->loaded)
		return RE::BSEventNotifyControl::kContinue;

	auto formID = eventRef->GetFormID();

	globals::features::raytracing.RemoveInstance(formID, true);

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Raytracing::CellAttachDetachEventHandler::ProcessEvent(const RE::CellAttachDetachEvent* a_event, RE::BSTEventSource<RE::CellAttachDetachEvent>*)
{
	bool attaching = a_event->status == RE::CellAttachDetachEvent::Status::StartAttach;
	bool detaching = a_event->status == RE::CellAttachDetachEvent::Status::StartDetach;

	if (!attaching && !detaching)
		return RE::BSEventNotifyControl::kContinue;

	auto& runtimeData = a_event->cell->GetRuntimeData();

	for (auto& reference : runtimeData.references) {	
		globals::features::raytracing.SetInstanceDetached(reference->GetFormID(), detaching);
	}

	auto* land = runtimeData.cellLand;

	if (!land)
		return RE::BSEventNotifyControl::kContinue;

	globals::features::raytracing.SetInstanceDetached(land->GetFormID(), detaching);

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Raytracing::BGSActorCellEventHandler::ProcessEvent(const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*)
{
	if (a_event->flags.underlying() != static_cast<uint32_t>(RE::BGSActorCellEvent::CellFlag::kEnter))
		return RE::BSEventNotifyControl::kContinue;

	auto* tesWaterSystem = RE::TESWaterSystem::GetSingleton();

	/*if (tesWaterSystem->waterObjects.empty()) {
		tesWaterSystem->waterObjects.push_back(RE::NiPointer(globals::features::raytracing.waterObject.get()));
	}*/

	if (tesWaterSystem->waterReflections.empty()) {
		tesWaterSystem->waterReflections.push_back(globals::features::raytracing.waterReflections);
	}

	tesWaterSystem->Enable();

	return RE::BSEventNotifyControl::kContinue;
}