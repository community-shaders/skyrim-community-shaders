#include "Features/ProceduralGrass.h"

#include "TerrainHeightMap.h"
#include "Utils/DebugSection.h"
#include "Utils/FileSystem.h"
#include "Utils/Serialize.h"

#include <filesystem>
#include <fstream>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ProceduralGrass::Settings,
	Enabled,
	Quality)

namespace
{
	constexpr auto TextureTypesFilename = "ProceduralGrassTypes.json";

	std::filesystem::path TextureTypesPath()
	{
		return Util::PathHelpers::GetCommunityShaderPath() / TextureTypesFilename;
	}

	bool IsNumericArray(const nlohmann::json& value, const size_t size)
	{
		if (!value.is_array() || value.size() != size)
			return false;
		return std::all_of(value.begin(), value.end(), [](const auto& component) { return component.is_number(); });
	}

	template <class T>
	bool IsValidOverrideValue(const nlohmann::json& value)
	{
		if constexpr (std::is_same_v<T, float>)
			return value.is_number();
		else
			return IsNumericArray(value, sizeof(T) / sizeof(float));
	}

	/** @brief Visits every persisted global setting with its JSON key, so load and save share one list. */
	template <class SettingsT, class Visitor>
	void ForEachSettingKey(SettingsT& s, Visitor&& v)
	{
		// Blade shape / material
		v("Height", s.grassHeight);
		v("Width", s.grassWidth);
		v("Stiffness", s.stiffness);
		v("TipWeight", s.tipWeight);
		v("Mid", s.mid);
		v("RotationalStiffness", s.rotationalStiffness);

		v("BakedMinAO", s.ao);
		v("Specular", s.specular);
		v("SpecularAnisotropy", s.specularAnisotropy);
		v("SheenStrength", s.sheenStrength);
		v("SheenRoughness", s.sheenRoughness);
		v("SheenTint", s.sheenTint);
		v("CurvedNormalStrength", s.curvedNormalStrength);

		v("SubsurfaceOpacity", s.subsurfaceOpacity);
		v("SubsurfaceTint", s.grassSubsurfaceTint);
		v("TransmissionStrength", s.grassTransmissionStrength);

		v("Roughness", s.baseMinTipRoughness);
		v("RoughnessTipStart", s.tipRoughnessStart);
		v("ClumpAOStrength", s.clumpAOStrength);

		// Colour
		v("BaseColor", s.baseColor);
		v("TipColor", s.tipColor);
		v("ColorHueVariation", s.grassColorHueVariation);
		v("ColorValueVariation", s.grassColorValueVariation);
		v("ColorTipDryStrength", s.grassColorTipDryStrength);
		v("ColorMottleStrength", s.grassColorMottleStrength);
		v("ColorCool", s.grassColorCool);
		v("ColorWarm", s.grassColorWarm);
		v("ColorTipDry", s.grassColorTipDry);

		// Detail / lighting
		v("BaseAO", s.grassBaseAO);
		v("ClumpColorStrength", s.grassClumpColorStrength);
		v("CanopySkyOcclusion", s.grassCanopySkyOcclusion);
		v("DensityAO", s.grassDensityAO);
		v("BounceStrength", s.grassBounceStrength);
		v("BounceColor", s.grassBounceColor);
		v("AmbientDesat", s.grassAmbientDesat);

		// Terrain blend / shadow
		v("TerrainBlendStrength", s.grassTerrainBlendStrength);
		v("TerrainBlendHeight", s.grassTerrainBlendHeight);
		v("TerrainBlendNormal", s.grassTerrainBlendNormal);
		v("TerrainBlendRough", s.grassTerrainBlendRough);

		// Clump
		v("ClumpGridSize", s.clumpGridSize);
		v("ClumpDistanceFactor", s.clumpDistanceFactor);
		v("ClumpFacingFactor", s.clumpFacingFactor);
		v("ClumpLeanFactor", s.clumpLeanFactor);
		v("ClumpHeightFactor", s.clumpHeightFactor);

		// Slope
		v("MinSlope", s.grassMinSlope);
		v("MaxSlope", s.grassMaxSlope);
		v("SlopeFacing", s.grassSlopeFacing);

		// Wind / animation
		v("WindAngle", s.windAngle);
		v("WindSpeed", s.windSpeed);

		// Placement
		v("GrassMapEdgeNoise", s.grassMapEdgeNoise);
		v("ViewThicken", s.grassViewThicken);

		// Per-LOD densities
		v("MidDensity", s.midGrassDensity);
		v("LowDensity", s.lowGrassDensity);

		// Debug
		v("DebugIgnoreGrassMap", s.debugIgnoreGrassMap);
		v("DebugDisableAllCulls", s.debugDisableAllCulls);
		v("DebugTierView", s.debugTierView);
		v("DebugIgnorePreProcessedFlag", s.debugIgnorePreProcessedFlag);
		v("DebugDisableVanillaGrass", s.debugDisableVanillaGrass);
	}

	/**
	 * @brief Visits every per-type overridable field: v.Section(label) opens each editor group, then
	 * v(key, baseValue, label, min, max[, format]) visits a field. A null format draws a colour picker.
	 */
	template <class SettingsT, class Visitor>
	void ForEachTypeOverride(SettingsT& s, Visitor&& v)
	{
		v.Section(T("feature.procedural_grass.shape_section", "Shape"));
		v("Height", s.grassHeight, T("feature.procedural_grass.height", "Height"), 0.0f, 150.0f, "%.1f");
		v("Width", s.grassWidth, T("feature.procedural_grass.width", "Width"), 0.0f, 10.0f, "%.1f");
		v("Stiffness", s.stiffness, T("feature.procedural_grass.k1", "K1"), -10.0f, 10.0f);
		v("TipWeight", s.tipWeight, T("feature.procedural_grass.k2", "K2"), -10.0f, 10.0f);
		v("Mid", s.mid, T("feature.procedural_grass.mid", "Mid"), 0.0f, 1.0f);
		v("RotationalStiffness", s.rotationalStiffness, T("feature.procedural_grass.rotational_stiffness", "Rotational Stiffness"), 0.0f, 10.0f);

		v.Section(T("feature.procedural_grass.slope_section", "Slope"));
		v("MinSlope", s.grassMinSlope, T("feature.procedural_grass.min_slope", "Min Slope (deg)"), 0.0f, 90.0f, "%.0f");
		v("MaxSlope", s.grassMaxSlope, T("feature.procedural_grass.max_slope", "Max Slope (deg)"), 0.0f, 90.0f, "%.0f");
		v("SlopeFacing", s.grassSlopeFacing, T("feature.procedural_grass.slope_facing", "Slope Facing"), 0.0f, 1.0f);

		v.Section(T("feature.procedural_grass.clump_section", "Clump"));
		v("ClumpGridSize", s.clumpGridSize, T("feature.procedural_grass.clump_grid_size", "Clump Grid Size"), ProceduralGrass::MinClumpGridSize, ProceduralGrass::MaxClumpGridSize, "%.0f");
		v("ClumpDistanceFactor", s.clumpDistanceFactor, T("feature.procedural_grass.clump_distance", "Clump Distance"), 0.0f, 1.0f);
		v("ClumpFacingFactor", s.clumpFacingFactor, T("feature.procedural_grass.clump_facing", "Clump Facing"), -1.0f, 1.0f);
		v("ClumpLeanFactor", s.clumpLeanFactor, T("feature.procedural_grass.clump_lean", "Clump Lean"), 0.0f, 1.0f);
		v("ClumpHeightFactor", s.clumpHeightFactor, T("feature.procedural_grass.clump_height", "Clump Height"), 0.0f, 1.0f);
		v("ClumpAOStrength", s.clumpAOStrength, T("feature.procedural_grass.clump_ao", "Clump AO"), 0.0f, 1.0f);
		v("ClumpColorStrength", s.grassClumpColorStrength, T("feature.procedural_grass.clump_colour", "Clump Colour"), 0.0f, 1.0f);

		v.Section(T("feature.procedural_grass.colour_section", "Colour"));
		v("BaseColor", s.baseColor, T("feature.procedural_grass.base_color", "Base Color"), 0.0f, 1.0f, nullptr);
		v("TipColor", s.tipColor, T("feature.procedural_grass.tip_color", "Tip Color"), 0.0f, 1.0f, nullptr);
		v("ColorTipDry", s.grassColorTipDry, T("feature.procedural_grass.dried_tip_tint", "Dried Tip Tint"), 0.0f, 2.0f);
		v("ColorCool", s.grassColorCool, T("feature.procedural_grass.cool_tint", "Cool/Green Tint"), 0.0f, 2.0f);
		v("ColorWarm", s.grassColorWarm, T("feature.procedural_grass.warm_tint", "Warm/Straw Tint"), 0.0f, 2.0f);
		v("HueVariation", s.grassColorHueVariation, T("feature.procedural_grass.hue_variation", "Hue Variation"), 0.0f, 1.0f);
		v("ValueVariation", s.grassColorValueVariation, T("feature.procedural_grass.brightness_variation", "Brightness Variation"), 0.0f, 1.0f);
		v("TipDryStrength", s.grassColorTipDryStrength, T("feature.procedural_grass.tip_dry_strength", "Tip Dry Strength"), 0.0f, 1.0f);
		v("MottleStrength", s.grassColorMottleStrength, T("feature.procedural_grass.mottle_strength", "Mottle Strength"), 0.0f, 0.5f);

		v.Section(T("feature.procedural_grass.lighting_section", "Lighting"));
		v("MinAO", s.ao, T("feature.procedural_grass.baked_min_ao", "Baked Min AO"), 0.0f, 1.0f);
		v("Specular", s.specular, T("feature.procedural_grass.specular", "Specular"), 0.0f, 1.0f);
		v("SpecularAnisotropy", s.specularAnisotropy, T("feature.procedural_grass.specular_anisotropy", "Specular Anisotropy"), 0.0f, 1.0f);
		v("SheenStrength", s.sheenStrength, T("feature.procedural_grass.sheen_strength", "Sheen Strength"), 0.0f, 1.0f);
		v("SheenRoughness", s.sheenRoughness, T("feature.procedural_grass.sheen_roughness", "Sheen Roughness"), 0.0f, 1.0f);
		v("SheenTint", s.sheenTint, T("feature.procedural_grass.sheen_tint", "Sheen Tint"), 0.0f, 1.0f);
		v("CurvedNormalStrength", s.curvedNormalStrength, T("feature.procedural_grass.curved_normal_strength", "Curved Normal Strength"), 0.0f, 1.0f);
		v("SubsurfaceOpacity", s.subsurfaceOpacity, T("feature.procedural_grass.subsurface_base_tip", "Subsurface Opacity (Base>Tip)"), 0.0f, 1.0f);
		v("SubsurfaceTint", s.grassSubsurfaceTint, T("feature.procedural_grass.subsurface_color", "Subsurface Tint"), 0.0f, 1.0f);
		v("TransmissionStrength", s.grassTransmissionStrength, T("feature.procedural_grass.transmission_strength", "Transmission Strength"), 0.0f, 2.0f);
		v("BaseMinTipRoughness", s.baseMinTipRoughness, T("feature.procedural_grass.roughness", "Roughness (Base>Min>Tip)"), 0.0f, 1.0f);
		v("TipRoughnessStart", s.tipRoughnessStart, T("feature.procedural_grass.roughness_tip_start", "Roughness Tip Start"), 0.05f, 0.95f);
		v("BounceStrength", s.grassBounceStrength, T("feature.procedural_grass.ground_bounce", "Ground Bounce"), 0.0f, 2.0f);
		v("BounceColor", s.grassBounceColor, T("feature.procedural_grass.ground_bounce_tint", "Ground Bounce Tint"), 0.0f, 2.0f);
		v("AmbientDesat", s.grassAmbientDesat, T("feature.procedural_grass.ambient_desaturation", "Ambient Desaturation"), 0.0f, 1.0f);
	}

	/** @brief Adapts a (key, value) callback to the override visitor shape, ignoring sections and editor ranges. */
	template <class Callback>
	struct OverrideVisitor
	{
		Callback callback;

		void Section(const char*) const {}

		template <class T>
		void operator()(const char* key, T& value, const char*, float, float, const char* = nullptr) const
		{
			callback(key, value);
		}
	};

	/** @brief Draws one override row: an enable checkbox seeded from the base value, then the field editor. */
	struct OverrideEditor
	{
		nlohmann::json& ov;
		bool& edited;

		void Section(const char* label) const { ImGui::SeparatorText(label); }

		template <class T>
		void operator()(const char* key, const T& base, const char* label, float mn, float mx, const char* fmt = "%.2f") const
		{
			bool has = ov.contains(key);

			if (ImGui::Checkbox(std::format("##en_{}", key).c_str(), &has)) {
				edited = true;
				if (has)
					ov[key] = base;
				else
					ov.erase(key);
			}

			ImGui::SameLine();
			ImGui::BeginDisabled(!has);
			T val = has && IsValidOverrideValue<T>(ov[key]) ? ov[key].get<T>() : base;
			bool changed;

			if constexpr (std::is_same_v<T, float>)
				changed = ImGui::SliderFloat(label, &val, mn, mx, fmt);
			else if constexpr (std::is_same_v<T, float2>)
				changed = ImGui::SliderFloat2(label, &val.x, mn, mx, fmt);
			else
				changed = fmt ? ImGui::SliderFloat3(label, &val.x, mn, mx, fmt) : ImGui::ColorEdit3(label, &val.x);

			if (changed && has) {
				ov[key] = val;
				edited = true;
			}

			ImGui::EndDisabled();
		}
	};

	uint64_t GrassTypeSettingsHash(const ProceduralGrass::Settings& settings)
	{
		uint64_t hash = PGrassCommon::GrassHashOffsetBasis;
		ForEachTypeOverride(settings, OverrideVisitor{ [&](const char*, const auto& value) {
			PGrassCommon::GrassHashValue(hash, value);
		} });
		// View thickening also changes the culling bounds resolved with the type tables.
		PGrassCommon::GrassHashValue(hash, settings.grassViewThicken);
		return hash;
	}

	/** @brief Keeps slider previews separate from applied LOD settings until the edit ends. */
	bool DensitySlider(const char* label, int& value, int mn, int mx, const char* const* names = nullptr)
	{
		ImGui::PushID(label);
		const auto previewID = ImGui::GetID("Preview");
		const auto editingID = ImGui::GetID("Editing");
		const auto frameID = ImGui::GetID("Frame");
		ImGui::PopID();

		auto* storage = ImGui::GetStateStorage();
		const int frame = ImGui::GetFrameCount();
		const bool editing = storage->GetBool(editingID) && storage->GetInt(frameID, -1) == frame - 1;
		int preview = editing ? storage->GetInt(previewID, value) : value;
		preview = std::clamp(preview, mn, mx);
		const auto flags = ImGuiSliderFlags_AlwaysClamp | (names ? ImGuiSliderFlags_NoInput : 0);
		const bool changed = ImGui::SliderInt(label, &preview, mn, mx, names ? names[preview] : "%d", flags);
		const bool active = ImGui::IsItemActive();
		const bool committed = ImGui::IsItemDeactivatedAfterEdit() || (changed && !active);

		storage->SetInt(previewID, preview);
		storage->SetBool(editingID, active);
		storage->SetInt(frameID, frame);
		if (!committed || preview == value)
			return false;

		value = preview;
		return true;
	}

	/** @brief Drops override entries whose key is unknown or whose value has the wrong shape. */
	void RemoveInvalidGrassTypeOverrides(nlohmann::json& overrides)
	{
		const ProceduralGrass::Settings defaults{};
		for (auto it = overrides.begin(); it != overrides.end();) {
			bool valid = false;
			ForEachTypeOverride(defaults, OverrideVisitor{ [&](const char* key, const auto& base) {
				if (it.key() == key)
					valid = IsValidOverrideValue<std::remove_cvref_t<decltype(base)>>(it.value());
			} });
			it = valid ? std::next(it) : overrides.erase(it);
		}
	}

	void DrawSettingDescription(const char* description)
	{
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(description);
	}

	/** @brief Draws a float slider with its hover description. Returns true when the value changed. */
	bool SettingSlider(const char* label, float& value, float mn, float mx, const char* description, const char* fmt = "%.2f")
	{
		const bool changed = ImGui::SliderFloat(label, &value, mn, mx, fmt);
		DrawSettingDescription(description);
		return changed;
	}

	/** @brief Draws a three-component slider, or a colour picker when @p fmt is null, with its hover description. */
	bool SettingSlider3(const char* label, float3& value, float mn, float mx, const char* description, const char* fmt = "%.2f")
	{
		const bool changed = fmt ? ImGui::SliderFloat3(label, &value.x, mn, mx, fmt) : ImGui::ColorEdit3(label, &value.x);
		DrawSettingDescription(description);
		return changed;
	}

	/** @brief Draws a checkbox with its hover description. Returns true when the value changed. */
	bool SettingCheckbox(const char* label, bool& value, const char* description)
	{
		const bool changed = ImGui::Checkbox(label, &value);
		DrawSettingDescription(description);
		return changed;
	}
}

PGrassCommon::GrassType ProceduralGrass::ResolveGrassType(const nlohmann::json& typeOverride) const
{
	// Present keys override the base setting; missing keys inherit it. The editor and the loader share this key list.
	GrassTypeFields fields = settings;
	if (typeOverride.is_object()) {
		ForEachTypeOverride(fields, OverrideVisitor{ [&](const char* key, auto& value) {
			using ValueT = std::remove_cvref_t<decltype(value)>;
			if (const auto it = typeOverride.find(key); it != typeOverride.end() && IsValidOverrideValue<ValueT>(*it))
				value = it->template get<ValueT>();
		} });
	}
	return BuildGrassType(fields);
}

void ProceduralGrass::LoadTextureTypes()
{
	settings.textureTypes.clear();

	const auto path = TextureTypesPath();
	std::ifstream input(path);
	if (!input.is_open()) {
		if (std::filesystem::exists(path))
			logger::warn("[Procedural Grass] Failed to open texture types file: {}", path.string());
		return;
	}

	try {
		json textureTypesJson;
		input >> textureTypesJson;
		if (!textureTypesJson.is_object()) {
			logger::warn("[Procedural Grass] Texture types file must contain an object: {}", path.string());
			return;
		}

		for (const auto& [key, variants] : textureTypesJson.items()) {
			if (!variants.is_array()) {
				logger::warn("[Procedural Grass] Ignoring invalid texture type variants for {}", key);
				continue;
			}

			std::vector<Settings::GrassTypeDef> defs;
			for (const auto& variant : variants) {
				if (!variant.is_object())
					continue;

				Settings::GrassTypeDef def;
				if (auto weight = variant.find("Weight"); weight != variant.end() && weight->is_number())
					def.weight = weight->get<float>();
				if (auto noGrass = variant.find("NoGrass"); noGrass != variant.end() && noGrass->is_boolean())
					def.noGrass = noGrass->get<bool>();
				if (auto overrides = variant.find("Overrides"); overrides != variant.end() && overrides->is_object()) {
					def.overrides = *overrides;
					RemoveInvalidGrassTypeOverrides(def.overrides);
				}
				defs.push_back(std::move(def));
			}
			if (!defs.empty())
				settings.textureTypes[key] = std::move(defs);
		}
	} catch (const nlohmann::json::exception& e) {
		logger::warn("[Procedural Grass] Failed to parse texture types file {}: {}", path.string(), e.what());
	}
}

void ProceduralGrass::SaveTextureTypes() const
{
	const auto path = TextureTypesPath();
	try {
		std::filesystem::create_directories(path.parent_path());
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("[Procedural Grass] Failed to create texture types directory {}: {}", path.parent_path().string(), e.what());
		return;
	}

	std::ofstream output(path);
	if (!output.is_open()) {
		logger::warn("[Procedural Grass] Failed to open texture types file for saving: {}", path.string());
		return;
	}

	json textureTypesJson = json::object();
	for (const auto& [key, defs] : settings.textureTypes) {
		json variants = json::array();
		for (const auto& def : defs) {
			json variant{ { "Weight", def.weight }, { "Overrides", def.overrides } };
			if (def.noGrass)
				variant["NoGrass"] = true;
			variants.push_back(std::move(variant));
		}
		textureTypesJson[key] = std::move(variants);
	}

	try {
		output << textureTypesJson.dump(1);
	} catch (const nlohmann::json::exception& e) {
		logger::warn("[Procedural Grass] Failed to save texture types file {}: {}", path.string(), e.what());
	}
}

void ProceduralGrass::DrawSettings()
{
	const auto baseTypeHash = GrassTypeSettingsHash(settings);

	SettingCheckbox(T("feature.procedural_grass.enabled", "Enabled"), settings.Enabled, T("feature.procedural_grass.enabled_tooltip", "Enables procedural grass rendering."));

	ImGui::Separator();

	if (ImGui::CollapsingHeader(T("feature.procedural_grass.base_type_section", "Base Grass Type Settings"), ImGuiTreeNodeFlags_DefaultOpen)) {
		SettingSlider3(T("feature.procedural_grass.base_color", "Base Color"), settings.baseColor, 0.0f, 1.0f, T("feature.procedural_grass.base_color_tooltip", "Sets the color at the base of each blade before texture-specific overrides."), nullptr);
		SettingSlider3(T("feature.procedural_grass.tip_color", "Tip Color"), settings.tipColor, 0.0f, 1.0f, T("feature.procedural_grass.tip_color_tooltip", "Sets the color at the tip of each blade before texture-specific overrides."), nullptr);

		if (ImGui::CollapsingHeader(T("feature.procedural_grass.colour_variation_section", "Colour Variation"))) {
			SettingSlider(T("feature.procedural_grass.hue_variation", "Hue Variation"), settings.grassColorHueVariation, 0.0f, 1.0f, T("feature.procedural_grass.hue_variation_tooltip", "Randomly shifts blade hue to reduce uniform coloring."));
			SettingSlider(T("feature.procedural_grass.brightness_variation", "Brightness Variation"), settings.grassColorValueVariation, 0.0f, 1.0f, T("feature.procedural_grass.brightness_variation_tooltip", "Randomly varies blade brightness."));
			SettingSlider(T("feature.procedural_grass.tip_dry_strength", "Tip Dry Strength"), settings.grassColorTipDryStrength, 0.0f, 1.0f, T("feature.procedural_grass.tip_dry_strength_tooltip", "Controls how strongly the dried-tip tint affects blade tips."));
			SettingSlider(T("feature.procedural_grass.mottle_strength", "Mottle Strength"), settings.grassColorMottleStrength, 0.0f, 0.5f, T("feature.procedural_grass.mottle_strength_tooltip", "Adds broad color variation across each blade."));

			SettingSlider3(T("feature.procedural_grass.cool_tint", "Cool/Green Tint"), settings.grassColorCool, 0.0f, 2.0f, T("feature.procedural_grass.cool_tint_tooltip", "Sets the tint used by cooler blade color variation."));
			SettingSlider3(T("feature.procedural_grass.warm_tint", "Warm/Straw Tint"), settings.grassColorWarm, 0.0f, 2.0f, T("feature.procedural_grass.warm_tint_tooltip", "Sets the tint used by warmer blade color variation."));
			SettingSlider3(T("feature.procedural_grass.dried_tip_tint", "Dried Tip Tint"), settings.grassColorTipDry, 0.0f, 2.0f, T("feature.procedural_grass.dried_tip_tint_tooltip", "Sets the color applied to dried blade tips."));
		}

		if (ImGui::CollapsingHeader(T("feature.procedural_grass.blade_lighting_section", "Blade Lighting"))) {
			ImGui::SeparatorText(T("feature.procedural_grass.material_lighting_section", "Blade Material"));
			SettingSlider(T("feature.procedural_grass.baked_min_ao", "Baked Min AO"), settings.ao, 0.0f, 1.0f, T("feature.procedural_grass.baked_min_ao_tooltip", "Sets the minimum ambient occlusion baked into each blade."));
			ImGui::SliderFloat2(T("feature.procedural_grass.subsurface_opacity", "Subsurface Opacity (Base>Tip)"), reinterpret_cast<float*>(&settings.subsurfaceOpacity), 0.0f, 1.0f, "%.2f");
			DrawSettingDescription(T("feature.procedural_grass.subsurface_opacity_tooltip", "Sets the opaque share of the blade material at its base and tip. 1 blocks transmission; lower values mix in thin-walled subsurface scattering. Blade visibility is unchanged."));
			SettingSlider3(T("feature.procedural_grass.subsurface_color", "Subsurface Tint"), settings.grassSubsurfaceTint, 0.0f, 1.0f, T("feature.procedural_grass.subsurface_color_tooltip", "Sets the thin-subsurface scattering albedo independently of the opaque blade colour. Blade colour variation remains in both scattered lobes. A brighter tint passes more coloured light while keeping the shared reflection/transmission budget bounded."));
			SettingSlider(T("feature.procedural_grass.transmission_strength", "Transmission Strength"), settings.grassTransmissionStrength, 0.0f, 2.0f, T("feature.procedural_grass.transmission_strength_tooltip", "Splits thin-subsurface scattering between reflection and transmission. 0 reflects all, 1 splits equally, 1.5 transmits 75%, and 2 transmits all. Subsurface opacity independently limits scattering coverage. Increasing transmission reduces reflection to conserve energy."));
			SettingSlider(T("feature.procedural_grass.specular", "Specular"), settings.specular, 0.0f, 1.0f, T("feature.procedural_grass.specular_tooltip", "Sets how much light the blade surface reflects at normal incidence. It controls highlights and the sky reflection that brightens fields at grazing angles. 0.04 matches OpenPBR's default dielectric IOR of 1.5."));
			SettingSlider(T("feature.procedural_grass.specular_anisotropy", "Specular Anisotropy"), settings.specularAnisotropy, 0.0f, 1.0f, T("feature.procedural_grass.specular_anisotropy_tooltip", "Stretches highlights across each blade, as its parallel veins do, so blade fronts catch the sun over a wider range of angles. 0 is a smooth, isotropic surface."));
			SettingSlider(T("feature.procedural_grass.sheen_strength", "Sheen Strength"), settings.sheenStrength, 0.0f, 1.0f, T("feature.procedural_grass.sheen_strength_tooltip", "Covers blades with fine hairs and wax bloom (the OpenPBR fuzz layer). They catch light at grazing angles and give grass a soft silvery sheen."));
			SettingSlider(T("feature.procedural_grass.sheen_roughness", "Sheen Roughness"), settings.sheenRoughness, 0.0f, 1.0f, T("feature.procedural_grass.sheen_roughness_tooltip", "Low values give a fibre-like sheen concentrated at grazing angles; high values give a broader, dusty sheen."));
			SettingSlider(T("feature.procedural_grass.sheen_tint", "Sheen Tint"), settings.sheenTint, 0.0f, 1.0f, T("feature.procedural_grass.sheen_tint_tooltip", "Colours the sheen toward the blade's own hue (the OpenPBR fuzz colour). 0 is a white sheen; higher values keep backlit glow and blade colour from greying under sky light."));
			SettingSlider(T("feature.procedural_grass.curved_normal_strength", "Curved Normal Strength"), settings.curvedNormalStrength, 0.0f, 1.0f, T("feature.procedural_grass.curved_normal_strength_tooltip", "How far the blade rolls across its width, as the angle its edges turn away from the middle (1 = 90 degrees). Sky light and screen-space GI see three times this roll, and the concave face loses sky behind its own halves, so the curvature still shows without direct light."));
			SettingSlider3(T("feature.procedural_grass.roughness", "Roughness (Base>Min>Tip)"), settings.baseMinTipRoughness, 0.0f, 1.0f, T("feature.procedural_grass.roughness_tooltip", "Sets roughness at the blade base, minimum point, and tip."));

			// Kept off 0 and 1 so neither smoothstep in the vertex shader collapses to a zero-width range.
			SettingSlider(T("feature.procedural_grass.roughness_tip_start", "Roughness Tip Start"), settings.tipRoughnessStart, 0.05f, 0.95f, T("feature.procedural_grass.roughness_tip_start_tooltip", "Sets where roughness begins transitioning toward the tip value."));

			ImGui::SeparatorText(T("feature.procedural_grass.canopy_lighting_section", "Canopy and Ambient"));
			SettingSlider(T("feature.procedural_grass.canopy_base_shading", "Canopy Base Shading"), settings.grassBaseAO, 0.0f, 1.0f, T("feature.procedural_grass.canopy_base_shading_tooltip", "Darkens blade bases beneath the grass canopy."));
			SettingSlider(T("feature.procedural_grass.canopy_sky_occlusion", "Canopy Sky Occlusion"), settings.grassCanopySkyOcclusion, 0.0f, 1.0f, T("feature.procedural_grass.canopy_sky_occlusion_tooltip", "Darkens blade surfaces deep in the grass that face sideways or down, toward neighbouring blades and the ground, while those facing the sky keep their light. Keeps blades shaped under overcast skies."));
			SettingSlider(T("feature.procedural_grass.density_occlusion", "Density Occlusion"), settings.grassDensityAO, 0.0f, 1.0f, T("feature.procedural_grass.density_occlusion_tooltip", "Darkens areas containing more overlapping blades."));

			SettingSlider(T("feature.procedural_grass.ground_bounce", "Ground Bounce"), settings.grassBounceStrength, 0.0f, 2.0f, T("feature.procedural_grass.ground_bounce_tooltip", "Controls indirect light reflected from the ground onto blades."));
			SettingSlider3(T("feature.procedural_grass.ground_bounce_tint", "Ground Bounce Tint"), settings.grassBounceColor, 0.0f, 2.0f, T("feature.procedural_grass.ground_bounce_tint_tooltip", "Tints indirect light reflected from the ground."));
			SettingSlider(T("feature.procedural_grass.ambient_desaturation", "Ambient Desaturation"), settings.grassAmbientDesat, 0.0f, 1.0f, T("feature.procedural_grass.ambient_desaturation_tooltip", "Removes color from ambient light on grass."));
		}

		if (ImGui::CollapsingHeader(T("feature.procedural_grass.terrain_blend_section", "Terrain Blend"))) {
			// Blade bases blend over the completed terrain G-buffer to soften the intersection.
			SettingSlider(T("feature.procedural_grass.base_dissolve", "Base Fade"), settings.grassTerrainBlendStrength, 0.0f, 1.0f, T("feature.procedural_grass.base_dissolve_tooltip", "Smoothly fades blade bases over terrain to hide their intersection."));
			SettingSlider(T("feature.procedural_grass.dissolve_height", "Fade Height (units)"), settings.grassTerrainBlendHeight, 0.0f, 60.0f, T("feature.procedural_grass.dissolve_height_tooltip", "Sets how far the terrain blend extends up each blade."), "%.1f");
			SettingSlider(T("feature.procedural_grass.base_normal_flatten", "Base Normal Flatten"), settings.grassTerrainBlendNormal, 0.0f, 1.0f, T("feature.procedural_grass.base_normal_flatten_tooltip", "Blends blade-base normals toward the terrain normal."));
			SettingSlider(T("feature.procedural_grass.base_roughness", "Base Roughness"), settings.grassTerrainBlendRough, 0.0f, 1.0f, T("feature.procedural_grass.base_roughness_tooltip", "Sets blade roughness near the terrain intersection."));
		}

		ImGui::Separator();

		SettingSlider(T("feature.procedural_grass.height", "Height"), settings.grassHeight, 0.0f, 150.0f, T("feature.procedural_grass.height_tooltip", "Sets the default blade height in world units."), "%.1f");
		SettingSlider(T("feature.procedural_grass.width", "Width"), settings.grassWidth, 0.0f, 10.0f, T("feature.procedural_grass.width_tooltip", "Sets the default blade width."), "%.1f");
		SettingSlider(T("feature.procedural_grass.view_thicken", "View Thicken"), settings.grassViewThicken, 0.0f, 2.0f, T("feature.procedural_grass.view_thicken_tooltip", "Widens blades viewed edge-on to keep them visible."));
		SettingSlider(T("feature.procedural_grass.k1", "K1"), settings.stiffness, -10.0f, 10.0f, T("feature.procedural_grass.k1_tooltip", "Controls random sideways curvature through the middle of each blade."));
		SettingSlider(T("feature.procedural_grass.k2", "K2"), settings.tipWeight, -10.0f, 10.0f, T("feature.procedural_grass.k2_tooltip", "Controls the random tilt applied to blade tips."));
		SettingSlider(T("feature.procedural_grass.mid", "Mid"), settings.mid, 0.0f, 1.0f, T("feature.procedural_grass.mid_tooltip", "Positions the middle control point along the blade to shape its curve."));
		SettingSlider(T("feature.procedural_grass.rotational_stiffness", "Rotational Stiffness"), settings.rotationalStiffness, 0.0f, 10.0f, T("feature.procedural_grass.rotational_stiffness_tooltip", "Controls how strongly blades resist rotating to face the wind."));

		ImGui::SeparatorText(T("feature.procedural_grass.clump_section", "Clump"));
		SettingSlider(T("feature.procedural_grass.clump_grid_size", "Clump Grid Size"), settings.clumpGridSize, MinClumpGridSize, MaxClumpGridSize, T("feature.procedural_grass.clump_grid_size_tooltip", "Sets the average spacing between generated grass clumps, in world units."), "%.0f");
		SettingSlider(T("feature.procedural_grass.clump_distance_factor", "Clump Distance Factor"), settings.clumpDistanceFactor, 0.0f, 1.0f, T("feature.procedural_grass.clump_distance_tooltip", "Pulls blades toward their clump center."));
		SettingSlider(T("feature.procedural_grass.clump_facing_factor", "Clump Facing Factor"), settings.clumpFacingFactor, -1.0f, 1.0f, T("feature.procedural_grass.clump_facing_tooltip", "Positive values turn blades away from their clump center so clumps splay outward. Negative values turn them inward."));
		SettingSlider(T("feature.procedural_grass.clump_lean_factor", "Clump Lean Factor"), settings.clumpLeanFactor, 0.0f, 1.0f, T("feature.procedural_grass.clump_lean_tooltip", "Turns the blades of each clump toward one shared direction, so neighboring clumps lean different ways."));
		SettingSlider(T("feature.procedural_grass.clump_height_factor", "Clump Height Factor"), settings.clumpHeightFactor, 0.0f, 1.0f, T("feature.procedural_grass.clump_height_tooltip", "Sets how much blades share their clump's height instead of their own, so neighboring clumps stand at different heights."));
		SettingSlider(T("feature.procedural_grass.clump_ao_strength", "Clump AO Strength"), settings.clumpAOStrength, 0.0f, 1.0f, T("feature.procedural_grass.clump_ao_tooltip", "Darkens blade bases toward each clump's center, where blades crowd together. Blade tips stay lit."));
		SettingSlider(T("feature.procedural_grass.clump_colour_patches", "Clump Colour Patches"), settings.grassClumpColorStrength, 0.0f, 1.0f, T("feature.procedural_grass.clump_color_tooltip", "Varies color between neighboring grass clumps."));

		ImGui::SeparatorText(T("feature.procedural_grass.slope_section", "Slope"));
		SettingSlider(T("feature.procedural_grass.max_slope", "Max Slope (deg)"), settings.grassMaxSlope, 0.0f, 90.0f, T("feature.procedural_grass.max_slope_tooltip", "Stops normal grass from growing on slopes above this angle. 90 disables the limit."), "%.0f");
		SettingSlider(T("feature.procedural_grass.min_slope", "Min Slope (deg)"), settings.grassMinSlope, 0.0f, 90.0f, T("feature.procedural_grass.min_slope_tooltip", "Stops grass from growing on slopes below this angle."), "%.0f");
		SettingSlider(T("feature.procedural_grass.slope_facing", "Slope Facing"), settings.grassSlopeFacing, 0.0f, 1.0f, T("feature.procedural_grass.slope_facing_tooltip", "Leans blades downhill based on terrain steepness."));
	}

	if (ImGui::CollapsingHeader(T("feature.procedural_grass.global_settings_section", "Global Grass Settings"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SeparatorText(T("feature.procedural_grass.wind_section", "Wind"));
		if (ImGui::SliderAngle(T("feature.procedural_grass.wind_direction", "Wind Direction"), &settings.windAngle)) {
			windDirection = float2(cos(settings.windAngle), sin(settings.windAngle));
		}
		DrawSettingDescription(T("feature.procedural_grass.wind_direction_tooltip", "Sets the horizontal direction of grass movement."));

		SettingSlider(T("feature.procedural_grass.wind_speed", "Wind Speed"), settings.windSpeed, 0.0f, 1.0f, T("feature.procedural_grass.wind_speed_tooltip", "Controls how quickly wind waves move through the grass."));

		ImGui::SeparatorText(T("feature.procedural_grass.lod_density_section", "LOD Density"));
		if (DensitySlider(T("feature.procedural_grass.high_density", "Density Preset"), settings.Quality, 0, static_cast<uint8_t>(Quality::Count) - 1, QualityNames))
			ApplyDensityPreset(settings.Quality);
		DrawSettingDescription(T("feature.procedural_grass.high_density_tooltip", "Sets blade density in the closest tier and matching densities for the middle and low tiers."));
		if (DensitySlider(T("feature.procedural_grass.mid_density", "Density (Mid LOD)"), settings.midGrassDensity, 8, 320))
			ApplyTierDensities();
		DrawSettingDescription(T("feature.procedural_grass.mid_density_tooltip", "Sets blade density in the middle-distance grass tier."));
		if (DensitySlider(T("feature.procedural_grass.low_density", "Density (Low LOD)"), settings.lowGrassDensity, 8, 640))
			ApplyTierDensities();
		DrawSettingDescription(T("feature.procedural_grass.low_density_tooltip", "Sets blade density in the low-detail grass tier."));
	}

	ImGui::Separator();

	DrawGrassTypeEditor();

	grassTypesDirty |= baseTypeHash != GrassTypeSettingsHash(settings);
}

void ProceduralGrass::DrawDebugSection()
{
	Util::DebugSection section;
	if (!section)
		return;

	bool invalidate = false;
	invalidate |= SettingCheckbox(T("feature.procedural_grass.debug_ignore_grass_map", "Ignore grass map (LTEX)"), settings.debugIgnoreGrassMap, T("feature.procedural_grass.debug_ignore_grass_map_tooltip", "Generates grass without consulting landscape texture grass assignments."));
	SettingSlider(T("feature.procedural_grass.debug_edge_noise", "Grass map edge noise (units)"), settings.grassMapEdgeNoise, 0.0f, 256.0f, T("feature.procedural_grass.debug_edge_noise_tooltip", "Jitters grass-map sampling near texture boundaries to soften distribution edges."), "%.0f");
	SettingCheckbox(T("feature.procedural_grass.debug_disable_culls", "Disable ALL generator culls"), settings.debugDisableAllCulls, T("feature.procedural_grass.debug_disable_culls_tooltip", "Disables generator rejection tests for debugging."));
	SettingCheckbox(T("feature.procedural_grass.debug_tier_view", "Visualize tiers"), settings.debugTierView, T("feature.procedural_grass.debug_tier_view_tooltip", "Colours blades by tier: High inner red, High outer orange, Mid yellow, Low green."));
	SettingCheckbox(T("feature.procedural_grass.debug_ignore_preprocessed", "Ignore preprocessed-node check"), settings.debugIgnorePreProcessedFlag, T("feature.procedural_grass.debug_ignore_preprocessed_tooltip", "Includes LAND meshes that are not marked as preprocessed."));
	SettingCheckbox(T("feature.procedural_grass.debug_disable_vanilla_grass", "Disable vanilla grass"), settings.debugDisableVanillaGrass, T("feature.procedural_grass.debug_disable_vanilla_grass_tooltip", "Turns vanilla grass off while procedural grass is enabled."));
	if (invalidate) {
		ClearGrassMapCache();
	}

	size_t grassSet = 0;
	size_t grassTotal = 0;
	for (const auto& entry : grassMapCache)
		for (const auto& quadrant : entry.second.quadrants) {
			grassTotal += quadrant.ids.size();
			for (const auto id : quadrant.ids)
				grassSet += id != 0;
		}

	const auto heightMap = globals::terrainHeightMap;
	const auto cached = heightMap->GetCached();
	ImGui::Text(T("feature.procedural_grass.debug_height_map_ready", "Height map ready: %s"), heightMap->IsReady() ? T("common.yes", "Yes") : T("common.no", "No"));
	ImGui::Text(T("feature.procedural_grass.debug_height_map_worldspace", "Height map worldspace: %s"), cached ? cached->worldspace.c_str() : T("feature.procedural_grass.debug_none", "<none>"));
	const auto posRange = heightMap->GetPosRange();
	ImGui::Text(T("feature.procedural_grass.debug_height_map_range", "Height map range: %.1f .. %.1f"), posRange.x, posRange.y);
	if (const auto pc = RE::PlayerCharacter::GetSingleton()) {
		const auto playerPos = pc->GetPosition();
		ImGui::Text(T("feature.procedural_grass.debug_player_z", "Player Z: %.1f"), playerPos.z);

		if (const auto landZ = GetLandHeightAt(playerPos.x, playerPos.y))
			ImGui::Text(T("feature.procedural_grass.debug_land_z", "LAND Z at player: %.1f  (delta %.1f)"), *landZ, *landZ - playerPos.z);
		else
			ImGui::TextUnformatted(T("feature.procedural_grass.debug_land_z_missing", "LAND Z at player: <no cached quadrant>"));
	}

	ImGui::Separator();

	ImGui::Text(T("feature.procedural_grass.debug_blade_counts", "Blades generated  high: %u  mid: %u  low: %u"),
		grassRendererHighLOD->ReadBladeCount(),
		grassRendererMidLOD->ReadBladeCount(),
		grassRendererLowLOD->ReadBladeCount());
	ImGui::Text(T("feature.procedural_grass.debug_quadrant_counts", "Quadrants  high: %zu  mid: %zu  low: %zu"),
		quadrantsHighLOD.size(), quadrantsMidLOD.size(), quadrantsLowLOD.size());
	ImGui::Text(T("feature.procedural_grass.debug_quadrant_rejects", "cells %u -> exterior %u -> land %u -> loadedData %u -> mesh %u -> preProcessed %u"),
		quadrantReject.cells, quadrantReject.withExterior, quadrantReject.withLand,
		quadrantReject.withLoadedData, quadrantReject.withMesh, quadrantReject.preProcessed);

	ImGui::Separator();

	ImGui::Text(T("feature.procedural_grass.debug_quadrants_cached", "Grass quadrants cached: %zu"), grassMapCache.size());
	ImGui::Text(T("feature.procedural_grass.debug_grass_samples", "Samples growing grass: %zu / %zu (%.1f%%)"), grassSet, grassTotal, grassTotal ? 100.0 * grassSet / grassTotal : 0.0);
}

bool ProceduralGrass::DrawTypeOverrides(nlohmann::json& ov) const
{
	bool edited = false;
	ForEachTypeOverride(settings, OverrideEditor{ ov, edited });
	ImGui::Spacing();
	if (Util::Button(T("feature.procedural_grass.clear_overrides", "Clear all overrides")) && !ov.empty()) {
		ov = nlohmann::json::object();
		edited = true;
	}
	return edited;
}

void ProceduralGrass::DrawGrassTypeEditor()
{
	if (!ImGui::CollapsingHeader(T("feature.procedural_grass.grass_types_section", "Grass Types")))
		return;

	const auto allocatedGrassVariants = typeAllocation.size();
	const auto maxGrassVariants = PGrassCommon::MaxGrassTypes - 2;
	const auto grassTypesDescription = std::vformat(
		T("feature.procedural_grass.grass_types_description",
			"Grass types are per landscape texture. Expand a texture and add one or more type variants; each overrides "
			"only the fields you tick (unticked fields inherit the base settings above) and carries a weight. A texture's "
			"blades are split between its variants in proportion to their weights. A texture with no variants grows the "
			"base type when it supports vanilla grass. Configured variants also enable procedural grass on textures with "
			"no vanilla grass. No Grass variants suppress their weighted share without consuming a type slot. Allocated "
			"grass variants: {} / {}."),
		std::make_format_args(allocatedGrassVariants, maxGrassVariants));
	ImGui::TextWrapped("%s", grassTypesDescription.c_str());

	static char filter[128] = "";
	ImGui::InputTextWithHint(T("feature.procedural_grass.filter", "Filter"), T("feature.procedural_grass.filter_hint", "editor id / plugin"), filter, sizeof(filter));
	static bool onlyGrass = true;
	ImGui::SameLine();
	ImGui::Checkbox(T("feature.procedural_grass.only_grass_growing", "Only grass-growing"), &onlyGrass);

	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler) {
		ImGui::TextDisabled("%s", T("feature.procedural_grass.data_handler_unavailable", "Data handler unavailable (not in a loaded game)."));
		return;
	}

	const auto matchesFilter = [](std::string_view haystack, const char* needle) {
		if (!needle[0])
			return true;
		const auto lower = [](std::string v) { std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); }); return v; };
		return lower(std::string(haystack)).find(lower(needle)) != std::string::npos;
	};

	bool typesChanged = false;

	if (ImGui::BeginChild("LandTextureList", ImVec2(0, 360), ImGuiChildFlags_Borders)) {
		for (auto* ltex : dataHandler->GetFormArray<RE::TESLandTexture>()) {
			if (!ltex)
				continue;

			const char* edid = ltex->GetFormEditorID();
			const std::string name = (edid && edid[0]) ? edid : T("feature.procedural_grass.no_editor_id", "<no editor id>");
			const std::string key = LandTextureKey(ltex);
			auto texIt = settings.textureTypes.find(key);
			const size_t count = texIt != settings.textureTypes.end() ? texIt->second.size() : 0;
			const bool growsVanillaGrass = !ltex->textureGrassList.empty();
			if (onlyGrass && !growsVanillaGrass && count == 0)
				continue;
			if (!matchesFilter(name, filter) && !matchesFilter(key, filter))
				continue;

			const auto vanillaGrassStatus = growsVanillaGrass ? "" : T("feature.procedural_grass.no_vanilla_grass", "(no vanilla grass) ");
			const auto typeLabel = count == 1 ? T("feature.procedural_grass.type_singular", "type") : T("feature.procedural_grass.type_plural", "types");
			const auto textureSummary = std::vformat(
				T("feature.procedural_grass.texture_type_summary", "{}   {}[{} {}]"),
				std::make_format_args(name, vanillaGrassStatus, count, typeLabel));
			if (!ImGui::TreeNode(key.c_str(), "%s", textureSummary.c_str()))
				continue;

			ImGui::TextDisabled("%s", key.c_str());

			auto& defs = settings.textureTypes[key];
			float totalWeight = 0.0f;
			for (const auto& d : defs)
				totalWeight += std::max(0.0f, d.weight);

			int removeIndex = -1;
			for (uint32_t i = 0; i < defs.size(); i++) {
				ImGui::PushID(static_cast<int>(i));
				auto& def = defs[i];

				const float pct = totalWeight > 0.0f ? 100.0f * std::max(0.0f, def.weight) / totalWeight : 0.0f;
				ImGui::AlignTextToFramePadding();
				const auto variantIndex = i + 1;
				const auto variantLabel = std::vformat(T("feature.procedural_grass.variant", "Variant {}"), std::make_format_args(variantIndex));
				ImGui::TextUnformatted(variantLabel.c_str());
				ImGui::SameLine();
				ImGui::PushItemWidth(90.0f);
				if (ImGui::DragFloat(T("feature.procedural_grass.weight", "Weight"), &def.weight, 0.1f, 0.0f, 100.0f, "%.1f"))
					typesChanged = true;
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::TextDisabled("(%.0f%%)", pct);
				ImGui::SameLine();
				if (ImGui::SmallButton(T("feature.procedural_grass.remove", "Remove")))
					removeIndex = static_cast<int>(i);

				if (ImGui::Checkbox(T("feature.procedural_grass.no_grass", "No Grass"), &def.noGrass))
					typesChanged = true;
				DrawSettingDescription(T("feature.procedural_grass.no_grass_tooltip", "Makes this weighted variant produce bare terrain instead of grass."));

				ImGui::BeginDisabled(def.noGrass);
				const auto overrideCount = def.overrides.is_object() ? def.overrides.size() : 0;
				const auto overridesLabel = std::vformat(
					T("feature.procedural_grass.overrides_count", "Overrides ({} set)"),
					std::make_format_args(overrideCount));
				if (ImGui::TreeNode("Overrides", "%s", overridesLabel.c_str())) {  // i18n-ignore: stable tree ID; display label is translated
					grassTypesDirty |= DrawTypeOverrides(def.overrides);
					ImGui::TreePop();
				}
				ImGui::EndDisabled();

				ImGui::PopID();
				ImGui::Separator();
			}

			if (removeIndex >= 0) {
				defs.erase(defs.begin() + removeIndex);
				typesChanged = true;
			}

			if (typeAllocation.size() + 2 < PGrassCommon::MaxGrassTypes) {
				if (ImGui::SmallButton(T("feature.procedural_grass.add_variant", "Add variant"))) {
					defs.push_back({});
					typesChanged = true;
				}
			} else {
				const auto typePoolCapacity = PGrassCommon::MaxGrassTypes - 2;
				const auto typePoolFull = std::vformat(
					T("feature.procedural_grass.type_pool_full", "Type pool full ({})."),
					std::make_format_args(typePoolCapacity));
				ImGui::TextDisabled("%s", typePoolFull.c_str());
			}

			if (defs.empty())
				settings.textureTypes.erase(key);  // don't persist textures the user opened but left empty

			ImGui::TreePop();
		}
	}
	ImGui::EndChild();

	if (typesChanged) {
		RebuildTypeAllocation();
		ClearGrassMapCache();
	}
}

void ProceduralGrass::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.Quality = std::clamp(settings.Quality, 0, static_cast<int32_t>(Quality::Count) - 1);

	ForEachSettingKey(settings, [&](const char* key, auto& value) { value = o_json.value(key, value); });
	settings.clumpGridSize = std::clamp(settings.clumpGridSize, MinClumpGridSize, MaxClumpGridSize);
	settings.clumpHeightFactor = std::clamp(settings.clumpHeightFactor, 0.0f, 1.0f);
	windDirection = float2(std::cos(settings.windAngle), std::sin(settings.windAngle));

	LoadTextureTypes();
	RebuildTypeAllocation();
	// Cached LAND ids carry the previous texture types; drop them as RestoreDefaultSettings does.
	ClearGrassMapCache();

	settings.midGrassDensity = std::clamp(settings.midGrassDensity, 8, 320);
	settings.lowGrassDensity = std::clamp(settings.lowGrassDensity, 8, 640);
	ApplyTierDensities();
}

void ProceduralGrass::ApplyDensityPreset(const int32_t quality)
{
	settings.Quality = std::clamp(quality, 0, static_cast<int32_t>(Quality::Count) - 1);
	const float highDensity = static_cast<float>(QualityDensities[settings.Quality]);

	// Mid draws two of High's four blades per patch and Low one, all on High's lattice.
	settings.midGrassDensity = static_cast<int>(highDensity);
	settings.lowGrassDensity = static_cast<int>(highDensity);

	ApplyTierDensities();
}

void ProceduralGrass::ApplyTierDensities()
{
	if (grassRendererHighLOD)
		grassRendererHighLOD->SetDensity(QualityDensities[settings.Quality]);
	if (grassRendererMidLOD)
		grassRendererMidLOD->SetDensity(static_cast<uint32_t>(settings.midGrassDensity));
	if (grassRendererLowLOD)
		grassRendererLowLOD->SetDensity(static_cast<uint32_t>(settings.lowGrassDensity));
}

void ProceduralGrass::SaveSettings(json& o_json)
{
	SaveTextureTypes();
	o_json = settings;
	ForEachSettingKey(settings, [&](const char* key, const auto& value) { o_json[key] = value; });
}

void ProceduralGrass::RestoreDefaultSettings()
{
	settings = {};
	RebuildTypeAllocation();
	ClearGrassMapCache();

	windDirection = float2(std::cos(settings.windAngle), std::sin(settings.windAngle));
	ApplyTierDensities();
}
