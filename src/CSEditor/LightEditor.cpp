#include "LightEditor.h"
#include "../Features/InverseSquareLighting.h"
#include "../Features/LightLimitFix.h"
#include "../I18n/I18n.h"
#include "../Menu.h"
#include "../Utils/UI.h"
#include "EditorWindow.h"
#include "WeatherUtils.h"
#include "RE/B/BSLight.h"
#include "RE/B/BSShadowLight.h"
#include "RE/E/ExtraEmittanceSource.h"

#define I18N_KEY_PREFIX "feature.light_editor."

#include <array>
#include <filesystem>
#include <numbers>
#include <fstream>
#include <regex>
#include <sstream>

std::vector<std::pair<std::string, RE::TESObjectLIGH*>> LightEditor::s_lighFormList;
std::vector<std::pair<std::string, RE::TESForm*>> LightEditor::s_emittanceFormList;

// Returns the named array member of a JSON object, or nullptr if missing / not an array.
static const nlohmann::ordered_json* GetArrayMember(const nlohmann::ordered_json& obj, const char* key)
{
	const auto it = obj.find(key);
	return (it != obj.end() && it->is_array()) ? &*it : nullptr;
}

// Downcast a BSLight to BSShadowLight only when it actually is one. Returns nullptr otherwise.
static RE::BSShadowLight* AsShadowLight(RE::BSLight* light)
{
	return (light && light->IsShadowLight()) ? static_cast<RE::BSShadowLight*>(light) : nullptr;
}

static void ScheduleConsoleCommand(std::string cmd, RE::TESObjectREFR* refr = nullptr)
{
	if (auto* taskInterface = SKSE::GetTaskInterface()) {
		taskInterface->AddTask([cmd = std::move(cmd), refr]() {
			// Write the console selected-ref global directly (same RELOCATION_ID as
			// CommonLibSSE-NG/src/RE/C/Console.cpp:32 Console::GetSelectedRefHandle).
			// Console::SetSelectedRef requires a Console instance that only exists
			// while the console UI is open, so we write the global directly.
			// We also pass refr to CompileAndRun as thisObj to cover command handlers
			// that read from the script parameter rather than the console global.
			static REL::Relocation<RE::ObjectRefHandle*> selectedRef{
				RELOCATION_ID(519394, AE_CHECK(SKSE::RUNTIME_SSE_1_6_1130, 405935, 504099))
			};

			RE::ObjectRefHandle prevHandle;
			if (refr) {
				prevHandle = *selectedRef;
				*selectedRef = RE::ObjectRefHandle(refr);
			}

			const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			if (auto* script = factory ? static_cast<RE::Script*>(factory->Create()) : nullptr) {
				script->SetCommand(cmd);
				script->CompileAndRun(refr);
				delete script;
			}

			if (refr)
				*selectedRef = prevHandle;
		});
	}
}

static bool WriteLPConfig(const std::filesystem::path& filePath, nlohmann::ordered_json& config)
{
	std::ofstream outFile(filePath);
	if (!outFile.is_open()) {
		logger::warn("[LightEditor] Failed to write Light Placer config: {}", filePath.string());
		return false;
	}
	std::string output = config.dump(1, '\t');

	static const std::regex vec3Pattern(R"(\[\n\s*([-\d.eE+]+),\n\s*([-\d.eE+]+),\n\s*([-\d.eE+]+)\n\s*\])");
	output = std::regex_replace(output, vec3Pattern, "[$1, $2, $3]");

	{
		static const std::regex floatPattern(R"(-?\d+\.\d+)");
		std::string rounded;
		rounded.reserve(output.size());
		std::sregex_iterator it(output.begin(), output.end(), floatPattern), end;
		size_t pos = 0;
		for (; it != end; ++it) {
			rounded += output.substr(pos, it->position() - pos);
			double v = std::round(std::stod(it->str()) * 10000.0) / 10000.0;
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.4f", v);
			std::string s(buf);
			s.erase(s.find_last_not_of('0') + 1);
			if (s.back() == '.') s.pop_back();
			rounded += s;
			pos = it->position() + it->length();
		}
		rounded += output.substr(pos);
		output = std::move(rounded);
	}

	outFile << output;
	outFile.flush();
	if (outFile.fail()) {
		logger::warn("[LightEditor] Failed to write Light Placer config to {}: stream error", filePath.string());
		return false;
	}
	return true;
}

void LightEditor::EnsureEmittanceFormListBuilt()
{
	if (!s_emittanceFormList.empty())
		return;
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh)
		return;
	auto containsCaseInsensitive = [](const std::string& str, std::string_view needle) {
		return std::ranges::search(str, needle, [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		}).begin() != str.end();
	};

	auto addForms = [&](auto& formArray, RE::FormType expectedType) {
		for (auto* form : formArray) {
			if (!form || form->formID == 0 || form->GetFormType() != expectedType)
				continue;
			std::string edid = clib_util::editorID::get_editorID(form);
			if (edid.empty())
				continue;
			if (!containsCaseInsensitive(edid, "fx") && !containsCaseInsensitive(edid, "weather"))
				continue;
			s_emittanceFormList.emplace_back(std::move(edid), static_cast<RE::TESForm*>(form));
		}
	};
	addForms(dh->GetFormArray<RE::TESRegion>(), RE::FormType::Region);
	std::ranges::sort(s_emittanceFormList, [](const auto& a, const auto& b) { return a.first < b.first; });
}


RE::FormID LightEditor::ResolveFormEntry(const std::string& entry)
{
	const auto tildePos = entry.find('~');
	const bool hasPrefix = entry.starts_with("0x") || entry.starts_with("0X");
	if (tildePos == std::string::npos) {
		if (!hasPrefix)
			return 0;
		try { return static_cast<RE::FormID>(std::stoul(entry.substr(2), nullptr, 16)); }
		catch (...) { return 0; }
	}
	const auto hexStart = hasPrefix ? 2 : 0;
	RE::FormID relativeID;
	try { relativeID = static_cast<RE::FormID>(std::stoul(entry.substr(hexStart, tildePos - hexStart), nullptr, 16)); }
	catch (...) { return 0; }
	auto* dh = RE::TESDataHandler::GetSingleton();
	auto* form = dh ? dh->LookupForm(relativeID, entry.substr(tildePos + 1)) : nullptr;
	return form ? form->GetFormID() : 0;
}

void LightEditor::ApplyLighFormData(const RE::TESObjectLIGH* ligh)
{
	current.data.lighFormId = ligh->formID;

	current.data.flags.reset(LightLimitFix::LightFlags::InverseSquare);
	current.data.flags.reset(LightLimitFix::LightFlags::Linear);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kInverseSquare)))
		current.data.flags.set(LightLimitFix::LightFlags::InverseSquare);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kLinear)))
		current.data.flags.set(LightLimitFix::LightFlags::Linear);

	const float size = ligh->data.fov >= 50.f ? std::numbers::sqrt2_v<float> : ligh->data.fov;
	current.data.size = std::clamp(size, 0.01f, 50.f);
	current.data.cutoffOverride = std::clamp(ligh->data.fallofExponent, 0.01f, 1.f);
	current.data.radius = static_cast<float>(ligh->data.radius);
	current.data.fade = ligh->fade;
	current.data.diffuse.red   = ligh->data.color.red   / 255.f;
	current.data.diffuse.green = ligh->data.color.green / 255.f;
	current.data.diffuse.blue  = ligh->data.color.blue  / 255.f;
}

void LightEditor::EnsureLighFormListBuilt()
{
	if (!s_lighFormList.empty())
		return;
	if (auto* dh = RE::TESDataHandler::GetSingleton()) {
		for (auto* form : dh->GetFormArray<RE::TESObjectLIGH>()) {
			if (!form || form->formID == 0)
				continue;
			std::string edid = clib_util::editorID::get_editorID(form);
			if (!edid.empty())
				s_lighFormList.emplace_back(std::move(edid), form);
		}
		std::ranges::sort(s_lighFormList, [](const auto& a, const auto& b) { return a.first < b.first; });
	}
}

void LightEditor::DrawSettings()
{
	// Header
	ImGui::Text("%s", T(TKEY("header"), "Light Editor"));
	ImGui::Separator();

	const bool isAttaching = (attachPhase != AttachPhase::Idle);
	if (isAttaching) {
		ImGui::TextColored(Util::Colors::GetInfo(), "%s", T(TKEY("attaching_light"), "Attaching light, please wait..."));
		ImGui::Separator();
		ImGui::BeginDisabled();
	}

	ImGui::Checkbox(T(TKEY("disable_regular_falloff_lights"), "Disable Regular Falloff Lights"), &disableRegularLights);
	ImGui::Checkbox(T(TKEY("disable_inverse_square_falloff_lights"), "Disable Inverse Square Falloff Lights"), &disableInvSqLights);

	if (ImGui::Button(T(TKEY("toggle_all_lp_lights"), "Toggle All LP Lights"))) {
		ScheduleConsoleCommand("tlp 0");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("toggle_all_lp_lights_tooltip"), "Toggle all Light Placer lights on/off (tlp 0)."));
	}

	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("toggle_lp_markers"), "Toggle LP Markers"))) {
		ScheduleConsoleCommand("tlp 1");
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("toggle_lp_markers_tooltip"), "Toggle Light Placer debug markers (tlp 1)."));
	}

	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("reload_lp"), "Reload LP"))) {
		RestoreOriginal();
		previous = {};
		waitFrames = 3;
		ScheduleConsoleCommand("reloadlp");
		EditorWindow::GetSingleton()->ShowNotification(T(TKEY("reloading_lp_configs"), "Reloading Light Placer configs..."), Util::Colors::GetInfo());
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("reload_lp_tooltip"), "Reload all Light Placer JSON configs in-game (reloadlp)."));
	}

	ImGui::SameLine();
	DrawAddLightButton();

	if (picker.IsPicking()) {
		ImGui::TextColored(Util::Colors::GetInfo(), "%s", T(TKEY("pick_mesh_prompt"), "Click a mesh to attach a light... (right-click / ESC to cancel)"));
	}

	DrawAddLightPopup();

	ImGui::Separator();

	ImGui::Text(T(TKEY("total_lights"), "Total Lights: %u"), totalLightCount);
	ImGui::Text(T(TKEY("active_shadow_lights"), "Active Shadow Lights: %u"), activeShadowLightCount);
	ImGui::Separator();

	{
		const auto& style = ImGui::GetStyle();
		const float arrowWidth = ImGui::GetFrameHeight();

		const char* filterLabels[] = {
			T(TKEY("filter_ref_lights"), "Ref Lights"),
			T(TKEY("filter_attached_lights"), "Attached Lights"),
			T(TKEY("filter_other_lights"), "Other Lights")
		};
		const char* sortLabels[] = {
			T(TKEY("sort_none"), "None"),
			T(TKEY("sort_distance"), "Distance"),
			T(TKEY("sort_form_id"), "FormID"),
			T(TKEY("sort_editor_id"), "EditorID")
		};

		const float filterComboWidth = ImGui::CalcTextSize(filterLabels[static_cast<int>(FilterOption::AttachedLights)]).x + style.FramePadding.x * 2 + arrowWidth;
		const float sortComboWidth = ImGui::CalcTextSize(sortLabels[static_cast<int>(SortOption::EditorID)]).x + style.FramePadding.x * 2 + arrowWidth;

		ImGui::SetNextItemWidth(filterComboWidth);
		int selectedFilter = static_cast<int>(filterOption);
		if (ImGui::Combo("##Type", &selectedFilter, filterLabels, static_cast<int>(FilterOption::Count))) {
			filterOption = static_cast<FilterOption>(selectedFilter);
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(sortComboWidth);
		int selectedSort = static_cast<int>(sortOption);
		if (ImGui::Combo("##Sorting", &selectedSort, sortLabels, static_cast<int>(SortOption::Count))) {
			sortOption = static_cast<SortOption>(selectedSort);
		}

		ImGui::SameLine();
		ImGui::Checkbox(T(TKEY("shadows_only"), "Shadows Only"), &shadowsOnly);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("shadows_only_tooltip"), "Only show lights with HemiShadow or OmniShadow flags."));
		}
	}

	static constexpr const char* kLightsComboId = "LightsCombo";
	LightInfo thisFrameHovered = {};
	const bool lightsComboOpen = ImGui::BeginCombo(T(TKEY("lights"), "Lights"), selected.isSelected ? GetLightName(selected).c_str() : T(TKEY("select_a_light"), "Select a light"));
	if (lightsComboOpen) {
		auto searchText = Util::DrawComboSearchInput(kLightsComboId);
		for (auto& light : lights) {
			const auto displayName = GetLightName(light);
			if (!searchText.empty() && !Util::StringMatchesSearch(displayName, searchText))
				continue;
			const bool isSelected = light == selected;
			if (ImGui::Selectable(displayName.c_str(), isSelected)) {
				selected = light;
				Util::ClearComboSearch(kLightsComboId);
			}
			if (ImGui::IsItemHovered())
				thisFrameHovered = light;
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	} else {
		Util::ClearComboSearch(kLightsComboId);
	}

	// Hover flash: update state when hovered item changes or combo closes.
	// Only apply to lights with a valid id (ref/attached); skip Other lights.
	if (thisFrameHovered.id != 0 || !lightsComboOpen) {
		if (!(thisFrameHovered == comboHoveredLight)) {
			if (hoverFlashNiLight) {
				if (auto* rd = ISLCommon::RuntimeLightDataExt::Get(hoverFlashNiLight.get()))
					rd->fade = hoverFlashOriginalFade;
				hoverFlashNiLight.reset();
			}
			comboHoveredLight = thisFrameHovered;
			hoverFlashVisible = true;
			hoverFlashLastToggle = ImGui::GetTime();
		}
	}
	if (comboHoveredLight.id != 0) {
		const double now = ImGui::GetTime();
		if (now - hoverFlashLastToggle >= 0.25) {
			hoverFlashVisible = !hoverFlashVisible;
			hoverFlashLastToggle = now;
		}
	}

	ImGui::Separator();

	if (!selected.isSelected) {
		if (isAttaching) ImGui::EndDisabled();
		return;
	}

	if (selected.isRef || selected.isAttached) {
		ImGui::Text(T(TKEY("owner"), "Owner: 0x%08X | %s"), selected.id, displayInfo.ownerEditorId.c_str());
		ImGui::Text(T(TKEY("owner_last_edited_by"), "Owner last edited by: %s"), displayInfo.ownerLastEditedBy.c_str());
		ImGui::Text(T(TKEY("base_object"), "Base Object: 0x%08X | %s"), displayInfo.baseObjectFormId, selected.name.c_str());
		ImGui::Text(T(TKEY("ligh"), "LIGH: 0x%08X | %s"), displayInfo.lighFormId, displayInfo.lighEditorId.c_str());
		ImGui::Text(T(TKEY("cell"), "Cell: 0x%08X | %s"), displayInfo.cellFormId, displayInfo.cellEditorId.c_str());
		if (lpInfo.isLPLight)
			ImGui::Text(T(TKEY("config"), "Config: Data\\LightPlacer\\%s.json"), lpInfo.configPath.c_str());
	} else {
		ImGui::Text(T(TKEY("memory_address"), "Memory Address: %p"), selected.ptr);
		ImGui::Text(T(TKEY("ni_light_name"), "NiLight Name: %s"), selected.name.c_str());
	}

	ImGui::Separator();

	if (ImGui::Button(T(TKEY("reset"), "Reset"))) {
		current = original;
		if (lpInfo.isLPLight) {
			lpFlagSet = originalLpFlagSet;
			SyncLPFlagsToRuntime();
		}
		shadowDepthBias = originalShadowDepthBias;
		ApplyShadowDepthBias();
		waitFrames = 1;
	}

	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("toggle_light"), "Toggle Light"))) {
		if (lpInfo.isLPLight && activeRefr)
			ScheduleConsoleCommand("tlp 0", activeRefr);
		else if (current.data.fade == 0.0f)
			current.data.fade = cachedFadeBeforeToggle;
		else {
			cachedFadeBeforeToggle = current.data.fade;
			current.data.fade = 0.0f;
		}
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (lpInfo.isLPLight)
			ImGui::Text("%s", T(TKEY("toggle_light_lp_tooltip"), "Toggle this reference's LP-placed lights on/off (tlp 0)."));
		else
			ImGui::Text("%s", T(TKEY("toggle_light_tooltip"), "Toggle this light on/off."));
	}

	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		{
			auto _style = Util::StatusButtonStyle(lpMatchFound ? Util::Colors::GetSuccess() : Util::Colors::GetError());
			if (ImGui::Button(T(TKEY("save_to_light_placer"), "Save to Light Placer"))) {
				const bool ok = SaveToLightPlacer(saveColorToLP);
				if (ok) {
					ScheduleConsoleCommand("reloadlp");
					previous = {};
					waitFrames = 3;
					lpMatchFound = true;
				}
				EditorWindow::GetSingleton()->ShowNotification(
					ok ? I18n::GetSingleton()->Format(TKEY("saved_to_config"), {{"path", lpInfo.configPath}}, "Saved to {path}").c_str()
					   : T(TKEY("save_failed"), "Save failed \xe2\x80\x94 see log"),
					ok ? Util::Colors::GetSuccess() : Util::Colors::GetError());
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (lpMatchFound)
				ImGui::Text(T(TKEY("save_to_lp_match_tooltip"), "Matching entry found in %s.\nSave current settings to the Light Placer JSON."), lpInfo.configPath.c_str());
			else
				ImGui::Text(T(TKEY("save_to_lp_no_match_tooltip"), "No matching entry found in %s.\nSaving will fail."), lpInfo.configPath.c_str());
		}
	}
	ImGui::SameLine();
	ImGui::Checkbox(T(TKEY("log_mode"), "Log Mode"), &extendedLogMode);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("log_mode_tooltip"), "Extend slider ranges and use a logarithmic scale."));
	}

	if (lpInfo.isLPLight) {
		auto doFilterButton = [&](bool isWhiteList) {
			bool& inList = isWhiteList ? lpInWhitelist : lpInBlacklist;
			const char* addLabel    = isWhiteList ? T(TKEY("add_to_whitelist"), "Add to Whitelist")         : T(TKEY("add_to_blacklist"), "Add to Blacklist");
			const char* removeLabel = isWhiteList ? T(TKEY("remove_from_whitelist"), "Remove from Whitelist") : T(TKEY("remove_from_blacklist"), "Remove from Blacklist");
			const ImVec4 activeColor = isWhiteList ? Util::Colors::GetSuccess() : Util::Colors::GetError();

			bool clicked = false;
			if (inList) {
				auto _style = Util::StatusButtonStyle(activeColor);
				clicked = ImGui::Button(removeLabel);
			} else {
				clicked = ImGui::Button(addLabel);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T(TKEY("filter_button_tooltip"), "%s\nFormat: %s\nReload LP to apply."), inList ? removeLabel : addLabel,
					FormatOwnerFormEntry(activeRefr).c_str());
			}
			if (clicked) {
				if (ModifyLPFilterList(isWhiteList, !inList)) {
					inList = !inList;
					const char* msg = inList
					    ? (isWhiteList ? T(TKEY("added_to_whitelist"), "Added to whitelist") : T(TKEY("added_to_blacklist"), "Added to blacklist"))
					    : (isWhiteList ? T(TKEY("removed_from_whitelist"), "Removed from whitelist") : T(TKEY("removed_from_blacklist"), "Removed from blacklist"));
					EditorWindow::GetSingleton()->ShowNotification(msg, Util::Colors::GetInfo());
				} else {
					EditorWindow::GetSingleton()->ShowNotification(T(TKEY("filter_update_failed"), "Filter update failed \xe2\x80\x94 see log"), Util::Colors::GetError());
				}
			}
		};

		doFilterButton(true);
		ImGui::SameLine();
		doFilterButton(false);
	}

	ImGui::Spacing();

	if (selected.isAttached) {
		EnsureLighFormListBuilt();
		if (lpInfo.isLPLight && useExternalEmittance)
			EnsureEmittanceFormListBuilt();
		const char* kOriginalLabel = T(TKEY("original"), "(Original)");
		const char* previewEdid = kOriginalLabel;
		for (auto& [edid, ligh] : s_lighFormList)
			if (ligh->GetFormID() == current.data.lighFormId) { previewEdid = edid.c_str(); break; }

		static constexpr const char* kLighOverrideId = "LighFormOverride";
		const auto bulbTypeLabel = fmt::format("{}##combo", T(TKEY("bulb_type"), "Bulb type"));
		if (ImGui::BeginCombo(bulbTypeLabel.c_str(), previewEdid)) {
			auto searchText = Util::DrawComboSearchInput(kLighOverrideId);
			if (searchText.empty() || Util::StringMatchesSearch(kOriginalLabel, searchText)) {
				if (ImGui::Selectable(kOriginalLabel, current.data.lighFormId == original.data.lighFormId)) {
					current.data = original.data;
					Util::ClearComboSearch(kLighOverrideId);
				}
				if (current.data.lighFormId == original.data.lighFormId)
					ImGui::SetItemDefaultFocus();
			}
			for (auto& [edid, ligh] : s_lighFormList) {
				if (!searchText.empty() && !Util::StringMatchesSearch(edid, searchText))
					continue;
				const bool isCurrent = ligh->GetFormID() == current.data.lighFormId;
				if (ImGui::Selectable(edid.c_str(), isCurrent)) {
					const float savedFade     = current.data.fade;
					const float savedRadius   = current.data.radius;
					const float savedSize     = current.data.size;
					const float savedCutoff   = current.data.cutoffOverride;
					ApplyLighFormData(ligh);
					current.data.fade          = savedFade;
					current.data.radius        = savedRadius;
					current.data.size          = savedSize;
					current.data.cutoffOverride = savedCutoff;
					Util::ClearComboSearch(kLighOverrideId);
				}
				if (isCurrent)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		} else {
			Util::ClearComboSearch(kLighOverrideId);
		}

		if (lpInfo.isLPLight && useExternalEmittance) {
			static constexpr const char* kEmittanceComboId = "EmittanceFormCombo";
			const char* kNoneLabel = T(TKEY("none"), "(None)");
			const char* preview = externalEmittanceEdid.empty() ? kNoneLabel : externalEmittanceEdid.c_str();
			const auto externalEmittanceLabel = fmt::format("{}##combo", T(TKEY("external_emittance"), "External Emittance"));
			if (ImGui::BeginCombo(externalEmittanceLabel.c_str(), preview)) {
				auto searchText = Util::DrawComboSearchInput(kEmittanceComboId);
				if (searchText.empty() || Util::StringMatchesSearch(kNoneLabel, searchText)) {
					if (ImGui::Selectable(kNoneLabel, externalEmittanceEdid.empty())) {
						externalEmittanceEdid = {};
						Util::ClearComboSearch(kEmittanceComboId);
					}
					if (externalEmittanceEdid.empty())
						ImGui::SetItemDefaultFocus();
				}
				for (auto& [edid, form] : s_emittanceFormList) {
					if (!searchText.empty() && !Util::StringMatchesSearch(edid, searchText))
						continue;
					const bool isCurrent = edid == externalEmittanceEdid;
					if (ImGui::Selectable(edid.c_str(), isCurrent)) {
						externalEmittanceEdid = edid;
						Util::ClearComboSearch(kEmittanceComboId);
					}
					if (isCurrent)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			} else {
				Util::ClearComboSearch(kEmittanceComboId);
			}
		}
	}

	ImGui::Spacing();

	WeatherUtils::DrawColorEdit(T(TKEY("color"), "Color"), reinterpret_cast<float3&>(current.data.diffuse));
	if (lpInfo.isLPLight) {
		ImGui::SameLine();
		const auto saveColorLabel = fmt::format("{}##color", T(TKEY("save_color"), "Save"));
		ImGui::Checkbox(saveColorLabel.c_str(), &saveColorToLP);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("save_color_tooltip"), "Include color when saving to Light Placer.\nWhen unchecked, the light falls back to the LIGH form color."));
		}
	}

	// Dispatches to logarithmic+extended or normal slider based on extendedLogMode.
	// Log scale requires min > 0, so extended mins are nudged above zero where needed.
	auto drawSlider = [&](const char* label, float& value,
	                       float normalMin, float normalMax,
	                       float extMin, float extMax,
	                       const char* format) -> bool {
		if (extendedLogMode)
			return ImGui::SliderFloat(label, &value, extMin, extMax, format, ImGuiSliderFlags_Logarithmic);
		return static_cast<bool>(WeatherUtils::DrawSliderFloat(label, value, normalMin, normalMax, nullptr, format));
	};

	drawSlider(T(TKEY("intensity"), "Intensity"), current.data.fade, 0.01f, 16.f, 0.01f, 1024.f, "%.3f");

	const auto isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);

	if (isInvSq)
		ImGui::BeginDisabled();
	drawSlider(T(TKEY("radius"), "Radius"), current.data.radius, 2.f, 8096.f, 2.f, 65536.f, "%.0f");
	if (isInvSq)
		ImGui::EndDisabled();

	if (isInvSq) {
		drawSlider(T(TKEY("size"), "Size"), current.data.size, 0.01f, 10.0f, 0.001f, 100.f, "%.3f");
		WeatherUtils::DrawSliderFloat(T(TKEY("cutoff"), "Cutoff"), current.data.cutoffOverride, 0.01f, 1.f, nullptr, "%.3f");
	}

	if (HasShadowFlags(current.tesFlags.underlying())) {
		if (drawSlider(T(TKEY("shadow_depth_bias"), "Shadow Depth Bias"), shadowDepthBias, 0.0f, 10.0f, 0.01f, 50.f, "%.2f"))
			ApplyShadowDepthBias();
	}

	ImGui::Spacing();

	if (!selected.isOther && current.data.lighFormId != 0 && selected.hasPosition) {
		ImGui::Text(T(TKEY("position_format"), "X: %.2f, Y: %.2f, Z: %.2f"), displayInfo.pos.x, displayInfo.pos.y, displayInfo.pos.z);
		ImGui::Spacing();
		ImGui::SliderFloat3(T(TKEY("position"), "Position"), &current.pos.x, -1000.f, 1000.f, "%.0f");

		ImGui::Spacing();

		auto* flags = reinterpret_cast<uint32_t*>(&current.tesFlags);
		auto* runtimeFlags = reinterpret_cast<uint32_t*>(&current.data.flags);

		if (lpInfo.isLPLight) {
			ImGui::Text("%s", T(TKEY("lp_flags"), "LP Flags"));
			static constexpr const char* kLPFlagNames[] = {
				"NoExternalEmittance", "PortalStrict", "IgnoreScale",
				"InverseSquare", "Flicker", "Linear", "Shadow",
				"RandomAnimStart", "SyncAddonNodes", "UpdateOnCellTransition", "UpdateOnWaiting"
			};
			for (const char* flagName : kLPFlagNames) {
				const bool isInvSqEntry = (std::string_view(flagName) == "InverseSquare");
				const bool disabled = isInvSqEntry && selected.isSpotlight;
				if (disabled) ImGui::BeginDisabled();
				bool inSet = lpFlagSet.contains(flagName);
				if (ImGui::Checkbox(flagName, &inSet)) {
					if (inSet) lpFlagSet.insert(flagName);
					else       lpFlagSet.erase(flagName);
					SyncLPFlagsToRuntime();
				}
				if (disabled) ImGui::EndDisabled();
			}
		}

		ImGui::Text("%s", T(TKEY("light_flags"), "Light Flags"));
		ImGui::BeginDisabled(lpInfo.isLPLight);

		if (!lpInfo.isLPLight) {
			// Inverse Square is disabled for spotlights since they have their own falloff model.
			ImGui::BeginDisabled(selected.isSpotlight);
			ImGui::CheckboxFlags(T(TKEY("inverse_square"), "Inverse Square"), runtimeFlags, static_cast<uint32_t>(LightLimitFix::LightFlags::InverseSquare));
			ImGui::EndDisabled();
			ImGui::CheckboxFlags(T(TKEY("linear"), "Linear"), runtimeFlags, static_cast<uint32_t>(LightLimitFix::LightFlags::Linear));
		}

		// Dynamic and Negative are always shown; Flicker/OmniShadow/PortalStrict are hidden for LP lights.
		ImGui::CheckboxFlags(T(TKEY("tes_flag_dynamic"),        "Dynamic"),       flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDynamic));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_negative"),       "Negative"),      flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kNegative));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags(T(TKEY("tes_flag_flicker"),    "Flicker"),       flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_flicker_slow"),   "Flicker Slow"),  flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlickerSlow));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_pulse"),          "Pulse"),         flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulse));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_pulse_slow"),     "Pulse Slow"),    flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulseSlow));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_hemi_shadow"),    "Hemi Shadow"),   flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags(T(TKEY("tes_flag_omni_shadow"),   "Omni Shadow"),   flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags(T(TKEY("tes_flag_portal_strict"), "Portal Strict"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict));

		ImGui::EndDisabled();
	}

	if (isAttaching) ImGui::EndDisabled();
}

static constexpr std::string_view kPopupPrefsPath =
	R"(Data\SKSE\Plugins\CommunityShaders\LightEditorPrefs.json)";

void LightEditor::SavePopupPrefs() const
{
	nlohmann::ordered_json j;
	j["addConfigSearch"] = addConfigSearch;
	j["addAttachMode"]   = addAttachMode;
	j["addLighSearch"]   = addLighSearch;
	j["addPopupMode"]    = addPopupMode;
	j["addLightSubMode"] = addLightSubMode;
	std::ofstream out(kPopupPrefsPath.data());
	if (out.is_open())
		out << j.dump(1, '\t');
}

void LightEditor::LoadPopupPrefs()
{
	std::ifstream in(kPopupPrefsPath.data());
	if (!in.is_open())
		return;
	nlohmann::ordered_json j;
	try {
		in >> j;
	} catch (...) {
		return;
	}
	if (auto it = j.find("addConfigSearch"); it != j.end() && it->is_string()) {
		auto s = it->get<std::string>();
		std::strncpy(addConfigSearch, s.c_str(), sizeof(addConfigSearch) - 1);
	}
	if (auto it = j.find("addAttachMode"); it != j.end() && it->is_number_integer())
		addAttachMode = it->get<int>();
	if (auto it = j.find("addLighSearch"); it != j.end() && it->is_string()) {
		auto s = it->get<std::string>();
		std::strncpy(addLighSearch, s.c_str(), sizeof(addLighSearch) - 1);
	}
	if (auto it = j.find("addPopupMode"); it != j.end() && it->is_number_integer())
		addPopupMode = it->get<int>();
	if (auto it = j.find("addLightSubMode"); it != j.end() && it->is_number_integer())
		addLightSubMode = it->get<int>();
}

void LightEditor::DrawAddLightButton()
{
	if (ImGui::Button(T(TKEY("select_mesh"), "Select Mesh"))) {
		picker.BeginPick();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("select_mesh_tooltip"), "Click a mesh in the world to attach a new bulb, edit an existing bulb, or whitelist/blacklist this reference."));
	}
}

std::vector<std::string> LightEditor::ScanLPConfigPaths() const
{
	std::vector<std::string> paths;
	// Mirrors LightPlacer's own scanning approach exactly:
	//   - relative path (not absolute via GetDataPath) so USVFS intercepts correctly
	//   - no error_code on the iterator (throwing version uses cached WIN32_FIND_DATA)
	//   - is_directory() / extension() only (no is_regular_file(ec) which triggers an
	//     unhookable GetFileAttributesW call that makes virtual files disappear)
	const std::filesystem::path root(R"(Data\LightPlacer)");
	std::error_code existsEc;
	if (!std::filesystem::exists(root, existsEc)) {
		logger::warn("[LightEditor] Data\\LightPlacer not found ({})", existsEc.message());
		return paths;
	}
	try {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
			if (entry.is_directory() || entry.path().extension() != L".json")
				continue;
			// Use path operations rather than character-count stripping — lexically_relative
			// handles prefix removal by component so any prefix casing/format variation from
			// USVFS doesn't corrupt the relative path, and stem() strips the extension cleanly.
			const auto relPath = entry.path().lexically_relative(root);
			std::string rel = (relPath.parent_path() / relPath.stem()).generic_string();
			if (!rel.empty() && rel.find("..") == std::string::npos)
				paths.push_back(std::move(rel));
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("[LightEditor] ScanLPConfigPaths error: {}", e.what());
	}
	logger::info("[LightEditor] Found {} LP config(s)", paths.size());
	std::ranges::sort(paths);
	return paths;
}

void LightEditor::DrawAddLightPopup()
{
	if (addLightPopupOpen) {
		if (!addPopupPrefsLoaded) {
			LoadPopupPrefs();
			addPopupPrefsLoaded = true;
		}
		ImGui::OpenPopup(T(TKEY("select_mesh"), "Select Mesh"));
		addLightPopupOpen = false;
	}

	const float scale = Util::GetUIScale();
	// Anchor toward the top of the screen so combo dropdowns have room to open below.
	const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.1f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(520 * scale, 0), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal(T(TKEY("select_mesh"), "Select Mesh"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(T(TKEY("picked_editor_id"), "EditorID: %s"), pickedMesh.editorId.empty() ? T(TKEY("none_value"), "(none)") : pickedMesh.editorId.c_str());
		ImGui::Text(T(TKEY("picked_mesh"), "Mesh: %s"), pickedMesh.modelPath.empty() ? T(TKEY("none_value"), "(none)") : pickedMesh.modelPath.c_str());
		ImGui::Text(T(TKEY("picked_base_form_id"), "Base FormID: 0x%08X"), pickedMesh.baseFormId);
		ImGui::Text(T(TKEY("picked_plugin"), "Plugin: %s"), pickedMesh.sourcePlugin.empty() ? T(TKEY("unknown_value"), "(unknown)") : pickedMesh.sourcePlugin.c_str());
		ImGui::Separator();

		// --- Mode selector ---
		const bool hasBulbs = !attachedBulbs.empty();
		auto drawModeBtn = [&](const char* label, int mode, bool available, const char* unavailTip) {
			const bool selected = (addPopupMode == mode);
			if (selected) {
				auto _s = Util::StatusButtonStyle(Util::Colors::GetInfo());
				ImGui::Button(label);
			} else if (available) {
				if (ImGui::Button(label))
					addPopupMode = mode;
			} else {
				{
					auto _d = Util::DisableGuard(true);
					ImGui::Button(label);
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", unavailTip);
			}
		};

		const char* noBulbsTip = T(TKEY("no_attached_bulbs"), "This mesh has no attached Light Placer bulbs.");
		drawModeBtn(T(TKEY("mode_add_light"), "Add Light"), ModeAddLight, true, "");
		ImGui::SameLine();
		drawModeBtn(T(TKEY("mode_edit_bulb"), "Edit Bulb"), ModeEditBulb, hasBulbs, noBulbsTip);
		ImGui::SameLine();
		drawModeBtn(T(TKEY("add_to_whitelist"), "Add to Whitelist"), ModeWhitelist, hasBulbs, noBulbsTip);
		ImGui::SameLine();
		drawModeBtn(T(TKEY("add_to_blacklist"), "Add to Blacklist"), ModeBlacklist, hasBulbs, noBulbsTip);
		ImGui::Separator();

		if (addPopupMode == ModeAddLight) {
			auto drawSubModeBtn = [&](const char* label, int subMode) {
				const bool selected = (addLightSubMode == subMode);
				if (selected) {
					auto _s = Util::StatusButtonStyle(Util::Colors::GetInfo());
					ImGui::Button(label);
				} else {
					if (ImGui::Button(label))
						addLightSubMode = subMode;
				}
			};

			if (hasBulbs) {
				drawSubModeBtn(T(TKEY("sub_mode_new_point"), "Add new point"), SubModeNewPoint);
				ImGui::SameLine();
				drawSubModeBtn(T(TKEY("sub_mode_to_entry"),  "Add to entry"),  SubModeToEntry);
				ImGui::SameLine();
				drawSubModeBtn(T(TKEY("sub_mode_new_entry"), "Add new entry"), SubModeNewEntry);
				ImGui::Separator();
			}

			if (!hasBulbs || addLightSubMode == SubModeNewEntry) {
				// --- Target JSON ---
				const char* configPreview = (addSelectedConfig >= 0 && addSelectedConfig < (int)lpConfigPaths.size())
				                                ? lpConfigPaths[addSelectedConfig].c_str()
				                                : T(TKEY("select_a_config"), "Select a config");
				if (ImGui::BeginCombo(T(TKEY("target_json"), "Target JSON"), configPreview, ImGuiComboFlags_HeightLarge)) {
					if (ImGui::IsWindowAppearing())
						ImGui::SetKeyboardFocusHere();
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::InputText("##cfg_search", addConfigSearch, sizeof(addConfigSearch));
					ImGui::Separator();
					if (lpConfigPaths.empty())
						ImGui::TextDisabled("%s", T(TKEY("no_configs_found"), "No configs found in Data\\LightPlacer\\"));
					const std::string_view cfgFilter = addConfigSearch;
					for (int i = 0; i < (int)lpConfigPaths.size(); ++i) {
						if (!cfgFilter.empty() && !Util::StringMatchesSearch(lpConfigPaths[i], std::string(cfgFilter)))
							continue;
						const bool isSel = (i == addSelectedConfig);
						if (ImGui::Selectable(lpConfigPaths[i].c_str(), isSel))
							addSelectedConfig = i;
						if (isSel)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				// --- Attach by (only after a config is chosen) ---
				if (addSelectedConfig >= 0) {
					ImGui::Text("%s", T(TKEY("attach_by"), "Attach by:"));
					ImGui::SameLine();

					auto drawAttachBtn = [&](const char* label, int mode, bool available, const char* unavailTip) {
						const bool selected = (addAttachMode == mode);
						if (selected) {
							auto _s = Util::StatusButtonStyle(Util::Colors::GetInfo());
							ImGui::Button(label);
						} else if (available) {
							if (ImGui::Button(label))
								addAttachMode = mode;
						} else {
							{
								auto _d = Util::DisableGuard(true);
								ImGui::Button(label);
							}
							if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
								ImGui::SetTooltip("%s", unavailTip);
						}
					};

					drawAttachBtn(T(TKEY("attach_model"),     "Model"),    0, !pickedMesh.modelPath.empty(),    T(TKEY("no_model_path"),    "No model path on this object."));
					ImGui::SameLine();
					drawAttachBtn(T(TKEY("attach_form_id"),   "FormID"),   1, !pickedMesh.sourcePlugin.empty(), T(TKEY("no_source_plugin"), "No source plugin on this object."));
					ImGui::SameLine();
					drawAttachBtn(T(TKEY("attach_editor_id"), "EditorID"), 2, !pickedMesh.editorId.empty(),     T(TKEY("no_editor_id_on_object"), "No EditorID on this object."));
				}

				// --- Light record (only after attach mode is chosen) ---
				if (addSelectedConfig >= 0 && addAttachMode >= 0) {
					EnsureLighFormListBuilt();
					const char* lighPreview = T(TKEY("select_a_light"), "Select a light");
					for (auto& [edid, ligh] : s_lighFormList)
						if (ligh->GetFormID() == addSelectedLighFormId) { lighPreview = edid.c_str(); break; }
					if (ImGui::BeginCombo(T(TKEY("light_record"), "Light record"), lighPreview, ImGuiComboFlags_HeightLarge)) {
						if (ImGui::IsWindowAppearing())
							ImGui::SetKeyboardFocusHere();
						ImGui::SetNextItemWidth(-1.0f);
						ImGui::InputText("##ligh_search", addLighSearch, sizeof(addLighSearch));
						ImGui::Separator();
						const std::string_view lighFilter = addLighSearch;
						for (auto& [edid, ligh] : s_lighFormList) {
							if (!lighFilter.empty() && !Util::StringMatchesSearch(edid, std::string(lighFilter)))
								continue;
							const bool isSel = ligh->GetFormID() == addSelectedLighFormId;
							if (ImGui::Selectable(edid.c_str(), isSel))
								addSelectedLighFormId = ligh->GetFormID();
							if (isSel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				ImGui::Separator();
				if (addSelectedConfig >= 0 && addAttachMode >= 0 && addSelectedLighFormId != 0) {
					std::string reason;
					const bool canAdd = CanAddBulb(reason);
					ImGui::BeginDisabled(!canAdd);
					const bool clicked = ImGui::Button(T(TKEY("add_bulb"), "Add Bulb"));
					ImGui::EndDisabled();
					if (!canAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						ImGui::SetTooltip("%s", reason.c_str());

					if (clicked && canAdd) {
						const bool ok = AddBulbToConfig();
						if (ok) {
							attachConfigPath = lpConfigPaths[addSelectedConfig];
							pendingSelectConfigPath = attachConfigPath;
							pendingSelectLighEdid = {};
							for (auto& [edid, ligh] : s_lighFormList)
								if (ligh->GetFormID() == addSelectedLighFormId) { pendingSelectLighEdid = edid; break; }
							pendingSelectRefrId = 0;
							if (auto refr = pickedMesh.refrHandle.get())
								pendingSelectRefrId = refr->GetFormID();
							RestoreOriginal();
							previous = {};
							ScheduleConsoleCommand("reloadlp");
							attachPendingRefr = pickedMesh.refrHandle;
							attachPhase = AttachPhase::WaitingForReload;
							attachPhaseStart = std::chrono::steady_clock::now();
							SavePopupPrefs();
							ImGui::CloseCurrentPopup();
						} else {
							EditorWindow::GetSingleton()->ShowNotification(
								T(TKEY("add_light_failed"), "Failed to add light \xE2\x80\x94 see log"),
								Util::Colors::GetError());
						}
					}
				}
			}

			if (hasBulbs && addLightSubMode == SubModeNewPoint) {
				const char* bulbPreview = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size())
				    ? attachedBulbs[addSelectedBulb].lightEDID.c_str()
				    : T(TKEY("select_a_bulb"), "Select a bulb");
				if (ImGui::BeginCombo(T(TKEY("attached_bulb"), "Attached bulb"), bulbPreview, ImGuiComboFlags_HeightLarge)) {
					if (ImGui::IsWindowAppearing())
						ImGui::SetKeyboardFocusHere();
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::InputText("##bulb_search_pt", addBulbSearch, sizeof(addBulbSearch));
					ImGui::Separator();
					const std::string_view filter = addBulbSearch;
					for (int i = 0; i < (int)attachedBulbs.size(); ++i) {
						const auto& b = attachedBulbs[i];
						const std::string lbl = fmt::format("{}  ({})", b.lightEDID, b.configPath);
						if (!filter.empty() && !Util::StringMatchesSearch(lbl, std::string(filter)))
							continue;
						const bool isSel = (i == addSelectedBulb);
						if (ImGui::Selectable(lbl.c_str(), isSel))
							addSelectedBulb = i;
						if (isSel)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::Separator();
				const bool canAddPt = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
				ImGui::BeginDisabled(!canAddPt);
				const bool clickedPt = ImGui::Button(T(TKEY("add_point"), "Add Point"));
				ImGui::EndDisabled();

				if (clickedPt && canAddPt) {
					const auto& bulb = attachedBulbs[addSelectedBulb];
					const bool ok = AddPointToConfig(bulb);
					if (ok) {
						attachConfigPath        = bulb.configPath;
						pendingSelectConfigPath = bulb.configPath;
						pendingSelectLighEdid   = bulb.lightEDID;
						pendingSelectRefrId     = bulb.refrId;
						RestoreOriginal();
						previous = {};
						ScheduleConsoleCommand("reloadlp");
						attachPendingRefr  = pickedMesh.refrHandle;
						attachPhase        = AttachPhase::WaitingForReload;
						attachPhaseStart   = std::chrono::steady_clock::now();
						SavePopupPrefs();
						ImGui::CloseCurrentPopup();
					} else {
						EditorWindow::GetSingleton()->ShowNotification(
							T(TKEY("add_light_failed"), "Failed to add light \xE2\x80\x94 see log"),
							Util::Colors::GetError());
					}
				}
			}

			if (hasBulbs && addLightSubMode == SubModeToEntry) {
				// Bulb picker — identifies the parent top-level entry
				const char* bulbPreview2 = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size())
				    ? attachedBulbs[addSelectedBulb].lightEDID.c_str()
				    : T(TKEY("select_a_bulb"), "Select a bulb");
				if (ImGui::BeginCombo(T(TKEY("attached_bulb"), "Attached bulb"), bulbPreview2, ImGuiComboFlags_HeightLarge)) {
					if (ImGui::IsWindowAppearing())
						ImGui::SetKeyboardFocusHere();
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::InputText("##bulb_search_te", addBulbSearch, sizeof(addBulbSearch));
					ImGui::Separator();
					const std::string_view filter2 = addBulbSearch;
					for (int i = 0; i < (int)attachedBulbs.size(); ++i) {
						const auto& b = attachedBulbs[i];
						const std::string lbl2 = fmt::format("{}  ({})", b.lightEDID, b.configPath);
						if (!filter2.empty() && !Util::StringMatchesSearch(lbl2, std::string(filter2)))
							continue;
						const bool isSel2 = (i == addSelectedBulb);
						if (ImGui::Selectable(lbl2.c_str(), isSel2))
							addSelectedBulb = i;
						if (isSel2)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				// Light record picker
				EnsureLighFormListBuilt();
				const char* lighPreview2 = T(TKEY("select_a_light"), "Select a light");
				for (auto& [edid, ligh] : s_lighFormList)
					if (ligh->GetFormID() == addSelectedLighFormId) { lighPreview2 = edid.c_str(); break; }
				if (ImGui::BeginCombo(T(TKEY("light_record"), "Light record"), lighPreview2, ImGuiComboFlags_HeightLarge)) {
					if (ImGui::IsWindowAppearing())
						ImGui::SetKeyboardFocusHere();
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::InputText("##ligh_search_te", addLighSearch, sizeof(addLighSearch));
					ImGui::Separator();
					const std::string_view lighFilter2 = addLighSearch;
					for (auto& [edid, ligh] : s_lighFormList) {
						if (!lighFilter2.empty() && !Util::StringMatchesSearch(edid, std::string(lighFilter2)))
							continue;
						const bool isSel3 = ligh->GetFormID() == addSelectedLighFormId;
						if (ImGui::Selectable(edid.c_str(), isSel3))
							addSelectedLighFormId = ligh->GetFormID();
						if (isSel3)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::Separator();
				const bool bulbOk = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
				const std::string teEdid = [&]() -> std::string {
					for (auto& [edid, ligh] : s_lighFormList)
						if (ligh->GetFormID() == addSelectedLighFormId) return edid;
					return {};
				}();
				const bool lightOk = !teEdid.empty();
				const bool isDupe  = bulbOk && lightOk &&
				                     LightAlreadyInEntry(attachedBulbs[addSelectedBulb], teEdid);
				const bool canAddTE = bulbOk && lightOk && !isDupe;

				ImGui::BeginDisabled(!canAddTE);
				const bool clickedTE = ImGui::Button(T(TKEY("add_to_entry_btn"), "Add to Entry"));
				ImGui::EndDisabled();
				if (!canAddTE && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					if (isDupe)
						ImGui::SetTooltip("%s", T(TKEY("light_already_in_entry"), "This light already exists in this entry."));
					else if (!bulbOk)
						ImGui::SetTooltip("%s", T(TKEY("select_a_bulb"), "Select a bulb"));
					else
						ImGui::SetTooltip("%s", T(TKEY("choose_light_record"), "Choose a light record."));
				}

				if (clickedTE && canAddTE) {
					const auto& bulb = attachedBulbs[addSelectedBulb];
					const bool ok = AddLightToExistingEntry(bulb, teEdid);
					if (ok) {
						attachConfigPath        = bulb.configPath;
						pendingSelectConfigPath = bulb.configPath;
						pendingSelectLighEdid   = teEdid;
						pendingSelectRefrId     = bulb.refrId;
						RestoreOriginal();
						previous = {};
						ScheduleConsoleCommand("reloadlp");
						attachPendingRefr  = pickedMesh.refrHandle;
						attachPhase        = AttachPhase::WaitingForReload;
						attachPhaseStart   = std::chrono::steady_clock::now();
						SavePopupPrefs();
						ImGui::CloseCurrentPopup();
					} else {
						EditorWindow::GetSingleton()->ShowNotification(
							T(TKEY("add_light_failed"), "Failed to add light \xE2\x80\x94 see log"),
							Util::Colors::GetError());
					}
				}
			}
		} // end ModeAddLight

		if (addPopupMode == ModeEditBulb || addPopupMode == ModeWhitelist || addPopupMode == ModeBlacklist) {
			// --- Attached-bulb picker (shared by Edit Bulb / Whitelist / Blacklist) ---
			const char* bulbPreview = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size())
			                              ? attachedBulbs[addSelectedBulb].lightEDID.c_str()
			                              : T(TKEY("select_a_bulb"), "Select a bulb");
			if (ImGui::BeginCombo(T(TKEY("attached_bulb"), "Attached bulb"), bulbPreview, ImGuiComboFlags_HeightLarge)) {
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputText("##bulb_search", addBulbSearch, sizeof(addBulbSearch));
				ImGui::Separator();
				const std::string_view bulbFilter = addBulbSearch;
				for (int i = 0; i < (int)attachedBulbs.size(); ++i) {
					const auto& bulb = attachedBulbs[i];
					const std::string label = fmt::format("{}  ({})", bulb.lightEDID, bulb.configPath);
					if (!bulbFilter.empty() && !Util::StringMatchesSearch(label, std::string(bulbFilter)))
						continue;
					const bool isSel = (i == addSelectedBulb);
					if (ImGui::Selectable(label.c_str(), isSel))
						addSelectedBulb = i;
					if (isSel)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::Separator();
			const bool bulbChosen = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
			const char* confirmLabel =
				addPopupMode == ModeEditBulb  ? T(TKEY("open_in_editor"), "Open in Editor")      :
				addPopupMode == ModeWhitelist ? T(TKEY("add_to_whitelist"), "Add to Whitelist")  :
				                                T(TKEY("add_to_blacklist"), "Add to Blacklist");
			ImGui::BeginDisabled(!bulbChosen);
			const bool confirm = ImGui::Button(confirmLabel);
			ImGui::EndDisabled();

			if (confirm && bulbChosen) {
				const auto& bulb = attachedBulbs[addSelectedBulb];
				if (addPopupMode == ModeEditBulb) {
					pendingSelectRefrId     = bulb.refrId;
					pendingSelectConfigPath = bulb.configPath;
					pendingSelectLighEdid   = bulb.lightEDID;
					pendingAutoSelect       = true;
					pendingAutoSelectTTL    = 10;
					filterOption            = FilterOption::AttachedLights;
					SavePopupPrefs();
					ImGui::CloseCurrentPopup();
				} else {
					MatchContext ctx;
					ctx.ownerModelPath = pickedMesh.modelPath;
					ctx.ownerEditorId  = pickedMesh.editorId;
					ctx.baseFormId     = pickedMesh.baseFormId;
					ctx.lightEDID      = bulb.lightEDID;
					ctx.refr           = pickedMesh.refrHandle.get().get();
					const bool isWhite = (addPopupMode == ModeWhitelist);
					const bool ok = ModifyLPFilterListFor(bulb.configPath, ctx, isWhite, true);
					if (ok) {
						ScheduleConsoleCommand("reloadlp");
						EditorWindow::GetSingleton()->ShowNotification(
							isWhite ? T(TKEY("added_to_whitelist"), "Added to whitelist")
							        : T(TKEY("added_to_blacklist"), "Added to blacklist"),
							Util::Colors::GetSuccess());
					} else {
						EditorWindow::GetSingleton()->ShowNotification(
							T(TKEY("filter_update_failed"), "Filter update failed \xe2\x80\x94 see log"),
							Util::Colors::GetError());
					}
					SavePopupPrefs();
					ImGui::CloseCurrentPopup();
				}
			}
		}

		if (ImGui::Button(T(TKEY("close"), "Close")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				EditorWindow::GetSingleton()->suppressNextEditorEscape = true;
			SavePopupPrefs();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

std::string LightEditor::AddEntryTargetString() const
{
	switch (addAttachMode) {
	case 0:
		return pickedMesh.modelPath;
	case 1:
		if (!pickedMesh.sourcePlugin.empty())
			return fmt::format("0x{:X}~{}", pickedMesh.baseFormId & 0x00FFFFFF, pickedMesh.sourcePlugin);
		return {};
	case 2:
		return pickedMesh.editorId;
	default:
		return {};
	}
}

bool LightEditor::CanAddBulb(std::string& reasonOut) const
{
	if (!pickedMesh.refrHandle || pickedMesh.baseFormId == 0) {
		reasonOut = T(TKEY("no_base_record"), "Picked object has no base record.");
		return false;
	}
	if (addSelectedConfig < 0 || addSelectedConfig >= (int)lpConfigPaths.size()) {
		reasonOut = T(TKEY("choose_target_json"), "Choose a target JSON.");
		return false;
	}
	if (addAttachMode < 0) {
		reasonOut = T(TKEY("choose_attach_type"), "Choose an attach type (Model, FormID, or EditorID).");
		return false;
	}
	if (addSelectedLighFormId == 0) {
		reasonOut = T(TKEY("choose_light_record"), "Choose a light record.");
		return false;
	}
	if (addAttachMode == 0 && pickedMesh.modelPath.empty()) {
		reasonOut = T(TKEY("object_no_model_path"), "This object has no model path.");
		return false;
	}
	if (addAttachMode == 1 && pickedMesh.sourcePlugin.empty()) {
		reasonOut = T(TKEY("object_no_source_plugin"), "This object has no source plugin for a FormID entry.");
		return false;
	}
	if (addAttachMode == 2 && pickedMesh.editorId.empty()) {
		reasonOut = T(TKEY("object_no_editor_id"), "This object has no EditorID.");
		return false;
	}

	// Duplicate check: does the chosen JSON already target this model/FormID with this LIGH?
	const std::string lighEdid = [this]() -> std::string {
		for (auto& [edid, ligh] : s_lighFormList)
			if (ligh->GetFormID() == addSelectedLighFormId)
				return edid;
		return {};
	}();
	if (lighEdid.empty()) {
		reasonOut = T(TKEY("light_no_editor_id"), "Selected light record has no EditorID.");
		return false;
	}

	nlohmann::ordered_json configArray;
	{
		const auto filePath = std::filesystem::path("Data\\LightPlacer") / (lpConfigPaths[addSelectedConfig] + ".json");
		std::ifstream in(filePath);
		if (in.is_open()) {
			try { in >> configArray; } catch (...) { configArray = nlohmann::ordered_json::array(); }
		}
	}
	if (configArray.is_array()) {
		const std::string target = AddEntryTargetString();
		auto normalize = [](std::string s) {
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			std::replace(s.begin(), s.end(), '\\', '/');
			return s;
		};
		const std::string normTarget = normalize(target);
		const char* key = addAttachMode == 0 ? "models" : "formIDs";
		for (const auto& entry : configArray) {
			auto arrIt = entry.find(key);
			if (arrIt == entry.end() || !arrIt->is_array())
				continue;
			bool targetMatch = false;
			for (const auto& v : *arrIt)
				if (v.is_string() && normalize(v.get<std::string>()) == normTarget) { targetMatch = true; break; }
			if (!targetMatch)
				continue;
			auto lightsIt = entry.find("lights");
			if (lightsIt == entry.end() || !lightsIt->is_array())
				continue;
			for (const auto& le : *lightsIt) {
				auto d = le.find("data");
				if (d == le.end() || !d->is_object())
					continue;
				auto l = d->find("light");
				if (l != d->end() && l->is_string() && l->get<std::string>() == lighEdid) {
					reasonOut = T(TKEY("duplicate_entry"), "An entry for this object with this light already exists.");
					return false;
				}
			}
		}
	}

	reasonOut.clear();
	return true;
}

bool LightEditor::AddBulbToConfig()
{
	if (addSelectedConfig < 0 || addSelectedConfig >= (int)lpConfigPaths.size())
		return false;

	const std::string lighEdid = [this]() -> std::string {
		for (auto& [edid, ligh] : s_lighFormList)
			if (ligh->GetFormID() == addSelectedLighFormId)
				return edid;
		return {};
	}();
	if (lighEdid.empty())
		return false;

	const std::string target = AddEntryTargetString();
	if (target.empty())
		return false;

	const auto configPath = lpConfigPaths[addSelectedConfig];
	const auto filePath = std::filesystem::path("Data\\LightPlacer") / (configPath + ".json");

	nlohmann::ordered_json configArray = nlohmann::ordered_json::array();
	{
		std::ifstream in(filePath);
		if (in.is_open()) {
			try {
				in >> configArray;
			} catch (const nlohmann::json::parse_error& e) {
				logger::warn("[LightEditor] Failed to parse {} when adding bulb: {}", filePath.string(), e.what());
				return false;
			}
		}
	}
	if (!configArray.is_array())
		return false;

	nlohmann::ordered_json data;
	data["light"] = lighEdid;
	data["fade"] = 1;
	data["radius"] = 1;
	data["flags"] = "";

	nlohmann::ordered_json light;
	light["data"] = std::move(data);
	light["points"] = nlohmann::ordered_json::array({ nlohmann::ordered_json::array({ 0, 0, 1 }) });

	nlohmann::ordered_json newEntry;
	newEntry[addAttachMode == 0 ? "models" : "formIDs"] = nlohmann::ordered_json::array({ target });
	newEntry["lights"] = nlohmann::ordered_json::array({ std::move(light) });

	configArray.push_back(std::move(newEntry));

	if (!WriteLPConfig(filePath, configArray)) {
		logger::warn("[LightEditor] Failed to write new bulb to {}", filePath.string());
		return false;
	}
	logger::info("[LightEditor] Added bulb '{}' to {} (target '{}')", lighEdid, filePath.string(), target);
	return true;
}

bool LightEditor::AddPointToConfig(const AttachedBulb& bulb)
{
	const auto filePath = std::filesystem::path("Data\\LightPlacer") / (bulb.configPath + ".json");
	nlohmann::ordered_json configArray;
	{
		std::ifstream in(filePath);
		if (!in.is_open()) {
			logger::warn("[LightEditor] AddPointToConfig: cannot open {}", filePath.string());
			return false;
		}
		try { in >> configArray; } catch (const nlohmann::json::parse_error& e) {
			logger::warn("[LightEditor] AddPointToConfig: parse error in {}: {}", filePath.string(), e.what());
			return false;
		}
	}
	if (!configArray.is_array())
		return false;

	MatchContext ctx;
	ctx.ownerModelPath = pickedMesh.modelPath;
	ctx.ownerEditorId  = pickedMesh.editorId;
	ctx.baseFormId     = pickedMesh.baseFormId;
	ctx.lightEDID      = bulb.lightEDID;
	ctx.refr           = pickedMesh.refrHandle.get().get();

	auto* lightEntry = FindMatchingLightEntry(configArray, ctx, false);
	if (!lightEntry) {
		logger::warn("[LightEditor] AddPointToConfig: no matching entry for '{}' in {}",
			bulb.lightEDID, filePath.string());
		return false;
	}

	auto& points = (*lightEntry)["points"];
	if (!points.is_array())
		points = nlohmann::ordered_json::array();
	points.push_back(nlohmann::ordered_json::array({ 0, 0, 1 }));

	if (!WriteLPConfig(filePath, configArray)) {
		logger::warn("[LightEditor] AddPointToConfig: write failed for {}", filePath.string());
		return false;
	}
	logger::info("[LightEditor] AddPointToConfig: added point to '{}' in {}", bulb.lightEDID, filePath.string());
	return true;
}

bool LightEditor::LightAlreadyInEntry(const AttachedBulb& bulb, const std::string& lighEdid) const
{
	const auto filePath = std::filesystem::path("Data\\LightPlacer") / (bulb.configPath + ".json");
	std::ifstream in(filePath);
	if (!in.is_open())
		return false;
	nlohmann::ordered_json configArray;
	try { in >> configArray; } catch (...) { return false; }
	if (!configArray.is_array())
		return false;

	auto normalizePath = [](std::string path) -> std::string {
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	};
	const std::string normModel = normalizePath(pickedMesh.modelPath);

	for (const auto& entry : configArray) {
		bool entryMatches = false;
		if (auto* models = GetArrayMember(entry, "models"); !normModel.empty() && models)
			for (const auto& v : *models)
				if (v.is_string() && normalizePath(v.get<std::string>()) == normModel) { entryMatches = true; break; }
		if (!entryMatches) {
			if (auto* formIDs = GetArrayMember(entry, "formIDs"); formIDs) {
				for (const auto& v : *formIDs) {
					if (!v.is_string()) continue;
					const std::string s = v.get<std::string>();
					const bool hasPrefix = s.starts_with("0x") || s.starts_with("0X");
					if (s.find('~') == std::string::npos && !hasPrefix) {
						if (!pickedMesh.editorId.empty() && s == pickedMesh.editorId) { entryMatches = true; break; }
					} else if (pickedMesh.baseFormId != 0) {
						if (ResolveFormEntry(s) == pickedMesh.baseFormId) { entryMatches = true; break; }
					}
				}
			}
		}
		if (!entryMatches) continue;
		if (auto* lightsArr = GetArrayMember(entry, "lights"))
			for (const auto& le : *lightsArr)
				if (le.contains("data") && le["data"].contains("light") &&
				    le["data"]["light"].is_string() &&
				    le["data"]["light"].get<std::string>() == lighEdid)
					return true;
	}
	return false;
}

bool LightEditor::AddLightToExistingEntry(const AttachedBulb& bulb, const std::string& lighEdid)
{
	const auto filePath = std::filesystem::path("Data\\LightPlacer") / (bulb.configPath + ".json");
	nlohmann::ordered_json configArray;
	{
		std::ifstream in(filePath);
		if (!in.is_open()) {
			logger::warn("[LightEditor] AddLightToExistingEntry: cannot open {}", filePath.string());
			return false;
		}
		try { in >> configArray; } catch (const nlohmann::json::parse_error& e) {
			logger::warn("[LightEditor] AddLightToExistingEntry: parse error in {}: {}", filePath.string(), e.what());
			return false;
		}
	}
	if (!configArray.is_array())
		return false;

	auto normalizePath = [](std::string path) -> std::string {
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	};
	const std::string normModel  = normalizePath(pickedMesh.modelPath);
	const std::string ownerEdid  = pickedMesh.editorId;
	const RE::FormID  baseFormId = pickedMesh.baseFormId;

	nlohmann::ordered_json* parentEntry = nullptr;
	for (auto& entry : configArray) {
		if (auto* models = GetArrayMember(entry, "models"); !normModel.empty() && models)
			for (const auto& v : *models)
				if (v.is_string() && normalizePath(v.get<std::string>()) == normModel) { parentEntry = &entry; break; }
		if (!parentEntry) {
			if (auto* formIDs = GetArrayMember(entry, "formIDs"); formIDs) {
				for (const auto& v : *formIDs) {
					if (!v.is_string()) continue;
					const std::string s = v.get<std::string>();
					const bool hasPrefix = s.starts_with("0x") || s.starts_with("0X");
					if (s.find('~') == std::string::npos && !hasPrefix) {
						if (!ownerEdid.empty() && s == ownerEdid) { parentEntry = &entry; break; }
					} else if (baseFormId != 0) {
						const RE::FormID resolved = ResolveFormEntry(s);
						if (resolved != 0 && resolved == baseFormId) { parentEntry = &entry; break; }
					}
				}
			}
		}
		if (parentEntry) break;
	}

	if (!parentEntry) {
		logger::warn("[LightEditor] AddLightToExistingEntry: no matching top-level entry in {}", filePath.string());
		return false;
	}

	// Duplicate guard (should be checked by UI already, but be safe)
	if (auto* lightsArr = GetArrayMember(*parentEntry, "lights"))
		for (const auto& le : *lightsArr)
			if (le.contains("data") && le["data"].contains("light") &&
			    le["data"]["light"].is_string() &&
			    le["data"]["light"].get<std::string>() == lighEdid)
				return false;

	nlohmann::ordered_json newData;
	newData["light"]  = lighEdid;
	newData["fade"]   = 1;
	newData["radius"] = 1;
	newData["flags"]  = "";

	nlohmann::ordered_json newLight;
	newLight["data"]   = std::move(newData);
	newLight["points"] = nlohmann::ordered_json::array({ nlohmann::ordered_json::array({ 0, 0, 1 }) });

	auto& lightsArr = (*parentEntry)["lights"];
	if (!lightsArr.is_array())
		lightsArr = nlohmann::ordered_json::array();
	lightsArr.push_back(std::move(newLight));

	if (!WriteLPConfig(filePath, configArray)) {
		logger::warn("[LightEditor] AddLightToExistingEntry: write failed for {}", filePath.string());
		return false;
	}
	logger::info("[LightEditor] AddLightToExistingEntry: added '{}' to existing entry in {}", lighEdid, filePath.string());
	return true;
}

bool LightEditor::HasShadowFlags(uint32_t tesFlags)
{
	return (tesFlags & (static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow) |
	                    static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow) |
	                    static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kSpotShadow))) != 0;
}

std::string LightEditor::GetLightName(LightInfo& lightInfo)
{
	if (lightInfo.isRef)
		return fmt::format("0x{:08X} - {}", lightInfo.id, lightInfo.name.c_str());
	if (lightInfo.isAttached)
		return fmt::format("0x{:08X}|{} - {}", lightInfo.id, lightInfo.index, lightInfo.name.c_str());
	return fmt::format("{:p} - {}", lightInfo.ptr, lightInfo.name.c_str());
}

void LightEditor::GatherLights()
{
	// Attach-to-mesh timed flow: runs unconditionally every frame so the timer is not
	// gated behind ShouldSwallowInput (menu focus can be lost when the popup closes).
	// Each step waits kAttachStepDelay (500ms): disable, then enable, then finalize.
	if (attachPhase != AttachPhase::Idle) {
		const auto now = std::chrono::steady_clock::now();
		if (now - attachPhaseStart >= kAttachStepDelay) {
			attachPhaseStart = now;
			switch (attachPhase) {
			case AttachPhase::WaitingForReload:
				if (auto refr = attachPendingRefr.get())
					ScheduleConsoleCommand("disable", refr.get());
				attachPhase = AttachPhase::WaitingForEnable;
				break;
			case AttachPhase::WaitingForEnable:
				if (auto refr = attachPendingRefr.get())
					ScheduleConsoleCommand("enable", refr.get());
				attachPendingRefr = {};
				attachPhase = AttachPhase::WaitingForRespawn;
				break;
			case AttachPhase::WaitingForRespawn:
				attachPhase = AttachPhase::Idle;
				waitFrames = 3;
				pendingAutoSelect = true;
				pendingAutoSelectTTL = 10;
				filterOption = FilterOption::AttachedLights;
				EditorWindow::GetSingleton()->ShowNotification(
					I18n::GetSingleton()->Format(TKEY("added_light_to_config"), {{"path", attachConfigPath}}, "Added light to {path}").c_str(),
					Util::Colors::GetSuccess());
				break;
			default:
				break;
			}
		}
	}

	if (!Menu::GetSingleton()->ShouldSwallowInput()) {
		ResetOverrides();
		return;
	}

	if (!selected.isSelected && savedSelection.isSelected) {
		selected = savedSelection;
		savedSelection = {};
	}

	picker.Update();
	if (auto hit = picker.TakeResult(); hit.valid) {
		pickedMesh = hit;
		GatherAttachedBulbs(pickedMesh.refrHandle.get().get());
		lpConfigPaths = ScanLPConfigPaths();
		addSelectedConfig = -1;
		addAttachMode = -1;
		addSelectedLighFormId = 0;
		addPopupMode = -1;
		addLightSubMode = -1;
		addLightPopupOpen = true;
	}

	// Skip a few frames after disruptive operations (reloadlp, Disable/Enable, position change)
	// so the game has time to rebuild the light list before we resample it.
	if (waitFrames > 0) {
		waitFrames--;
		return;
	}

	bool foundSelected = false;

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& light) {
		const auto bsLight = light.get();
		if (!bsLight)
			return;

		const auto niLight = bsLight->light.get();
		if (!niLight)
			return;

		LightInfo current;
		RE::TESObjectLIGH* ligh = nullptr;

		const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
		const auto refr = niLight->GetUserData();
		if (refr) {
			if (refr->IsDisabled())
				return;
			if (auto* objRef = refr->GetObjectReference()) {
				if (objRef->GetFormType() == RE::FormType::Light)
					ligh = objRef->As<RE::TESObjectLIGH>();
				current.id = refr->GetFormID();
				current.name = clib_util::editorID::get_editorID(objRef);
				current.index = lightsAttached[refr]++;
			}
		}

		current.isRef = ligh != nullptr;

		if (!current.isRef && runtimeData->lighFormId != 0) {
			if (auto* lighForm = RE::TESForm::LookupByID(runtimeData->lighFormId))
				ligh = lighForm->As<RE::TESObjectLIGH>();
		}

		current.isSpotlight = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow);
		const bool isShadow = ligh && HasShadowFlags(ligh->data.flags.underlying());

		totalLightCount++;
		if (isShadow)
			activeShadowLightCount++;

		if ((shadowsOnly) && (!ligh || !isShadow)) {
			return;
		}

		current.isAttached = !current.isRef && refr != nullptr;
		current.isOther = (!current.isRef && !current.isAttached) || (current.isSpotlight);

		const bool isRefMatch = (current.isRef && !current.isSpotlight) && filterOption == FilterOption::RefLights;
		const bool isAttachedMatch = current.isAttached && filterOption == FilterOption::AttachedLights;
		const bool isOtherMatch = current.isOther && filterOption == FilterOption::OtherLights;

		if (!(isRefMatch || isAttachedMatch || isOtherMatch))
			return;

		if (current.isRef) {
			current.position = refr->GetPosition();
			current.hasPosition = true;
		} else if (niLight->parent) {
			current.position = niLight->parent->world.translate;
			current.hasPosition = true;
		}
		if (current.isOther) {
			current.ptr = reinterpret_cast<void*>(niLight);
			if (current.name.empty())
				current.name = niLight->name.c_str();
			current.index = 0;
		}

		if (pendingAutoSelect && current.isAttached && current.id == pendingSelectRefrId) {
			const auto parsedName = ParseLPLightName(niLight->name.c_str());
			if (parsedName.isLPLight && parsedName.configPath == pendingSelectConfigPath && parsedName.lightEDID == pendingSelectLighEdid) {
				selected = current;
				pendingAutoSelect = false;
			}
		}

		current.isSelected = selected == current;

		lights.push_back(current);

		// Capture the NiLight for hover-flash on the first frame this light is hovered.
		if (comboHoveredLight.id != 0 && current == comboHoveredLight && !hoverFlashNiLight) {
			hoverFlashNiLight.reset(niLight);
			const auto* rd = ISLCommon::RuntimeLightDataExt::Get(niLight);
			hoverFlashOriginalFade = (rd && rd->fade > 0.f) ? rd->fade : 1.f;
		}

		if (!current.isSelected)
			return;
		selected = current;
		foundSelected = true;
		UpdateSelectedLight(refr, ligh, niLight, bsLight);
	};

	lights.clear();
	lightsAttached.clear();
	totalLightCount = 0;
	activeShadowLightCount = 0;
	const auto smState = globals::game::smState;
	const auto shadowSceneNode = smState->shadowSceneNode[0];

	const auto& activeLights = shadowSceneNode->GetRuntimeData().activeLights;

	for (auto& light : activeLights) {
		addLight(light);
	}

	const auto& activeShadowLights = shadowSceneNode->GetRuntimeData().activeShadowLights;

	for (auto& light : activeShadowLights) {
		addLight(light);
	}

	if (!foundSelected) {
		RestoreOriginal();
		previous = {};
		selected = {};
	}

	SortLights();

	if (pendingAutoSelect && --pendingAutoSelectTTL <= 0)
		pendingAutoSelect = false;
}

void LightEditor::GatherAttachedBulbs(RE::TESObjectREFR* refr)
{
	attachedBulbs.clear();
	addSelectedBulb = -1;
	addBulbSearch[0] = '\0';
	if (!refr)
		return;

	const auto smState = globals::game::smState;
	const auto shadowSceneNode = smState->shadowSceneNode[0];
	std::unordered_map<RE::TESObjectREFR*, uint32_t> running;

	auto collect = [&](const RE::NiPointer<RE::BSLight>& light) {
		auto* bsLight = light.get();
		if (!bsLight)
			return;
		auto* niLight = bsLight->light.get();
		if (!niLight)
			return;
		auto* owner = niLight->GetUserData();
		if (owner != refr)
			return;
		const auto parsed = ParseLPLightName(niLight->name.c_str());
		if (!parsed.isLPLight)
			return;
		AttachedBulb bulb;
		bulb.lightEDID  = parsed.lightEDID;
		bulb.configPath = parsed.configPath;
		bulb.refrId     = refr->GetFormID();
		bulb.index      = running[owner]++;
		attachedBulbs.push_back(std::move(bulb));
	};

	for (auto& light : shadowSceneNode->GetRuntimeData().activeLights)
		collect(light);
	for (auto& light : shadowSceneNode->GetRuntimeData().activeShadowLights)
		collect(light);
}

#undef I18N_KEY_PREFIX

void LightEditor::ResetOverrides()
{
	if (selected.isSelected)
		savedSelection = selected;
	RestoreOriginal();
	if (hoverFlashNiLight) {
		if (auto* rd = ISLCommon::RuntimeLightDataExt::Get(hoverFlashNiLight.get()))
			rd->fade = hoverFlashOriginalFade;
		hoverFlashNiLight.reset();
	}
	comboHoveredLight = {};
	selected = {};
	previous = {};
}

void LightEditor::ApplyShadowDepthBias()
{
	if (auto* shadowLight = AsShadowLight(activeBsLight.get()))
		shadowLight->GetRuntimeData().shadowBiasScale = shadowDepthBias;
}

void LightEditor::UpdateSelectedLight(RE::TESObjectREFR* refr, RE::TESObjectLIGH* ligh, RE::NiLight* niLight, RE::BSLight* bsLight)
{
	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	auto tesFlags = ligh ? &ligh->data.flags : nullptr;

	// Per-selection initialization: snapshots the light's original state, populates lpInfo,
	// and runs a dry-run save to determine whether a matching LP JSON entry exists.
	if (previous != selected) {
		RestoreOriginal();

		original.tesFlags = tesFlags ? static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(tesFlags->underlying()) : static_cast<ISLCommon::TES_LIGHT_FLAGS_EXT>(0);
		original.data = *runtimeData;
		original.pos = selected.isRef ? refr->GetPosition() : (niLight->parent ? niLight->parent->local.translate : RE::NiPoint3{});

		current = original;

		auto* originalShadowLight = AsShadowLight(bsLight);
		originalShadowDepthBias = originalShadowLight ? originalShadowLight->GetRuntimeData().shadowBiasScale : 0.0f;
		shadowDepthBias = originalShadowDepthBias;
		cachedFadeBeforeToggle = 0.0f;

		lpInfo = selected.isAttached ? ParseLPLightName(niLight->name.c_str()) : LPLightInfo{};
		if (lpInfo.isLPLight && refr) {
			if (auto* baseObj = refr->GetObjectReference()) {
				lpInfo.ownerEditorId = clib_util::editorID::get_editorID(baseObj);
				if (auto* model = baseObj->As<RE::TESModel>()) {
					if (const char* path = model->GetModel())
						lpInfo.ownerModelPath = path;
				}
			}
		}
		activeIsRef = selected.isRef;
		activeRefr = refr;
		activeLigh = ligh;

		lpMatchFound = lpInfo.isLPLight && SaveToLightPlacer(false, true);
		if (lpInfo.isLPLight) {
			RefreshLPJsonState();
			originalLpFlagSet = lpFlagSet;
		}

		externalEmittanceEdid = {};
		useExternalEmittance = false;
		if (refr) {
			if (const auto* extra = refr->extraList.GetByType<RE::ExtraEmittanceSource>())
				if (extra->source)
					externalEmittanceEdid = clib_util::editorID::get_editorID(extra->source);
			useExternalEmittance = !externalEmittanceEdid.empty();
		}

		previous = selected;
	}

	activeNiLight.reset(niLight);
	activeBsLight.reset(bsLight);

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		const bool isShadow = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kHemiShadow, RE::TES_LIGHT_FLAGS::kOmniShadow);
		current.data.radius = InverseSquareLighting::CalculateRadius(
			current.data.fade * 4.f, isShadow,
			std::clamp(current.data.cutoffOverride, 0.01f, 1.0f),
			std::clamp(current.data.size, 0.1f, 50.0f));
	}

	if (selected.isRef) {
		const auto currentPos = refr->GetPosition();
		if (currentPos != current.pos) {
			refr->SetPosition(current.pos);
			waitFrames = 1;
		}
		displayInfo.pos = current.pos;
	} else if (selected.isAttached) {
		if (niLight->parent) {
			const auto currentPos = niLight->parent->local.translate;
			if (currentPos != current.pos) {
				niLight->parent->local.translate = current.pos;
				RE::NiUpdateData updateData;
				niLight->parent->Update(updateData);
				waitFrames = 1;
			}
			displayInfo.pos = current.pos;
		} else {
			displayInfo.pos = {};
		}
	}

	if (!selected.isOther && refr && tesFlags && current.tesFlags.underlying() != tesFlags->underlying()) {
		*tesFlags = static_cast<RE::TES_LIGHT_FLAGS>(current.tesFlags.underlying());
		refr->Disable();
		refr->Enable(false);
		waitFrames = 1;
	}

	displayInfo.ownerFormId = refr ? refr->GetFormID() : 0;
	displayInfo.ownerEditorId = refr ? clib_util::editorID::get_editorID(refr) : "Unknown";
	displayInfo.baseObjectFormId = refr && refr->GetBaseObject() ? refr->GetBaseObject()->formID : 0;
	displayInfo.ownerLastEditedBy = refr && refr->GetDescriptionOwnerFile() ? refr->GetDescriptionOwnerFile()->fileName : "Unknown";
	displayInfo.cellFormId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormID() : 0;
	displayInfo.cellEditorId = refr && refr->GetParentCell() ? refr->GetParentCell()->GetFormEditorID() : "Unknown";
	displayInfo.lighFormId = ligh ? ligh->GetFormID() : 0;
	displayInfo.lighEditorId = ligh ? clib_util::editorID::get_editorID(ligh) : "Unknown";
}

bool LightEditor::ApplyOverrides(RE::NiLight* niLight, ISLCommon::RuntimeLightDataExt* runtimeData) const
{
	if (hoverFlashNiLight && niLight == hoverFlashNiLight.get() && niLight != activeNiLight.get()) {
		runtimeData->fade = hoverFlashVisible ? hoverFlashOriginalFade : 0.f;
		return true;
	}

	if (niLight != activeNiLight.get())
		return false;

	runtimeData->lighFormId = current.data.lighFormId;
	runtimeData->diffuse = current.data.diffuse;
	runtimeData->fade = current.data.fade;
	runtimeData->cutoffOverride = current.data.cutoffOverride;
	runtimeData->size = current.data.size;

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare))
		runtimeData->flags.set(LightLimitFix::LightFlags::InverseSquare);
	else
		runtimeData->flags.reset(LightLimitFix::LightFlags::InverseSquare);

	if (current.data.flags.any(LightLimitFix::LightFlags::Linear))
		runtimeData->flags.set(LightLimitFix::LightFlags::Linear);
	else
		runtimeData->flags.reset(LightLimitFix::LightFlags::Linear);

	return true;
}

void LightEditor::RestoreOriginal()
{
	if (!activeNiLight)
		return;

	auto* runtimeData = ISLCommon::RuntimeLightDataExt::Get(activeNiLight.get());
	*runtimeData = original.data;

	if (activeIsRef && activeRefr) {
		activeRefr->SetPosition(original.pos);
	} else if (activeNiLight->parent) {
		activeNiLight->parent->local.translate = original.pos;
		RE::NiUpdateData updateData;
		activeNiLight->parent->Update(updateData);
	}

	if (activeLigh && activeRefr && current.tesFlags.underlying() != original.tesFlags.underlying()) {
		activeLigh->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(original.tesFlags.underlying());
		activeRefr->Disable();
		activeRefr->Enable(false);
	}

	if (auto* shadowLight = AsShadowLight(activeBsLight.get()))
		shadowLight->GetRuntimeData().shadowBiasScale = originalShadowDepthBias;

	activeNiLight.reset();
	activeBsLight.reset();
	activeRefr = nullptr;
	activeLigh = nullptr;
	activeIsRef = false;
}

LightEditor::LPLightInfo LightEditor::ParseLPLightName(const std::string& name)
{
	LPLightInfo info;

	constexpr std::string_view prefix = "LP_Light[";
	if (!name.starts_with(prefix))
		return info;

	auto bracketEnd = name.find(']');
	if (bracketEnd == std::string::npos)
		return info;

	auto inner = name.substr(prefix.size(), bracketEnd - prefix.size());
	auto pipePos = inner.find('|');
	if (pipePos == std::string::npos)
		return info;

	info.configPath = inner.substr(0, pipePos);
	info.lightEDID = inner.substr(pipePos + 1);

	if (info.configPath.find("..") != std::string::npos) {
		logger::warn("[LightEditor] Rejected LP light name with path traversal: {}", name);
		return info;
	}

	info.isLPLight = true;
	return info;
}

LightEditor::MatchContext LightEditor::MakeSelectedContext() const
{
	MatchContext ctx;
	ctx.ownerModelPath = lpInfo.ownerModelPath;
	ctx.ownerEditorId  = lpInfo.ownerEditorId;
	ctx.baseFormId     = (activeRefr && activeRefr->GetObjectReference()) ? activeRefr->GetObjectReference()->formID : 0;
	ctx.lightEDID      = lpInfo.lightEDID;
	ctx.refr           = activeRefr;
	return ctx;
}

bool LightEditor::MatchesLPFilters(const nlohmann::ordered_json& lightEntry, RE::TESObjectREFR* refr)
{
	if (!refr)
		return true;

	auto matchesEntry = [&](const std::string& entry) -> bool {
		const bool hasPrefix = entry.starts_with("0x") || entry.starts_with("0X");
		if (entry.find('~') != std::string::npos || hasPrefix) {
			const RE::FormID resolvedId = ResolveFormEntry(entry);
			return resolvedId != 0 && resolvedId == refr->GetFormID();
		}
		if (auto* cell = refr->GetParentCell())
			if (entry == cell->GetFormEditorID())
				return true;
		if (auto* worldspace = refr->GetWorldspace()) {
			auto wsEdid = clib_util::editorID::get_editorID(worldspace);
			if (entry == wsEdid)
				return true;
		}
		return false;
	};

	auto anyMatches = [&](const nlohmann::ordered_json& list) {
		for (const auto& item : list)
			if (item.is_string() && matchesEntry(item.get<std::string>()))
				return true;
		return false;
	};

	if (auto* wl = GetArrayMember(lightEntry, "whiteList"); wl && !anyMatches(*wl))
		return false;
	if (auto* bl = GetArrayMember(lightEntry, "blackList"); bl && anyMatches(*bl))
		return false;

	return true;
}

bool LightEditor::LoadLPConfig(nlohmann::ordered_json& out) const
{
	const auto filePath = std::filesystem::path("Data\\LightPlacer") / (lpInfo.configPath + ".json");
	if (!std::filesystem::exists(filePath)) {
		logger::warn("[LightEditor] Light Placer config not found: {}", filePath.string());
		return false;
	}
	std::ifstream inFile(filePath);
	if (!inFile.is_open()) {
		logger::warn("[LightEditor] Failed to open Light Placer config: {}", filePath.string());
		return false;
	}
	try {
		inFile >> out;
	} catch (const nlohmann::json::parse_error& e) {
		logger::warn("[LightEditor] Failed to parse Light Placer config: {} - {}", filePath.string(), e.what());
		return false;
	}
	return out.is_array();
}

nlohmann::ordered_json* LightEditor::FindMatchingLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, bool applyFilters)
{
	auto normalizePath = [](std::string path) -> std::string {
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	};
	auto arrayContainsString = [](const nlohmann::ordered_json& arr, const std::function<bool(const std::string&)>& pred) -> bool {
		for (const auto& elem : arr)
			if (elem.is_string() && pred(elem.get<std::string>()))
				return true;
		return false;
	};
	const std::string normalizedOwner = normalizePath(ctx.ownerModelPath);

	for (auto& entry : configArray) {
		auto lightsIt = entry.find("lights");
		if (lightsIt == entry.end() || !lightsIt->is_array())
			continue;

		bool entryMatches = false;
		if (auto* models = GetArrayMember(entry, "models"); !normalizedOwner.empty() && models)
			entryMatches = arrayContainsString(*models, [&](const std::string& s) { return normalizePath(s) == normalizedOwner; });
		if (!entryMatches) {
			if (auto* formIDs = GetArrayMember(entry, "formIDs"); formIDs) {
				entryMatches = arrayContainsString(*formIDs, [&](const std::string& s) -> bool {
					const bool hasPrefix = s.starts_with("0x") || s.starts_with("0X");
					if (s.find('~') == std::string::npos && !hasPrefix)
						return !ctx.ownerEditorId.empty() && s == ctx.ownerEditorId;
					if (ctx.baseFormId == 0)
						return false;
					const RE::FormID resolved = ResolveFormEntry(s);
					return resolved != 0 && resolved == ctx.baseFormId;
				});
			}
		}

		if (!entryMatches)
			continue;

		for (auto& lightEntry : entry["lights"]) {
			if (!lightEntry.contains("data"))
				continue;
			auto& data = lightEntry["data"];
			if (!data.contains("light") || !data["light"].is_string())
				continue;
			if (data["light"].get<std::string>() != ctx.lightEDID)
				continue;
			if (applyFilters && !MatchesLPFilters(lightEntry, ctx.refr))
				continue;
			return &lightEntry;
		}
	}
	return nullptr;
}

bool LightEditor::SaveToLightPlacer(bool includeColor, bool dryRun)
{
	if (!lpInfo.isLPLight)
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadLPConfig(configArray))
		return false;

	auto* matchedEntry = FindMatchingLightEntry(configArray, MakeSelectedContext());
	if (!matchedEntry) {
		logger::warn("[LightEditor] No matching entry found for model '{}' with light EDID '{}' in {}.json",
			lpInfo.ownerModelPath, lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	if (dryRun)
		return true;

	static constexpr std::array managedDataKeys = {
		"color", "light", "fade", "radius", "size", "cutoff", "shadowDepthBias", "externalEmittance", "flags", "offset", "rotation"
	};
	static constexpr std::array managedEntryKeys = { "data", "points", "nodes", "whiteList", "blackList" };

	auto& data = (*matchedEntry)["data"];

	const bool isInvSq = lpFlagSet.contains("InverseSquare");
	std::string newFlags;
	for (const auto& flag : lpFlagSet) {
		if (!newFlags.empty()) newFlags += "|";
		newFlags += flag;
	}

	nlohmann::ordered_json newData;
	if (includeColor || data.contains("color"))
		newData["color"] = { current.data.diffuse.red, current.data.diffuse.green, current.data.diffuse.blue };
	newData["light"] = data["light"];
	newData["fade"] = current.data.fade;
	if (isInvSq) {
		newData["size"] = current.data.size;
		newData["cutoff"] = current.data.cutoffOverride;
	} else {
		newData["radius"] = current.data.radius;
	}
	if (data.contains("shadowDepthBias"))
		newData["shadowDepthBias"] = shadowDepthBias;
	if (useExternalEmittance && !externalEmittanceEdid.empty())
		newData["externalEmittance"] = externalEmittanceEdid;
	else if (!useExternalEmittance && data.contains("externalEmittance"))
		newData["externalEmittance"] = data["externalEmittance"];
	if (!newFlags.empty())
		newData["flags"] = newFlags;
	if (data.contains("offset"))
		newData["offset"] = data["offset"];
	if (data.contains("rotation"))
		newData["rotation"] = data["rotation"];
	for (auto& [key, val] : data.items())
		if (std::ranges::find(managedDataKeys, key) == managedDataKeys.end())
			newData[key] = val;
	data = std::move(newData);

	const char* pointsKey = matchedEntry->contains("points") ? "points" : (matchedEntry->contains("nodes") ? "nodes" : nullptr);
	if (pointsKey) {
		auto& pts = (*matchedEntry)[pointsKey];
		if (pts.is_array() && !pts.empty() && pts[0].is_array() && pts[0].size() >= 3)
			pts[0] = nlohmann::ordered_json::array({ static_cast<int>(current.pos.x), static_cast<int>(current.pos.y), static_cast<int>(current.pos.z) });
	}

	// Normalise key order for every data and lightEntry in the file so the whole config is consistent.
	auto normalizeData = [](nlohmann::ordered_json& d) {
		nlohmann::ordered_json nd;
		if (d.contains("color"))             nd["color"]             = d["color"];
		if (d.contains("light"))             nd["light"]             = d["light"];
		if (d.contains("fade"))              nd["fade"]              = d["fade"];
		if (d.contains("radius"))            nd["radius"]            = d["radius"];
		if (d.contains("size"))              nd["size"]              = d["size"];
		if (d.contains("cutoff"))            nd["cutoff"]            = d["cutoff"];
		if (d.contains("shadowDepthBias"))   nd["shadowDepthBias"]   = d["shadowDepthBias"];
		if (d.contains("externalEmittance")) nd["externalEmittance"] = d["externalEmittance"];
		if (d.contains("flags"))             nd["flags"]             = d["flags"];
		if (d.contains("offset"))            nd["offset"]            = d["offset"];
		if (d.contains("rotation"))          nd["rotation"]          = d["rotation"];
		for (auto& [key, val] : d.items())
			if (std::ranges::find(managedDataKeys, key) == managedDataKeys.end())
				nd[key] = val;
		d = std::move(nd);
	};

	auto normalizeLightEntry = [&normalizeData](nlohmann::ordered_json& le) {
		if (le.contains("data"))
			normalizeData(le["data"]);
		nlohmann::ordered_json newEntry;
		if (le.contains("data"))       newEntry["data"]      = le["data"];
		if (le.contains("points"))     newEntry["points"]    = le["points"];
		else if (le.contains("nodes")) newEntry["nodes"]     = le["nodes"];
		if (le.contains("whiteList"))  newEntry["whiteList"] = le["whiteList"];
		if (le.contains("blackList"))  newEntry["blackList"] = le["blackList"];
		for (auto& [key, val] : le.items())
			if (std::ranges::find(managedEntryKeys, key) == managedEntryKeys.end())
				newEntry[key] = val;
		le = std::move(newEntry);
	};

	for (auto& topEntry : configArray) {
		auto lightsIt = topEntry.find("lights");
		if (lightsIt == topEntry.end() || !lightsIt->is_array())
			continue;
		for (auto& le : *lightsIt)
			normalizeLightEntry(le);
	}

	const auto filePath = std::filesystem::path("Data\\LightPlacer") / (lpInfo.configPath + ".json");
	if (!WriteLPConfig(filePath, configArray))
		return false;

	original.pos = current.pos;
	logger::info("[LightEditor] Saved light settings to {}", filePath.string());
	return true;
}

void LightEditor::SortLights()
{
	if (filterOption == FilterOption::OtherLights && (sortOption == SortOption::FormID || sortOption == SortOption::EditorID))
		sortOption = SortOption::None;

	switch (sortOption) {
	case SortOption::Distance:
		{
			const auto playerPos = RE::PlayerCharacter::GetSingleton()->GetPosition();
			std::ranges::sort(lights, [&](const LightInfo& a, const LightInfo& b) {
				if (a.hasPosition != b.hasPosition)
					return a.hasPosition;
				return a.position.GetSquaredDistance(playerPos) < b.position.GetSquaredDistance(playerPos);
			});
			break;
		}
	case SortOption::FormID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return std::tie(a.id, a.index) < std::tie(b.id, b.index);
		});
		break;
	case SortOption::EditorID:
		std::ranges::sort(lights, [](const LightInfo& a, const LightInfo& b) {
			return a.name < b.name;
		});
		break;
	case SortOption::None:
	default:
		break;
	}
}

std::string LightEditor::FormatOwnerFormEntry(RE::TESObjectREFR* refr)
{
	if (!refr)
		return {};
	const auto* ownerFile = refr->GetDescriptionOwnerFile();
	if (!ownerFile || !ownerFile->fileName)
		return {};
	const RE::FormID relativeId = refr->formID & 0x00FFFFFF;
	return fmt::format("0x{:X}~{}", relativeId, ownerFile->fileName);
}

void LightEditor::RefreshLPJsonState()
{
	lpInWhitelist = false;
	lpInBlacklist = false;
	lpFlagSet.clear();
	if (!lpInfo.isLPLight || !activeRefr)
		return;

	const std::string ownerEntry = FormatOwnerFormEntry(activeRefr);

	nlohmann::ordered_json configArray;
	if (!LoadLPConfig(configArray))
		return;

	const auto* lightEntry = FindMatchingLightEntry(configArray, MakeSelectedContext(), false);
	if (!lightEntry)
		return;

	// Filter state
	if (!ownerEntry.empty()) {
		auto containsEntry = [&](const char* listKey) {
			const auto it = lightEntry->find(listKey);
			if (it == lightEntry->end() || !it->is_array())
				return false;
			for (const auto& elem : *it)
				if (elem.is_string() && elem.get<std::string>() == ownerEntry)
					return true;
			return false;
		};
		lpInWhitelist = containsEntry("whiteList");
		lpInBlacklist = containsEntry("blackList");
	}

	// LP flags
	const auto dataIt = lightEntry->find("data");
	if (dataIt != lightEntry->end() && dataIt->is_object()) {
		const auto flagsIt = dataIt->find("flags");
		if (flagsIt != dataIt->end() && flagsIt->is_string()) {
			std::istringstream ss(flagsIt->get<std::string>());
			std::string flag;
			while (std::getline(ss, flag, '|')) {
				if (!flag.empty())
					lpFlagSet.insert(flag);
			}
		}
	}

	SyncLPFlagsToRuntime();
}

void LightEditor::SyncLPFlagsToRuntime()
{
	if (!lpInfo.isLPLight)
		return;

	if (lpFlagSet.contains("InverseSquare"))
		current.data.flags.set(LightLimitFix::LightFlags::InverseSquare);
	else
		current.data.flags.reset(LightLimitFix::LightFlags::InverseSquare);

	if (lpFlagSet.contains("Linear"))
		current.data.flags.set(LightLimitFix::LightFlags::Linear);
	else
		current.data.flags.reset(LightLimitFix::LightFlags::Linear);

	auto& tesUnderlying = reinterpret_cast<uint32_t&>(current.tesFlags);
	auto syncTesBit = [&](RE::TES_LIGHT_FLAGS bit, bool val) {
		const auto mask = static_cast<uint32_t>(bit);
		if (val) tesUnderlying |= mask;
		else     tesUnderlying &= ~mask;
	};
	syncTesBit(RE::TES_LIGHT_FLAGS::kFlicker,      lpFlagSet.contains("Flicker"));
	syncTesBit(RE::TES_LIGHT_FLAGS::kOmniShadow,   lpFlagSet.contains("Shadow"));
	syncTesBit(RE::TES_LIGHT_FLAGS::kPortalStrict,  lpFlagSet.contains("PortalStrict"));
}

void LightEditor::MutateFilterList(nlohmann::ordered_json& lightEntry, const char* listKey, const std::string& ownerEntry, bool add)
{
	auto& list = lightEntry[listKey];
	if (add) {
		if (!list.is_array())
			list = nlohmann::ordered_json::array();
		for (const auto& elem : list)
			if (elem.is_string() && elem.get<std::string>() == ownerEntry)
				return;
		list.push_back(ownerEntry);
	} else {
		if (!list.is_array())
			return;
		list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& elem) {
			return elem.is_string() && elem.template get<std::string>() == ownerEntry;
		}), list.end());
		if (list.empty())
			lightEntry.erase(listKey);
	}
}

bool LightEditor::ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, bool isWhiteList, bool add)
{
	if (!ctx.refr)
		return false;
	const std::string ownerEntry = FormatOwnerFormEntry(ctx.refr);
	if (ownerEntry.empty())
		return false;

	const auto filePath = std::filesystem::path("Data\\LightPlacer") / (configPath + ".json");
	nlohmann::ordered_json configArray;
	{
		std::ifstream in(filePath);
		if (!in.is_open())
			return false;
		try {
			in >> configArray;
		} catch (const nlohmann::json::parse_error&) {
			return false;
		}
	}
	if (!configArray.is_array())
		return false;

	auto* lightEntry = FindMatchingLightEntry(configArray, ctx, false);
	if (!lightEntry)
		return false;

	MutateFilterList(*lightEntry, isWhiteList ? "whiteList" : "blackList", ownerEntry, add);
	return WriteLPConfig(filePath, configArray);
}

bool LightEditor::ModifyLPFilterList(bool isWhiteList, bool add)
{
	if (!lpInfo.isLPLight || !activeRefr)
		return false;
	return ModifyLPFilterListFor(lpInfo.configPath, MakeSelectedContext(), isWhiteList, add);
}
