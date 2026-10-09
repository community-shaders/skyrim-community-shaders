#include "Effects11Editor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <imgui_stdlib.h>

#include "EditorWidgets.h"
#include "Features/Effects11.h"
#include "Features/Effects11/EffectManager.h"
#include "Features/Effects11/PresetManager.h"
#include "Features/Effects11/SettingManager.h"
#include "Features/Effects11/SettingsPatches.h"
#include "Features/Effects11/ShaderPatches.h"
#include "Features/Effects11/UITree.h"
#include "Features/Effects11/WeatherManager.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.effects11.editor."

namespace
{
	constexpr uint32_t kInvalidSettingID = 0xFFFFFFFF;
	constexpr float kWindowMargin = 12.0f;
	constexpr float kPanelWidthRatio = 0.26f;
	constexpr float kPanelMinWidth = 340.0f;
	constexpr float kPanelMaxWidth = 620.0f;
	constexpr float kPanelMaxScreenShare = 0.45f;
	constexpr float kActiveWeight = 0.001f;
	constexpr float kInactiveAlpha = 0.45f;
	constexpr float kWeightBarHeight = 3.0f;
	constexpr int kPeriodCount = 8;
	constexpr int kExteriorPeriodCount = 6;

	enum class Group
	{
		General,
		TimeAndWeather,
		Lighting,
		Sky,
		Atmosphere,
		Camera,
		Rain,
		Other,
		Count
	};

	struct CategoryEntry
	{
		std::string_view key;
		Group group;
	};

	// Order within a group is the order sections are listed in
	constexpr CategoryEntry kCategories[] = {
		{ "GLOBAL", Group::General },
		{ "EFFECT", Group::General },
		{ "COLORCORRECTION", Group::General },
		{ "TIMEOFDAY", Group::TimeAndWeather },
		{ "WEATHER", Group::TimeAndWeather },
		{ "ENVIRONMENT", Group::Lighting },
		{ "IMAGEBASEDLIGHTING", Group::Lighting },
		{ "PARTICLE", Group::Lighting },
		{ "LIGHTSPRITE", Group::Lighting },
		{ "FIRE", Group::Lighting },
		{ "WATER", Group::Lighting },
		{ "SKY", Group::Sky },
		{ "PROCEDURALSUN", Group::Sky },
		{ "SUNGLARE", Group::Sky },
		{ "CLOUDSHADOWS", Group::Sky },
		{ "VOLUMETRICFOG", Group::Atmosphere },
		{ "VOLUMETRICRAYS", Group::Atmosphere },
		{ "GAMEVOLUMETRICRAYS", Group::Atmosphere },
		{ "ADAPTATION", Group::Camera },
		{ "DEPTHOFFIELD", Group::Camera },
		{ "BLOOM", Group::Camera },
		{ "LENS", Group::Camera },
		{ "RAIN", Group::Rain },
	};

	bool IsKnownCategory(std::string_view a_category)
	{
		return std::any_of(std::begin(kCategories), std::end(kCategories), [&](const CategoryEntry& e) { return e.key == a_category; });
	}

	const char* GroupName(Group a_group)
	{
		switch (a_group) {
		case Group::General:
			return T("feature.effects11.group.general", "General");
		case Group::TimeAndWeather:
			return T("feature.effects11.group.time_weather", "Time & Weather");
		case Group::Lighting:
			return T("feature.effects11.group.lighting", "Lighting");
		case Group::Sky:
			return T("feature.effects11.group.sky", "Sky");
		case Group::Atmosphere:
			return T("feature.effects11.group.atmosphere", "Atmosphere");
		case Group::Camera:
			return T("feature.effects11.group.camera", "Camera");
		case Group::Rain:
			return T("feature.effects11.group.rain", "Rain");
		default:
			return T("feature.effects11.group.other", "Other");
		}
	}

	struct CategoryText
	{
		const char* name = nullptr;
		const char* description = nullptr;
	};

	CategoryText GetCategoryText(std::string_view a_category)
	{
		if (a_category == "GLOBAL")
			return { T("feature.effects11.category.global", "Global"), T("feature.effects11.category.global_desc", "Preset-wide switches.") };
		if (a_category == "EFFECT")
			return { T("feature.effects11.category.effect", "Effects"), T("feature.effects11.category.effect_desc", "Turn individual shader files and Effects 11 features on or off.") };
		if (a_category == "COLORCORRECTION")
			return { T("feature.effects11.category.colorcorrection", "Color Correction"), T("feature.effects11.category.colorcorrection_desc", "Final brightness and gamma, applied after the shader chain.") };
		if (a_category == "TIMEOFDAY")
			return { T("feature.effects11.category.timeofday", "Time of Day"), T("feature.effects11.category.timeofday_desc", "When each time-of-day period peaks, in game hours, and how long dawn and dusk last.") };
		if (a_category == "WEATHER")
			return { T("feature.effects11.category.weather", "Weather"), T("feature.effects11.category.weather_desc", "Per-weather values from the files listed in _weatherlist.ini.") };
		if (a_category == "ENVIRONMENT")
			return { T("feature.effects11.category.environment", "Environment"), T("feature.effects11.category.environment_desc", "Sun, ambient and point light strength and color, plus fog.") };
		if (a_category == "IMAGEBASEDLIGHTING")
			return { T("feature.effects11.category.imagebasedlighting", "Image Based Lighting"), T("feature.effects11.category.imagebasedlighting_desc", "Strength of image based lighting.") };
		if (a_category == "PARTICLE")
			return { T("feature.effects11.category.particle", "Particles"), T("feature.effects11.category.particle_desc", "Particle brightness and how much scene lighting affects particles.") };
		if (a_category == "LIGHTSPRITE")
			return { T("feature.effects11.category.lightsprite", "Light Sprites"), T("feature.effects11.category.lightsprite_desc", "Brightness of glow sprites around light sources.") };
		if (a_category == "FIRE")
			return { T("feature.effects11.category.fire", "Fire"), T("feature.effects11.category.fire_desc", "Brightness and contrast of fire.") };
		if (a_category == "WATER")
			return { T("feature.effects11.category.water", "Water"), T("feature.effects11.category.water_desc", "Water color, waves, fresnel, sun response and reflections.") };
		if (a_category == "SKY")
			return { T("feature.effects11.category.sky", "Sky"), T("feature.effects11.category.sky_desc", "Sky gradient, clouds, sun, moon and stars.") };
		if (a_category == "PROCEDURALSUN")
			return { T("feature.effects11.category.proceduralsun", "Procedural Sun"), T("feature.effects11.category.proceduralsun_desc", "Size, edge and glow of the procedural sun disk.") };
		if (a_category == "SUNGLARE")
			return { T("feature.effects11.category.sunglare", "Sun Glare"), T("feature.effects11.category.sunglare_desc", "Glow around the sun.") };
		if (a_category == "CLOUDSHADOWS")
			return { T("feature.effects11.category.cloudshadows", "Cloud Shadows"), T("feature.effects11.category.cloudshadows_desc", "Strength of the shadows clouds cast on the ground.") };
		if (a_category == "VOLUMETRICFOG")
			return { T("feature.effects11.category.volumetricfog", "Volumetric Fog"), T("feature.effects11.category.volumetricfog_desc", "Brightness and color of volumetric fog.") };
		if (a_category == "VOLUMETRICRAYS")
			return { T("feature.effects11.category.volumetricrays", "Volumetric Rays"), T("feature.effects11.category.volumetricrays_desc", "Effects 11 sun rays.") };
		if (a_category == "GAMEVOLUMETRICRAYS")
			return { T("feature.effects11.category.gamevolumetricrays", "Game Volumetric Rays"), T("feature.effects11.category.gamevolumetricrays_desc", "Adjustments to the game's own god rays.") };
		if (a_category == "ADAPTATION")
			return { T("feature.effects11.category.adaptation", "Adaptation"), T("feature.effects11.category.adaptation_desc", "Eye adaptation: how fast and how far exposure follows scene brightness.") };
		if (a_category == "DEPTHOFFIELD")
			return { T("feature.effects11.category.depthoffield", "Depth of Field"), T("feature.effects11.category.depthoffield_desc", "How fast focus and aperture follow the scene.") };
		if (a_category == "BLOOM")
			return { T("feature.effects11.category.bloom", "Bloom"), T("feature.effects11.category.bloom_desc", "Overall bloom amount handed to the shaders.") };
		if (a_category == "LENS")
			return { T("feature.effects11.category.lens", "Lens"), T("feature.effects11.category.lens_desc", "Overall lens effect amount handed to the shaders.") };
		if (a_category == "RAIN")
			return { T("feature.effects11.category.rain", "Rain"), T("feature.effects11.category.rain_desc", "Motion stretch and transparency of rain drops.") };
		return {};
	}

	const char* PeriodName(int a_period)
	{
		switch (a_period) {
		case 0:
			return T("feature.effects11.period.dawn", "Dawn");
		case 1:
			return T("feature.effects11.period.sunrise", "Sunrise");
		case 2:
			return T("feature.effects11.period.day", "Day");
		case 3:
			return T("feature.effects11.period.sunset", "Sunset");
		case 4:
			return T("feature.effects11.period.dusk", "Dusk");
		case 5:
			return T("feature.effects11.period.night", "Night");
		case 6:
			return T("feature.effects11.period.interior_day", "Interior Day");
		case 7:
			return T("feature.effects11.period.interior_night", "Interior Night");
		default:
			return "";
		}
	}

	/** Title-cases an unknown ini section ("NEWSECTION" -> "Newsection") so it reads like the others. */
	std::string FallbackCategoryName(const std::string& a_category)
	{
		std::string name = a_category;
		for (size_t i = 1; i < name.size(); ++i)
			name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
		return name;
	}

	std::string FormatFloat(float a_value, int a_decimals)
	{
		return std::format("{:.{}f}", a_value, std::clamp(a_decimals, 0, 6));
	}

	std::string FormatColor(const float3& a_color)
	{
		return std::format("{:.3f}, {:.3f}, {:.3f}", a_color.x, a_color.y, a_color.z);
	}

	/** Path of a preset file relative to the game folder, which is how users recognize it. */
	std::string DisplayPath(const std::filesystem::path& a_path)
	{
		std::error_code ec;
		const auto gameFolder = std::filesystem::current_path(ec);
		const auto relative = ec ? std::filesystem::path() : a_path.lexically_relative(gameFolder);
		if (relative.empty() || relative.native().starts_with(std::filesystem::path("..").native()))
			return a_path.string();
		return relative.string();
	}

	/** Shader file whose "Enable..." switch lives in the EFFECT section, if any. */
	Effect* EffectForToggle(const Setting& a_setting)
	{
		if (a_setting.category != "EFFECT")
			return nullptr;
		auto& effectManager = EffectManager::GetSingleton();
		if (a_setting.key == "EnableDepthOfField")
			return &effectManager.enbDepthOfField;
		if (a_setting.key == "EnableBloom")
			return &effectManager.enbBloom;
		if (a_setting.key == "EnableLens")
			return &effectManager.enbLens;
		if (a_setting.key == "EnableAdaptation")
			return &effectManager.enbAdaptation;
		if (a_setting.key == "EnablePostPassShader")
			return &effectManager.enbEffectPostPass;
		return nullptr;
	}

	struct EffectFile
	{
		Effect* effect;
		uint32_t enableSettingID;
		const char* friendlyName;
	};

	/** Shader files in the order the editor lists them; enbeffect.fx is required and has no switch of its own. */
	std::vector<EffectFile> GetEffectFiles()
	{
		auto& effectManager = EffectManager::GetSingleton();
		const auto& ids = effectManager.ids;
		return {
			{ &effectManager.enbEffect, kInvalidSettingID, T("feature.effects11.editor.file_effect", "Main Effect") },
			{ &effectManager.enbDepthOfField, ids.useDepthOfField, T("feature.effects11.editor.file_depthoffield", "Depth of Field") },
			{ &effectManager.enbBloom, ids.useBloom, T("feature.effects11.editor.file_bloom", "Bloom") },
			{ &effectManager.enbLens, ids.useLens, T("feature.effects11.editor.file_lens", "Lens") },
			{ &effectManager.enbAdaptation, ids.useAdaptation, T("feature.effects11.editor.file_adaptation", "Adaptation") },
			{ &effectManager.enbEffectPostPass, ids.usePostPass, T("feature.effects11.editor.file_postpass", "Post Pass") },
		};
	}

	/** True when a parameter of the effect matches the search, so empty sections can be skipped. */
	bool EffectHasMatches(const Effect& a_effect, std::string_view a_filter)
	{
		if (a_filter.empty())
			return true;
		if (a_effect.uiTechniques.size() > 1 && Effects11UI::ContainsNoCase(a_effect.techniqueDropdown.name, a_filter))
			return true;
		for (const auto& uiVar : a_effect.uiVariables) {
			if (uiVar.isLabel || uiVar.isHidden || uiVar.displayName.empty())
				continue;
			if (Effects11UI::ContainsNoCase(uiVar.displayName, a_filter) || Effects11UI::ContainsNoCase(uiVar.name, a_filter) ||
				Effects11UI::ContainsNoCase(uiVar.group, a_filter))
				return true;
		}
		return false;
	}

	/** Places the next window along the left or right screen edge, leaving the middle of the screen free. */
	void SetNextPanelPlacement(bool a_rightSide, bool a_reset)
	{
		const auto* viewport = ImGui::GetMainViewport();
		const float scale = Util::GetUIScale();
		const float margin = kWindowMargin * scale;
		const float maxWidth = (std::max)(kPanelMinWidth * scale, (std::min)(kPanelMaxWidth * scale, viewport->WorkSize.x * kPanelMaxScreenShare));
		const float width = std::clamp(viewport->WorkSize.x * kPanelWidthRatio, kPanelMinWidth * scale, maxWidth);
		const float height = viewport->WorkSize.y - margin * 2.0f;
		const float x = a_rightSide ? viewport->WorkPos.x + viewport->WorkSize.x - width - margin : viewport->WorkPos.x + margin;

		const ImGuiCond cond = a_reset ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
		ImGui::SetNextWindowPos(ImVec2(x, viewport->WorkPos.y + margin), cond);
		ImGui::SetNextWindowSize(ImVec2(width, height), cond);
	}

	/** Puts the next item on the current line if it fits, otherwise on a new line. */
	void SameLineIfFits(float a_width)
	{
		ImGui::SameLine();
		if (ImGui::GetContentRegionAvail().x < a_width)
			ImGui::NewLine();
	}

	bool MenuItemResetFloat(float& a_value, float a_default, int a_decimals)
	{
		const std::string label = std::format("{} ({})", T(TKEY("reset_default"), "Reset to default"), FormatFloat(a_default, a_decimals));
		if (!ImGui::MenuItem(label.c_str()))
			return false;
		a_value = a_default;
		return true;
	}

	bool MenuItemsFloatClipboard(float& a_value, float a_min, float a_max, int a_decimals)
	{
		if (ImGui::MenuItem(T(TKEY("copy"), "Copy")))
			Effects11UI::Clipboard::SetFloat(a_value);
		const bool canPaste = Effects11UI::Clipboard::HasFloat();
		const std::string paste = canPaste ? std::format("{} ({})", T(TKEY("paste"), "Paste"), FormatFloat(Effects11UI::Clipboard::GetFloat(), a_decimals)) : T(TKEY("paste"), "Paste");
		if (!ImGui::MenuItem(paste.c_str(), nullptr, false, canPaste))
			return false;
		a_value = std::clamp(Effects11UI::Clipboard::GetFloat(), a_min, a_max);
		return true;
	}
}

Effects11Editor& Effects11Editor::GetSingleton()
{
	static Effects11Editor instance;
	return instance;
}

// Open state

void Effects11Editor::Open(bool a_returnToMenu)
{
	if (!globals::features::effects11.loaded)
		return;
	returnToMenu = a_returnToMenu;
	suppressNextEscape = false;
	popupOpenLastFrame = false;
	RefreshPresetPaths();
	open.store(true, std::memory_order_relaxed);
	globals::menu->IsEnabled = false;
}

void Effects11Editor::Close(bool a_restoreMenu)
{
	if (!open.exchange(false, std::memory_order_relaxed))
		return;
	pendingAction = PendingAction::None;
	if (a_restoreMenu && returnToMenu)
		globals::menu->IsEnabled = true;
	returnToMenu = false;
}

void Effects11Editor::Toggle()
{
	if (IsOpen())
		Close();
	else
		Open(globals::menu->IsEnabled);
}

bool Effects11Editor::ShouldHandleEscapeKey()
{
	if (suppressNextEscape) {
		suppressNextEscape = false;
		return false;
	}
	return !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

// Frame

void Effects11Editor::Draw()
{
	if (!IsOpen())
		return;
	if (!globals::features::effects11.loaded) {
		Close(false);
		return;
	}

	// An ESC that closes a popup or leaves a text field must not also close the editor on key release.
	// ImGui closes popups while starting the frame, so check the state the previous frame ended with.
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && (ImGui::GetIO().WantTextInput || popupOpenLastFrame))
		suppressNextEscape = true;

	DrawSettingsWindow();
	if (IsOpen() && showShaderPanel && EffectManager::GetSingleton().IsPresetLoaded())
		DrawShaderWindow();
	resetLayout = false;
	popupOpenLastFrame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void Effects11Editor::HandleShortcuts(bool& a_focusSearch)
{
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		return;
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
		Save();
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
		a_focusSearch = true;
}

// Settings window

void Effects11Editor::DrawSettingsWindow()
{
	SetNextPanelPlacement(false, resetLayout);

	bool keepOpen = true;
	const std::string title = std::format("{}###Effects11Editor", T(TKEY("window_title"), "Effects 11"));
	if (Util::BeginWithRoundedClose(title.c_str(), &keepOpen, ImGuiWindowFlags_NoScrollbar)) {
		HandleShortcuts(focusSettingsSearch);
		if (!EffectManager::GetSingleton().IsPresetLoaded()) {
			DrawNoPreset();
		} else {
			DrawToolbar();
			ImGui::Separator();
			DrawStatus();
			ImGui::Separator();
			DrawSearchBar(settingsFilter, T(TKEY("search_settings"), "Search settings..."), focusSettingsSearch);
			DrawPeriodFocusCombo();
			ImGui::Spacing();
			if (ImGui::BeginChild("##sections", ImVec2(0.0f, 0.0f)))
				DrawSections();
			ImGui::EndChild();
		}
		DrawPendingActionPopup();
	}
	ImGui::End();

	if (!keepOpen)
		Close();
}

void Effects11Editor::DrawNoPreset()
{
	const auto& mainEffect = EffectManager::GetSingleton().enbEffect;

	Util::Text::Warning("%s", T(TKEY("no_preset_title"), "No preset loaded"));
	ImGui::Spacing();
	ImGui::PushTextWrapPos(0.0f);
	if (mainEffect.IsFilePresent()) {
		ImGui::TextUnformatted(T(TKEY("preset_failed"), "enbeffect.fx was found but failed to compile, so Effects 11 stays off until it is fixed."));
		for (const auto& error : mainEffect.GetErrors())
			Util::Text::WrappedError("%s", error.c_str());
	} else {
		ImGui::TextUnformatted(T(TKEY("no_preset_body"),
			"Effects 11 runs ENB-style presets. Install a preset so that enbseries.ini and the "
			"enbseries folder (containing at least enbeffect.fx) are in the game folder or in Data, "
			"then click Reload Shaders."));
	}
	ImGui::Spacing();
	Util::TextUnformattedDisabled(T(TKEY("looking_for"), "Looking for:"));
	ImGui::TextUnformatted(presetPaths.mainEffect.c_str());
	ImGui::PopTextWrapPos();
	ImGui::Spacing();

	if (ImGui::Button(T(TKEY("reload_shaders"), "Reload Shaders")))
		ReloadShaders();
}

void Effects11Editor::DrawToolbar()
{
	auto& settingManager = SettingManager::GetSingleton();
	const auto& menuSettings = globals::menu->GetSettings();

	const uint32_t useEffectID = settingManager.GetSettingID("UseEffect", "GLOBAL");
	bool enabled = settingManager.GetValue<bool>(useEffectID);
	if (ImGui::Checkbox(T(TKEY("enable_effects"), "Enable Effects 11"), &enabled)) {
		settingManager.SetValue<bool>(useEffectID, enabled);
		dirty = true;
	}
	{
		const auto tip = I18n::GetSingleton()->Format(TKEY("enable_effects_tip"),
			{ { "key", Util::Input::KeyIdToString(menuSettings.Effects11ToggleKey) } },
			"Master switch for the whole preset.\nHotkey: {key}");
		Util::AddTooltip(tip.c_str());
	}

	// Shader panel toggle, right-aligned on the same line
	const char* panelLabel = showShaderPanel ? T(TKEY("hide_shader_panel"), "Hide Shader Parameters") : T(TKEY("show_shader_panel"), "Show Shader Parameters");
	const float panelWidth = ImGui::CalcTextSize(panelLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SameLine();
	const float avail = ImGui::GetContentRegionAvail().x;
	if (avail >= panelWidth)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - panelWidth);
	else
		ImGui::NewLine();
	if (ImGui::Button(std::format("{}###shaderPanel", panelLabel).c_str()))
		showShaderPanel = !showShaderPanel;
	Util::AddTooltip(T(TKEY("shader_panel_tip"), "Show or hide the panel with the .fx shader parameters."));

	const char* saveLabel = T(TKEY("save"), "Save");
	if (dirty ? Util::SuccessButton(saveLabel) : ImGui::Button(saveLabel))
		Save();
	Util::AddTooltip(T(TKEY("save_tip"), "Write every change to enbseries.ini, the weather files and the shader .ini files.\nShortcut: Ctrl+S"));

	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("revert"), "Revert")))
		RequestAction(PendingAction::Revert);
	Util::AddTooltip(T(TKEY("revert_tip"), "Discard unsaved changes and reload every value from disk."));

	ImGui::SameLine();
	const char* reloadLabel = T(TKEY("reload_shaders"), "Reload Shaders");
	if (shaderReloadNeeded ? Util::WarningButton(reloadLabel) : ImGui::Button(reloadLabel))
		RequestAction(PendingAction::ReloadShaders);
	Util::AddTooltip(T(TKEY("reload_shaders_tip"),
		"Recompile the .fx files and reload everything from disk.\n"
		"Use this after changing a compile-time option or editing a shader file."));

	const char* stateText = dirty ? T(TKEY("unsaved_changes"), "Unsaved changes") : T(TKEY("all_saved"), "All changes saved");
	SameLineIfFits(ImGui::CalcTextSize(stateText).x);
	ImGui::AlignTextToFramePadding();
	if (dirty)
		Util::Text::Warning("%s", stateText);
	else
		Util::TextUnformattedDisabled(stateText);

	if (shaderReloadNeeded)
		Util::Text::WrappedWarning("%s", T(TKEY("reload_needed"), "Compile-time options changed. Save, then click Reload Shaders to apply them."));
}

void Effects11Editor::DrawStatus()
{
	auto& settingManager = SettingManager::GetSingleton();
	auto& effectManager = EffectManager::GetSingleton();
	const auto& commonData = effectManager.commonData;

	if (!ImGui::BeginTable("##status", 2, ImGuiTableFlags_SizingStretchProp))
		return;
	ImGui::TableSetupColumn("##key", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

	auto row = [](const char* a_label) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		Util::TextUnformattedDisabled(a_label);
		ImGui::TableSetColumnIndex(1);
	};

	// Preset
	row(T(TKEY("status_preset"), "Preset"));
	{
		ImGui::TextUnformatted(presetPaths.iniDisplay.c_str());
		Util::AddTooltip(presetPaths.iniFull.c_str());
		if (const uint32_t failed = effectManager.GetFailedEffectCount()) {
			ImGui::SameLine();
			const auto text = I18n::GetSingleton()->Format(TKEY("status_failed_files"), { { "count", std::to_string(failed) } }, "({count} failed)");
			Util::Text::Error("%s", text.c_str());
			Util::AddTooltip(T(TKEY("status_failed_files_tip"), "See the Shader Parameters panel for the compiler errors."));
		}
	}

	// Time of day
	row(T(TKEY("status_time"), "Time"));
	{
		const float hour = std::clamp(commonData.weather[3], 0.0f, 24.0f);
		const int totalMinutes = static_cast<int>(hour * 60.0f) % (24 * 60);
		ImGui::Text("%02d:%02d   %s", totalMinutes / 60, totalMinutes % 60, ActivePeriodSummary(true).c_str());
		if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
			Util::TextUnformattedDisabled(T(TKEY("status_time_tip"), "How much each period contributes right now:"));
			for (int period = 0; period < kPeriodCount; ++period) {
				const float weight = PeriodWeight(period);
				if (weight > kActiveWeight)
					ImGui::Text("%s  %.0f%%", PeriodName(period), weight * 100.0f);
				else
					Util::TextUnformattedDisabled(std::format("{}  0%", PeriodName(period)).c_str());
			}
			ImGui::EndTooltip();
		}
	}

	// Weather
	row(T(TKEY("status_weather"), "Weather"));
	if (!settingManager.IsWeatherSystemEnabled()) {
		Util::TextUnformattedDisabled(T(TKEY("weather_off"), "Off, enbseries.ini values only"));
		Util::AddTooltip(T(TKEY("weather_off_tip"), "Turn on \"Enable Multiple Weathers\" in the Weather section to use the per-weather files."));
	} else {
		auto& weatherManager = WeatherManager::GetSingleton();
		auto weatherName = [&](uint32_t a_id) -> std::string {
			if (auto* entry = weatherManager.FindWeatherEntry(a_id))
				return entry->fileName;
			return I18n::GetSingleton()->Format(TKEY("weather_no_file"), { { "id", std::format("0x{:08X}", a_id) } }, "{id} (no weather file)");
		};
		const auto current = effectManager.currentWeatherID;
		const auto previous = effectManager.previousWeatherID;
		const float blend = std::clamp(commonData.weather[2], 0.0f, 1.0f);

		std::string text = weatherName(current);
		if (previous != current && blend < 0.999f) {
			text = I18n::GetSingleton()->Format(TKEY("weather_transition"),
				{ { "current", text }, { "previous", weatherName(previous) }, { "percent", std::format("{:.0f}", blend * 100.0f) } },
				"{current} ({percent}%, from {previous})");
		}
		ImGui::TextUnformatted(text.c_str());
		Util::AddTooltip(T(TKEY("weather_tip"),
			"The weather file in use. Weathers without a file in _weatherlist.ini use the enbseries.ini values.\n"
			"While weathers blend, edits go to the weather that dominates the blend."));
	}

	// Location
	row(T(TKEY("status_location"), "Location"));
	ImGui::TextUnformatted(IsInterior() ? T(TKEY("location_interior"), "Interior") : T(TKEY("location_exterior"), "Exterior"));

	ImGui::EndTable();
}

void Effects11Editor::DrawSearchBar(std::string& a_filter, const char* a_hint, bool& a_focusRequest)
{
	ImGui::PushID(a_hint);
	const float clearWidth = a_filter.empty() ? 0.0f : ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
	if (a_focusRequest) {
		ImGui::SetKeyboardFocusHere();
		a_focusRequest = false;
	}
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearWidth);
	ImGui::InputTextWithHint("##search", a_hint, &a_filter, ImGuiInputTextFlags_EscapeClearsAll);
	Util::AddTooltip(T(TKEY("search_tip"), "Filter by name. Shortcut: Ctrl+F. Esc clears the search."));
	if (!a_filter.empty()) {
		ImGui::SameLine();
		if (ImGui::Button("X##clear", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
			a_filter.clear();
		Util::AddTooltip(T(TKEY("clear_search"), "Clear the search"));
	}
	ImGui::PopID();
}

void Effects11Editor::DrawPeriodFocusCombo()
{
	ImGui::AlignTextToFramePadding();
	Util::TextUnformattedDisabled(T(TKEY("period_focus"), "Time of day"));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);

	std::string preview;
	if (periodFocus == kFocusCurrent)
		preview = I18n::GetSingleton()->Format(TKEY("focus_current_preview"), { { "periods", ActivePeriodSummary(false) } }, "Current: {periods}");
	else if (periodFocus == kFocusAll)
		preview = T(TKEY("focus_all"), "All periods");
	else
		preview = PeriodName(periodFocus);

	if (ImGui::BeginCombo("##periodFocus", preview.c_str())) {
		if (ImGui::Selectable(T(TKEY("focus_current"), "Current time of day"), periodFocus == kFocusCurrent))
			periodFocus = kFocusCurrent;
		Util::AddTooltip(T(TKEY("focus_current_tip"), "Show only the periods that are blended into the image right now."));
		if (ImGui::Selectable(T(TKEY("focus_all"), "All periods"), periodFocus == kFocusAll))
			periodFocus = kFocusAll;
		ImGui::Separator();
		for (int period = 0; period < kPeriodCount; ++period) {
			const float weight = PeriodWeight(period);
			const std::string label = weight > kActiveWeight ? std::format("{}  ({:.0f}%)", PeriodName(period), weight * 100.0f) : PeriodName(period);
			if (ImGui::Selectable(label.c_str(), periodFocus == period))
				periodFocus = period;
		}
		ImGui::EndCombo();
	}
	Util::AddTooltip(T(TKEY("period_focus_tip"),
		"Which time-of-day values to show. Values of periods that are not visible right now are dimmed."));

	if (periodFocus >= 0 && !IsPeriodActive(periodFocus)) {
		const auto hint = I18n::GetSingleton()->Format(TKEY("period_not_active"), { { "period", PeriodName(periodFocus) } },
			"{period} is not showing right now, so edits to it are not visible yet.");
		ImGui::PushTextWrapPos(0.0f);
		Util::TextUnformattedDisabled(hint.c_str());
		ImGui::PopTextWrapPos();
	}
}

std::vector<const Setting*> Effects11Editor::CollectRows(const std::string& a_category, const char* a_name) const
{
	auto& settingManager = SettingManager::GetSingleton();
	const bool categoryMatches = Effects11UI::ContainsNoCase(a_name, settingsFilter) || Effects11UI::ContainsNoCase(a_category, settingsFilter);

	std::vector<const Setting*> rows;
	for (const auto& key : settingManager.GetSettingsByCategory(a_category)) {
		// The master switch lives in the toolbar
		if (a_category == "GLOBAL" && key == "UseEffect")
			continue;
		const Setting* setting = settingManager.GetSettingInfo(key, a_category);
		if (!setting)
			continue;
		if (!categoryMatches && !Effects11UI::ContainsNoCase(Effects11UI::PrettifyName(key), settingsFilter) && !Effects11UI::ContainsNoCase(key, settingsFilter))
			continue;
		rows.push_back(setting);
	}
	return rows;
}

void Effects11Editor::DrawSections()
{
	const auto categories = SettingManager::GetSingleton().GetCategories();
	auto registered = [&](std::string_view a_category) {
		return std::find(categories.begin(), categories.end(), a_category) != categories.end();
	};

	bool drewAnything = false;
	for (int groupIndex = 0; groupIndex < static_cast<int>(Group::Count); ++groupIndex) {
		const auto group = static_cast<Group>(groupIndex);

		std::vector<std::string> groupCategories;
		if (group == Group::Other) {
			for (const auto& category : categories)
				if (!IsKnownCategory(category))
					groupCategories.push_back(category);
		} else {
			for (const auto& entry : kCategories)
				if (entry.group == group && registered(entry.key))
					groupCategories.emplace_back(entry.key);
		}

		struct Section
		{
			std::string category;
			std::string name;
			const char* description;
			std::vector<const Setting*> rows;
		};
		std::vector<Section> sections;
		for (const auto& category : groupCategories) {
			const auto text = GetCategoryText(category);
			std::string name = text.name ? text.name : FallbackCategoryName(category);
			auto rows = CollectRows(category, name.c_str());
			if (!rows.empty())
				sections.push_back({ category, std::move(name), text.description, std::move(rows) });
		}
		if (sections.empty())
			continue;

		ImGui::SeparatorText(GroupName(group));
		for (const auto& section : sections)
			DrawCategory(section.category, section.name.c_str(), section.description, section.rows);
		drewAnything = true;
	}

	if (!drewAnything && !settingsFilter.empty()) {
		const auto text = I18n::GetSingleton()->Format(TKEY("no_settings_match"), { { "search", settingsFilter } }, "No settings match \"{search}\".");
		Util::TextUnformattedDisabled(text.c_str());
	}
}

void Effects11Editor::DrawCategory(const std::string& a_category, const char* a_name, const char* a_description, const std::vector<const Setting*>& a_rows)
{
	auto& settingManager = SettingManager::GetSingleton();
	const bool active = settingManager.IsCategoryEnabled(a_category);
	const bool exteriorOnly = settingManager.IsCategoryExteriorOnly(a_category);
	const bool weatherAware = settingManager.IsWeatherSystemEnabled() && settingManager.CategoryHasWeatherSupport(a_category);

	if (!settingsFilter.empty())
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	const std::string label = std::format("{}###section_{}", a_name, a_category);
	const bool expanded = ImGui::CollapsingHeader(label.c_str(), a_category == "EFFECT" ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);

	const std::string weatherTag = weatherAware ? EditTargetFile(a_category) : std::string();
	const char* exteriorTag = exteriorOnly && IsInterior() ? T(TKEY("tag_exterior_only"), "Exterior only") : nullptr;
	const char* offTag = active ? nullptr : T(TKEY("tag_off"), "Off");
	Effects11UI::HeaderTags({ { weatherTag.empty() ? nullptr : weatherTag.c_str(), Util::Colors::GetInfo() },
		{ exteriorTag, Util::Colors::GetWarning() },
		{ offTag, Util::Colors::GetDisabled() } });

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip()) {
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
		if (a_description)
			ImGui::TextUnformatted(a_description);
		Util::TextUnformattedDisabled(std::format("[{}]", a_category).c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	if (!expanded)
		return;

	ImGui::PushID(a_category.c_str());
	ImGui::PushTextWrapPos(0.0f);
	if (a_description)
		Util::TextUnformattedDisabled(a_description);
	if (exteriorOnly && IsInterior())
		Util::Text::WrappedWarning("%s", T(TKEY("exterior_only_note"), "These values only apply outdoors."));
	ImGui::PopTextWrapPos();

	if (!active) {
		const auto [dependencyKey, dependencyCategory] = settingManager.GetCategoryDependency(a_category);
		const uint32_t dependencyID = settingManager.GetSettingID(dependencyKey, dependencyCategory);
		Util::Text::WrappedWarning("%s", T(TKEY("section_off"), "This section is switched off, so its values have no effect."));
		if (dependencyID != kInvalidSettingID) {
			bool on = false;
			const std::string toggle = std::format("{}##dependency", Effects11UI::PrettifyName(dependencyKey));
			if (ImGui::Checkbox(toggle.c_str(), &on)) {
				settingManager.SetValue<bool>(dependencyID, true);
				dirty = true;
			}
		}
	}

	// Rain needs enbraindrops.png; without it the section cannot do anything
	const auto& raindropStatus = globals::features::effects11.raindropStatus;
	const bool usable = a_category != "RAIN" || raindropStatus.empty();
	if (!usable) {
		const auto unavailable = I18n::GetSingleton()->Format(TKEY("rain_unavailable"), { { "reason", raindropStatus } }, "Rain is unavailable: {reason}");
		Util::Text::WrappedWarning("%s", unavailable.c_str());
	}

	if (weatherAware)
		DrawCategoryWeatherToggle(a_category);

	if (Effects11UI::BeginPropertyTable("##rows")) {
		for (const auto* setting : a_rows)
			DrawSettingRow(*setting, active && usable);
		Effects11UI::EndPropertyTable();
	}

	if (a_category == "WEATHER")
		DrawWeatherFileList();

	ImGui::PopID();
	ImGui::Spacing();
}

void Effects11Editor::DrawCategoryWeatherToggle(const std::string& a_category)
{
	auto& settingManager = SettingManager::GetSingleton();
	const bool interior = IsInterior();
	if (interior && settingManager.IsCategoryExteriorOnly(a_category))
		return;

	bool perWeather = !(interior ? settingManager.GetIgnoreWeatherSystemInterior(a_category) : settingManager.GetIgnoreWeatherSystem(a_category));
	const char* label = interior ? T(TKEY("per_weather_interior"), "Per-weather values indoors") : T(TKEY("per_weather"), "Per-weather values");
	if (ImGui::Checkbox(label, &perWeather)) {
		if (interior)
			settingManager.SetIgnoreWeatherSystemInterior(a_category, !perWeather);
		else
			settingManager.SetIgnoreWeatherSystem(a_category, !perWeather);
		dirty = true;
	}
	const char* interiorTip = T(TKEY("per_weather_interior_tip"),
		"On: indoors, this section uses the values of the current weather's file.\n"
		"Off: indoors, it uses the enbseries.ini values (IgnoreWeatherSystemInterior).");
	const char* exteriorTip = T(TKEY("per_weather_tip"),
		"On: this section uses the values of the current weather's file.\n"
		"Off: it uses the enbseries.ini values in every weather (IgnoreWeatherSystem).");
	Util::AddTooltip(interior ? interiorTip : exteriorTip);

	const auto target = I18n::GetSingleton()->Format(TKEY("editing_file"), { { "file", EditTargetFile(a_category) } }, "Editing {file}");
	SameLineIfFits(ImGui::CalcTextSize(target.c_str()).x);
	ImGui::AlignTextToFramePadding();
	Util::Text::Info("%s", target.c_str());
}

void Effects11Editor::DrawWeatherFileList()
{
	const auto& entries = WeatherManager::GetSingleton().GetWeatherEntries();
	const auto label = I18n::GetSingleton()->Format(TKEY("weather_files"), { { "count", std::to_string(entries.size()) } }, "Weather files ({count})");
	if (!ImGui::TreeNodeEx(std::format("{}###weatherFiles", label).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
		return;

	if (entries.empty()) {
		ImGui::PushTextWrapPos(0.0f);
		Util::TextUnformattedDisabled(T(TKEY("weather_files_none"), "No weather files are loaded. Weather files are listed in enbseries/_weatherlist.ini."));
		ImGui::PopTextWrapPos();
	} else {
		const auto& effectManager = EffectManager::GetSingleton();
		const auto current = effectManager.currentWeatherID;
		const auto previous = effectManager.previousWeatherID;

		std::vector<const WeatherManager::WeatherEntry*> sorted;
		sorted.reserve(entries.size());
		for (const auto& [key, entry] : entries)
			sorted.push_back(&entry);
		std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->fileName < b->fileName; });

		if (ImGui::BeginTable("##weatherFiles", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
			for (const auto* entry : sorted) {
				const bool isCurrent = std::find(entry->weatherIDs.begin(), entry->weatherIDs.end(), current) != entry->weatherIDs.end();
				const bool isPrevious = !isCurrent && std::find(entry->weatherIDs.begin(), entry->weatherIDs.end(), previous) != entry->weatherIDs.end();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if (isCurrent)
					Util::Text::Info("%s", entry->fileName.c_str());
				else
					ImGui::TextUnformatted(entry->fileName.c_str());
				if (isCurrent || isPrevious) {
					ImGui::SameLine();
					Util::TextUnformattedDisabled(isCurrent ? T(TKEY("weather_current"), "(current)") : T(TKEY("weather_previous"), "(previous)"));
				}

				ImGui::TableSetColumnIndex(1);
				std::string ids;
				for (size_t i = 0; i < entry->weatherIDs.size(); ++i)
					ids += std::format("{}0x{:06X}", i ? ", " : "", entry->weatherIDs[i]);
				Util::TextUnformattedDisabled(ids.c_str());
			}
			ImGui::EndTable();
		}
	}
	ImGui::TreePop();
}

void Effects11Editor::DrawSettingRow(const Setting& a_setting, bool a_categoryActive)
{
	auto& settingManager = SettingManager::GetSingleton();
	const std::string name = Effects11UI::PrettifyName(a_setting.key);
	const bool dependencyMet = a_setting.dependsOnKey.empty() || settingManager.GetValue<bool>(a_setting.dependsOnKey, a_setting.dependsOnCategory);
	const bool editable = a_categoryActive && dependencyMet;

	ImGui::PushID(static_cast<int>(a_setting.id));
	bool labelHovered = false;
	ImGui::BeginDisabled(!editable);

	switch (a_setting.type) {
	case SettingType::Bool:
		{
			labelHovered = Effects11UI::PropertyLabel(name.c_str(), !editable);
			bool value = settingManager.GetValue<bool>(a_setting.id, true);
			bool changed = ImGui::Checkbox("##v", &value);

			if (const auto* effect = EffectForToggle(a_setting); effect && !effect->IsFilePresent()) {
				ImGui::SameLine();
				const auto missing = I18n::GetSingleton()->Format(TKEY("file_not_in_preset"), { { "file", effect->GetName() } }, "{file} is not in this preset");
				Util::TextUnformattedDisabled(missing.c_str());
			}

			if (editable) {
				if (labelHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
					ImGui::OpenPopup("##ctx");
				if (ImGui::BeginPopupContextItem("##ctx")) {
					const bool defaultValue = std::get_if<bool>(&a_setting.defaultValue) && std::get<bool>(a_setting.defaultValue);
					const auto label = std::format("{} ({})", T(TKEY("reset_default"), "Reset to default"), defaultValue ? T(TKEY("on"), "On") : T(TKEY("off"), "Off"));
					if (ImGui::MenuItem(label.c_str())) {
						value = defaultValue;
						changed = true;
					}
					ImGui::EndPopup();
				}
			}

			if (changed) {
				settingManager.SetValue<bool>(a_setting.id, value);
				dirty = true;
			}
			break;
		}
	case SettingType::Float:
		{
			labelHovered = Effects11UI::PropertyLabel(name.c_str(), !editable);
			float value = settingManager.GetValue<float>(a_setting.id, true);
			const int decimals = Effects11UI::DecimalsForStep(a_setting.step);
			const std::string format = Effects11UI::MakeFormat(decimals);
			bool changed = Effects11UI::FloatValue("##v", &value, a_setting.minValue, a_setting.maxValue, a_setting.step, format.c_str());

			if (editable) {
				if (labelHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
					ImGui::OpenPopup("##ctx");
				if (ImGui::BeginPopupContextItem("##ctx")) {
					const auto* defaultValue = std::get_if<float>(&a_setting.defaultValue);
					if (defaultValue)
						changed |= MenuItemResetFloat(value, *defaultValue, decimals);
					ImGui::Separator();
					changed |= MenuItemsFloatClipboard(value, a_setting.minValue, a_setting.maxValue, decimals);
					ImGui::EndPopup();
				}
			}

			if (changed) {
				settingManager.SetValue<float>(a_setting.id, value);
				dirty = true;
			}
			break;
		}
	case SettingType::TimeOfDay:
		DrawTimeOfDayRows(a_setting, name.c_str(), editable, labelHovered);
		break;
	case SettingType::ColorTimeOfDay:
		DrawColorTimeOfDayRow(a_setting, name.c_str(), editable, labelHovered);
		break;
	}

	ImGui::EndDisabled();
	if (labelHovered)
		DrawSettingTooltip(a_setting, name, dependencyMet);
	ImGui::PopID();
}

void Effects11Editor::DrawTimeOfDayRows(const Setting& a_setting, const char* a_label, bool a_editable, bool& a_labelHovered)
{
	auto& settingManager = SettingManager::GetSingleton();
	auto value = settingManager.GetValue<TimeOfDayValue>(a_setting.id, true);
	const auto* defaults = std::get_if<TimeOfDayValue>(&a_setting.defaultValue);
	const bool exteriorOnly = settingManager.IsCategoryExteriorOnly(a_setting.category);
	const int applicable = exteriorOnly ? kExteriorPeriodCount : kPeriodCount;
	const int decimals = Effects11UI::DecimalsForStep(a_setting.step);
	const auto periods = FocusedPeriods(exteriorOnly);

	if (periods.empty()) {
		a_labelHovered = Effects11UI::PropertyLabel(a_label, true);
		ImGui::AlignTextToFramePadding();
		Util::TextUnformattedDisabled(T(TKEY("no_period_value"), "Not used in this period"));
		return;
	}

	bool changed = false;
	for (size_t i = 0; i < periods.size(); ++i) {
		const int period = periods[i];
		if (i == 0)
			a_labelHovered = Effects11UI::PropertyLabel(a_label, !a_editable);
		else
			Effects11UI::PropertyContinuation();

		const bool inactive = !IsPeriodActive(period);
		if (inactive)
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * kInactiveAlpha);
		ImGui::PushID(period);

		const std::string format = Effects11UI::MakeFormat(decimals, PeriodName(period));
		changed |= Effects11UI::FloatValue("##v", &value.values[period], a_setting.minValue, a_setting.maxValue, a_setting.step, format.c_str());

		if (a_editable) {
			if (i == 0 && a_labelHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
				ImGui::OpenPopup("##ctx");
			if (ImGui::BeginPopupContextItem("##ctx")) {
				float periodValue = value.values[period];
				Util::TextUnformattedDisabled(PeriodName(period));
				if (defaults && MenuItemResetFloat(periodValue, defaults->values[period], decimals))
					changed = true;
				if (defaults && ImGui::MenuItem(T(TKEY("reset_all_periods"), "Reset all periods to default"))) {
					std::copy_n(defaults->values, kPeriodCount, value.values);
					periodValue = value.values[period];
					changed = true;
				}
				const auto setAll = I18n::GetSingleton()->Format(TKEY("set_all_periods"), { { "value", FormatFloat(periodValue, decimals) } }, "Set all periods to {value}");
				if (ImGui::MenuItem(setAll.c_str())) {
					std::fill_n(value.values, applicable, periodValue);
					changed = true;
				}
				ImGui::Separator();
				if (MenuItemsFloatClipboard(periodValue, a_setting.minValue, a_setting.maxValue, decimals))
					changed = true;
				value.values[period] = periodValue;
				ImGui::EndPopup();
			}
		}

		ImGui::PopID();
		if (inactive)
			ImGui::PopStyleVar();
	}

	if (changed) {
		settingManager.SetValue<TimeOfDayValue>(a_setting.id, value);
		dirty = true;
	}
}

void Effects11Editor::DrawColorTimeOfDayRow(const Setting& a_setting, const char* a_label, bool a_editable, bool& a_labelHovered)
{
	auto& settingManager = SettingManager::GetSingleton();
	auto value = settingManager.GetValue<ColorTimeOfDayValue>(a_setting.id, true);
	const auto* defaults = std::get_if<ColorTimeOfDayValue>(&a_setting.defaultValue);
	const bool exteriorOnly = settingManager.IsCategoryExteriorOnly(a_setting.category);
	const int applicable = exteriorOnly ? kExteriorPeriodCount : kPeriodCount;

	a_labelHovered = Effects11UI::PropertyLabel(a_label, !a_editable);

	const float swatch = ImGui::GetFrameHeight();
	const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
	const float available = ImGui::GetContentRegionAvail().x;
	const float barHeight = kWeightBarHeight * Util::GetUIScale();
	float lineWidth = 0.0f;
	bool changed = false;

	for (int period = 0; period < applicable; ++period) {
		if (period > 0 && lineWidth + spacing + swatch <= available) {
			ImGui::SameLine(0.0f, spacing);
			lineWidth += spacing + swatch;
		} else {
			lineWidth = swatch;
		}

		const bool focused = IsPeriodFocused(period);
		if (!focused)
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * kInactiveAlpha);
		ImGui::PushID(period);

		float rgb[3] = { value.values[period].x, value.values[period].y, value.values[period].z };
		constexpr ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoTooltip |
		                                      ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoOptions;
		if (ImGui::ColorEdit3("##c", rgb, flags)) {
			value.values[period] = { rgb[0], rgb[1], rgb[2] };
			changed = true;
		}

		// Bar along the bottom of the swatch showing how much this period contributes right now
		const float weight = PeriodWeight(period);
		if (weight > kActiveWeight) {
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, max.y - barHeight), ImVec2(min.x + (max.x - min.x) * weight, max.y),
				ImGui::GetColorU32(Util::Colors::GetInfo()));
		}

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip()) {
			if (weight > kActiveWeight)
				ImGui::Text("%s  (%.0f%%)", PeriodName(period), weight * 100.0f);
			else
				ImGui::TextUnformatted(PeriodName(period));
			Util::TextUnformattedDisabled(FormatColor(value.values[period]).c_str());
			ImGui::EndTooltip();
		}

		if (a_editable && ImGui::BeginPopupContextItem("##ctx")) {
			Util::TextUnformattedDisabled(PeriodName(period));
			if (defaults && ImGui::MenuItem(T(TKEY("reset_default"), "Reset to default"))) {
				value.values[period] = defaults->values[period];
				changed = true;
			}
			if (defaults && ImGui::MenuItem(T(TKEY("reset_all_periods"), "Reset all periods to default"))) {
				std::copy_n(defaults->values, kPeriodCount, value.values);
				changed = true;
			}
			if (ImGui::MenuItem(T(TKEY("set_all_periods_color"), "Use this color for all periods"))) {
				std::fill_n(value.values, applicable, value.values[period]);
				changed = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem(T(TKEY("copy"), "Copy"))) {
				const float copy[3] = { value.values[period].x, value.values[period].y, value.values[period].z };
				Effects11UI::Clipboard::SetColor(copy);
			}
			if (ImGui::MenuItem(T(TKEY("paste"), "Paste"), nullptr, false, Effects11UI::Clipboard::HasColor())) {
				float paste[3];
				Effects11UI::Clipboard::GetColor(paste);
				value.values[period] = { paste[0], paste[1], paste[2] };
				changed = true;
			}
			ImGui::EndPopup();
		}

		ImGui::PopID();
		if (!focused)
			ImGui::PopStyleVar();
	}

	if (changed) {
		settingManager.SetValue<ColorTimeOfDayValue>(a_setting.id, value);
		dirty = true;
	}
}

void Effects11Editor::DrawSettingTooltip(const Setting& a_setting, const std::string& a_name, bool a_dependencyMet)
{
	if (!ImGui::BeginTooltip())
		return;

	auto& settingManager = SettingManager::GetSingleton();
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
	ImGui::TextUnformatted(a_name.c_str());
	Util::TextUnformattedDisabled(std::format("[{}] {}", a_setting.category, a_setting.key).c_str());

	const int decimals = Effects11UI::DecimalsForStep(a_setting.step);
	switch (a_setting.type) {
	case SettingType::Bool:
		if (const auto* defaultValue = std::get_if<bool>(&a_setting.defaultValue))
			ImGui::Text("%s %s", T(TKEY("default_label"), "Default:"), *defaultValue ? T(TKEY("on"), "On") : T(TKEY("off"), "Off"));
		break;
	case SettingType::Float:
		ImGui::Text("%s %s - %s", T(TKEY("range_label"), "Range:"), FormatFloat(a_setting.minValue, decimals).c_str(), FormatFloat(a_setting.maxValue, decimals).c_str());
		if (const auto* defaultValue = std::get_if<float>(&a_setting.defaultValue))
			ImGui::Text("%s %s", T(TKEY("default_label"), "Default:"), FormatFloat(*defaultValue, decimals).c_str());
		break;
	case SettingType::TimeOfDay:
		ImGui::Text("%s %s - %s", T(TKEY("range_label"), "Range:"), FormatFloat(a_setting.minValue, decimals).c_str(), FormatFloat(a_setting.maxValue, decimals).c_str());
		if (const auto* defaultValue = std::get_if<TimeOfDayValue>(&a_setting.defaultValue))
			ImGui::Text("%s %s", T(TKEY("default_label"), "Default:"), FormatFloat(defaultValue->values[0], decimals).c_str());
		ImGui::Text("%s %s", T(TKEY("now_label"), "In effect now:"), FormatFloat(settingManager.GetInterpolatedTimeOfDayValue(a_setting.key, a_setting.category), decimals).c_str());
		break;
	case SettingType::ColorTimeOfDay:
		ImGui::Text("%s %s", T(TKEY("now_label"), "In effect now:"), FormatColor(settingManager.GetInterpolatedColorTimeOfDayValue(a_setting.key, a_setting.category)).c_str());
		break;
	}

	const std::string file = a_setting.hasWeatherSupport ? EditTargetFile(a_setting.category) : std::string("enbseries.ini");
	const auto savedTo = I18n::GetSingleton()->Format(TKEY("saved_to"), { { "file", file } }, "Saved to {file}");
	Util::Text::Info("%s", savedTo.c_str());

	if (!a_dependencyMet) {
		const auto needs = I18n::GetSingleton()->Format(TKEY("needs_setting"), { { "setting", Effects11UI::PrettifyName(a_setting.dependsOnKey) } },
			"Turn on \"{setting}\" to use this.");
		Util::Text::WrappedWarning("%s", needs.c_str());
	}
	Util::TextUnformattedDisabled(T(TKEY("context_hint"), "Right-click a value for reset, copy and paste."));
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

// Shader window

void Effects11Editor::DrawShaderWindow()
{
	SetNextPanelPlacement(true, resetLayout);

	bool keepOpen = true;
	const std::string title = std::format("{}###Effects11ShaderParameters", T(TKEY("shader_window_title"), "Shader Parameters"));
	if (Util::BeginWithRoundedClose(title.c_str(), &keepOpen, ImGuiWindowFlags_NoScrollbar)) {
		HandleShortcuts(focusShaderSearch);
		DrawSearchBar(shaderFilter, T(TKEY("search_parameters"), "Search parameters..."), focusShaderSearch);
		DrawPeriodFocusCombo();

		if (hiddenPeriodParameters > 0 && periodFocus != kFocusAll) {
			const auto hidden = I18n::GetSingleton()->Format(TKEY("hidden_period_parameters"), { { "count", std::to_string(hiddenPeriodParameters) } },
				"{count} time-of-day parameters are hidden by the Time of day filter.");
			ImGui::PushTextWrapPos(0.0f);
			Util::TextUnformattedDisabled(hidden.c_str());
			ImGui::PopTextWrapPos();
			if (ImGui::SmallButton(T(TKEY("show_all_periods"), "Show all periods")))
				periodFocus = kFocusAll;
		}
		ImGui::Spacing();

		if (ImGui::BeginChild("##effects", ImVec2(0.0f, 0.0f))) {
			const auto files = GetEffectFiles();
			UITree::ViewOptions totals;

#ifdef ENABLE_ENB_EXTENDER
			// Parameters a preset marks as top level are shared across files and listed first
			std::vector<Effect*> compiled;
			for (const auto& file : files)
				if (file.effect->IsCompiled())
					compiled.push_back(file.effect);
			if (!compiled.empty()) {
				UITree::ViewOptions view;
				view.filter = shaderFilter;
				view.showPeriod = [this](const std::string& a_period) { return IsEffectPeriodShown(a_period); };
				ExtendedEffect::RenderMergedUI(compiled, UITree::FilterMode::TopLevelOnly, &view);
				totals.drawn += view.drawn;
				totals.changed |= view.changed;
				totals.compileTimeChanged |= view.compileTimeChanged;
			}
#endif

			std::string missing;
			for (const auto& file : files) {
				if (!file.effect->IsFilePresent()) {
					missing += (missing.empty() ? "" : ", ") + file.effect->GetName();
					continue;
				}
				DrawEffectSection(*file.effect, file.enableSettingID, file.friendlyName, totals);
			}

			if (!shaderFilter.empty() && totals.drawn == 0) {
				const auto text = I18n::GetSingleton()->Format(TKEY("no_parameters_match"), { { "search", shaderFilter } }, "No parameters match \"{search}\".");
				Util::TextUnformattedDisabled(text.c_str());
			}
			if (!missing.empty()) {
				ImGui::Spacing();
				const auto text = I18n::GetSingleton()->Format(TKEY("files_not_in_preset"), { { "files", missing } }, "Not in this preset: {files}");
				ImGui::PushTextWrapPos(0.0f);
				Util::TextUnformattedDisabled(text.c_str());
				ImGui::PopTextWrapPos();
			}

			if (totals.changed)
				dirty = true;
			if (totals.compileTimeChanged)
				shaderReloadNeeded = true;
		}
		ImGui::EndChild();
	}
	ImGui::End();

	if (!keepOpen)
		showShaderPanel = false;
	hiddenPeriodParameters = CountHiddenPeriodParameters();
}

void Effects11Editor::DrawEffectSection(Effect& a_effect, uint32_t a_enableSettingID, const char* a_friendlyName, UITree::ViewOptions& a_totals)
{
	auto& settingManager = SettingManager::GetSingleton();
	const std::string fileName = a_effect.GetName();
	const bool failed = !a_effect.GetErrors().empty();
	const bool nameMatches = Effects11UI::ContainsNoCase(a_friendlyName, shaderFilter) || Effects11UI::ContainsNoCase(fileName, shaderFilter);

	// While searching, only list files with a match (errors always stay visible)
	if (!failed && !nameMatches && !EffectHasMatches(a_effect, shaderFilter))
		return;

	ImGui::PushID(fileName.c_str());
	const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
	const bool hasToggle = a_enableSettingID != kInvalidSettingID;
	const bool enabled = !hasToggle || settingManager.GetValue<bool>(a_enableSettingID);

	if (!shaderFilter.empty())
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	ImGui::SetNextItemAllowOverlap();
	if (failed)
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetError());
	else if (!enabled)
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetDisabled());
	const bool expanded = ImGui::CollapsingHeader(std::format("{}###fx", a_friendlyName).c_str(),
		&a_effect == &EffectManager::GetSingleton().enbEffect ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
	if (failed || !enabled)
		ImGui::PopStyleColor();

	const float toggleWidth = hasToggle ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
	const char* failedTag = failed ? T(TKEY("tag_failed"), "Failed to compile") : nullptr;
	const char* offTag = !failed && !enabled ? T(TKEY("tag_off"), "Off") : nullptr;
	Effects11UI::HeaderTags({ { fileName.c_str(), Util::Colors::GetDisabled() },
								{ failedTag, Util::Colors::GetError() },
								{ offTag, Util::Colors::GetDisabled() } },
		toggleWidth);

	if (hasToggle) {
		ImGui::SameLine();
		ImGui::SetCursorPosX(rightEdge - ImGui::GetFrameHeight() - ImGui::GetStyle().FramePadding.x);
		bool toggle = enabled;
		if (ImGui::Checkbox("##enabled", &toggle)) {
			settingManager.SetValue<bool>(a_enableSettingID, toggle);
			dirty = true;
		}
		const auto tip = I18n::GetSingleton()->Format(TKEY("file_toggle_tip"), { { "file", fileName } }, "Run {file}. Same switch as in the Effects section.");
		Util::AddTooltip(tip.c_str());
	}

	if (expanded) {
		if (failed) {
			for (const auto& error : a_effect.GetErrors())
				Util::Text::WrappedError("%s", error.c_str());
			if (ImGui::SmallButton(T(TKEY("copy_errors"), "Copy errors"))) {
				std::string all;
				for (const auto& error : a_effect.GetErrors())
					all += error + "\n";
				ImGui::SetClipboardText(all.c_str());
			}
		} else {
#ifdef ENABLE_ENB_EXTENDER
			UITree::ViewOptions view;
			view.filter = nameMatches ? std::string_view{} : std::string_view{ shaderFilter };
			view.showPeriod = [this](const std::string& a_period) { return IsEffectPeriodShown(a_period); };
			Effect* self = &a_effect;
			ExtendedEffect::RenderMergedUI({ &self, 1 }, UITree::FilterMode::NonTopLevelOnly, &view);
			a_totals.drawn += view.drawn;
			a_totals.changed |= view.changed;
			a_totals.compileTimeChanged |= view.compileTimeChanged;
#else
			a_effect.RenderImGui();
			a_totals.drawn++;
#endif
		}
		ImGui::Spacing();
	} else if (nameMatches || failed) {
		a_totals.drawn++;
	}
	ImGui::PopID();
}

int Effects11Editor::CountHiddenPeriodParameters() const
{
	int hidden = 0;
	for (const auto& file : GetEffectFiles()) {
		if (!file.effect->IsCompiled())
			continue;
		for (const auto& uiVar : file.effect->uiVariables) {
			if (!uiVar.timePeriod.empty() && !uiVar.isHidden && !uiVar.displayName.empty() && !IsEffectPeriodShown(uiVar.timePeriod))
				++hidden;
		}
	}
	return hidden;
}

// Actions

void Effects11Editor::Save()
{
	if (!EffectManager::GetSingleton().IsPresetLoaded())
		return;
	SettingManager::GetSingleton().Save();
	EffectManager::GetSingleton().Save();
	dirty = false;
}

void Effects11Editor::RefreshPresetPaths()
{
	auto& presetManager = PresetManager::GetSingleton();
	const auto iniPath = presetManager.GetENBSeriesIniPath();
	presetPaths.iniFull = iniPath.string();
	presetPaths.iniDisplay = DisplayPath(iniPath);
	presetPaths.mainEffect = EffectManager::GetSingleton().enbEffect.GetFilePath().string();
}

void Effects11Editor::Revert()
{
	RefreshPresetPaths();
	SettingManager::GetSingleton().Load();
	// Load only overwrites keys present in the ini, so edits to omitted keys must be reset first
	for (const auto& file : GetEffectFiles()) {
		for (auto& uiVar : file.effect->uiVariables)
			Effect::RestoreDefaultValue(uiVar);
	}
	EffectManager::GetSingleton().Load();
	dirty = false;
}

void Effects11Editor::ReloadShaders()
{
	RefreshPresetPaths();
	Util::ShaderPatches::Load();
	Util::SettingsPatches::Load();
	SettingManager::GetSingleton().Load();
	EffectManager::GetSingleton().Apply();
	dirty = false;
	shaderReloadNeeded = false;
}

void Effects11Editor::RequestAction(PendingAction a_action)
{
	if (dirty) {
		pendingAction = a_action;
		return;
	}
	if (a_action == PendingAction::Revert)
		Revert();
	else if (a_action == PendingAction::ReloadShaders)
		ReloadShaders();
}

void Effects11Editor::DrawPendingActionPopup()
{
	const std::string title = std::format("{}###Effects11Unsaved", T(TKEY("unsaved_title"), "Unsaved changes"));
	if (pendingAction != PendingAction::None && !ImGui::IsPopupOpen(title.c_str()))
		ImGui::OpenPopup(title.c_str());

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	const bool reload = pendingAction == PendingAction::ReloadShaders;
	const char* reloadBody = T(TKEY("unsaved_reload_body"), "Reloading shaders reads every value from disk again.\nWhat should happen to your unsaved changes?");
	const char* revertBody = T(TKEY("unsaved_revert_body"), "Reverting discards all unsaved changes.");
	ImGui::TextUnformatted(reload ? reloadBody : revertBody);
	ImGui::Spacing();

	auto finish = [this]() {
		pendingAction = PendingAction::None;
		ImGui::CloseCurrentPopup();
	};

	if (reload) {
		if (Util::SuccessButton(T(TKEY("save_and_reload"), "Save and reload"))) {
			Save();
			ReloadShaders();
			finish();
		}
		ImGui::SameLine();
	}
	if (Util::WarningButton(reload ? T(TKEY("reload_without_saving"), "Reload without saving") : T(TKEY("discard_changes"), "Discard changes"))) {
		if (reload)
			ReloadShaders();
		else
			Revert();
		finish();
	}
	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("cancel"), "Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		finish();

	ImGui::EndPopup();
}

// Launcher on the feature page

void Effects11Editor::DrawLauncher()
{
	auto& effectManager = EffectManager::GetSingleton();
	auto& settingManager = SettingManager::GetSingleton();
	auto& menuSettings = globals::menu->GetSettings();
	const bool presetLoaded = effectManager.IsPresetLoaded();

	if (presetLoaded)
		Util::Text::Success("%s", T(TKEY("preset_loaded"), "Preset loaded"));
	else
		Util::Text::Warning("%s", T(TKEY("no_preset_title"), "No preset loaded"));
	ImGui::SameLine();
	if (presetPaths.iniFull.empty())
		RefreshPresetPaths();
	Util::TextUnformattedDisabled(presetPaths.iniDisplay.c_str());
	Util::AddTooltip(presetPaths.iniFull.c_str());

	ImGui::Spacing();
	const float scale = Util::GetUIScale();
	const ImVec2 buttonSize((std::min)(ImGui::GetContentRegionAvail().x, 320.0f * scale), ImGui::GetFrameHeight() * 1.6f);
	if (ImGui::Button(T(TKEY("open_editor"), "Open Effects 11 Editor"), buttonSize))
		Open(true);
	if (!menuSettings.Effects11EditorKey.empty()) {
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize.y - ImGui::GetTextLineHeight()) * 0.5f);
		const auto hotkey = I18n::GetSingleton()->Format(TKEY("editor_hotkey"),
			{ { "key", Util::Input::KeyIdToString(menuSettings.Effects11EditorKey) } }, "Hotkey: {key}");
		Util::TextUnformattedDisabled(hotkey.c_str());
	}
	ImGui::PushTextWrapPos(0.0f);
	Util::TextUnformattedDisabled(T(TKEY("launcher_body"),
		"The editor opens over the game, like the ENB editor, so every change shows live. "
		"Close it or press Esc to come back here."));
	ImGui::PopTextWrapPos();

	ImGui::Spacing();
	ImGui::Separator();

	const uint32_t useEffectID = settingManager.GetSettingID("UseEffect", "GLOBAL");
	bool enabled = presetLoaded && settingManager.GetValue<bool>(useEffectID);
	ImGui::BeginDisabled(!presetLoaded);
	if (ImGui::Checkbox(T(TKEY("enable_effects"), "Enable Effects 11"), &enabled)) {
		settingManager.SetValue<bool>(useEffectID, enabled);
		dirty = true;
	}
	ImGui::EndDisabled();
	{
		const auto tip = I18n::GetSingleton()->Format(TKEY("enable_effects_tip"),
			{ { "key", Util::Input::KeyIdToString(menuSettings.Effects11ToggleKey) } },
			"Master switch for the whole preset.\nHotkey: {key}");
		Util::AddTooltip(tip.c_str(), ImGuiHoveredFlags_AllowWhenDisabled);
	}
	if (dirty) {
		SameLineIfFits(ImGui::CalcTextSize(T(TKEY("unsaved_changes"), "Unsaved changes")).x);
		ImGui::AlignTextToFramePadding();
		Util::Text::Warning("%s", T(TKEY("unsaved_changes"), "Unsaved changes"));
		ImGui::SameLine();
		if (Util::SuccessButton(T(TKEY("save"), "Save")))
			Save();
	}

	ImGui::SeparatorText(T("menu.settings.tab_keybindings", "Keybindings"));
	Util::InputComboWidget(T("menu.settings.effects11_editor_key", "Effects 11 Editor Key:"), menuSettings.Effects11EditorKey,
		globals::menu->settingEffects11EditorKey, "Change##Effects11EditorKey");
	Util::InputComboWidget(T("menu.settings.effects11_toggle_key", "Effects 11 Toggle Key:"), menuSettings.Effects11ToggleKey,
		globals::menu->settingEffects11ToggleKey, "Change##Effects11ToggleKey");

	if (!effectManager.enbEffect.IsFilePresent())
		return;

	ImGui::SeparatorText(T(TKEY("shader_files"), "Shader files"));
	if (!ImGui::BeginTable("##files", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
		return;
	for (const auto& file : GetEffectFiles()) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(file.effect->GetName().c_str());
		ImGui::TableSetColumnIndex(1);
		if (!file.effect->IsFilePresent()) {
			Util::TextUnformattedDisabled(T(TKEY("file_missing"), "Not in preset"));
		} else if (!file.effect->GetErrors().empty()) {
			Util::Text::Error("%s", T(TKEY("tag_failed"), "Failed to compile"));
			if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
				for (const auto& error : file.effect->GetErrors())
					ImGui::TextUnformatted(error.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		} else if (file.enableSettingID != kInvalidSettingID && !settingManager.GetValue<bool>(file.enableSettingID)) {
			Util::TextUnformattedDisabled(T(TKEY("file_off"), "Loaded, switched off"));
		} else {
			Util::Text::Success("%s", T(TKEY("file_loaded"), "Loaded"));
		}
	}
	ImGui::EndTable();
}

// Time of day

float Effects11Editor::PeriodWeight(int a_period) const
{
	const auto& commonData = EffectManager::GetSingleton().commonData;
	if (a_period >= 0 && a_period < 4)
		return commonData.timeOfDay1[a_period];
	if (a_period >= 4 && a_period < kPeriodCount)
		return commonData.timeOfDay2[a_period - 4];
	return 0.0f;
}

bool Effects11Editor::IsPeriodActive(int a_period) const
{
	return PeriodWeight(a_period) > kActiveWeight;
}

bool Effects11Editor::IsPeriodFocused(int a_period) const
{
	if (periodFocus == kFocusAll)
		return true;
	if (periodFocus == kFocusCurrent)
		return IsPeriodActive(a_period);
	return periodFocus == a_period;
}

bool Effects11Editor::IsEffectPeriodShown(const std::string& a_period) const
{
	if (periodFocus == kFocusAll)
		return true;
	// Parameters of shader files use one "Interior" period for both interior periods
	if (a_period == "Interior") {
		return periodFocus == kFocusCurrent ? IsInterior() :
		                                      (periodFocus == 6 || periodFocus == 7);
	}
	static constexpr std::string_view names[] = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night" };
	for (int period = 0; period < kExteriorPeriodCount; ++period) {
		if (a_period == names[period])
			return IsPeriodFocused(period);
	}
	return true;
}

std::vector<int> Effects11Editor::FocusedPeriods(bool a_exteriorOnly) const
{
	const int count = a_exteriorOnly ? kExteriorPeriodCount : kPeriodCount;
	std::vector<int> periods;
	if (periodFocus >= 0) {
		if (periodFocus < count)
			periods.push_back(periodFocus);
		return periods;
	}
	if (periodFocus == kFocusCurrent) {
		for (int period = 0; period < count; ++period)
			if (IsPeriodActive(period))
				periods.push_back(period);
		// Exterior-only values while indoors: nothing is active, so show them all rather than nothing
		if (!periods.empty())
			return periods;
	}
	for (int period = 0; period < count; ++period)
		periods.push_back(period);
	return periods;
}

std::string Effects11Editor::ActivePeriodSummary(bool a_withWeights) const
{
	std::vector<std::pair<float, int>> active;
	for (int period = 0; period < kPeriodCount; ++period) {
		const float weight = PeriodWeight(period);
		if (weight > kActiveWeight)
			active.emplace_back(weight, period);
	}
	std::sort(active.begin(), active.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

	std::string summary;
	for (const auto& [weight, period] : active) {
		if (!summary.empty())
			summary += ", ";
		summary += PeriodName(period);
		if (a_withWeights && active.size() > 1)
			summary += std::format(" {:.0f}%", weight * 100.0f);
	}
	return summary.empty() ? "-" : summary;
}

bool Effects11Editor::IsInterior() const
{
	return EffectManager::GetSingleton().commonData.eInteriorFactor > 0.5f;
}

uint32_t Effects11Editor::EditWeatherID() const
{
	return EffectManager::GetSingleton().GetDominantWeatherID();
}

std::string Effects11Editor::EditTargetFile(const std::string& a_category) const
{
	static const std::string baseFile = "enbseries.ini";
	auto& settingManager = SettingManager::GetSingleton();
	if (!settingManager.IsWeatherSystemEnabled() || !settingManager.CategoryHasWeatherSupport(a_category))
		return baseFile;
	const bool ignoreWeather = IsInterior() ? settingManager.GetIgnoreWeatherSystemInterior(a_category) : settingManager.GetIgnoreWeatherSystem(a_category);
	if (ignoreWeather)
		return baseFile;
	if (auto* entry = WeatherManager::GetSingleton().FindWeatherEntry(EditWeatherID()))
		return entry->fileName;
	return baseFile;
}

#undef I18N_KEY_PREFIX
