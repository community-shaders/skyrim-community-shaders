#include "PhysicalSky.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <imgui_stdlib.h>
#include <set>

#include "CSEditor/EditorWindow.h"
#include "CSEditor/SceneManager/SceneSettingsContextRules.h"
#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "CloudShadows.h"
#include "Deferred.h"
#include "Features/ViewAtmosphere.h"
#include "I18n/I18n.h"
#include "LinearLighting.h"
#include "PostProcessing/ColorSpace.h"
#include "SkySync.h"
#include "TerrainShadows.h"

#include "I18n/I18n.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.physical_sky."
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PhysicalSky::WorldspaceInfo,
	zBottom)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TexNdfSettings,
	heightPath,
	modelingPath)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	NdfNoiseParameters,
	type, seed, frequency, octaves, persistence, lacunarity, contrast, bias, repetitions, responseExponent)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudNoiseBand,
	frequency, octaves, persistence, exponent, contrast, bias, perlinMix)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudNoiseSettings,
	skipShapeNoise, useVoxelNoise, procedural, seed, shape, warp, warpCorrelation)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	NdfNoiseInput,
	parameters, texturePath)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	NdfNoiseLayer,
	noise, frequency, exponent, offset, range)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	NdfGenerationParameters,
	primary, secondary, coverageGain, modeling, modelingGain, heightVariation,
	heightAuxiliaryRange, bottomTypeRange, baseHeight, bottomTypeExponent, heightFromCoverage,
	localBlendMode, localModelingWeight, localHeightWeight, localWindScale, windOffset)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ProceduralNdfSettings,
	parameters, noise, local, localMaskPath)

void to_json(nlohmann::json& j, const NdfSettings& value)
{
	j = { { "version", 3 }, { "type", value.type }, { "texture", value.texture }, { "procedural", value.procedural }, { "localCloud", value.localCloud }, { "localTexelSize", value.localTexelSize } };
}

void from_json(const nlohmann::json& j, NdfSettings& value)
{
	value = {};
	if (j.value("version", 0u) != 2u && j.value("version", 0u) != 3u)
		return;
	value.type = j.value("type", NdfType::Procedural) == NdfType::Texture ? NdfType::Texture : NdfType::Procedural;
	value.texture = j.value("texture", TexNdfSettings{});
	value.procedural = j.value("procedural", ProceduralNdfSettings{});
	value.localCloud = j.value("localCloud", SceneBlend{});
	value.localTexelSize = j.value("localTexelSize", 32.f);
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LowCloudSettings,
	ndfAltitudeOffset,
	ndfAltitudeScale,
	curvatureRadius,
	ndfScale,
	bottomSpreadScale,
	bottomSpreadHeight,
	bottomSoftness,
	bottomSoftnessHeight,
	topExpansionScale,
	densityScale)

void to_json(nlohmann::json& j, const CirrusSettings& value)
{
	j = { { "enabled", value.enabled }, { "altitude", value.altitude }, { "uvSize", value.uvSize }, { "densityScale", value.densityScale }, { "weather", value.weather } };
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MidCloudSettings,
	enabled, altitude, thickness, densityScale, uvSize, edgeSoftness,
	cellFrequency, cellStrength, stratusCellStrength, warpStrength, detailScale, detailMode, heightPower, bands, weather)

void from_json(const nlohmann::json& j, CirrusSettings& value)
{
	value = {};
	value.enabled = j.value("enabled", value.enabled);
	value.altitude = j.value("altitude", value.altitude);
	value.uvSize = j.value("uvSize", value.uvSize);
	value.densityScale = j.value("densityScale", value.densityScale);
	if (j.contains("weather") && SceneBlend::IsValid(j["weather"]))
		value.weather = j["weather"].get<SceneBlend>();
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudLightingSettings,
	brightness,
	sunExtinction,
	viewExtinction,
	dropletDiameter,
	phaseForwardG,
	phaseBackwardG,
	msAlbedo,
	msStrength,
	msBoundaryWidth,
	ambientStrength,
	scatterDensityThreshold,
	scatterDensityPower,
	cirrusLightThickness)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudWindSettings,
	lowVelocity, highVelocity, development, disturbance)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudLayer,
	low,
	middle,
	cirrus,
	lighting,
	wind)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PhysicalSky::Settings::LightningSettings,
	rate, distanceRange, radius, cloudLuminance, channelLuminance, channelRadius,
	strokeCount, strokeDuration, strokeInterval, strokeDecay,
	groundChance, cloudBaseChance, color)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PhysicalSky::Settings,
	enabled,
	enableAllExteriorCells,
	forceEnableAllInteriorCells,
	overrideDirLight,
	lightSkyStatics,
	skyStaticsBrightness,
	halfResApShadow,
	vanillaMix,
	trMix,
	apLumMix,
	apTrMix,
	cloudShadowRemapRange,
	sunlightColor,
	masserColor,
	secundaColor,
	proceduralSun,
	sunDiskRad,

	worldspaceWhitelist,
	groundAlbedo,
	planetRadius,
	atmosphereRadius,
	rayleighFalloff,
	rayleighScatter,
	rayleighScatterAP1,
	aerosolType,
	aerosolLoading,
	aerosolHumidity,
	aerosolFalloff,
	aerosolPhaseG,
	aerosolScatter,
	aerosolAbsorption,
	ozoneAltitude,
	ozoneThickness,
	ozoneAbsorption,
	ozoneAbsorptionAP1,
	fallbackZBottom,
	enableVanillaClouds,
	cloudRelightMix,
	cloudOriginalMix,
	silverLiningMix,
	silverLiningSpread,
	enableVolumetricClouds,
	lightningIntensity,
	lightning,
	rayMarchRange,
	shadowVolumeRange,
	marchStepScale,
	cloudBlurRadius,
	cloudMap,
	cloudLayer,
	cloudNoise,
	voxelClouds)

namespace
{
	RE::TESWorldSpace* GetCurrentWorldspace()
	{
		auto* tes = globals::game::tes ? globals::game::tes : RE::TES::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;

		auto* worldspace = tes ? tes->GetRuntimeData2().worldSpace : nullptr;
		if (!worldspace && cell)
			worldspace = cell->GetRuntimeData().worldSpace;

		return worldspace;
	}

	bool IsCurrentCellInterior()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		return cell && cell->IsInteriorCell();
	}

	std::string GetCurrentWorldspaceEditorID()
	{
		auto* worldspace = GetCurrentWorldspace();
		if (!worldspace)
			return {};

		return worldspace->GetFormEditorID();
	}

	std::string TrimEditorID(std::string editorID)
	{
		const auto first = std::ranges::find_if_not(editorID, [](unsigned char c) {
			return std::isspace(c);
		});
		const auto last = std::find_if_not(editorID.rbegin(), editorID.rend(), [](unsigned char c) {
			return std::isspace(c);
		}).base();

		if (first >= last)
			return {};

		return { first, last };
	}

	void InfoBox(const char* str)
	{
		if (ImGui::BeginTable("Info", 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame, { -1, 0 })) {
			ImGui::TableNextColumn();
			ImGui::TextWrapped(str);
			ImGui::EndTable();
		}
	}

	// Transform a float3 color/coefficient by a 3x3 color space matrix stored in a 4x4 Matrix
	float3 TransformColor(const float3& v, const DirectX::SimpleMath::Matrix& m)
	{
		return {
			v.x * m._11 + v.y * m._12 + v.z * m._13,
			v.x * m._21 + v.y * m._22 + v.z * m._23,
			v.x * m._31 + v.y * m._32 + v.z * m._33
		};
	}

	////////////////////////////////////////////////////////////////////////////////////////////////
	// Scene-scoped aerosol authoring.
	//
	// The scene layer blends numeric addresses, so a scene can only carry the aerosol appearance as
	// the coefficient triple the shader reads: the mixture, loading and humidity are authoring
	// inputs that derive that triple, and a discrete mixture cannot be interpolated at all. In a
	// scene page the feature therefore draws the picker below, which writes the derived coefficients
	// into the armed context's own entries, and leaves the authoring knobs to the main menu.

	constexpr std::string_view kSceneFeatureShortName = "PhysicalSky";

	/// Session UI state of the scene picker: the mixture a scene page is about to write, and the
	/// context it was seeded for. Not persisted, and deliberately outside `settings`.
	struct SceneAerosolPicker
	{
		Aerosol::Type type = Aerosol::Type::Custom;
		float loading = 1.f;
		float humidity = 50.f;
		SceneSettingsManager::SceneContextId context;
		bool seeded = false;
	};

	SceneAerosolPicker sceneAerosolPicker;

	/// One scene entry behind the aerosol optics the shader reads.
	struct AerosolOpticsAddress
	{
		std::string_view settingPath;  // a vector member carries its own path; the scalar has none
		std::string_view settingKey;
		const float* value;
	};

	/// The seven addresses a mixture resolves to. A vector's components are separate entries, keyed
	/// by component, which is how the catalog describes them.
	std::array<AerosolOpticsAddress, 7> GetAerosolOpticsAddresses(const Aerosol::Optics& a_optics)
	{
		return { { { "aerosolScatter", "x", &a_optics.scatter.x },
			{ "aerosolScatter", "y", &a_optics.scatter.y },
			{ "aerosolScatter", "z", &a_optics.scatter.z },
			{ "aerosolAbsorption", "x", &a_optics.absorption.x },
			{ "aerosolAbsorption", "y", &a_optics.absorption.y },
			{ "aerosolAbsorption", "z", &a_optics.absorption.z },
			{ "", "aerosolPhaseG", &a_optics.phaseG } } };
	}

	std::vector<std::string> GetAerosolOpticsSettingPath(const AerosolOpticsAddress& a_address)
	{
		std::vector<std::string> path;
		if (!a_address.settingPath.empty())
			path.emplace_back(a_address.settingPath);
		return path;
	}

	/// Whether an entry this context owns holds a value, rather than being a tombstone.
	bool IsAuthoredAerosolEntry(std::span<const SceneSettingsManager::SettingEntry> a_entries,
		std::optional<size_t> a_index)
	{
		return a_index && *a_index < a_entries.size() && !a_entries[*a_index].deleted;
	}

	/// Writes the coefficients a mixture resolves to into the armed context's own entries, so a
	/// weather or time-of-day transition interpolates the optics rather than switching the mixture.
	/// @return How many addresses the context now authors.
	size_t WriteSceneAerosolOptics(const SceneSettingsManager::SceneContextId& a_context,
		const Aerosol::Optics& a_optics)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto rules = SceneSettingsContextRules::GetSceneContextRules(a_context);
		const auto featureName = std::string(kSceneFeatureShortName);
		const auto addresses = GetAerosolOpticsAddresses(a_optics);

		// Structure first: clearing a tombstone or adding an entry renumbers the entries behind it,
		// so no index may be carried across this pass (the interceptor's guard resolves the same way).
		for (const auto& address : addresses) {
			const auto path = GetAerosolOpticsSettingPath(address);
			const auto key = std::string(address.settingKey);
			if (!SceneSettingsManager::IsSettingAllowedForType(rules.sceneType, featureName, path, key,
					rules.requireNumeric))
				continue;
			auto index = manager->FindContextUserEntry(a_context, featureName, path, key);
			if (index && !IsAuthoredAerosolEntry(manager->GetContextEntries(a_context), index)) {
				// A tombstone holds no value, so it goes before the new one lands.
				manager->ClearContextTombstone(a_context, featureName, path, key);
				index = std::nullopt;
			}
			if (!index)
				manager->AddContextSetting(a_context, featureName, path, key, true);
		}

		// Value pass: every address the context can hold now resolves to a live entry.
		std::array<SceneSettingsManager::EntryValueUpdate, 7> updates{};
		size_t count = 0;
		for (const auto& address : addresses) {
			const auto path = GetAerosolOpticsSettingPath(address);
			const auto key = std::string(address.settingKey);
			const auto index = manager->FindContextUserEntry(a_context, featureName, path, key);
			if (!index)
				continue;
			updates[count++] = { *index, json(*address.value) };
		}
		if (count == 0)
			return 0;

		// One atomic update, saved and reapplied once: the adds above only deferred their own save.
		manager->UpdateContextEntryValues(a_context, std::span(updates.data(), count));
		return count;
	}

	/// How many of the coefficients in a context come from the context's own entries.
	size_t CountSceneAerosolOptics(const SceneSettingsManager::SceneContextId& a_context)
	{
		const Aerosol::Optics none{};
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto featureName = std::string(kSceneFeatureShortName);
		const auto entries = manager->GetContextEntries(a_context);
		size_t authored = 0;
		for (const auto& address : GetAerosolOpticsAddresses(none)) {
			if (IsAuthoredAerosolEntry(entries,
					manager->FindContextUserEntry(a_context, featureName,
						GetAerosolOpticsSettingPath(address), std::string(address.settingKey))))
				++authored;
		}
		return authored;
	}

	/// Drops every coefficient this context authored, restoring inheritance from the layers below.
	void ClearSceneAerosolOptics(const SceneSettingsManager::SceneContextId& a_context)
	{
		const Aerosol::Optics none{};
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto featureName = std::string(kSceneFeatureShortName);
		for (const auto& address : GetAerosolOpticsAddresses(none)) {
			const auto path = GetAerosolOpticsSettingPath(address);
			const auto key = std::string(address.settingKey);
			const auto index = manager->FindContextUserEntry(a_context, featureName, path, key);
			if (index && IsAuthoredAerosolEntry(manager->GetContextEntries(a_context), index))
				manager->RemoveContextSetting(a_context, *index);
		}
	}

	/// The scene page's substitute for the authoring knobs: pick a mixture, and its coefficients
	/// become this context's own. The knobs themselves hold no scene value, which is why the scene
	/// policy bars them.
	void DrawSceneAerosolPicker(const SceneSettingsManager::SceneContextId& a_context,
		const char* const a_typeNames[], int a_typeCount)
	{
		const auto rules = SceneSettingsContextRules::GetSceneContextRules(a_context);
		// Judged on the scalar coefficient, which every context that can hold the aerosol look accepts.
		const bool allowed = SceneSettingsManager::IsSettingAllowedForType(rules.sceneType,
			std::string(kSceneFeatureShortName), {}, "aerosolPhaseG", rules.requireNumeric);

		ImGui::TextWrapped("%s", T(TKEY("aerosol_scene_desc"),
									 "Scenes blend the optical coefficients below, so a mixture is written as the coefficients it resolves to. Particle loading and relative humidity are authoring inputs and are not stored per scene."));

		ImGui::BeginDisabled(!allowed);

		bool changed = false;
		int type = static_cast<int>(sceneAerosolPicker.type);
		if (ImGui::Combo(T(TKEY("aerosol_type"), "Aerosol Type"), &type, a_typeNames, a_typeCount)) {
			sceneAerosolPicker.type = type > 0 && type < static_cast<int>(Aerosol::Type::Count) ?
			                              static_cast<Aerosol::Type>(type) :
			                              Aerosol::Type::Custom;
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("aerosol_type_desc"), "OPAC mixtures: continental clean, maritime clean, urban, and desert. Marine particles respond strongly to humidity; urban pollution includes absorbing soot. Dust also contains a water-soluble fraction."));

		if (sceneAerosolPicker.type != Aerosol::Type::Custom) {
			changed |= ImGui::SliderFloat(T(TKEY("aerosol_loading"), "Particle Loading"), &sceneAerosolPicker.loading, 0.f, 10.f, "%.2f x", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("aerosol_loading_desc"), "Multiplier of the selected type's reference dry particle number densities. 1x uses the OPAC reference mixture; 0 removes aerosols. Humidity changes particle size, not this loading. This is not AQI or PM2.5."));
			changed |= ImGui::SliderFloat(T(TKEY("aerosol_humidity"), "Relative Humidity"), &sceneAerosolPicker.humidity, 0.f, 99.f, "%.1f %%", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("aerosol_humidity_desc"), "Effective relative humidity throughout the aerosol layer. Uses OPAC hygroscopic growth data from 0 to 99% RH. Higher humidity usually increases scattering; absorption and forward scattering follow the selected mixture. Does not simulate condensation or fog droplets."));
		}

		if (changed && sceneAerosolPicker.type != Aerosol::Type::Custom) {
			WriteSceneAerosolOptics(a_context,
				Aerosol::Evaluate(sceneAerosolPicker.type, sceneAerosolPicker.loading, sceneAerosolPicker.humidity));
		}

		const auto authored = CountSceneAerosolOptics(a_context);
		if (authored > 0) {
			ImGui::Text(T(TKEY("aerosol_scene_authored"), "Aerosol coefficients stored for this scene: %zu of 7."), authored);
			ImGui::SameLine();
			if (ImGui::Button(T(TKEY("aerosol_scene_clear"), "Clear"))) {
				ClearSceneAerosolOptics(a_context);
				changed = true;
			}
		} else {
			ImGui::TextDisabled("%s", T(TKEY("aerosol_scene_inherited"), "Aerosol coefficients: inherited from the settings below the scene."));
		}

		ImGui::EndDisabled();

		if (!allowed)
			Util::AddTooltip(T(TKEY("aerosol_scene_unsupported"), "This place cannot hold the aerosol coefficients."), Util::kTooltipWhenDisabled);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void PhysicalSky::DataLoaded()
{
	cloudStormManager.LoadInstances();
	voxelCloudManager.LoadInstances();
	if (!globals::features::skySync.loaded) {
		failedLoadedMessage = T("feature.physical_sky.sky_sync_required", "Sky Sync is required for Physical Sky to function.");
		loaded = false;
	}
}

void PhysicalSky::RestoreDefaultSettings()
{
	settings = {};
	// The scene picker mirrors the inputs it seeds from, so it goes back with them.
	sceneAerosolPicker = {};
}

void PhysicalSky::LoadSettings(json& o_json)
{
	// The aerosol mixture, loading and humidity are authoring inputs: the value the shader reads and
	// the scene layer blends is the coefficient triple they resolve to. A document that moves one of
	// the three (a config, a preset, a feature override) therefore has to re-derive that triple,
	// while a document carrying only the scene layer's own values leaves the inputs alone - which is
	// what keeps a blend of the inputs from overwriting the blend of the coefficients.
	const auto loadedType = settings.aerosolType;
	const auto loadedLoading = settings.aerosolLoading;
	const auto loadedHumidity = settings.aerosolHumidity;
	settings = o_json;
	settings.cloudBlurRadius = std::clamp(settings.cloudBlurRadius, 0, 2);
	settings.SanitizeLightning();
	if (settings.aerosolType != loadedType ||
		settings.aerosolLoading != loadedLoading ||
		settings.aerosolHumidity != loadedHumidity)
		settings.ApplyAerosolOptics();
}

void PhysicalSky::SaveSettings(json& o_json)
{
	o_json = settings;
}

Aerosol::Optics PhysicalSky::Settings::GetAerosolOptics() const
{
	if (aerosolType > Aerosol::Type::Custom && aerosolType < Aerosol::Type::Count)
		return Aerosol::Evaluate(aerosolType, aerosolLoading, aerosolHumidity);
	return { aerosolScatter, aerosolAbsorption, aerosolPhaseG };
}

void PhysicalSky::Settings::ApplyAerosolOptics()
{
	// Custom already answers with the members, so this leaves an authored coefficient set untouched.
	const auto optics = GetAerosolOptics();
	aerosolScatter = optics.scatter;
	aerosolAbsorption = optics.absorption;
	aerosolPhaseG = optics.phaseG;
}

void PhysicalSky::DrawSettings()
{
	if (SceneWidgetInterceptor::IsArmed())
		ImGui::TextWrapped("%s", T(TKEY("scene_settings_hint"), "Scene settings control sunlight, atmosphere and the cloud weather: coverage, altitude, shape, motion, lighting and cirrus. Map source, noise and rendering quality stay global and are hidden while editing a scene. Interior settings require Physical Sky to be enabled for interiors."));

	if (ImGui::BeginTabBar("##PHYSSKY")) {
		if (ImGui::BeginTabItem(T(TKEY("general"), "General"))) {
			SettingsGeneral();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(T(TKEY("celestials"), "Celestials"))) {
			SettingsCelestials();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(T(TKEY("atmosphere"), "Atmosphere"))) {
			SettingsAtmosphere();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(T(TKEY("clouds"), "Clouds"))) {
			SettingsClouds();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(T(TKEY("weather_effects"), "Weather Effects"))) {
			SettingsWeatherEffects();
			ImGui::EndTabItem();
		}
		if (!SceneWidgetInterceptor::IsArmed() && ImGui::BeginTabItem(T(TKEY("debug"), "Debug"))) {
			SettingsDebug();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

void PhysicalSky::SettingsGeneral()
{
	const auto currentWorldspaceName = GetCurrentWorldspaceEditorID();
	const bool inInterior = IsCurrentCellInterior();

	if (ImGui::BeginTable("Info", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame, { -1, 0 })) {
		ImGui::TableNextColumn();
		ImGui::Text("%s", T(TKEY("shader_status"), "Shader Status: "));
		ImGui::TableNextColumn();
		if (ShadersOK())
			ImGui::TextColored({ 0, 1, 0, 1 }, T("feature.physical_sky.ok", "OK"));
		else
			ImGui::TextColored({ 1, 0, 0, 1 }, T("feature.physical_sky.error", "ERROR"));

		ImGui::TableNextColumn();
		ImGui::Text("%s", T(TKEY("worldspace"), "Worldspace: "));
		ImGui::TableNextColumn();

		if (inInterior) {
			if (settings.forceEnableAllInteriorCells)
				ImGui::Text("%s", T(TKEY("interior_enabled_forced"), "Interior (Enabled, Forced)"));
			else
				ImGui::Text("%s", T(TKEY("interior_disabled"), "Interior (Disabled)"));
		} else if (!currentWorldspaceName.empty()) {
			if (settings.worldspaceWhitelist.contains(currentWorldspaceName)) {
				ImGui::Text(T(TKEY("enabled_whitelist"), "%s (Enabled, Whitelist)"), currentWorldspaceName.c_str());
			} else if (settings.enableAllExteriorCells) {
				ImGui::Text(T(TKEY("enabled_fallback_z_bottom"), "%s (Enabled, Fallback Z Bottom)"), currentWorldspaceName.c_str());
			} else {
				ImGui::Text(T(TKEY("disabled"), "%s (Disabled)"), currentWorldspaceName.c_str());
			}
		} else {
			ImGui::Text("%s", T(TKEY("unknown_worldspace_unavailable"), "Unknown (Worldspace Unavailable)"));
		}

		ImGui::EndTable();
	}

	ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &settings.enabled);
	ImGui::SameLine();
	ImGui::Checkbox(T(TKEY("enable_all_exterior_cells"), "Enable All Exterior Cells"), &settings.enableAllExteriorCells);
	ImGui::SameLine();
	ImGui::Checkbox(T(TKEY("force_enable_all_interior_cells"), "Force Enable All Interior Cells"), &settings.forceEnableAllInteriorCells);
	if (settings.enableAllExteriorCells || settings.forceEnableAllInteriorCells) {
		ImGui::InputFloat(T(TKEY("fallback_z_bottom"), "Fallback Z Bottom"), &settings.fallbackZBottom, 10.f, 100.f, "%.1f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("used_when_current_worldspace_is_not_in_whitelist"), "Used when current worldspace is not in whitelist (or worldspace data is unavailable), including forced interiors."));
	}

	if (!SceneWidgetInterceptor::IsArmed()) {
		ImGui::SeparatorText(T(TKEY("worldspace_whitelist"), "Worldspace Whitelist"));
		static std::string newWorldspaceEditorID;
		static float newWorldspaceZBottom = -14500.f;

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
		ImGui::InputTextWithHint(T(TKEY("editor_id"), "Editor ID"), "Tamriel", &newWorldspaceEditorID);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
		ImGui::InputFloat(T(TKEY("z_bottom"), "Z Bottom"), &newWorldspaceZBottom, 10.f, 100.f, "%.1f");

		const auto addOrUpdateWorldspace = [&](std::string editorID, float zBottom) {
			editorID = TrimEditorID(std::move(editorID));
			if (!editorID.empty())
				settings.worldspaceWhitelist[editorID].zBottom = zBottom;
		};

		if (ImGui::Button(T(TKEY("add_update"), "Add / Update"))) {
			addOrUpdateWorldspace(newWorldspaceEditorID, newWorldspaceZBottom);
		}

		if (!currentWorldspaceName.empty()) {
			ImGui::SameLine();
			const auto currentIt = settings.worldspaceWhitelist.find(currentWorldspaceName);
			const bool currentWhitelisted = currentIt != settings.worldspaceWhitelist.end();
			if (ImGui::Button(currentWhitelisted ? T(TKEY("remove_current_worldspace"), "Remove Current Worldspace") : T(TKEY("add_current_worldspace"), "Add Current Worldspace"))) {
				if (currentWhitelisted) {
					settings.worldspaceWhitelist.erase(currentIt);
				} else {
					addOrUpdateWorldspace(currentWorldspaceName, newWorldspaceZBottom);
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", currentWorldspaceName.c_str());
		}

		if (ImGui::BeginTable("WorldspaceWhitelist", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, { -1, 0 })) {
			ImGui::TableSetupColumn(T(TKEY("editor_id"), "Editor ID"));
			ImGui::TableSetupColumn(T(TKEY("z_bottom"), "Z Bottom"), ImGuiTableColumnFlags_WidthFixed, 160.f);
			ImGui::TableSetupColumn(T(TKEY("action"), "Action"), ImGuiTableColumnFlags_WidthFixed, 90.f);
			ImGui::TableHeadersRow();

			std::string removeEditorID;
			for (auto& [editorID, info] : settings.worldspaceWhitelist) {
				ImGui::PushID(editorID.c_str());
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(editorID.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::InputFloat("##ZBottom", &info.zBottom, 10.f, 100.f, "%.1f");

				ImGui::TableSetColumnIndex(2);
				if (ImGui::Button(T(TKEY("remove"), "Remove"), { -1, 0 }))
					removeEditorID = editorID;

				ImGui::PopID();
			}

			if (!removeEditorID.empty())
				settings.worldspaceWhitelist.erase(removeEditorID);

			ImGui::EndTable();
		}
	}

	ImGui::SeparatorText(T(TKEY("post_processing"), "Post Processing"));
	{
		ImGui::SliderFloat(T(TKEY("vanilla_mix"), "Vanilla Mix"), &settings.vanillaMix, 0.f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("blend_in_vanilla_sky_color"), "Blend in vanilla sky color."));
	}
}

void PhysicalSky::SettingsCelestials()
{
	InfoBox(T(TKEY("the_sun_and_moons_and_their_lights"), "The sun and moons, and their lights."));

	ImGui::Checkbox(T(TKEY("override_directional_light"), "Override Directional Light"), &settings.overrideDirLight);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("overrides_the_color_of_directional_light_linear_tonemapper"), "Overrides the color of directional light. A 1.0 transmittance mix is recommended."));
	ImGui::SliderFloat(T(TKEY("transmittance_mix"), "Transmittance Mix"), &settings.trMix, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("apply_additional_atmospheric_tranmisttance_on_the_directional_light"),
							  "Apply additional atmospheric tranmisttance on the directional light.\n"
							  "Introduces natural yellowening at sunset with white sunlight."));

	ImGui::SeparatorText(T(TKEY("sky_statics"), "Sky Statics"));
	{
		ImGui::Checkbox(T(TKEY("light_sky_statics"), "Light Sky Statics"), &settings.lightSkyStatics);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("treat_sky_statics_as_albedo_and_tint_them"), "Treat sky statics as albedo and tint them with directional lighting."));

		ImGui::BeginDisabled(!settings.lightSkyStatics);
		ImGui::SliderFloat(T(TKEY("brightness"), "Brightness"), &settings.skyStaticsBrightness, 0.f, 4.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("multiplies_directional_lighting_applied_to_sky_statics"), "Multiplies directional lighting applied to sky statics."));
		ImGui::EndDisabled();
	}

	ImGui::SeparatorText(T(TKEY("sun"), "Sun"));
	{
		ImGui::PushID("Sun");
		ImGui::ColorEdit3(T(TKEY("light_color"), "Light Color"), &settings.sunlightColor.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("sun_light_color_hint"), "Linear RGB illuminance before atmospheric extinction. The lux control scales all channels together, preserving the color."));
		const char* illuminanceLabel = T(TKEY("sun_illuminance"), "Solar Illuminance");
		SceneWidgetInterceptor::InterceptFloat3(illuminanceLabel, &settings.sunlightColor.x, [&](float* rgb) {
			float3 color{ rgb[0], rgb[1], rgb[2] };
			float illuminance;
			// Match Color::RGBToLuminance in the active working gamut.
			if (globals::features::linearLighting.IsACEScgActive()) {
				static const auto matrix = getWhiteAdaptedRGBMatrix("sRGB", "ACEScg");
				const auto working = TransformColor(color, matrix);
				illuminance = working.x * 0.2722287168f + working.y * 0.6740817658f + working.z * 0.0536895174f;
			} else {
				illuminance = color.x * 0.2125f + color.y * 0.7154f + color.z * 0.0721f;
			}
			const float previous = illuminance;
			if (!ImGui::DragFloat(illuminanceLabel, &illuminance, 0.1f, 0.f, FLT_MAX, "%.3f lux", ImGuiSliderFlags_AlwaysClamp) || !std::isfinite(illuminance))
				return false;
			color = previous > 0.f ? color * (illuminance / previous) : float3(illuminance);
			rgb[0] = color.x;
			rgb[1] = color.y;
			rgb[2] = color.z;
			return true;
		});
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(T(TKEY("sun_illuminance_hint"), "Illuminance on a surface perpendicular to the sun, before atmospheric extinction: 10 lux supplies 10 linear lighting units. Calibrated for LL + PBR with Override Directional Light enabled. Directional Light Multiplier, horizon fading and shadows apply afterward."));
		ImGui::Checkbox(T(TKEY("procedural_sun"), "Procedural Sun"), &settings.proceduralSun);
		ImGui::SliderAngle(T(TKEY("sun_disk_angular_radius"), "Sun Disk Angular Radius"), &settings.sunDiskRad, 0.f, 10.f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("real_world_sun_disk_angular_radius_is_about"), "Real world sun disk angular radius is about 0.27 degrees."));
		ImGui::PopID();
	}

	ImGui::SeparatorText(T(TKEY("masser"), "Masser"));
	{
		ImGui::PushID("Masser");
		ImGui::ColorEdit3(T(TKEY("light_color"), "Light Color"), &settings.masserColor.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("light_color_hint"), "This sets the light color BEFORE it goes through the atmosphere i.e. extraterrestrial radiance."));
		ImGui::PopID();
	}

	ImGui::SeparatorText(T(TKEY("secunda"), "Secunda"));
	{
		ImGui::PushID("Secunda");
		ImGui::ColorEdit3(T(TKEY("light_color"), "Light Color"), &settings.secundaColor.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("light_color_hint"), "This sets the light color BEFORE it goes through the atmosphere i.e. extraterrestrial radiance."));
		ImGui::PopID();
	}
}

void PhysicalSky::SettingsAtmosphere()
{
	InfoBox(T(TKEY("the_composition_and_physical_properties_of_the_atmosphere"), "The composition and physical properties of the atmosphere."));

	ImGui::TextWrapped("%s", T("feature.view_atmosphere.quality_location", "Grid quality and temporal sampling are configured in Atmosphere and Fog."));

	ImGui::SliderFloat(T(TKEY("ap_luminance_mix"), "AP Luminance Mix"), &settings.apLumMix, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("add_light_scattered_by_air_aerial_perspective_to"), "Add light scattered by air (Aerial Perspective) to the scene."));
	ImGui::SliderFloat(T(TKEY("ap_transmittance_mix"), "AP Transmittance Mix"), &settings.apTrMix, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("remove_light_absorbed_by_air_aerial_perspective_from"), "Remove light absorbed by air (Aerial Perspective) from the scene."));

	if (globals::features::viewAtmosphere.settings.enabled)
		ImGui::TextWrapped("%s", T(TKEY("froxel_mix_hint"), "In the shared volume, AP mixes scale air scattering and extinction, including the foreground atmosphere of the sky. Fog density is independent."));

	ImGui::BeginDisabled(globals::features::viewAtmosphere.IsReady());
	ImGui::Checkbox(T(TKEY("half_resolution_cloud_shadow"), "Half Resolution Cloud Shadow"), &settings.halfResApShadow);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("froxel_shadow_hint"), "The shared atmosphere volume computes shadows per froxel. This option only affects the fallback AP shadow pass."));

	ImGui::SliderFloat2(T(TKEY("cloud_shadow_remap"), "Cloud Shadow Remap"), &settings.cloudShadowRemapRange.x, 0.f, 1.f, "%.2f");

	ImGui::SeparatorText(T(TKEY("air_molecules_rayleigh"), "Air Molecules (Rayleigh)"));
	{
		ImGui::PushID("Rayleigh");
		ImGui::TextWrapped("%s", T(TKEY("particles_much_smaller_than_the_wavelength_of_light"),
									 "Particles much smaller than the wavelength of light. They have almost complete symmetry in forward and backward scattering. "
									 "On earth, they are what makes the sky blue and, at sunset, red. Usually needs no extra change."));

		ImGui::ColorEdit3(T(TKEY("scatter_srgb"), "Scatter (sRGB)"), &settings.rayleighScatter.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::ColorEdit3(T(TKEY("scatter_ap1"), "Scatter (AP1)"), &settings.rayleighScatterAP1.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("band_averaged_coefficients_for_acescg_ap1_primaries_computed"), "Band-averaged coefficients for ACEScg (AP1) primaries, computed via CIE 1931 spectral integration."));
		ImGui::SliderFloat(T(TKEY("falloff"), "Falloff"), &settings.rayleighFalloff, 0.f, 2.f, "%.2f km^-1");
		ImGui::PopID();
	}

	ImGui::SeparatorText(T(TKEY("aerosol_mie"), "Aerosol (Mie)"));
	{
		ImGui::PushID("Mie");
		ImGui::TextWrapped("%s", T(TKEY("aerosol_model_desc"), "Suspended particles scatter and absorb light. Choose an aerosol type to control particle loading and relative humidity, or Custom to edit optical coefficients."));
		const char* types[] = {
			T(TKEY("aerosol_custom"), "Custom"),
			T(TKEY("aerosol_continental"), "Continental background"),
			T(TKEY("aerosol_maritime"), "Marine"),
			T(TKEY("aerosol_urban"), "Urban pollution"),
			T(TKEY("aerosol_desert"), "Desert dust")
		};
		// The proxy stays a plain cast of the stored enum: the scene settings catalog
		// resolves a combo's backing setting through exactly this shape, and a ternary
		// here would leave the setting out of the catalog.
		int type = static_cast<int>(settings.aerosolType);

		// A scene page authors the coefficients the shader reads, because that is the only part of
		// the aerosol look a transition can interpolate; the main menu authors the mixture those
		// coefficients come from. Both end up at the same three addresses, so the widgets below are
		// drawn once and the mixture never has to be an interpolated value.
		const auto* scene = SceneWidgetInterceptor::GetArmedContext();
		const bool sceneScoped = scene && !scene->baseline;
		if (sceneScoped) {
			// Seed once per context: a scene page opens on the look the author already sees.
			if (!sceneAerosolPicker.seeded || sceneAerosolPicker.context != scene->contextId) {
				sceneAerosolPicker.type = settings.aerosolType < Aerosol::Type::Count ?
				                              settings.aerosolType :
				                              Aerosol::Type::Custom;
				sceneAerosolPicker.loading = settings.aerosolLoading;
				sceneAerosolPicker.humidity = settings.aerosolHumidity;
				sceneAerosolPicker.context = scene->contextId;
				sceneAerosolPicker.seeded = true;
			}
			DrawSceneAerosolPicker(scene->contextId, types, IM_ARRAYSIZE(types));
		} else {
			if (type < 0 || type >= static_cast<int>(Aerosol::Type::Count))
				type = 0;
			if (ImGui::Combo(T(TKEY("aerosol_type"), "Aerosol Type"), &type, types, IM_ARRAYSIZE(types))) {
				// Selecting a mixture re-derives the coefficients; switching back to Custom keeps
				// them, because a physical mixture had already written them, so the look is
				// continuous either way.
				settings.aerosolType = static_cast<Aerosol::Type>(type);
				settings.ApplyAerosolOptics();
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("aerosol_type_desc"), "OPAC mixtures: continental clean, maritime clean, urban, and desert. Marine particles respond strongly to humidity; urban pollution includes absorbing soot. Dust also contains a water-soluble fraction."));

			if (type != 0) {
				if (ImGui::SliderFloat(T(TKEY("aerosol_loading"), "Particle Loading"), &settings.aerosolLoading, 0.f, 10.f, "%.2f x", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
					settings.ApplyAerosolOptics();
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("%s", T(TKEY("aerosol_loading_desc"), "Multiplier of the selected type's reference dry particle number densities. 1x uses the OPAC reference mixture; 0 removes aerosols. Humidity changes particle size, not this loading. This is not AQI or PM2.5."));
				if (ImGui::SliderFloat(T(TKEY("aerosol_humidity"), "Relative Humidity"), &settings.aerosolHumidity, 0.f, 99.f, "%.1f %%", ImGuiSliderFlags_AlwaysClamp))
					settings.ApplyAerosolOptics();
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("%s", T(TKEY("aerosol_humidity_desc"), "Effective relative humidity throughout the aerosol layer. Uses OPAC hygroscopic growth data from 0 to 99% RH. Higher humidity usually increases scattering; absorption and forward scattering follow the selected mixture. Does not simulate condensation or fog droplets."));
			}
		}
		// A scene page shows the coefficients whatever mixture they came from: they are what that
		// page stores, and the picker above is only a shortcut for writing them.
		if (sceneScoped || type == 0) {
			ImGui::SliderFloat(T(TKEY("anisotropy"), "Anisotropy"), &settings.aerosolPhaseG, -0.999f, 0.999f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::ColorEdit3(T(TKEY("scatter"), "Scatter"), &settings.aerosolScatter.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			ImGui::ColorEdit3(T(TKEY("absorption"), "Absorption"), &settings.aerosolAbsorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		}
		if (ImGui::TreeNode(T(TKEY("aerosol_vertical_distribution"), "Vertical Distribution"))) {
			ImGui::SliderFloat(T(TKEY("falloff"), "Falloff"), &settings.aerosolFalloff, 0.f, 2.f, "%.2f km^-1");
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	ImGui::SeparatorText(T(TKEY("ozone"), "Ozone"));
	{
		ImGui::PushID("Ozone");
		ImGui::TextWrapped("%s", T(TKEY("the_ozone_layer_high_up_in_the_sky"),
									 "The ozone layer high up in the sky that mainly absorbs light of certain wavelength. "
									 "It keeps the zenith sky blue, especially at sunrise or sunset."));

		ImGui::ColorEdit3(T(TKEY("absorption_srgb"), "Absorption (sRGB)"), &settings.ozoneAbsorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::ColorEdit3(T(TKEY("absorption_ap1"), "Absorption (AP1)"), &settings.ozoneAbsorptionAP1.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("band_averaged_coefficients_for_acescg_ap1_primaries_computed_2"), "Band-averaged coefficients for ACEScg (AP1) primaries, computed via CIE 1931 spectral integration."));
		ImGui::DragFloat(T(TKEY("mean_altitude"), "Mean Altitude"), &settings.ozoneAltitude, .1f, 0.f, 100.f, "%.3f km");
		ImGui::DragFloat(T(TKEY("layer_thickness"), "Layer Thickness"), &settings.ozoneThickness, .1f, 0.f, 50.f, "%.3f km");
		ImGui::PopID();
	}

	ImGui::SeparatorText(T(TKEY("planetary_parameters"), "Planetary Parameters"));
	{
		ImGui::InputFloat(T(TKEY("planet_radius"), "Planet Radius"), &settings.planetRadius, 1.f, 100000.f, "%.1f km");
		ImGui::InputFloat(T(TKEY("atmosphere_radius"), "Atmosphere Radius"), &settings.atmosphereRadius, 1.f, 100000.f, "%.1f km");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("planet_radius_is_the_distance_from_the_planet"),
								  "Planet radius is the distance from the planet center to sea level.\n"
								  "Atmosphere radius is the distance from the planet center to the top of atmosphere.\n"
								  "On Earth, they are about 6360 km and 6420 km respectively."));
	}
}

void PhysicalSky::SettingsClouds()
{
	DrawCloudStatus();
	ImGui::Spacing();

	ImGui::BeginDisabled(!settings.enableVolumetricClouds);
	SettingsVolumetricClouds();
	ImGui::EndDisabled();

	ImGui::Spacing();
	DrawVanillaCloudSection();
}

void PhysicalSky::DrawCloudStatus()
{
	const bool scene = SceneWidgetInterceptor::IsArmed();

	ImGui::Checkbox(T(TKEY("enable_volumetric_clouds"), "Enable Volumetric Clouds"), &settings.enableVolumetricClouds);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("enable_volumetric_clouds_tooltip"), "Enable ray-marched volumetric clouds with NDF-based cloud shapes."));

	if (settings.enableVolumetricClouds) {
		const bool ready = ShadersOK();
		ImGui::SameLine();
		Util::Badge(ready ? T(TKEY("cloud_status_ready"), "Ready") : T(TKEY("cloud_status_unavailable"), "Not Ready"),
			ready ? Util::Colors::GetSuccess() : Util::Colors::GetWarning());
		if (ImGui::IsItemHovered())
			Util::AddTooltip(T(TKEY("cloud_status_tooltip"),
				"Ready means the cloud shaders and the generated shape noise are available. Not Ready usually means shaders are still compiling or a required cloud texture is missing."));
	}

	if (scene) {
		if (!settings.enableVolumetricClouds)
			Util::Text::WrappedWarning("%s", T(TKEY("cloud_scene_disabled_global"),
												 "Volumetric clouds are disabled in the global feature settings, so this scene cannot show clouds until they are enabled there."));
		else
			Util::Text::WrappedSecondary("%s", T(TKEY("cloud_scene_scope"),
												   "Editing this scene's cloud weather. Map source, noise inputs and rendering quality stay global and are hidden here."));
	}

	DrawCloudLayerProfile();
}

void PhysicalSky::DrawCloudLayerProfile()
{
	const auto& low = settings.cloudLayer.low;
	const auto& cirrus = settings.cloudLayer.cirrus;
	const float2 altitudeKm = low.GetNdfAltitudeRangeKm();
	const float cirrusKm = cirrus.GetAltitudeKm();
	const auto middleAltitude = settings.cloudLayer.middle.GetAltitudeRange();
	const float scale = Util::GetUIScale();
	const float width = ImGui::GetContentRegionAvail().x;
	const float height = 184.0f * scale;
	if (width < 240.0f * scale || height <= 0.0f)
		return;

	const ImVec2 min = ImGui::GetCursorScreenPos();
	const ImVec2 max{ min.x + width, min.y + height };
	auto* drawList = ImGui::GetWindowDrawList();
	const ImVec4 accent = Util::Colors::GetAccent();
	const ImVec4 secondary = Util::Colors::GetSecondary();
	const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
	const ImU32 mutedColor = ImGui::ColorConvertFloat4ToU32(secondary);
	const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
	const ImU32 panelColor = ImGui::ColorConvertFloat4ToU32({ frame.x, frame.y, frame.z, 0.35f });
	const ImU32 bandEdge = ImGui::ColorConvertFloat4ToU32({ accent.x, accent.y, accent.z, 0.85f });
	const ImU32 gridColor = ImGui::ColorConvertFloat4ToU32({ accent.x, accent.y, accent.z, 0.12f });
	const ImVec4 popup = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
	const ImU32 chipColor = ImGui::ColorConvertFloat4ToU32({ popup.x, popup.y, popup.z, 0.82f });
	const float rounding = ImGui::GetStyle().FrameRounding;

	drawList->AddRectFilled(min, max, panelColor, rounding);

	const float left = min.x + 14.0f * scale;
	const float right = max.x - 14.0f * scale;
	const float top = min.y + 14.0f * scale;
	const float bottom = max.y - 14.0f * scale;
	const float maxKm = std::max({ altitudeKm.y, cirrus.enabled ? cirrusKm : 0.0f,
							settings.cloudLayer.middle.enabled ? middleAltitude.y * .001f : 0.f, 4.0f }) *
	                    1.12f;
	const auto yFor = [&](float km) {
		const float t = std::clamp(std::log1p(std::max(km, 0.0f) * 2.0f) / std::log1p(maxKm * 2.0f), 0.0f, 1.0f);
		return bottom - t * (bottom - top);
	};
	const auto labelAt = [&](const char* a_label, float centerY, ImU32 a_color, bool a_chip = false) {
		const ImVec2 size = ImGui::CalcTextSize(a_label);
		const float y = std::clamp(centerY - size.y * 0.5f, top, bottom - size.y);
		const ImVec2 pos{ left + 8.0f * scale, y };
		if (a_chip) {
			const ImVec2 pad{ 6.0f * scale, 2.0f * scale };
			drawList->AddRectFilled({ pos.x - pad.x, pos.y - pad.y },
				{ pos.x + size.x + pad.x, pos.y + size.y + pad.y }, chipColor, 3.0f * scale);
		}
		drawList->AddText(pos, a_color, a_label);
	};

	const float baseY = yFor(altitudeKm.x);
	const float topY = yFor(altitudeKm.y);
	const auto* slab = settings.enableVolumetricClouds && texCloudCrossSection && cloudCrossSectionReady ?
	                       texCloudCrossSection->srv.get() :
	                       nullptr;
	const int bandStrips = 12;
	for (int i = 0; i < bandStrips; ++i) {
		const float t0 = static_cast<float>(i) / bandStrips;
		const float t1 = static_cast<float>(i + 1) / bandStrips;
		const float alpha = slab ? std::lerp(0.22f, 0.08f, (t0 + t1) * 0.5f) :
		                           std::lerp(0.36f, 0.15f, (t0 + t1) * 0.5f);
		const ImU32 color = ImGui::ColorConvertFloat4ToU32({ accent.x, accent.y, accent.z, alpha });
		drawList->AddRectFilled({ left, topY + (baseY - topY) * t0 }, { right, topY + (baseY - topY) * t1 + 1.0f }, color);
	}
	if (slab) {
		drawList->AddImage(reinterpret_cast<ImTextureID>(slab), { left, topY }, { right, baseY },
			{ 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, 238));
	}
	drawList->AddRect({ left, topY }, { right, baseY }, bandEdge, 4.0f * scale, 0, 1.0f * scale);

	// A quiet axis on the left, with ticks where the data actually sits.
	drawList->AddLine({ left - 7.0f * scale, top }, { left - 7.0f * scale, bottom }, gridColor, 1.0f * scale);
	drawList->AddLine({ left - 10.0f * scale, baseY }, { left, baseY }, gridColor, 1.0f * scale);
	drawList->AddLine({ left - 10.0f * scale, topY }, { left, topY }, gridColor, 1.0f * scale);

	const std::string lowLabel = std::format("{}  {:.0f}-{:.0f} m", T(TKEY("cloud_profile_low"), "Low clouds"),
		altitudeKm.x * 1000.0f, altitudeKm.y * 1000.0f);
	labelAt(lowLabel.c_str(), (topY + baseY) * 0.5f, textColor, slab != nullptr);

	if (settings.cloudLayer.middle.enabled) {
		const float middleBottom = yFor(middleAltitude.x * .001f);
		const float middleTop = yFor(middleAltitude.y * .001f);
		drawList->AddRectFilled({ left, middleTop }, { right, middleBottom }, ImGui::GetColorU32({ accent.x, accent.y, accent.z, .18f }));
		drawList->AddRect({ left, middleTop }, { right, middleBottom }, bandEdge);
		const auto label = std::format("{}  {:.0f}-{:.0f} m", T(TKEY("cloud_profile_middle"), "Middle clouds (As / Ac)"), middleAltitude.x, middleAltitude.y);
		labelAt(label.c_str(), (middleTop + middleBottom) * .5f, textColor);
	}
	if (cirrus.enabled) {
		const float cirrusY = yFor(cirrusKm);
		drawList->AddLine({ left, cirrusY }, { right, cirrusY },
			ImGui::ColorConvertFloat4ToU32({ accent.x, accent.y, accent.z, 0.65f }), 1.5f * scale);
		drawList->AddLine({ left - 10.0f * scale, cirrusY }, { left, cirrusY }, gridColor, 1.0f * scale);
		const std::string cirrusLabel = std::format("{}  {:.0f} m", T(TKEY("cloud_profile_cirrus"), "Cirrus"), cirrusKm * 1000.0f);
		labelAt(cirrusLabel.c_str(), cirrusY - ImGui::GetTextLineHeight() * 0.75f, textColor);
	}

	labelAt(T(TKEY("cloud_profile_ground"), "Ground  0 m"), bottom - ImGui::GetTextLineHeight() * 0.5f, mutedColor);
	ImGui::Dummy({ width, height });
}

void PhysicalSky::SettingsWeatherEffects()
{
	if (!settings.enableVolumetricClouds)
		ImGui::TextWrapped("%s", T(TKEY("lightning_cloud_requirement"), "Lightning requires volumetric clouds. Enable them in Clouds to see the effect."));
	if (ImGui::CollapsingHeader(T(TKEY("lightning_section"), "Lightning"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("lightning_intensity"), "Lightning Intensity"), &settings.lightningIntensity, 0.f, 4.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_intensity_desc"), "Controls lightning, its illumination and thunder using Physical Sky weather settings, independently of vanilla weather. A nonzero value replaces vanilla flashes and thunder; zero restores them."));
		ImGui::DragFloat(T(TKEY("lightning_rate"), "Discharge Rate"), &settings.lightning.rate, 0.1f, 0.f, 600.f, "%.1f / min", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_rate_desc"), "Requested average discharge rate from Physical Sky weather settings. Discharges do not overlap, and preparing a new cloud source limits the actual frequency. Zero stops automatic discharges but keeps vanilla lightning suppressed while Lightning Intensity is nonzero."));
		ImGui::DragFloat2(T(TKEY("lightning_distance"), "Discharge Distance Range"), &settings.lightning.distanceRange.x, 0.05f, 0.05f, 50.f, "%.2f km", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_distance_desc"), "Minimum and maximum horizontal distance from the camera. A local storm uses its own footprint within this range."));
		ImGui::DragFloat(T(TKEY("lightning_radius"), "Luminous Cloud Radius"), &settings.lightning.radius, 10.f, 10.f, 5000.f, "%.0f m", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_radius_desc"), "Equivalent radius of the luminous cloud patch. A larger radius increases both its visible footprint and the area that illuminates the scene."));
		ImGui::DragFloat(T(TKEY("lightning_cloud_luminance"), "Peak Cloud Luminance"), &settings.lightning.cloudLuminance, 10.f, 0.f, 1.0e9f, "%.0f cd/m^2", ImGuiSliderFlags_AlwaysClamp);
		ImGui::DragFloat(T(TKEY("lightning_channel_luminance"), "Peak Channel Luminance"), &settings.lightning.channelLuminance, 10000.f, 0.f, 1.0e9f, "%.0f cd/m^2", ImGuiSliderFlags_AlwaysClamp);
		Util::Text::WrappedSecondary("%s", T(TKEY("lightning_photometry_desc"), "Uses the same photometric scale as Solar Illuminance: 1 lux is 1 linear illuminance unit. Luminance and emitting area determine scene illuminance. Calibrated for Linear Lighting and PBR; exposure and lighting multipliers apply afterward. Lightning Intensity multiplies these peak values."));
		ImGui::DragFloat(T(TKEY("lightning_channel_radius"), "Channel Radius"), &settings.lightning.channelRadius, 0.01f, 0.001f, 5.f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_channel_radius_desc"), "Physical radius of the main luminous channel. Its projected area also determines its contribution to scene illumination."));
		ImGui::SliderInt(T(TKEY("lightning_stroke_count"), "Return Strokes"), &settings.lightning.strokeCount, 1, 1024, "%d", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_stroke_count_desc"), "Repeated flashes along one channel, not new lightning locations. A long discharge delays the next location even at a high discharge rate. Use New Lightning to interrupt the current discharge."));
		ImGui::DragFloat(T(TKEY("lightning_stroke_duration"), "Stroke Duration"), &settings.lightning.strokeDuration, 1.f, 1.f, 60000.f, "%.0f ms", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_stroke_duration_desc"), "Full optical pulse duration: a fast rise followed by a fading tail. Peak luminance stays fixed; the light received by each frame is integrated over its duration."));
		ImGui::DragFloat(T(TKEY("lightning_stroke_interval"), "Stroke Interval"), &settings.lightning.strokeInterval, 1.f, 1.f, 60000.f, "%.0f ms", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_stroke_interval_desc"), "Time between stroke starts, at least one stroke duration. Settings apply to the next discharge or replay."));
		ImGui::SliderFloat(T(TKEY("lightning_stroke_decay"), "Return Stroke Strength"), &settings.lightning.strokeDecay, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_stroke_decay_desc"), "Brightness retained by each successive stroke. One keeps every stroke at the configured peak."));
		ImGui::SliderFloat(T(TKEY("lightning_ground_chance"), "Ground Strike Chance"), &settings.lightning.groundChance, 0.f, 100.f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_ground_chance_desc"), "Attempts a ground strike against loaded terrain. If no surface is found, the discharge stays near the cloud base."));
		ImGui::SliderFloat(T(TKEY("lightning_base_chance"), "Cloud Base Discharge Chance"), &settings.lightning.cloudBaseChance, 0.f, 100.f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip(T(TKEY("lightning_base_chance_desc"), "Cloud-base discharges in addition to ground strikes. The remaining percentage is embedded cloud lightning. Both chances together cannot exceed 100%."));
		ImGui::ColorEdit3(T(TKEY("lightning_color"), "Lightning Color"), &settings.lightning.color.x, ImGuiColorEditFlags_Float);
		settings.SanitizeLightning();
		ImGui::Text(T(TKEY("lightning_duration"), "Discharge Duration: %.2f s"), ((settings.lightning.strokeCount - 1) * settings.lightning.strokeInterval + settings.lightning.strokeDuration) * 0.001f);
		if (settings.enableVolumetricClouds && settings.lightningIntensity > 0.f && (!csLightningQuery || !csLightningComposite || !csLightningCubeComposite))
			Util::Text::Error("%s", T(TKEY("lightning_unavailable"), "Lightning rendering is unavailable. Nonzero Lightning Intensity still suppresses vanilla flashes and thunder. See the log for details."));
		if (ImGui::Button(T(TKEY("lightning_trigger"), "New Lightning")))
			lightning.Trigger();
		Util::AddTooltip(T(TKEY("lightning_trigger_desc"), "Creates a new source using the current distance, radius and event chances. Requires active Physical Sky lightning and a suitable cloud."));
		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("lightning_replay"), "Replay Lightning")))
			lightning.Replay();
		Util::AddTooltip(T(TKEY("lightning_replay_desc"), "Replays the last source and channel geometry with the current brightness and timing. Requires active Physical Sky lightning. If there is no previous source, searches for one in the configured range."));
	}
}

void PhysicalSky::SettingsVolumetricClouds()
{
	DrawLowCloudSection();

	ImGui::SetNextItemOpen(settings.cloudLayer.middle.enabled, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader(T(TKEY("middle_clouds"), "Middle Clouds (As / Ac)"), ImGuiTreeNodeFlags_SpanAvailWidth))
		MidCloudMapManager::DrawSettings(settings.cloudLayer.middle, ndfTexManager);
	DrawCirrusSection();
	DrawCloudStormSection();
	if (ImGui::CollapsingHeader(T(TKEY("voxel_section"), "Voxel Clouds"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
		const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
		voxelCloudManager.DrawSettings(settings.voxelClouds, GetCurrentWorldspaceEditorID(),
			{ camera.x * Util::Units::GAME_UNIT_TO_M, camera.y * Util::Units::GAME_UNIT_TO_M, (camera.z - cbData.zBottom) * Util::Units::GAME_UNIT_TO_M });
	}
	DrawCloudMotionSection();
	DrawCloudLightingSection();
	DrawCloudResourcesSection();
	DrawCloudQualitySection();
}

void PhysicalSky::DrawWeatherMapSection()
{
	const bool scene = SceneWidgetInterceptor::IsArmed();

	if (ImGui::TreeNode(T(TKEY("cloud_weather_map"), "Weather Map"))) {
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_weather_map_desc"),
											   "Low-cloud coverage, cloud types and height distribution. Noise inputs are configured under Shared Noise & Textures."));
		DrawCloudWeatherPreview(false);
		if (!scene) {
			if (ImGui::TreeNode(T(TKEY("cloud_map_source"), "Map Source"))) {
				ndfManager.DrawNdfSource(settings.cloudMap, ndfTexManager);
				ImGui::TreePop();
			}
			Util::Text::Secondary("%s: %s", T(TKEY("cloud_source_label"), "Source"), NdfManager::GetSettingsTypeName(settings.cloudMap));
		}

		ndfManager.DrawNdfShape(settings.cloudMap.procedural.parameters);

		if (ImGui::TreeNode(T(TKEY("ndf_overlay_section"), "Generator Overlay"))) {
			ndfManager.DrawNdfOverlay(settings.cloudMap, ndfTexManager);
			ImGui::TreePop();
		}

		ImGui::TreePop();
	}
}

void PhysicalSky::DrawCloudWeatherPreview(bool middle, bool standalone)
{
	const size_t layer = middle ? 1 : 0;
	ImGui::PushID(middle ? "MiddleWeatherPreview" : "LowWeatherPreview");
	if (!standalone) {
		const float available = ImGui::GetContentRegionAvail().x;
		ImGui::Checkbox(T(TKEY("cloud_preview_show"), "Show live preview"), &showCloudWeatherPreview[layer]);
		auto* editor = EditorWindow::GetSingleton();
		if (editor->open) {
			const auto* label = T(TKEY("cloud_preview_open_window"), "Open in Editor window");
			const auto& style = ImGui::GetStyle();
			if (ImGui::GetItemRectSize().x + style.ItemSpacing.x + ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.f <= available)
				ImGui::SameLine();
			if (Util::Button(label)) {
				editor->showCloudWeatherMaps = true;
				editor->cloudWeatherPreviewTab = static_cast<int>(layer);
			}
		}
	}
	if (standalone || showCloudWeatherPreview[layer]) {
		const auto& live = globals::features::physicalSky;
		if (!loaded || !live.cbData.enabled || !live.settings.enableVolumetricClouds || (middle && !live.settings.cloudLayer.middle.enabled)) {
			Util::Text::WrappedSecondary("%s", T(TKEY("cloud_preview_inactive"), "Enable this cloud layer in the active scene to generate its live weather map."));
		} else {
			const char* channels[] = {
				T(TKEY("ndf_preview_coverage"), "Coverage"),
				T(TKEY("cloud_preview_type"), "Cloud type"),
				T(TKEY("ndf_preview_bottom_height"), "Bottom height"),
				T(TKEY("ndf_preview_top_height"), "Top height")
			};
			auto& channel = cloudWeatherPreviewChannel[layer];
			ImGui::Combo(T(TKEY("cloud_preview_channel"), "Display"), &channel, channels, IM_ARRAYSIZE(channels));
			Util::Text::WrappedSecondary("%s", T(TKEY("cloud_preview_live_hint"), "Live map of the active scene, including weather transitions. It shows the weather field before cloud detail and density scaling."));
			if (channel == 0)
				Util::Text::WrappedSecondary("%s", T(TKEY("cloud_preview_coverage_legend"), "Black: clear   White: full coverage"));
			else if (channel == 1)
				Util::Text::WrappedSecondary("%s", middle ? T(TKEY("cloud_preview_middle_type_legend"), "Black: altostratus (As)   White: altocumulus (Ac)") : T(TKEY("cloud_preview_low_type_legend"), "Black to white: increasing top cloud type"));
			else
				Util::Text::WrappedSecondary("%s", T(TKEY("cloud_preview_height_legend"), "Black: layer base   White: top of the layer's height range"));
			const auto maps = ndfManager.GetNdf(live.settings.cloudMap, ndfTexManager);
			const auto* id = standalone ? (middle ? "middleWeatherWindow" : "lowWeatherWindow") : (middle ? "middleWeatherInline" : "lowWeatherInline");
			ID3D11ShaderResourceView* source = middle ? midCloudMapManager.Weather() : channel >= 2 ? maps.height :
			                                                                                          maps.modeling;
			const uint32_t component = middle ? std::array{ 1u, 4u, 2u, 3u }[channel] : std::array{ 1u, 2u, 1u, 2u }[channel];
			const float width = std::max(1.f, std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * (standalone ? 32.f : 20.f)));
			texturePreview.DrawMap(source, id, component, middle && channel != 0, width);
		}
	}
	ImGui::PopID();
}

void PhysicalSky::DrawCloudResourcesSection()
{
	if (SceneWidgetInterceptor::IsArmed())
		return;

	if (ImGui::CollapsingHeader(T(TKEY("cloud_resources"), "Shared Noise & Textures"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_resources_desc"),
											   "Global inputs shared by the cloud layers. Weather noise supplies coverage and type; shape noise adds detail to low and middle clouds."));
		if (ImGui::TreeNode(T(TKEY("cloud_weather_noise"), "Weather Noise"))) {
			ndfManager.DrawNdfNoise(settings.cloudMap, ndfTexManager);
			ImGui::TreePop();
		}
		if (ImGui::TreeNode(T(TKEY("cloud_shape_noise"), "Shape Noise"))) {
			cloudNoiseGenerator.DrawSettings(settings.cloudNoise);
			ImGui::Checkbox(T(TKEY("ndf_use_voxel_noise"), "Use NVDF Noise for NDF Clouds"), &settings.cloudNoise.useVoxelNoise);
			ImGui::TreePop();
		}
		if (ImGui::TreeNode(T(TKEY("ndf_texture_inputs"), "Texture inputs"))) {
			ndfTexManager.DrawUI();
			ImGui::TreePop();
		}
		if (Util::Button(T(TKEY("reload_cloud_textures"), "Reload Cloud Textures"), { -FLT_MIN, 0 })) {
			ndfTexManager.Reload();
			localNdfManager.RefreshAssets();
			LoadCloudTextures();
		}
		ImGui::Text("%s: %s", T(TKEY("cloud_shape_source"), "Shape source"),
			(settings.cloudNoise.procedural || !importedShapeNoiseSrv) ? T(TKEY("cloud_source_generated"), "Generated") : T(TKEY("cloud_source_dds"), "DDS"));
		ImGui::Text("%s: %s", T(TKEY("cloud_profile_source"), "Profile source"), cloudProfileGenerated ? T(TKEY("cloud_source_packed"), "Packed top/bottom or analytic fallback") : T(TKEY("cloud_source_dds"), "DDS"));
		ImGui::Text("%s: %s", T(TKEY("cloud_adjustment_source"), "Adjustment source"), (settings.cloudNoise.procedural || cloudAdjustmentGenerated) ? T(TKEY("cloud_source_generated"), "Generated") : T(TKEY("cloud_source_dds"), "DDS"));
		if (baseShapeNoiseSrv && cloudProfileLutSrv && cloudAdjustmentLutSrv)
			ImGui::TextColored(Util::Colors::GetSuccess(), "%s", T(TKEY("cloud_textures_loaded"), "Cloud Textures: Loaded"));
		else
			Util::Text::WrappedWarning("%s", T(TKEY("cloud_textures_preparing"), "Cloud Textures: Preparing"));
	}
}

void PhysicalSky::DrawLowCloudSection()
{
	auto& low = settings.cloudLayer.low;

	ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader(T(TKEY("composition"), "Low Clouds"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_low_desc"),
											   "The main deck: altitude and scale, silhouette shaping, and opacity."));

		ImGui::SeparatorText(T(TKEY("cloud_density_group"), "Density"));
		ImGui::SliderFloat(T(TKEY("cloud_density_scale"), "Extinction Scale"), &low.densityScale, 0.f, 1.f, "%.3f 1/m");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("cloud_density_scale_desc"), "Extinction per metre equals this scale times the reconstructed cloud density. It controls view opacity, sunlight attenuation and cloud shadows. At density 0.1 and scale 0.75, a 40 m path transmits about 5% of light."));

		ImGui::SeparatorText(T(TKEY("cloud_altitude_group"), "Altitude & Scale"));
		ImGui::DragFloat(T(TKEY("ndf_altitude_offset"), "NDF Base Altitude"), &low.ndfAltitudeOffset, 1.f, 0.f, 20000.f, "%.0f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::DragFloat(T(TKEY("ndf_altitude_scale"), "NDF Height Span"), &low.ndfAltitudeScale, 1.f, 1.f, 20000.f, "%.0f m", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("ndf_altitude_encoding"), "Altitude = base altitude + NDF height * height span. Applies to both generated and imported maps."));
		ImGui::SliderFloat2(T(TKEY("ndf_scale"), "NDF Scale"), &low.ndfScale.x, 1.f, 50.f, "%.2f km");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("ndf_scale_desc"), "Horizontal size of the weather map. Larger values stretch cloud masses across more of the sky."));
		if (ImGui::SliderFloat(T(TKEY("cloud_curvature_radius"), "Cloud Curvature Radius"), &low.curvatureRadius, 25.f, 10000.f, "%.2f km", ImGuiSliderFlags_Logarithmic))
			volMainHistoryValid = false;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("cloud_curvature_radius_desc"), "Controls how far clouds bend below the horizon. Smaller radii increase the curvature; NDF thickness stays fixed."));

		DrawWeatherMapSection();

		ImGui::SeparatorText(T(TKEY("cloud_shape_group"), "Shape"));
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_weather_shape_desc"), "These weather controls shape both global and local NDF clouds. A local map changes coverage, types and heights; it does not carry a separate density or softness preset."));
		ImGui::SliderFloat(T(TKEY("cloud_bottom_spread_scale"), "Bottom Spread"), &low.bottomSpreadScale, 0.f, 1.f, "%.3f");
		ImGui::SliderFloat(T(TKEY("cloud_bottom_spread_height"), "Bottom Spread Height"), &low.bottomSpreadHeight, 0.001f, 1.f, "%.3f");
		ImGui::SliderFloat(T(TKEY("cloud_bottom_softness"), "Bottom Softness"), &low.bottomSoftness, 0.f, 1.f, "%.3f");
		ImGui::SliderFloat(T(TKEY("cloud_bottom_softness_height"), "Bottom Softness Height"), &low.bottomSoftnessHeight, 0.f, 1.f, "%.3f");
		ImGui::SliderFloat(T(TKEY("cloud_top_expansion"), "Top Expansion"), &low.topExpansionScale, 0.f, 1.f, "%.3f");
		if (ImGui::TreeNode(T(TKEY("cloud_local_section"), "Local Cloud"))) {
			Util::Text::WrappedSecondary("%s", T(TKEY("cloud_local_desc"),
												   "A single authored cloud map placed in the world for hero shapes; it replaces the weather map inside its rectangle."));
			const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
			localNdfManager.DrawSettings(settings.cloudMap, GetCurrentWorldspaceEditorID(),
				{ camera.x * Util::Units::GAME_UNIT_TO_M, camera.y * Util::Units::GAME_UNIT_TO_M });
			ImGui::TreePop();
		}
	}
}

void PhysicalSky::DrawCirrusSection()
{
	auto& cirrus = settings.cloudLayer.cirrus;

	ImGui::SetNextItemOpen(cirrus.enabled, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader(T(TKEY("cirrus"), "Cirrus"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_cirrus_desc"),
											   "The high ice-cloud veil. It is a separate layer with its own weather map and lighting."));
		CirrusMapManager::DrawSettings(cirrus, settings.cloudLayer.lighting, ndfTexManager);
	}
}

void PhysicalSky::DrawCloudMotionSection()
{
	auto& wind = settings.cloudLayer.wind;

	ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader(T(TKEY("cloud_motion"), "Cloud Motion"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_motion_desc"),
											   "How the weather pattern travels and evolves: X/Y velocity for direction and speed, development and disturbance for shape change."));

		ImGui::SliderFloat2(T(TKEY("cloud_low_wind_velocity"), "Low Cloud Velocity (X/Y)"), &wind.lowVelocity.x, -80.f, 80.f, "%.1f m/s", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat2(T(TKEY("cloud_high_wind_velocity"), "Middle / Cirrus Velocity (X/Y)"), &wind.highVelocity.x, -80.f, 80.f, "%.1f m/s", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("cloud_wind_velocity_desc"), "Horizontal velocity in world X/Y axes. Scene transitions blend the components, including across direction changes. The resulting travel speed is capped at 80 m/s."));
		ImGui::SliderFloat(T(TKEY("cloud_development"), "Development Speed"), &wind.development, 0.f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("cloud_development_desc"), "Controls internal low-cloud evolution. Zero freezes development while wind continues to carry the clouds."));
		ImGui::SliderFloat(T(TKEY("cloud_disturbance"), "Disturbance Strength"), &wind.disturbance, 0.f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("cloud_disturbance_desc"), "Controls local low-cloud deformation. High and low wind differences also produce a gradual cloud lean."));

		const auto speed = [](const float2& velocity) {
			return std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
		};
		Util::Text::Secondary("%s  %.1f m/s     %s  %.1f m/s",
			T(TKEY("cloud_motion_low_speed"), "Low deck"), speed(wind.lowVelocity),
			T(TKEY("cloud_motion_high_speed"), "Middle / Cirrus"), speed(wind.highVelocity));
	}
}

void PhysicalSky::DrawCloudLightingSection()
{
	auto& lighting = settings.cloudLayer.lighting;

	ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader(T(TKEY("cloud_lighting"), "Cloud Lighting"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_lighting_method"), "Low clouds combine single scattering, energy-normalized local multiple scattering and ambient illumination. Albedo controls absorption and droplet size controls angular scattering. Cirrus uses a thin-layer approximation."));

		ImGui::SeparatorText(T(TKEY("cloud_lighting_intensity"), "Intensity & Transmission"));
		ImGui::SliderFloat(T(TKEY("cloud_brightness"), "Cloud Brightness"), &lighting.brightness, 0.f, 4.f, "%.3f");
		ImGui::SliderFloat(T(TKEY("cloud_sun_extinction"), "Directional Extinction Scale"), &lighting.sunExtinction, 0.f, 4.f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_sun_extinction_desc"), "Scales directional-light attenuation for single and multiple scattering, and cloud shadows. A value of one uses the medium extinction. Ambient transport uses the unscaled medium optical depth."));
		ImGui::SliderFloat(T(TKEY("cloud_view_extinction"), "Low Cloud View Extinction Scale"), &lighting.viewExtinction, 0.f, 4.f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_view_extinction_desc"), "Scales low-cloud opacity along the viewing ray and the corresponding scattering integral. A value of one uses the medium extinction. Light-path attenuation and cirrus are independent of this control."));

		ImGui::SeparatorText(T(TKEY("cloud_lighting_scattering"), "Scattering"));
		ImGui::SliderFloat(T(TKEY("cloud_droplet_diameter"), "Mean Water Droplet Diameter"), &lighting.dropletDiameter, 5.f, 50.f, "%.1f um", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_droplet_diameter_desc"), "Mean water-droplet diameter in micrometers for the normalized HG-Draine Mie approximation. Larger droplets concentrate the forward peak into a narrower angle. The fitted range is 5 to 50 micrometers. The phase determines direct angular scattering and the multiple-scattering transport coefficient."));
		ImGui::SliderFloat(T(TKEY("cloud_ms_strength"), "Multiple Scattering Strength"), &lighting.msStrength, 0.f, 2.f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_ms_strength_desc"), "Scales local multiple scattering. One uses the finite-cloud energy calibration. Zero shows single scattering and ambient illumination."));
		ImGui::SliderFloat(T(TKEY("cloud_ms_boundary_width"), "Multiple Scattering Boundary Width"), &lighting.msBoundaryWidth, 1.f, 200.f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_ms_boundary_width_desc"), "Width of the local cloud boundary profile in meters, limited to one quarter of the cloud thickness. Controls density-dependent energy retention and dark edges. Energy calibration accounts for absorption and escape."));
		ImGui::SliderFloat(T(TKEY("cloud_ms_albedo"), "Low Cloud Single Scattering Albedo"), &lighting.msAlbedo, 0.f, 0.9999f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_ms_albedo_desc"), "Fraction of extinction caused by scattering rather than absorption. Applies to low-cloud single scattering, multiple scattering and ambient illumination. Values near one describe weakly absorbing water droplets."));

		ImGui::SeparatorText(T(TKEY("cloud_lighting_ambient"), "Sky & Ground Light"));
		ImGui::SliderFloat(T(TKEY("cloud_ambient_strength"), "Ambient Response Strength"), &lighting.ambientStrength, 0.f, 8.f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_shared_ambient_desc"), "Sky and ground radiance propagate through a directional cloud volume. Scattering follows droplet size and albedo; cloud density controls occlusion. Ground radiance comes from the reflection cubemap bottom face, with atmospheric transport and a directional ambient fallback. Strength 1 uses the medium response. The light field updates progressively. Cirrus uses its thin-layer response."));

		if (ImGui::TreeNode(T(TKEY("cloud_source_details"), "Low Cloud Source Integration"))) {
			ImGui::SliderFloat(T(TKEY("cloud_scatter_density_threshold"), "Scattering Density Threshold"), &lighting.scatterDensityThreshold, 0.001f, 1.f, "%.3f 1/m");
			ImGui::SliderFloat(T(TKEY("cloud_scatter_density_power"), "Scattering Density Power"), &lighting.scatterDensityPower, 0.01f, 4.f, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("%s", T(TKEY("cloud_source_desc"), "Below the threshold, the ratio of medium extinction to threshold, raised to Power, scales the effective scattering step for directional light. This is an artistic suppression of thin edges. View opacity, ambient and diffuse lighting use the full step. The threshold is measured before View Extinction Scale."));
			ImGui::TreePop();
		}

		if (!SceneWidgetInterceptor::IsArmed()) {
			ImGui::Spacing();
			if (Util::Button(T(TKEY("cloud_lighting_reset"), "Reset Cloud Lighting"), { -FLT_MIN, 0 })) {
				lighting = {};
				volMainHistoryValid = false;
			}
		}
	}
}

void PhysicalSky::DrawCloudQualitySection()
{
	if (SceneWidgetInterceptor::IsArmed())
		return;

	ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader(T(TKEY("cloud_quality"), "Quality & Performance"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_quality_desc"),
											   "Global rendering cost. A shorter ray or shadow range and a larger march step cost less; smaller steps keep more detail at a higher cost."));
		ImGui::SliderFloat(T(TKEY("ray_march_range"), "Ray March Range"), &settings.rayMarchRange, 1.f, 64.f, "%.1f km");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("ray_march_range_desc"), "Maximum low-cloud distance from the camera, including clear space before and between clouds."));
		ImGui::SliderFloat(T(TKEY("shadow_volume_range"), "Shadow Volume Range"), &settings.shadowVolumeRange, 1.f, 16.f, "%.1f km");
		ImGui::SliderFloat(T(TKEY("cloud_march_step_scale"), "Cloud March Step Scale"), &settings.marchStepScale, 0.125f, 2.f, "%.3f", ImGuiSliderFlags_Logarithmic);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_march_step_scale_desc"), "Step length multiplier. Larger steps cost less and lose thin detail; smaller steps cost more and resolve thin detail."));
		ImGui::SliderInt(T(TKEY("cloud_blur_radius"), "Cloud Blur Radius"), &settings.cloudBlurRadius, 0, 2, "%d px", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_blur_radius_desc"), "Final blur radius in internal rendering pixels, adjusted by cloud distance. 0 skips the blur pass; 1 is the default; 2 gives a wider blur."));
	}
}

void PhysicalSky::DrawVanillaCloudSection()
{
	const bool scene = SceneWidgetInterceptor::IsArmed();

	ImGui::SetNextItemOpen(!settings.enableVolumetricClouds, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader(T(TKEY("cloud_vanilla"), "Vanilla Clouds"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
		Util::Text::WrappedSecondary("%s", T(TKEY("cloud_vanilla_desc"),
											   "The original cloud geometry. Its relighting keeps the vanilla silhouette while Physical Sky supplies the light."));
		ImGui::Checkbox(T(TKEY("enable_vanilla_clouds"), "Enable Vanilla Clouds"), &settings.enableVanillaClouds);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("enable_vanilla_clouds_tooltip"), "Enable vanilla cloud geometry rendering with Physical Sky relighting."));

		if (!scene) {
			ImGui::BeginDisabled(!settings.enableVanillaClouds);
			ImGui::SliderFloat(T(TKEY("vanilla_mix"), "Vanilla Mix"), &settings.cloudOriginalMix, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat(T(TKEY("relight_mix"), "Relight Mix"), &settings.cloudRelightMix, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat(T(TKEY("silver_lining_accent"), "Silver Lining Accent"), &settings.silverLiningMix, 0.f, 1.f, "%.2f");
			ImGui::SliderFloat(T(TKEY("silver_lining_spread"), "Silver Lining Spread"), &settings.silverLiningSpread, -0.99f, 0.99f, "%.2f");
			ImGui::EndDisabled();
		} else {
			Util::Text::WrappedSecondary("%s", T(TKEY("cloud_vanilla_scene_scope"),
												   "Relighting mixes are global. Interior and location pages can switch vanilla clouds; weather and time-of-day pages blend cloud density instead."));
		}
	}
}

void PhysicalSky::SettingsDebug()
{
	InfoBox(T(TKEY("beep_boop"), "Beep Boop."));
	ImGui::Checkbox(T(TKEY("skip_cloud_shape_noise"), "Skip Cloud Shape and Detail Noise"), &settings.cloudNoise.skipShapeNoise);

	if (ImGui::Button(T(TKEY("recompile_shaders"), "Recompile Shaders")))
		ClearShaderCache();

	ImGui::SeparatorText(T(TKEY("values"), "Values"));
	{
		ImGui::InputFloat3(T(TKEY("sun_direction"), "Sun Direction"), &cbData.sunDir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::InputFloat3(T(TKEY("masser_direction"), "Masser Direction"), &cbData.masserDir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::InputFloat3(T(TKEY("secunda_direction"), "Secunda Direction"), &cbData.secundaDir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	}

	ImGui::SeparatorText(T(TKEY("textures"), "Textures"));
	{
		static float debugScale = 0.2f;
		ImGui::SliderFloat(T(TKEY("view_scale"), "View Scale"), &debugScale, 0.1f, 1.f);

		static const char* cubeFaces[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
		if (ImGui::SliderInt(T(TKEY("cubemap_face"), "Cubemap Face"), &debugCubeFace, 0, 5, cubeFaces[debugCubeFace]))
			debugCubeFaceSrvs.clear();

		if (ImGui::TreeNode(T(TKEY("sky"), "Sky"))) {
			BUFFER_VIEWER_NODE(texTrLut, debugScale);
			BUFFER_VIEWER_NODE(texMsLut, debugScale);
			BUFFER_VIEWER_NODE(texSvLut, debugScale);
			BUFFER_VIEWER_NODE(texSkyTail, debugScale);
			BUFFER_VIEWER_NODE(texApShadow, debugScale);
			DrawDebugVolume(texApLut ? texApLut->srv.get() : nullptr, "texApLut", debugApSlice, debugScale);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("cloud_shape"), "Cloud Shape"))) {
			ndfManager.DrawPreview();
			localNdfManager.DrawDebug(ndfTexManager, debugScale);
			const auto ndf = ndfManager.GetNdf(settings.cloudMap, ndfTexManager);
			DrawDebugCloudTexture(ndf.height, "ndfHeight", T(TKEY("debug_ndf_height"), "NDF height / shaping start"), debugScale);
			DrawDebugCloudTexture(ndf.modeling, "ndfModeling", T(TKEY("debug_ndf_modeling"), "NDF coverage / types"), debugScale);
			if (ImGui::TreeNode(T(TKEY("debug_weather_noise"), "Shared weather noise inputs"))) {
				const char* labels[] = {
					T(TKEY("debug_weather_noise_0"), "Noise input 0"),
					T(TKEY("debug_weather_noise_1"), "Noise input 1"),
					T(TKEY("debug_weather_noise_2"), "Noise input 2"),
					T(TKEY("debug_weather_noise_3"), "Noise input 3")
				};
				for (uint32_t i = 0; i < 4; ++i) {
					const auto id = std::format("weatherNoise{}", i);
					DrawDebugCloudTexture(ndfManager.GetNoiseInputs()[i], id.c_str(), labels[i], debugScale, true);
				}
				ImGui::TreePop();
			}
			DrawDebugCloudTexture(baseShapeNoiseSrv.get(), "activeShape", T(TKEY("debug_active_shape"), "Active cloud shape noise"), debugScale);
			DrawDebugCloudTexture(cloudProfileLutSrv.get(), "activeProfile", T(TKEY("debug_active_profile"), "Active vertical profile LUT"), debugScale);
			DrawDebugCloudTexture(cloudAdjustmentLutSrv.get(), "activeAdjustment", T(TKEY("debug_active_adjustment"), "Active top expansion / warp LUT"), debugScale);
			const auto cirrus = cirrusMapManager.GetTextures();
			DrawDebugCloudTexture(midCloudMapManager.Weather(), "middleWeather", T(TKEY("debug_middle_weather"), "Middle cloud density / weighted bottom / weighted top"), debugScale);
			DrawDebugCloudTexture(cirrus.weather, "cirrusWeather", T(TKEY("debug_cirrus_weather"), "Active cirrus coverage / type"), debugScale);
			DrawDebugCloudTexture(cirrus.patterns, "cirrusPatterns", T(TKEY("debug_cirrus_patterns"), "Active cirrus patterns"), debugScale);
			for (const auto& endpoint : settings.cloudLayer.cirrus.weather.states) {
				try {
					const auto state = endpoint.at("value").get<CirrusWeatherState>();
					if (!state.localMap.empty()) {
						const auto id = std::format("cirrusLocal{}", state.localMap);
						DrawDebugCloudTexture(ndfTexManager.Query(state.localMap), id.c_str(), T(TKEY("debug_cirrus_local"), "Cirrus local coverage / type / mask"), debugScale);
					}
				} catch (const nlohmann::json::exception&) {
				}
			}
			if (ImGui::TreeNode(T(TKEY("debug_noise_sources"), "Generated and source textures"))) {
				DrawDebugCloudTexture(cloudNoiseGenerator.Shape(), "generatedShape", T(TKEY("debug_generated_shape"), "Generated cloud shape noise"), debugScale);
				DrawDebugCloudTexture(cloudNoiseGenerator.Adjustment(), "generatedAdjustment", T(TKEY("debug_generated_adjustment"), "Generated top expansion / warp LUT"), debugScale);
				DrawDebugCloudTexture(ndfTexManager.Query("Data/Textures/PhysicalSky/top_lut.dds"), "topSource", T(TKEY("cloud_top_source"), "Top profile source"), debugScale);
				DrawDebugCloudTexture(ndfTexManager.Query("Data/Textures/PhysicalSky/bottom_lut.dds"), "bottomSource", T(TKEY("cloud_bottom_source"), "Bottom profile source"), debugScale);
				DrawDebugCloudTexture(importedShapeNoiseSrv.get(), "importedShape", T(TKEY("debug_imported_shape"), "Cloud shape DDS input"), debugScale);
				DrawDebugCloudTexture(importedAdjustmentLutSrv.get(), "sourceAdjustment", T(TKEY("debug_source_adjustment"), "Adjustment source (DDS or fallback)"), debugScale);
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("cloud_trace"), "Cloud Trace"))) {
			BUFFER_VIEWER_NODE(texVolLowTr, debugScale);
			BUFFER_VIEWER_NODE(texVolLowLum, debugScale);
			BUFFER_VIEWER_NODE(texVolLowAux, debugScale);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("cloud_reproject"), "Cloud Reproject"))) {
			BUFFER_VIEWER_NODE(texVolTr, debugScale);
			BUFFER_VIEWER_NODE(texVolLum, debugScale);
			BUFFER_VIEWER_NODE(texVolAux, debugScale);
			BUFFER_VIEWER_NODE(texVolFilteredTr, debugScale);
			BUFFER_VIEWER_NODE(texVolFilteredLum, debugScale);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("cloud_history"), "Cloud History"))) {
			BUFFER_VIEWER_NODE(texVolHistoryTr, debugScale);
			BUFFER_VIEWER_NODE(texVolHistoryLum, debugScale);
			BUFFER_VIEWER_NODE(texVolHistoryAux, debugScale);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("cloud_cubemap"), "Cloud Cubemap"))) {
			DrawDebugCube(texVolCubeTraceTr.get(), "texVolCubeTraceTr", debugScale);
			DrawDebugCube(texVolCubeTraceLum.get(), "texVolCubeTraceLum", debugScale);
			DrawDebugCube(texVolCubeTraceAux.get(), "texVolCubeTraceAux", debugScale);
			DrawDebugCube(texVolCubeTr.get(), "texVolCubeTr", debugScale);
			DrawDebugCube(texVolCubeLum.get(), "texVolCubeLum", debugScale);
			DrawDebugCube(texVolCubeAux.get(), "texVolCubeAux", debugScale);
			DrawDebugCube(texVolCubeHistoryTr.get(), "texVolCubeHistoryTr", debugScale);
			DrawDebugCube(texVolCubeHistoryLum.get(), "texVolCubeHistoryLum", debugScale);
			DrawDebugCube(texVolCubeHistoryAux.get(), "texVolCubeHistoryAux", debugScale);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode(T(TKEY("cloud_lighting"), "Cloud Lighting"))) {
			BUFFER_VIEWER_NODE(texVolCloudAmbientSH, 4.f);
			BUFFER_VIEWER_NODE(texCloudAmbientLighting, 1.f);
			DrawDebugCloudTexture(texShadowVolume ? texShadowVolume->srv.get() : nullptr, "cloud_shadow_transport", T(TKEY("cloud_shadow_transport"), "Cloud Shadow Transport"), debugScale);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("%s", T(TKEY("cloud_shadow_transport_desc"), "Red stores sunlight transmittance; green stores optical depth for multiple scattering. Select a channel and display range to inspect thick cloud paths."));
			DrawDebugCloudTexture(texShadowVolumeRaw ? texShadowVolumeRaw->srv.get() : nullptr, "cloud_shadow_integration", T(TKEY("cloud_shadow_integration"), "Cloud Shadow Integration"), debugScale);
			ImGui::TreePop();
		}
	}
}

#undef I18N_KEY_PREFIX

void PhysicalSky::SetupResources()
{
	auto device = globals::d3d::device;

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, sampTr.put()));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, sampSv.put()));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, sampNoise.put()));
	}

	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC tex2dDesc{
			.Width = kTrLutW,
			.Height = kTrLutH,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = tex2dDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = tex2dDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texTrLut = eastl::make_unique<Texture2D>(tex2dDesc);
		texTrLut->CreateSRV(srvDesc);
		texTrLut->CreateUAV(uavDesc);

		tex2dDesc.Width = kMsLutW;
		tex2dDesc.Height = kMsLutH;

		texMsLut = eastl::make_unique<Texture2D>(tex2dDesc);
		texMsLut->CreateSRV(srvDesc);
		texMsLut->CreateUAV(uavDesc);

		tex2dDesc.Width = kSvLutW;
		tex2dDesc.Height = kSvLutH;

		texSvLut = eastl::make_unique<Texture2D>(tex2dDesc);
		texSvLut->CreateSRV(srvDesc);
		texSvLut->CreateUAV(uavDesc);
		texSkyTail = eastl::make_unique<Texture2D>(tex2dDesc, "PhysicalSky::SkyTail");
		texSkyTail->CreateSRV(srvDesc);
		texSkyTail->CreateUAV(uavDesc);

		D3D11_TEXTURE3D_DESC tex3dDesc{
			.Width = kApLutW,
			.Height = kApLutH,
			.Depth = kApLutD,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 };
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
		uavDesc.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = kApLutD };

		texApLut = eastl::make_unique<Texture3D>(tex3dDesc);
		texApLut->CreateSRV(srvDesc);
		texApLut->CreateUAV(uavDesc);
	}
	{
		D3D11_TEXTURE2D_DESC texDesc;
		auto mainTex = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = 1;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texApShadow = eastl::make_unique<Texture2D>(texDesc);
		texApShadow->CreateSRV(srvDesc);
		texApShadow->CreateUAV(uavDesc);
	}

	CompileShaders();

	// Volumetric cloud resources
	SetupVolumetricResources();
}

void PhysicalSky::ClearShaderCache()
{
	texturePreview.ClearShaderCache();
	CompileShaders();
	CompileVolumetricShaders();
}

void PhysicalSky::CompileShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* csPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines = {};
		std::string_view entry = "main";
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &csTrLutGen, "LutGen.cs.hlsl", { { "LUTGEN", "0" } } },
		{ &csMsLutGen, "LutGen.cs.hlsl", { { "LUTGEN", "1" } } },
		{ &csSvLutGen, "LutGen.cs.hlsl", { { "LUTGEN", "2" } } },
		{ &csSkyTailGen, "LutGen.cs.hlsl", { { "LUTGEN", "2" }, { "SKY_TAIL", "" } } },
		{ &csApLutGen, "LutGen.cs.hlsl", { { "LUTGEN", "3" } } },
		{ &csShadowAccum, "ShadowAccum.cs.hlsl", {} },
		{ &csShadowAccumHalfRes, "ShadowAccum.cs.hlsl", { { "HALF_RES", "" } } }
	};

	for (auto& info : shaderInfos) {
		*info.csPtr = nullptr;
		auto path = std::filesystem::path("Data\\Shaders\\PhysicalSky") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.data())))
			info.csPtr->attach(rawPtr);
	}
}

bool PhysicalSky::ShadersOK()
{
	bool baseShadersOk = csTrLutGen && csMsLutGen && csSvLutGen && csApLutGen && csShadowAccum && csShadowAccumHalfRes &&
	                     texTrLut && texSvLut && texApLut && texApShadow;
	// The cloud maps themselves are created lazily by the first generation
	// dispatch, so readiness is a property of the generation shaders. The render
	// path still verifies every texture before binding.
	const bool ndfReady = (ndfManager.texHeight && ndfManager.texModeling && ndfManager.generatorProgram && ndfManager.noiseProgram);
	const bool cirrusMapsReady = !settings.cloudLayer.cirrus.enabled || cirrusMapManager.ShadersReady(settings.cloudLayer.cirrus);
	bool volumetricShadersOk = !settings.enableVolumetricClouds ||
	                           (vsCloudBoundary && psCloudBoundary && texCloudBoundary && cloudBoundaryCB && cloudBoundaryIndices && cloudBoundaryBlend && cloudBoundaryRasterizer && cloudBoundaryDepth &&
								   csCloudHeightBounds && csCloudHeightBoundsReduce && texCloudHeightBounds && texCloudHeightBoundsTiles && texCloudWeatherBounds &&
								   csCloudAmbientLighting && texCloudAmbientLighting &&
								   csCloudAmbientDensity && csCloudAmbientBoundary && csCloudAmbientSweep && csCloudAmbientSweepFar && csCloudAmbientProject && csCloudAmbientLayout && csCloudAmbientView && cloudAmbientVolumeCB &&
								   texCloudAmbientView[0] && texCloudAmbientView[1] && texCloudAmbientView[2] && texCloudAmbientView[3] &&
								   texCloudAmbientDensity && texCloudAmbientSweep && texCloudAmbientBoundary && texCloudAmbientLayout[0] && texCloudAmbientLayout[1] &&
								   texCloudAmbientSource[0][0] && texCloudAmbientSource[0][1] && texCloudAmbientSource[1][0] && texCloudAmbientSource[1][1] &&
								   texCloudAmbientSource[2][0] && texCloudAmbientSource[2][1] && texCloudAmbientSource[3][0] && texCloudAmbientSource[3][1] &&
								   csVolMainView && csUpperView && csUpperSunDepth && texUpperSunDepth && csVolMainWithoutStorm && csVolMainSingleVolcanic && csStormTiles && texStormTiles && csVolFilter && csVolFilterWide && csVolReproject && csVolCubeReproject && csVolShadowVolume && csVolShadowResample && texShadowVolumeRaw && csCloudShadowMean && csCloudShadowMeanVolcanic && texCloudShadowMean && csVolCubemap && csVolAmbientSH && csCloudAmbientReduce && texVolCloudAmbientSH && texCloudAmbientPartial &&
								   texVolTr && texVolLum && texVolAux && texVolLowTr && texVolLowLum && texVolLowAux &&
								   texVolFilteredTr && texVolFilteredLum && texVolFilteredAux &&
								   texVolHistoryTr && texVolHistoryLum && texVolHistoryAux && texVolCubeTr && texVolCubeLum &&
								   texVolCubeAux && texVolCubeHistoryTr && texVolCubeHistoryLum && texVolCubeHistoryAux &&
								   texVolCubeTraceTr && texVolCubeTraceLum && texVolCubeTraceAux &&
								   texShadowVolume && baseShapeNoiseSrv && cloudProfileLutSrv && cloudAdjustmentLutSrv && ndfReady && cirrusMapsReady);
	const bool voxelShadersOk = !voxelCloudManager.Count() || (cloudDispatchCb && csVoxelView && csVoxelLighting && csVolCubemapVoxel && csVolShadowVoxel && csCloudAmbientDensityVoxel && csCloudShadowMeanVoxel &&
																  texVoxelTraceTr && texVoxelTraceLum && texVoxelTraceAux && texVoxelTraceMotion && texVoxelTraceLightning);
	return baseShadersOk && volumetricShadersOk && voxelShadersOk;
}

bool PhysicalSky::SetLocalCloudState(const LocalNdfState& state)
{
	if (state.enabled && (state.worldspace.empty() || LocalNdfManager::AssetPath(state.asset).empty()))
		return false;
	settings.cloudMap.localCloud.states = nlohmann::json::array({ { { "value", state }, { "weight", 1.f } } });
	volMainHistoryValid = false;
	return true;
}

void PhysicalSky::Reset()
{
	const auto worldspaceID = GetCurrentWorldspaceEditorID();
	if (cloudAmbientWorldspace != worldspaceID) {
		cloudAmbientValid = false;
		cloudAmbientWorldspace = worldspaceID;
	}
	UpdateCloudWind();
	UpdateCloudStorm(worldspaceID);
	voxelCloudManager.Update(settings.voxelClouds, worldspaceID, settings.enabled && settings.enableVolumetricClouds);
	if (settings.enabled && settings.enableVolumetricClouds)
		localNdfManager.Update(settings.cloudMap, ndfTexManager, worldspaceID);
	const float2 lowAltitudeRange = settings.cloudLayer.low.GetNdfAltitudeRangeKm();
	const auto localAltitude = localNdfManager.GetAltitude();
	float lowCloudBaseKm = localAltitude.y > 0.f ? std::min(lowAltitudeRange.x, localAltitude.x * 0.001f) : lowAltitudeRange.x;
	float lowCloudTopKm = localAltitude.y > 0.f ? std::max(lowAltitudeRange.y, (localAltitude.x + localAltitude.y) * 0.001f) : lowAltitudeRange.y;
	if (cloudStormManager.Count()) {
		const auto stormAltitude = cloudStormManager.Altitude();
		lowCloudBaseKm = std::min(lowCloudBaseKm, stormAltitude.x * 0.001f);
		lowCloudTopKm = std::max(lowCloudTopKm, stormAltitude.y * 0.001f);
	}
	if (voxelCloudManager.Count()) {
		const auto voxelAltitude = voxelCloudManager.Altitude();
		lowCloudBaseKm = std::min(lowCloudBaseKm, voxelAltitude.x * 0.001f);
		lowCloudTopKm = std::max(lowCloudTopKm, voxelAltitude.y * 0.001f);
	}
	if (settings.cloudLayer.middle.enabled) {
		const auto altitude = settings.cloudLayer.middle.GetAltitudeRange();
		lowCloudBaseKm = std::min(lowCloudBaseKm, altitude.x * .001f);
		lowCloudTopKm = std::max(lowCloudTopKm, altitude.y * .001f);
	}
	const float traceBottomKm = settings.cloudLayer.cirrus.enabled ? std::min(lowCloudBaseKm, settings.cloudLayer.cirrus.GetAltitudeKm()) : lowCloudBaseKm;
	const float traceTopKm = settings.cloudLayer.cirrus.enabled ? std::max(lowCloudTopKm, settings.cloudLayer.cirrus.GetAltitudeKm()) : lowCloudTopKm;
	const float shadowBottomKm = settings.cloudLayer.low.GetShadowBottomKm(lowCloudBaseKm, settings.shadowVolumeRange, kShadowVolW);
	const float lowCloudThicknessKm = lowCloudTopKm - shadowBottomKm;
	auto& skySync = globals::features::skySync;
	skySync.workingLightColors = std::nullopt;

	auto& linearLighting = globals::features::linearLighting;

	bool allGood = settings.enabled && ShadersOK() && skySync.loaded && skySync.settings.Enabled;

	// check worldspace
	bool worldspaceEnabled = false;
	bool inInterior = false;
	bool inMainLoadingMenu = globals::game::ui && (globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));

	std::map<std::string, WorldspaceInfo>::const_iterator worldspaceIt = settings.worldspaceWhitelist.end();
	float zBottom = settings.fallbackZBottom;
	auto* tes = globals::game::tes ? globals::game::tes : RE::TES::GetSingleton();
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	inInterior = cell && cell->IsInteriorCell();
	if (tes) {
		auto* worldspace = tes->GetRuntimeData2().worldSpace;
		if (!worldspace && cell)
			worldspace = cell->GetRuntimeData().worldSpace;

		if (worldspace) {
			std::string worldspaceName = worldspace->GetFormEditorID();
			worldspaceIt = settings.worldspaceWhitelist.find(worldspaceName);
			worldspaceEnabled = worldspaceIt != settings.worldspaceWhitelist.end();
			if (worldspaceEnabled)
				zBottom = worldspaceIt->second.zBottom;
		}
	}
	bool allowForcedInterior = inInterior && settings.forceEnableAllInteriorCells;
	allGood &= (worldspaceEnabled || settings.enableAllExteriorCells || allowForcedInterior) && (!inInterior || allowForcedInterior) && !inMainLoadingMenu;

	if (!allGood) {
		lightning.Update(*this, worldspaceID, false);
		if (skySync.loaded && skySync.settings.Enabled)
			skySync.workingLightColors = std::nullopt;
		cbData.enabled = allGood;
		volMainHistoryValid = false;
		cloudAmbientValid = false;
		return;
	}

	// resolution
	float2 res{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	auto sunDir = skySync.rawDirections[static_cast<int>(SkySync::Caster::Sun)];
	auto masserDir = skySync.rawDirections[static_cast<int>(SkySync::Caster::Masser)];
	auto secundaDir = skySync.rawDirections[static_cast<int>(SkySync::Caster::Secunda)];

	// Wide gamut support: when ACEScg is active, transform color parameters
	// from sRGB gamut to AP1 gamut so that the LUTs are natively generated in AP1 space.
	// Spectral scattering/absorption coefficients use pre-computed AP1 band-averaged
	// values (from CIE 1931 spectral integration) rather than color-space matrix
	// transforms, since they represent wavelength-dependent physical quantities,
	// not tristimulus colors.
	bool wideGamut = linearLighting.IsACEScgActive();
	auto sRGBToWorkingGamut = [wideGamut](float3 v) -> float3 {
		if (!wideGamut)
			return v;
		static auto mat = getWhiteAdaptedRGBMatrix("sRGB", "ACEScg");
		return TransformColor(v, mat);
	};

	if (cbData.zBottom != zBottom)
		cloudAmbientValid = false;
	cbData = {
		.texDim = res,
		.rcpTexDim = float2(1.0f) / res,
		.frameDim = dynres,
		.rcpFrameDim = float2(1.0f) / dynres,
		.sunDir = { sunDir.x, sunDir.y, sunDir.z },
		.sunlightColor = sRGBToWorkingGamut(settings.sunlightColor),
		.trMix = settings.trMix,
		.masserDir = { masserDir.x, masserDir.y, masserDir.z },
		.apLumMix = settings.apLumMix,
		.masserColor = sRGBToWorkingGamut(settings.masserColor),
		.apTrMix = settings.apTrMix,
		.secundaDir = { secundaDir.x, secundaDir.y, secundaDir.z },
		.sunDiskCos = cos(settings.sunDiskRad) * (settings.proceduralSun ? 1.f : 0.f),
		.secundaColor = sRGBToWorkingGamut(settings.secundaColor),
		.enabled = allGood,
		.vanillaMix = settings.vanillaMix,
		.zBottom = zBottom,
		.rPlanet = settings.planetRadius / Util::Units::GAME_UNIT_TO_KM,
		.rAtmosphere = settings.atmosphereRadius / Util::Units::GAME_UNIT_TO_KM,
		.groundAlbedo = sRGBToWorkingGamut(settings.groundAlbedo),
		.cloudShadowRemapRange = settings.cloudShadowRemapRange,
		.aerosolFalloff = settings.aerosolFalloff * Util::Units::GAME_UNIT_TO_KM,
		// The coefficient members are what the scene layer blends: a weather or time-of-day
		// transition interpolates the optics a mixture resolved to, never the discrete mixture.
		.aerosolPhaseG = settings.aerosolPhaseG,
		.aerosolScatter = settings.aerosolScatter * 1e-3f * Util::Units::GAME_UNIT_TO_KM,
		.halfResApShadow = settings.halfResApShadow ? 1u : 0u,
		.aerosolAbsorption = settings.aerosolAbsorption * 1e-3f * Util::Units::GAME_UNIT_TO_KM,
		.rayleighFalloff = settings.rayleighFalloff * Util::Units::GAME_UNIT_TO_KM,
		.rayleighScatter = (wideGamut ? settings.rayleighScatterAP1 : settings.rayleighScatter) * 1e-3f * Util::Units::GAME_UNIT_TO_KM,
		.ozoneAltitude = settings.ozoneAltitude / Util::Units::GAME_UNIT_TO_KM,
		.ozoneThickness = settings.ozoneThickness / Util::Units::GAME_UNIT_TO_KM,
		.ozoneAbsorption = (wideGamut ? settings.ozoneAbsorptionAP1 : settings.ozoneAbsorption) * 1e-3f * Util::Units::GAME_UNIT_TO_KM,
		.enableVanillaClouds = settings.enableVanillaClouds ? 1u : 0u,
		.cloudRelightMix = settings.cloudRelightMix,
		.cloudOriginalMix = settings.cloudOriginalMix,
		.silverLiningMix = settings.silverLiningMix,
		.silverLiningSpread = settings.silverLiningSpread,
		.enableVolumetricClouds = settings.enableVolumetricClouds ? 1u : 0u,
		.shadowVolumeRange = settings.shadowVolumeRange / Util::Units::GAME_UNIT_TO_KM,
		.lowestCloudAltitude = traceBottomKm / Util::Units::GAME_UNIT_TO_KM,
		.highestCloudAltitude = traceTopKm / Util::Units::GAME_UNIT_TO_KM,
		.volCloudScatter = float3(std::max(settings.cloudLayer.lighting.sunExtinction, 0.f) * Util::Units::GAME_UNIT_TO_M),
		.volCloudUseSun = 0u,
		.volCloudAbsorption = float3(0.f),
		.volCloudLowBottom = shadowBottomKm / Util::Units::GAME_UNIT_TO_KM,
		.volCloudLowThickness = lowCloudThicknessKm / Util::Units::GAME_UNIT_TO_KM,
		.lightSkyStatics = settings.lightSkyStatics ? 1u : 0u,
		.skyStaticsBrightness = settings.skyStaticsBrightness,
	};
	lightning.Update(*this, worldspaceID, allGood && !inInterior && !globals::state->isMapMenuOpen && settings.enableVolumetricClouds && csLightningQuery && csLightningComposite && csLightningCubeComposite);
	auto lightningColor = sRGBToWorkingGamut({ cbData.lightningColorIntensity.x, cbData.lightningColorIntensity.y, cbData.lightningColorIntensity.z });
	// Same working-gamut Y and photometric scale as the solar illuminance control.
	const float lightningY = wideGamut ? lightningColor.x * 0.2722287168f + lightningColor.y * 0.6740817658f + lightningColor.z * 0.0536895174f :
	                                     lightningColor.x * 0.2125f + lightningColor.y * 0.7154f + lightningColor.z * 0.0721f;
	lightningColor = lightningY > 1.0e-6f ? lightningColor / lightningY : float3(1.f);
	cbData.lightningColorIntensity.x = lightningColor.x;
	cbData.lightningColorIntensity.y = lightningColor.y;
	cbData.lightningColorIntensity.z = lightningColor.z;

	if (settings.overrideDirLight) {
		const float pbrCompensationMult = linearLighting.IsLinearLightingActive() ? 1.0f : RE::NI_PI;  // Colors should match PBR values when not using linear lighting
		auto LightConvFn = [pbrCompensationMult](float3 color) {
			color /= pbrCompensationMult;
			return RE::NiColor(color.x, color.y, color.z);
		};
		skySync.workingLightColors = std::array{
			LightConvFn(cbData.sunlightColor),
			LightConvFn(cbData.masserColor),
			LightConvFn(cbData.secundaColor)
		};
	}

	RE::NiPoint3 posCam = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
		posCam = cam->cameraRoot->world.translate;
		cbData.zCameraPlanet = posCam.z - cbData.zBottom + cbData.rPlanet;
	}
	// Keep the astronomical sun as the cloud source until it is below the
	// horizon everywhere in the traced cloud region, not just at the camera.
	// The remaining night path retains the scene's lunar directional light.
	const float planetRadius = std::max(cbData.rPlanet, 1.0f);
	const float cloudRadius = std::max(planetRadius + cbData.highestCloudAltitude, planetRadius);
	const float horizonDip = std::acos(std::clamp(planetRadius / cloudRadius, 0.0f, 1.0f));
	// March range now starts at each cloud shell, so it cannot bound the visible
	// region's local-up tilt. Use the two planet-tangent horizon angles instead.
	const float observerRadius = std::max(cbData.zCameraPlanet, planetRadius);
	const float observerHorizonDip = std::acos(std::clamp(planetRadius / observerRadius, 0.0f, 1.0f));
	const float localUpRange = observerHorizonDip + horizonDip;
	const float solarLimit = std::min(horizonDip + localUpRange + 0.00465f, RE::NI_PI * 0.5f);
	cbData.volCloudUseSun = sunDir.z > -std::sin(solarLimit) ? 1u : 0u;
}

void PhysicalSky::EarlyPrepass()
{
	if (cbData.enabled) {
		GenerateLuts();
	}
}

void PhysicalSky::ReflectionsPrepass()
{
	if (cbData.enabled) {
		auto* lightningReflection = texLightningCubeLum ? texLightningCubeLum->srv.get() : nullptr;
		globals::d3d::context->PSSetShaderResources(109, 1, &lightningReflection);
		std::array srvs = { texTrLut->srv.get(), texSvLut->srv.get(), texApLut->srv.get() };
		globals::d3d::context->PSSetShaderResources(61, (uint)srvs.size(), srvs.data());
		ID3D11ShaderResourceView* shadowSrv = texShadowVolume ? texShadowVolume->srv.get() : nullptr;
		globals::d3d::context->PSSetShaderResources(112, 1, &shadowSrv);
		auto* shadowMeanSrv = texCloudShadowMean ? texCloudShadowMean->srv.get() : nullptr;
		globals::d3d::context->PSSetShaderResources(69, 1, &shadowMeanSrv);
		if (texVolCubeTr && texVolCubeLum) {
			std::array<ID3D11ShaderResourceView*, 2> volCubeSrvs = { texVolCubeTr->srv.get(), texVolCubeLum->srv.get() };
			globals::d3d::context->PSSetShaderResources(114, (uint)volCubeSrvs.size(), volCubeSrvs.data());
			auto* cubeDepth = texVolCubeAux ? texVolCubeAux->srv.get() : nullptr;
			globals::d3d::context->PSSetShaderResources(67, 1, &cubeDepth);
		}
	}
}

void PhysicalSky::Prepass()
{
	if (settings.enabled && settings.enableVolumetricClouds) {
		const bool generatedShape = settings.cloudNoise.procedural || !importedShapeNoiseSrv;
		const bool generatedAdjustment = settings.cloudNoise.procedural || cloudAdjustmentGenerated;
		const bool generatedNoise = generatedShape || generatedAdjustment;
		if (generatedNoise && cloudNoiseGenerator.Update(settings.cloudNoise, importedAdjustmentLutSrv.get())) {
			volMainHistoryValid = false;
		}
		auto* shape = generatedShape && cloudNoiseGenerator.Shape() ? cloudNoiseGenerator.Shape() : importedShapeNoiseSrv.get();
		auto* adjustment = generatedAdjustment && cloudNoiseGenerator.Adjustment() ? cloudNoiseGenerator.Adjustment() : importedAdjustmentLutSrv.get();
		if (baseShapeNoiseSrv.get() != shape || cloudAdjustmentLutSrv.get() != adjustment) {
			baseShapeNoiseSrv.copy_from(shape);
			cloudAdjustmentLutSrv.copy_from(adjustment);
			volMainHistoryValid = false;
			cloudAmbientValid = false;
			cloudCrossSectionDirty = true;
		}
	}
	if (cbData.enabled) {
		const bool renderVolumetricClouds = settings.enableVolumetricClouds && ShadersOK();

		if (renderVolumetricClouds) {
			const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
			const float2 cameraMeters{ camera.x * Util::Units::GAME_UNIT_TO_M, camera.y * Util::Units::GAME_UNIT_TO_M };
			const float cameraDx = cameraMeters.x - cloudCrossSectionCamera.x;
			const float cameraDy = cameraMeters.y - cloudCrossSectionCamera.y;
			if (cameraDx * cameraDx + cameraDy * cameraDy > 32.f * 32.f)
				cloudCrossSectionDirty = true;

			auto historyCloudMap = nlohmann::json(settings.cloudMap);
			historyCloudMap.erase("localCloud");
			const auto cloudSettingsKey = nlohmann::json{
				{ "map", historyCloudMap },
				{ "skipShapeNoise", settings.cloudNoise.skipShapeNoise },
				{ "useVoxelNoise", settings.cloudNoise.useVoxelNoise },
				{ "low", settings.cloudLayer.low },
				{ "middle", settings.cloudLayer.middle },
				{ "cirrus", settings.cloudLayer.cirrus },
				{ "lighting", settings.cloudLayer.lighting },
				{ "stormRevision", cloudStormManager.Revision() },
				{ "range", settings.rayMarchRange },
				{ "step", settings.marchStepScale },
				{ "froxel", globals::features::viewAtmosphere.IsPrepared() },
				{ "planet", settings.planetRadius },
				{ "bottom", cbData.zBottom }
			}.dump();
			cloudWeatherChanged = cloudSettingsKey != volCloudSettingsKey;
			if (cloudWeatherChanged) {
				// A continuous weather blend changes values every frame. Keep the
				// accumulated image and transport solution, but refresh weather bounds.
				cloudHeightBoundsDirty = true;
				volCloudSettingsKey = cloudSettingsKey;
				cloudCrossSectionDirty = true;
			}
			if (ndfManager.UpdateNdf(settings.cloudMap, ndfTexManager)) {
				cloudHeightBoundsDirty = true;
				cloudCrossSectionDirty = true;
			}
			if (settings.cloudLayer.cirrus.enabled)
				cirrusMapManager.Update(settings.cloudLayer.cirrus, ndfTexManager, ndfManager);
			if (settings.cloudLayer.middle.enabled && midCloudMapManager.Update(settings.cloudLayer.middle, ndfTexManager, ndfManager))
				middleBoundsDirty = true;

			// Resource/layer switches and reference-frame changes still invalidate
			// history. Map regeneration and continuous appearance changes do not.
			auto noiseTopology = nlohmann::json::array();
			for (const auto& input : settings.cloudMap.procedural.noise) {
				const auto& noise = input.parameters;
				noiseTopology.push_back({ input.texturePath, noise.type, noise.seed,
					noise.frequency, noise.octaves, noise.lacunarity, noise.repetitions });
			}
			const auto historyKey = nlohmann::json{
				{ "ndfSource", { settings.cloudMap.type, settings.cloudMap.texture } },
				{ "noiseTopology", noiseTopology },
				{ "textures", ndfTexManager.revision },
				{ "skipShapeNoise", settings.cloudNoise.skipShapeNoise },
				{ "useVoxelNoise", settings.cloudNoise.useVoxelNoise },
				{ "middleEnabled", settings.cloudLayer.middle.enabled },
				{ "middleDetailMode", settings.cloudLayer.middle.detailMode },
				{ "cirrusEnabled", settings.cloudLayer.cirrus.enabled },
				{ "stormRevision", cloudStormManager.Revision() },
				{ "froxel", globals::features::viewAtmosphere.IsPrepared() },
				{ "planet", settings.planetRadius },
				{ "bottom", cbData.zBottom },
				{ "worldspace", GetCurrentWorldspaceEditorID() }
			}.dump();
			if (historyKey != volCloudHistoryKey) {
				volMainHistoryValid = false;
				cloudAmbientValid = false;
				volCloudHistoryKey = historyKey;
			}
			if (cloudCrossSectionDirty)
				RenderCloudCrossSection();
			RenderVolumetricClouds(VolumetricCloudPass::kShadowVolume);
		}

		GenerateSkyTail();
		if (!globals::features::viewAtmosphere.IsPrepared())
			AccumShadow();

		// Volumetric clouds
		if (renderVolumetricClouds && !globals::features::viewAtmosphere.CompositeEnabled()) {
			RenderVolumetricClouds(VolumetricCloudPass::kMainViewAndCubemap);
		} else if (!renderVolumetricClouds && texVolTr && texVolLum) {
			// Clear to neutral when disabled (white transmittance, black luminance)
			auto context = globals::d3d::context;
			FLOAT trClr[4] = { 1.f, 1.f, 1.f, 1.f };
			FLOAT lumClr[4] = { 0.f, 0.f, 0.f, 0.f };
			context->ClearUnorderedAccessViewFloat(texVolTr->uav.get(), trClr);
			context->ClearUnorderedAccessViewFloat(texVolLum->uav.get(), lumClr);
			if (texVolAux)
				context->ClearUnorderedAccessViewFloat(texVolAux->uav.get(), lumClr);
			if (texVolLowTr)
				context->ClearUnorderedAccessViewFloat(texVolLowTr->uav.get(), trClr);
			if (texVolLowLum)
				context->ClearUnorderedAccessViewFloat(texVolLowLum->uav.get(), lumClr);
			if (texVolLowAux)
				context->ClearUnorderedAccessViewFloat(texVolLowAux->uav.get(), lumClr);
			if (texVolHistoryTr)
				context->ClearUnorderedAccessViewFloat(texVolHistoryTr->uav.get(), trClr);
			if (texVolHistoryLum)
				context->ClearUnorderedAccessViewFloat(texVolHistoryLum->uav.get(), lumClr);
			if (texVolHistoryAux)
				context->ClearUnorderedAccessViewFloat(texVolHistoryAux->uav.get(), lumClr);
			if (texVolCubeTr)
				context->ClearUnorderedAccessViewFloat(texVolCubeTr->uav.get(), trClr);
			if (texVolCubeLum)
				context->ClearUnorderedAccessViewFloat(texVolCubeLum->uav.get(), lumClr);
			if (texShadowVolume) {
				constexpr FLOAT shadowClear[4] = { 1.f, 0.f, 0.f, 0.f };
				context->ClearUnorderedAccessViewFloat(texShadowVolume->uav.get(), shadowClear);
			}
			if (texCloudShadowMean) {
				ID3D11ShaderResourceView* nullShadowMean = nullptr;
				context->PSSetShaderResources(69, 1, &nullShadowMean);
				context->CSSetShaderResources(69, 1, &nullShadowMean);
				context->ClearUnorderedAccessViewFloat(texCloudShadowMean->uav.get(), lumClr);
			}
			volMainHistoryValid = false;
			volHistoryFrameDim = {};
			cloudAmbientValid = false;
		}

		std::array srvs = { texTrLut->srv.get(), texSvLut->srv.get(), texApLut->srv.get(), texApShadow->srv.get() };
		auto* channels = texLightningChannels ? texLightningChannels->srv.get() : nullptr;
		auto* reflection = texLightningCubeLum ? texLightningCubeLum->srv.get() : nullptr;
		globals::d3d::context->PSSetShaderResources(113, 1, &channels);
		globals::d3d::context->PSSetShaderResources(109, 1, &reflection);
		globals::d3d::context->PSSetShaderResources(61, (uint)srvs.size(), srvs.data());
		auto* skyTailSrv = texSkyTail ? texSkyTail->srv.get() : nullptr;
		globals::d3d::context->PSSetShaderResources(66, 1, &skyTailSrv);
		auto* shadowMeanSrv = texCloudShadowMean ? texCloudShadowMean->srv.get() : nullptr;
		globals::d3d::context->PSSetShaderResources(69, 1, &shadowMeanSrv);

		// Bind volumetric cloud results and shadow volume for pixel shaders. Use t110-t112 to avoid feature texture conflicts.
		// RenderVolumetricClouds already bound the reconstructed/composited result.
		// Do not replace it with the NDF-only trace before the sky is drawn.
		if (texVolTr && texVolLum && (!renderVolumetricClouds || globals::features::viewAtmosphere.CompositeEnabled())) {
			std::array<ID3D11ShaderResourceView*, 3> volSrvs = { texVolTr->srv.get(), texVolLum->srv.get(), texShadowVolume ? texShadowVolume->srv.get() : nullptr };
			globals::d3d::context->PSSetShaderResources(110, (uint)volSrvs.size(), volSrvs.data());
		}
		if (texVolCubeTr && texVolCubeLum) {
			std::array<ID3D11ShaderResourceView*, 2> volCubeSrvs = { texVolCubeTr->srv.get(), texVolCubeLum->srv.get() };
			globals::d3d::context->PSSetShaderResources(114, (uint)volCubeSrvs.size(), volCubeSrvs.data());
			auto* cubeDepth = texVolCubeAux ? texVolCubeAux->srv.get() : nullptr;
			globals::d3d::context->PSSetShaderResources(67, 1, &cubeDepth);
		}
	}
}

void PhysicalSky::GenerateLuts()
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	constexpr auto debugStr = "Physical Sky: LUT Generation";
	state->BeginPerfEvent(debugStr);
	{
		TracyD3D11Zone(state->tracyCtx, debugStr);

		auto samplers = std::array{ sampTr.get(), sampSv.get(), sampNoise.get() };
		std::array<ID3D11ShaderResourceView*, 2> srvs = {};
		ID3D11UnorderedAccessView* uav = nullptr;

		/* ---- DISPATCH ---- */
		context->CSSetSamplers(0, (int)samplers.size(), samplers.data());

		// -> transmittance
		uav = texTrLut->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(csTrLutGen.get(), nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::TransmittanceLut");
		context->Dispatch((kTrLutW + 7) >> 3, (kTrLutH + 7) >> 3, 1);
		globals::profiler->EndPass();

		// -> multiscatter
		uav = texMsLut->uav.get();
		srvs.at(0) = texTrLut->srv.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(csMsLutGen.get(), nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::MultiscatterLut");
		// Each group integrates sixteen directions for four LUT texels.
		context->Dispatch((kMsLutW + 1) >> 1, (kMsLutH + 1) >> 1, 1);
		globals::profiler->EndPass();

		// -> sky-view
		uav = texSvLut->uav.get();
		srvs.at(1) = texMsLut->srv.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(csSvLutGen.get(), nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::SkyViewLut");
		context->Dispatch((kSvLutW + 7) >> 3, (kSvLutH + 7) >> 3, 1);
		globals::profiler->EndPass();

		// -> aerial perspective
		uav = texApLut->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(csApLutGen.get(), nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::AerialPerspectiveLut");
		// Two rays per group; all depth intervals are evaluated in parallel.
		context->Dispatch((kApLutW + 1) >> 1, kApLutH, 1);
		globals::profiler->EndPass();

		/* ---- RESTORE ---- */
		samplers.fill(nullptr);
		srvs.fill(nullptr);
		uav = nullptr;

		context->CSSetSamplers(0, (int)samplers.size(), samplers.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(nullptr, nullptr, 0);
	}
	state->EndPerfEvent();
}

void PhysicalSky::GenerateSkyTail()
{
	if (!globals::features::viewAtmosphere.settings.enabled || !texSkyTail || !csSkyTailGen || !globals::features::viewAtmosphere.CompositeEnabled())
		return;
	auto* context = globals::d3d::context;
	ID3D11ShaderResourceView* nullSrv = nullptr;
	context->PSSetShaderResources(66, 1, &nullSrv);
	context->CSSetShaderResources(66, 1, &nullSrv);
	auto* cloudShadow = texShadowVolume ? texShadowVolume->srv.get() : nullptr;
	context->CSSetShaderResources(112, 1, &cloudShadow);
	auto* cloudShadowMean = texCloudShadowMean ? texCloudShadowMean->srv.get() : nullptr;
	context->CSSetShaderResources(69, 1, &cloudShadowMean);
	auto* cloudDiffuseTransport = globals::features::viewAtmosphere.CloudDiffuseTransport();
	context->CSSetShaderResources(70, 1, &cloudDiffuseTransport);
	ID3D11ShaderResourceView* srvs[] = { texTrLut->srv.get(), texMsLut->srv.get() };
	context->CSSetShaderResources(0, 2, srvs);
	auto* sampler = sampTr.get();
	context->CSSetSamplers(0, 1, &sampler);
	auto* uav = texSkyTail->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShader(csSkyTailGen.get(), nullptr, 0);
	globals::profiler->BeginPass("PhysicalSky::SkyTail");
	context->Dispatch((kSvLutW + 7) >> 3, (kSvLutH + 7) >> 3, 1);
	globals::profiler->EndPass();
	uav = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	srvs[0] = srvs[1] = nullptr;
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetShaderResources(112, 1, &nullSrv);
	context->CSSetShaderResources(69, 1, &nullSrv);
	context->CSSetShaderResources(70, 1, &nullSrv);
	sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);
	context->CSSetShader(nullptr, nullptr, 0);
}

void PhysicalSky::AccumShadow()
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	if (state->isMapMenuOpen) {
		constexpr FLOAT clearShadow[4] = {};
		context->ClearUnorderedAccessViewFloat(texApShadow->uav.get(), clearShadow);
		return;
	}

	auto& terrainShadows = globals::features::terrainShadows;
	auto& cloudShadows = globals::features::cloudShadows;

	uint resolution[2] = { (uint)cbData.frameDim.x, (uint)cbData.frameDim.y };
	if (settings.halfResApShadow) {
		resolution[0] = (resolution[0] + 1u) / 2u;
		resolution[1] = (resolution[1] + 1u) / 2u;
	}

	constexpr auto debugStr = "Physical Sky: Shadow Accumulation";
	state->BeginPerfEvent(debugStr);
	{
		TracyD3D11Zone(state->tracyCtx, debugStr);

		auto sampler = sampTr.get();
		ID3D11ShaderResourceView* directionalShadowLights = nullptr;
		if (auto* directionalShadowBuffer = Deferred::GetSingleton()->directionalShadowLights)
			directionalShadowLights = directionalShadowBuffer->srv.get();
		auto srvs = std::array{
			globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV,
			globals::deferred->GetDirectionalShadowMap(),
			static_cast<ID3D11ShaderResourceView*>(nullptr),
			terrainShadows.IsShadowMapReady() ? terrainShadows.texShadowHeight->srv.get() : nullptr,
			cloudShadows.loaded ? cloudShadows.texCloudShadowLayers[CloudShadows::kMaxCloudLayers - 1]->srv.get() : nullptr,
			settings.enableVolumetricClouds && texShadowVolume ? texShadowVolume->srv.get() : nullptr,
		};
		auto uav = texApShadow->uav.get();

		/* ---- DISPATCH ---- */
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShaderResources(98, 1, &directionalShadowLights);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(settings.halfResApShadow ? csShadowAccumHalfRes.get() : csShadowAccum.get(), nullptr, 0);
		globals::profiler->BeginPass("PhysicalSky::AccumShadow");
		context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		/* ---- RESTORE ---- */
		sampler = nullptr;
		srvs.fill(nullptr);
		directionalShadowLights = nullptr;
		uav = nullptr;

		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShaderResources(98, 1, &directionalShadowLights);
		context->CSSetShader(nullptr, nullptr, 0);
	}
	state->EndPerfEvent();
}

void PhysicalSky::ModifySky()
{
	auto context = globals::d3d::context;
	context->PSGetSamplers(3, 2, originalPSSamplers);
	context->VSGetConstantBuffers(6, 1, originalVSSkyFeatureBuffer.put());
	auto* featureBuffer = globals::state->featureDataCB->CB();
	context->VSSetConstantBuffers(6, 1, &featureBuffer);

	auto samplers = std::array{ sampTr.get(), sampSv.get() };
	context->PSSetSamplers(3, static_cast<UINT>(samplers.size()), samplers.data());
}

void PhysicalSky::RestoreSamplers()
{
	auto context = globals::d3d::context;
	context->PSSetSamplers(3, 2, originalPSSamplers);
	auto* featureBuffer = originalVSSkyFeatureBuffer.get();
	context->VSSetConstantBuffers(6, 1, &featureBuffer);
	originalVSSkyFeatureBuffer = nullptr;
}

void PhysicalSky::ModifyGrass()
{
	auto context = globals::d3d::context;
	context->PSGetSamplers(7, 1, originalPSGrassSampler.put());

	auto sampler = sampTileable.get();
	context->PSSetSamplers(7, 1, &sampler);
}

void PhysicalSky::RestoreGrassSampler()
{
	auto context = globals::d3d::context;
	auto sampler = originalPSGrassSampler.get();
	context->PSSetSamplers(7, 1, &sampler);
	originalPSGrassSampler = nullptr;
}

void PhysicalSky::ModifyWater()
{
	auto context = globals::d3d::context;
	context->PSGetSamplers(13, 1, originalPSWaterSampler.put());

	auto& samplerModifiedBits = globals::game::shadowState->GetRuntimeData().PSSamplerModifiedBits;
	originalPSWaterSamplerModifiedBits = samplerModifiedBits & (1u << 13);
	samplerModifiedBits &= ~(1u << 13);

	auto sampler = sampSv.get();
	context->PSSetSamplers(13, 1, &sampler);
}

void PhysicalSky::RestoreWaterSampler()
{
	auto context = globals::d3d::context;
	auto sampler = originalPSWaterSampler.get();
	context->PSSetSamplers(13, 1, &sampler);
	originalPSWaterSampler = nullptr;
	globals::game::shadowState->GetRuntimeData().PSSamplerModifiedBits |= originalPSWaterSamplerModifiedBits;
	originalPSWaterSamplerModifiedBits = 0;
}

void PhysicalSky::Hooks::BSSkyShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::physicalSky.ModifySky();
	func(This, Pass, RenderFlags);
}

void PhysicalSky::Hooks::BSSkyShader_RestoreGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::physicalSky.RestoreSamplers();
	func(This, Pass, RenderFlags);
}

void PhysicalSky::Hooks::BSGrassShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	globals::features::physicalSky.ModifyGrass();
}

void PhysicalSky::Hooks::BSGrassShader_RestoreGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	globals::features::physicalSky.RestoreGrassSampler();
}

void PhysicalSky::Hooks::BSWaterShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	globals::features::physicalSky.ModifyWater();
}

void PhysicalSky::Hooks::BSWaterShader_RestoreGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	globals::features::physicalSky.RestoreWaterSampler();
}
