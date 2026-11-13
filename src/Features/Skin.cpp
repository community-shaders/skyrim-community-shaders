#include "Skin.h"
#include "Skin/Editor/Files.h"
#include "Utils/UI.h"

#include <cmath>
#include <imgui_stdlib.h>

using namespace SkinMaterials;

void Skin::LoadSettings(json& a_json)
{
	std::lock_guard lock(mutex);
	Settings next;
	try {
		next.EnableSkin = a_json.value("EnableSkin", true);
		next.DetailTexture = a_json.value("DetailTexture", next.DetailTexture);
		next.MaterialOutputFolder = a_json.value("MaterialOutputFolder", next.MaterialOutputFolder);
		auto number = [&](const char* name, float& value, float minimum, float maximum) {
			const float input = a_json.value(name, value);
			if (!std::isfinite(input))
				throw std::runtime_error(std::string(T("feature.skin.invalid_setting", "Invalid setting: ")) + name);
			value = std::clamp(input, minimum, maximum);
		};
		const auto& profile = a_json.contains("DefaultProfile") ? a_json.at("DefaultProfile") : a_json;
		const auto fields = ParameterTable();
		for (size_t i = 0; i < fields.size(); ++i) {
			const auto& field = fields[i];
			float value = field.integer ? (profile.value(SettingName(i), field.initial != 0) ? 1.0f : 0.0f) : profile.value(SettingName(i), field.initial);
			if (!std::isfinite(value))
				throw std::runtime_error(std::string(T("feature.skin.invalid_setting", "Invalid setting: ")) + SettingName(i));
			next.DefaultProfile.values[i] = std::clamp(value, field.minimum, field.maximum);
		}
		if (!a_json.contains("DefaultProfile")) {
			if (a_json.contains("EnableDetail"))
				next.DefaultProfile[Parameter::DetailEnabled] = a_json.at("EnableDetail").get<bool>() ? 1.0f : 0.0f;
			number("DetailStrength", next.DefaultProfile[Parameter::DetailStrength], -2, 2);
			number("DetailTiling", next.DefaultProfile[Parameter::DetailTiling], 0.1f, 100);
			number("BodyTiling", next.DefaultProfile[Parameter::BodyTilingMultiplier], 0.1f, 5);
		}
		number("ExtraSkinWetness", next.ExtraSkinWetness, 0, 2);
		number("WetFadeTime", next.WetFadeTime, 0, 50);
		number("StartSweat", next.StartSweat, 0, 1);
		number("FullSweat", next.FullSweat, 0, 1);
		if (a_json.contains("WetParams")) {
			next.WetParams = a_json.at("WetParams").get<float4>();
			const auto& w = next.WetParams;
			if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z) || !std::isfinite(w.w) ||
				w.x < 0 || w.x > 1024 || w.y < 0 || w.y > 2 || w.z < 0 || w.z > 20 || w.w < 0 || w.w > 20)
				throw std::runtime_error(T("feature.skin.invalid_global_wetness_parameters", "Invalid global wetness parameters."));
		}
		settings = next;
	} catch (const std::exception& e) {
		logger::warn("[Advanced Skin] Global settings retained: {}", e.what());
	}
	Invalidate(true);
}

void Skin::SaveSettings(json& a_json)
{
	std::lock_guard lock(mutex);
	json profile = json::object();
	const auto fields = ParameterTable();
	for (size_t i = 0; i < fields.size(); ++i)
		profile[SettingName(i)] = fields[i].integer ? json(settings.DefaultProfile.values[i] != 0) : json(settings.DefaultProfile.values[i]);
	a_json = { { "EnableSkin", settings.EnableSkin }, { "DefaultProfile", profile },
		{ "DetailTexture", settings.DetailTexture }, { "MaterialOutputFolder", settings.MaterialOutputFolder },
		{ "ExtraSkinWetness", settings.ExtraSkinWetness }, { "WetFadeTime", settings.WetFadeTime },
		{ "StartSweat", settings.StartSweat }, { "FullSweat", settings.FullSweat }, { "WetParams", settings.WetParams } };
}

void Skin::RestoreDefaultSettings()
{
	std::lock_guard lock(mutex);
	settings = {};
	Invalidate(true);
}

void Skin::DrawSettings()
{
	std::lock_guard lock(mutex);
	bool changed = ImGui::Checkbox(T("feature.skin.enable_advanced_skin", "Enable Advanced Skin"), &settings.EnableSkin);
	ImGui::TextWrapped("%s", T("feature.skin.global_material_inheritance_v2", "Applies to all compatible skin. A skin texture's material file and per-character adjustments override only the values they explicitly set. Edit them in CS Editor > Skin Editor."));
	if (ImGui::CollapsingHeader(T("feature.skin.common_parameters", "Common skin parameters"), ImGuiTreeNodeFlags_DefaultOpen)) {
		const auto fields = ParameterTable();
		for (const auto parameter : ParameterOrder) {
			const auto i = static_cast<size_t>(parameter);
			auto& value = settings.DefaultProfile.values[i];
			if (fields[i].integer) {
				bool enabled = value != 0;
				changed |= ImGui::Checkbox(ParameterLabel(i), &enabled);
				value = enabled ? 1.0f : 0.0f;
			} else {
				changed |= ImGui::SliderFloat(ParameterLabel(i), &value, fields[i].minimum, fields[i].maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			}
		}
	}
	ImGui::TextWrapped("%s", T("feature.skin.global_detail_applies_when_a_material_has_no_detail_texture", "Global detail applies when a material has no detail texture. Its alpha mask uses the model's main UV, independently of tiling."));
	changed |= ImGui::InputText(T("feature.skin.global_detail_dds", "Global detail DDS"), &settings.DetailTexture);
	ImGui::SameLine();
	if (const auto chosen = SkinEditor::ResourcePicker(editor.browser, T("feature.skin.choose_dds", "Choose DDS"), settings.DetailTexture, ".dds")) {
		settings.DetailTexture = *chosen;
		changed = true;
	}
	if (Util::Button(T("feature.skin.reload_skin_resources", "Reload skin resources")))
		ReloadResources();
	DrawGlobalSettings();
	if (changed)
		Invalidate();
}

void Skin::DrawGlobalSettings()
{
	bool changed = ImGui::SliderFloat(T("feature.skin.extra_skin_wetness", "Extra skin wetness"), &settings.ExtraSkinWetness, 0, 2, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat(T("feature.skin.wetness_fade_time", "Wetness fade time"), &settings.WetFadeTime, 0, 50, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat(T("feature.skin.sweat_starts_below_stamina", "Sweat starts below stamina"), &settings.StartSweat, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat(T("feature.skin.full_sweat_below_stamina", "Full sweat below stamina"), &settings.FullSweat, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat(T("feature.skin.wetness_perlin_noise_scale", "Wetness Perlin Noise Scale"), &settings.WetParams.x, 0, 1024, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat(T("feature.skin.wetness_perlin_noise_lacunarity", "Wetness Perlin Noise Lacunarity"), &settings.WetParams.y, 0, 2, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat(T("feature.skin.wetness_perlin_noise_persistence", "Wetness Perlin Noise Persistence"), &settings.WetParams.z, 0, 20, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat(T("feature.skin.wetness_normal_scale", "Wetness Normal Scale"), &settings.WetParams.w, 0, 20, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	if (changed)
		Invalidate();
}
