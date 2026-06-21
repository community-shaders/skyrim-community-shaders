#include "LightEditor.h"
#include "../Features/InverseSquareLighting.h"
#include "../Features/LightLimitFix.h"
#include "../I18n/I18n.h"
#include "../Menu.h"
#include "../Utils/UI.h"
#include "EditorWindow.h"
#include "imgui_internal.h"
#include "RE/B/BSLight.h"
#include "RE/B/BSShadowLight.h"
#include "RE/E/ExtraEmittanceSource.h"
#include "RE/T/TESRegion.h"
#include "WeatherUtils.h"

#define I18N_KEY_PREFIX "feature.light_editor."

#include <array>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <regex>
#include <sstream>

std::vector<std::pair<std::string, RE::TESObjectLIGH*>> LightEditor::s_lighFormList;
std::vector<std::pair<std::string, RE::TESForm*>> LightEditor::s_emittanceFormList;

// Data/light-entry keys the editor authors itself, in canonical write order. Keys outside
// these lists are preserved verbatim (carried over after the managed ones).
static constexpr std::array kManagedDataKeys = {
	"color", "light", "fade", "radius", "size", "cutoff", "shadowDepthBias", "externalEmittance", "flags", "offset", "rotation"
};
static constexpr std::array kManagedEntryKeys = { "data", "points", "nodes", "whiteList", "blackList" };

/** @brief Returns the named array member of a JSON object, or nullptr if missing or not an array. */
static const nlohmann::ordered_json* GetArrayMember(const nlohmann::ordered_json& obj, const char* key)
{
	const auto it = obj.find(key);
	return (it != obj.end() && it->is_array()) ? &*it : nullptr;
}

/** @brief Returns a light entry's "data"/"light" EDID string, or nullptr if absent or not a string. */
static const std::string* GetLightEntryEdid(const nlohmann::ordered_json& lightEntry)
{
	const auto dataIt = lightEntry.find("data");
	if (dataIt == lightEntry.end() || !dataIt->is_object())
		return nullptr;
	const auto lightIt = dataIt->find("light");
	if (lightIt == dataIt->end() || !lightIt->is_string())
		return nullptr;
	return &lightIt->get_ref<const std::string&>();
}

/** @brief True if the JSON array holds the given string value. */
static bool ArrayContainsString(const nlohmann::ordered_json& arr, std::string_view value)
{
	for (const auto& elem : arr)
		if (elem.is_string() && std::string_view(elem.get_ref<const std::string&>()) == value)
			return true;
	return false;
}

/** @brief True if a string carries a "0x"/"0X" hex prefix. */
static bool HasHexPrefix(std::string_view s)
{
	return s.starts_with("0x") || s.starts_with("0X");
}

/** @brief Downcasts a BSLight to BSShadowLight only when it actually is one, else nullptr. */
static RE::BSShadowLight* AsShadowLight(RE::BSLight* light)
{
	return (light && light->IsShadowLight()) ? static_cast<RE::BSShadowLight*>(light) : nullptr;
}

/** @brief Schedules a console command on the task thread, optionally against a selected reference. */
static void ScheduleConsoleCommand(std::string cmd, RE::TESObjectREFR* refr = nullptr)
{
	if (auto* taskInterface = SKSE::GetTaskInterface()) {
		taskInterface->AddTask([cmd = std::move(cmd), refr]() {
			// Console::SetSelectedRef needs a live Console (UI open only), so write the selected-ref global
			// directly (RELOCATION_ID = Console::GetSelectedRefHandle); refr is passed as thisObj for handlers.
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

// Light Placer's JSON parser rejects nulls (e.g. a stray "whiteList": null), so strip every null
// member/element before persisting; WriteLPConfig is the single guarantee we never write one.
static void StripNullValues(nlohmann::ordered_json& node)
{
	if (node.is_object()) {
		for (auto it = node.begin(); it != node.end();) {
			if (it.value().is_null()) {
				it = node.erase(it);
			} else {
				StripNullValues(it.value());
				++it;
			}
		}
	} else if (node.is_array()) {
		for (auto it = node.begin(); it != node.end();) {
			if (it->is_null()) {
				it = node.erase(it);
			} else {
				StripNullValues(*it);
				++it;
			}
		}
	}
}

/** @brief Writes an LP config to disk, stripping nulls and compacting vec3/float formatting. */
static bool WriteLPConfig(const std::filesystem::path& filePath, nlohmann::ordered_json& config)
{
	StripNullValues(config);

	std::ofstream outFile(filePath);
	if (!outFile.is_open()) {
		logger::warn("[LightEditor] Failed to write Light Placer config: {}", filePath.string());
		return false;
	}
	std::string output = config.dump(1, '\t');

	static const std::regex vec3Pattern(R"(\[\n\s*([-\d.eE+]+),\n\s*([-\d.eE+]+),\n\s*([-\d.eE+]+)\n\s*\])");
	output = std::regex_replace(output, vec3Pattern, "[$1, $2, $3]");

	{
		// Leading boundary group restricts rounding to genuine number tokens (preceded by ':', '[', ',', ws)
		// so digits inside strings (e.g. "..._x1.4.nif") are untouched; std::regex has no lookbehind for this.
		static const std::regex floatPattern(R"(([:\[,\s])(-?\d+\.\d+))");
		std::string rounded;
		rounded.reserve(output.size());
		std::sregex_iterator it(output.begin(), output.end(), floatPattern), end;
		size_t pos = 0;
		for (; it != end; ++it) {
			const auto& match = *it;
			rounded += output.substr(pos, match.position() - pos);
			rounded += match[1].str();
			double v = std::round(std::stod(match[2].str()) * 10000.0) / 10000.0;
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.4f", v);
			std::string s(buf);
			s.erase(s.find_last_not_of('0') + 1);
			if (s.back() == '.')
				s.pop_back();
			rounded += s;
			pos = match.position() + match.length();
		}
		rounded += output.substr(pos);
		output = std::move(rounded);
	}

	// Collapse whiteList / blackList string arrays onto a single line each.
	{
		static const std::regex filterListPattern(R"("(?:white|black)List":\s*\[[\s\S]*?\])");
		static const std::regex quotedStr(R"("[^"]*")");
		std::string result;
		result.reserve(output.size());
		std::sregex_iterator it(output.begin(), output.end(), filterListPattern), end;
		std::string::const_iterator lastPos = output.cbegin();
		for (; it != end; ++it) {
			const auto& m = *it;
			result.append(lastPos, m[0].first);
			const std::string block(m[0].first, m[0].second);
			const size_t bracket = block.find('[');
			std::string line(block, 0, bracket);
			line += '[';
			bool first = true;
			for (std::sregex_iterator s(block.cbegin() + bracket, block.cend(), quotedStr), sEnd; s != sEnd; ++s) {
				if (!first)
					line += ", ";
				line += (*s)[0];
				first = false;
			}
			line += ']';
			result += line;
			lastPos = m[0].second;
		}
		result.append(lastPos, output.cend());
		output = std::move(result);
	}

	outFile << output;
	outFile.flush();
	if (outFile.fail()) {
		logger::warn("[LightEditor] Failed to write Light Placer config to {}: stream error", filePath.string());
		return false;
	}
	return true;
}

/** @brief Builds the Light Placer config path from its extension-less relative path. */
static std::filesystem::path LPConfigFilePath(const std::string& configPath)
{
	return std::filesystem::path("Data\\LightPlacer") / (configPath + ".json");
}

/**
 * @brief Loads a Light Placer config array into `out`.
 * @return False if the file is missing, fails to parse, or is not a JSON array.
 */
static bool LoadConfigArray(const std::string& configPath, nlohmann::ordered_json& out)
{
	const auto filePath = LPConfigFilePath(configPath);
	std::ifstream in(filePath);
	if (!in.is_open())
		return false;
	try {
		in >> out;
	} catch (const nlohmann::json::parse_error& e) {
		logger::warn("[LightEditor] Failed to parse {}: {}", filePath.string(), e.what());
		return false;
	}
	return out.is_array();
}

/** @brief Lower-cases and forward-slashes a model path so casing/separator variants compare equal. */
static std::string NormalizeModelPath(std::string path)
{
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	std::replace(path.begin(), path.end(), '\\', '/');
	return path;
}

/** @brief Builds the default LP light object for a freshly added bulb. */
static nlohmann::ordered_json MakeLightObject(const std::string& lighEdid)
{
	nlohmann::ordered_json data;
	data["light"] = lighEdid;
	data["fade"] = 1;
	data["radius"] = 1;
	data["flags"] = "";

	nlohmann::ordered_json light;
	light["data"] = std::move(data);
	light["points"] = nlohmann::ordered_json::array({ nlohmann::ordered_json::array({ 0, 0, 1 }) });
	return light;
}

/** @brief Overwrites a light entry's first point/node with the integer-rounded position (no-op if absent). */
static void SetFirstPointFromPos(nlohmann::ordered_json& lightEntry, const RE::NiPoint3& pos)
{
	const char* pointsKey = lightEntry.contains("points") ? "points" : (lightEntry.contains("nodes") ? "nodes" : nullptr);
	if (!pointsKey)
		return;
	auto& pts = lightEntry[pointsKey];
	if (pts.is_array() && !pts.empty() && pts[0].is_array() && pts[0].size() >= 3)
		pts[0] = nlohmann::ordered_json::array({ static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(pos.z) });
}

/** @brief True if the entry's "lights" array already contains a light with the given EDID. */
static bool EntryContainsLight(const nlohmann::ordered_json& entry, const std::string& lighEdid)
{
	if (auto* lights = GetArrayMember(entry, "lights"))
		for (const auto& le : *lights)
			if (const auto* edid = GetLightEntryEdid(le); edid && *edid == lighEdid)
				return true;
	return false;
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

void LightEditor::ApplyExternalEmittance(RE::TESForm* source)
{
	// Selected-bulb-only preview; deliberately does NOT write the ref's ExtraEmittanceSource (that makes
	// the engine drive every touched bulb even when unselected). Persistence is via Save to Light Placer.
	activeEmittanceSource = source;

	// An explicit source is incompatible with NoExternalEmittance, so drop the flag (else the saved
	// entry and reloadlp would suppress the source).
	if (lpInfo.isLPLight && source)
		lpFlagSet.erase("NoExternalEmittance");
}

void LightEditor::ClearExternalEmittance()
{
	externalEmittanceEdid = {};
	useExternalEmittance = false;
	activeEmittanceSource = nullptr;
	emittanceColorActive = false;
}

void LightEditor::QueueReselectCurrentLP()
{
	// reloadlp recreates LP bulbs, shifting per-iteration indices so the (id, index) match fails.
	// Re-acquire by stable identity (owner ref + config + light EDID) via the pendingAutoSelect path.
	if (!lpInfo.isLPLight)
		return;
	pendingSelectRefrId = selected.id;
	pendingSelectConfigPath = lpInfo.configPath;
	pendingSelectLighEdid = lpInfo.lightEDID;
	pendingAutoSelect = true;
	pendingAutoSelectTTL = 10;
}

void LightEditor::UpdateEmittanceColor()
{
	// Only regions carry a live time/weather-driven emittanceColor; anything else leaves the lerp
	// inactive so ApplyOverrides uses the base color.
	auto* region = activeEmittanceSource ? activeEmittanceSource->As<RE::TESRegion>() : nullptr;
	if (!region) {
		emittanceColorActive = false;
		return;
	}

	// The game already varies emittanceColor smoothly with time/weather, so track it directly;
	// smoothing would only add lag when scrubbing the time slider.
	emittanceColor = region->emittanceColor;
	emittanceColorActive = true;
}

// Picking a form sets the reference's runtime emittance source live; "(None)" removes it. LP bulbs
// additionally persist the choice via Save to Light Placer.
void LightEditor::DrawExternalEmittanceCombo()
{
	if (!activeRefr)
		return;

	EnsureEmittanceFormListBuilt();

	static constexpr const char* kEmittanceComboId = "EmittanceFormCombo";
	const char* kNoneLabel = T(TKEY("none"), "(None)");
	const char* preview = externalEmittanceEdid.empty() ? kNoneLabel : externalEmittanceEdid.c_str();
	const auto externalEmittanceLabel = fmt::format("{}##combo", T(TKEY("external_emittance"), "External Emittance"));
	if (ImGui::BeginCombo(externalEmittanceLabel.c_str(), preview)) {
		auto searchText = Util::DrawComboSearchInput(kEmittanceComboId);
		if (searchText.empty() || Util::StringMatchesSearch(kNoneLabel, searchText)) {
			if (ImGui::Selectable(kNoneLabel, externalEmittanceEdid.empty())) {
				ClearExternalEmittance();
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
				useExternalEmittance = true;
				ApplyExternalEmittance(form);
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

RE::FormID LightEditor::ResolveFormEntry(const std::string& entry)
{
	const auto tildePos = entry.find('~');
	const bool hasPrefix = HasHexPrefix(entry);
	if (tildePos == std::string::npos) {
		if (!hasPrefix)
			return 0;
		try {
			return static_cast<RE::FormID>(std::stoul(entry.substr(2), nullptr, 16));
		} catch (...) {
			return 0;
		}
	}
	const auto hexStart = hasPrefix ? 2 : 0;
	RE::FormID relativeID;
	try {
		relativeID = static_cast<RE::FormID>(std::stoul(entry.substr(hexStart, tildePos - hexStart), nullptr, 16));
	} catch (...) {
		return 0;
	}
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
	current.data.diffuse.red = ligh->data.color.red / 255.f;
	current.data.diffuse.green = ligh->data.color.green / 255.f;
	current.data.diffuse.blue = ligh->data.color.blue / 255.f;
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

const std::string* LightEditor::LighEdidPtrForFormId(RE::FormID formId)
{
	for (auto& [edid, ligh] : s_lighFormList)
		if (ligh->GetFormID() == formId)
			return &edid;
	return nullptr;
}

std::string LightEditor::LighEdidForFormId(RE::FormID formId)
{
	const auto* edid = LighEdidPtrForFormId(formId);
	return edid ? *edid : std::string{};
}

void LightEditor::DrawSettings()
{
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
		QueueReselectCurrentLP();  // capture before RestoreOriginal clears lpInfo/activeRefr
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
	bool anyItemHovered = false;  // mouse over any combo entry this frame (flashable or not)
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
			// Flash only a flashable target: a ref/attached light (id != 0) other than the selected one
			// (whose fade ApplyOverrides drives). Selected/Other hover leaves thisFrameHovered empty, clearing it.
			if (ImGui::IsItemHovered()) {
				anyItemHovered = true;
				if (!isSelected && light.id != 0)
					thisFrameHovered = light;
			}
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	} else {
		Util::ClearComboSearch(kLightsComboId);
	}

	// Re-evaluate the hover flash whenever the mouse is over an entry or the combo closed. Moving onto
	// a non-flashable target (empty thisFrameHovered) stops the blink; dead-space hover keeps it going.
	if (anyItemHovered || !lightsComboOpen) {
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
		if (isAttaching)
			ImGui::EndDisabled();
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
		activeEmittanceSource = originalEmittanceSource;
		externalEmittanceEdid = originalExternalEmittanceEdid;
		useExternalEmittance = originalUseExternalEmittance;
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
					ReloadLPAndReselect();
					lpMatchFound = true;
				}
				const std::string okMsg = I18n::GetSingleton()->Format(TKEY("saved_to_config"), { { "path", lpInfo.configPath } }, "Saved to {path}");
				NotifyResult(ok, okMsg.c_str(), T(TKEY("save_failed"), "Save failed \xe2\x80\x94 see log"));
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (lpMatchFound)
				ImGui::Text(T(TKEY("save_to_lp_match_tooltip"), "Matching entry found in %s.\nSave current settings to the Light Placer JSON."), lpInfo.configPath.c_str());
			else
				ImGui::Text(T(TKEY("save_to_lp_no_match_tooltip"), "No matching entry found in %s.\nSaving will fail."), lpInfo.configPath.c_str());
		}

		ImGui::SameLine();
		if (Util::ErrorButton(T(TKEY("delete_entry"), "Delete")))
			deleteConfirmPopupRequested = true;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("delete_entry_tooltip"), "Delete this light entry from the Light Placer JSON.\nIf it is the only light in its entry, the whole models/formIDs entry is removed too."));
		}
		DrawDeleteConfirmation();
	}
	ImGui::SameLine();
	ImGui::Checkbox(T(TKEY("log_mode"), "Log Mode"), &extendedLogMode);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("log_mode_tooltip"), "Extend slider ranges and use a logarithmic scale."));
	}

	if (lpInfo.isLPLight) {
		auto doFilterButton = [&](bool isWhiteList) {
			bool& inList = isWhiteList ? lpInWhitelist : lpInBlacklist;
			const char* addLabel = isWhiteList ? T(TKEY("add_to_whitelist"), "Add to Whitelist") : T(TKEY("add_to_blacklist"), "Add to Blacklist");
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
					const char* msg = inList ? (isWhiteList ? T(TKEY("added_to_whitelist"), "Added to whitelist") : T(TKEY("added_to_blacklist"), "Added to blacklist")) : (isWhiteList ? T(TKEY("removed_from_whitelist"), "Removed from whitelist") : T(TKEY("removed_from_blacklist"), "Removed from blacklist"));
					EditorWindow::GetSingleton()->ShowNotification(msg, Util::Colors::GetInfo());
				} else {
					EditorWindow::GetSingleton()->ShowNotification(T(TKEY("filter_update_failed"), "Filter update failed \xe2\x80\x94 see log"), Util::Colors::GetError());
				}
			}
		};

		doFilterButton(true);
		ImGui::SameLine();
		doFilterButton(false);

		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("save_as_separate_entry"), "Save as Separate Entry"))) {
			const bool ok = SaveAsSeparateEntry(saveColorToLP);
			if (ok) {
				ReloadLPAndReselect();
				lpInBlacklist = true;
			}
			NotifyResult(ok,
				T(TKEY("saved_as_separate_entry"), "Saved as separate entry"),
				T(TKEY("save_failed"), "Save failed \xe2\x80\x94 see log"));
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T(TKEY("save_as_separate_entry_tooltip"), "Fork this bulb into a new whitelist entry for %s with the current edits, and blacklist it from the shared entry so the edits apply only to this reference.\nReload LP to apply."),
				FormatOwnerFormEntry(activeRefr).c_str());
		}
	}

	ImGui::Spacing();

	if (selected.isAttached) {
		EnsureLighFormListBuilt();
		const char* kOriginalLabel = T(TKEY("original"), "(Original)");
		const char* previewEdid = kOriginalLabel;
		if (const auto* edid = LighEdidPtrForFormId(current.data.lighFormId))
			previewEdid = edid->c_str();

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
					// Swap the LIGH form but keep the user's edited fade/radius/size/cutoff.
					const float savedFade = current.data.fade;
					const float savedRadius = current.data.radius;
					const float savedSize = current.data.size;
					const float savedCutoff = current.data.cutoffOverride;
					ApplyLighFormData(ligh);
					current.data.fade = savedFade;
					current.data.radius = savedRadius;
					current.data.size = savedSize;
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
	}

	// External emittance applies to any reference-backed bulb, so it lives outside the attached-only block.
	DrawExternalEmittanceCombo();

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

	// Logarithmic+extended or normal slider per extendedLogMode. Log scale requires min > 0, so
	// extended mins are nudged above zero where needed.
	auto drawSlider = [&](const char* label, float& value,
						  float normalMin, float normalMax,
						  float extMin, float extMax,
						  const char* format) -> bool {
		if (extendedLogMode)
			return ImGui::SliderFloat(label, &value, extMin, extMax, format, ImGuiSliderFlags_Logarithmic);
		return static_cast<bool>(WeatherUtils::DrawSliderFloat(label, value, normalMin, normalMax, nullptr, format));
	};

	const auto isInvSq = current.data.flags.any(LightLimitFix::LightFlags::InverseSquare);

	// "Intensity" is only meaningful for Inverse Square bulbs; otherwise this value is the light's fade.
	const char* fadeLabel = isInvSq ? T(TKEY("intensity"), "Intensity") : T(TKEY("fade"), "Fade");
	drawSlider(fadeLabel, current.data.fade, 0.01f, 16.f, 0.01f, 1024.f, "%.3f");

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
		if (drawSlider(T(TKEY("shadow_depth_bias"), "Shadow Depth Bias"), shadowDepthBias, 0.0f, 50.0f, 0.01f, 50.f, "%.2f"))
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
				if (disabled)
					ImGui::BeginDisabled();
				bool inSet = lpFlagSet.contains(flagName);
				if (ImGui::Checkbox(flagName, &inSet)) {
					if (inSet)
						lpFlagSet.insert(flagName);
					else
						lpFlagSet.erase(flagName);
					// NoExternalEmittance and an emittance source are mutually exclusive: enabling the
					// flag clears the source (combo shows None; the source line is dropped on save).
					if (inSet && std::string_view(flagName) == "NoExternalEmittance")
						ClearExternalEmittance();
					SyncLPFlagsToRuntime();
				}
				if (disabled)
					ImGui::EndDisabled();
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
		ImGui::CheckboxFlags(T(TKEY("tes_flag_dynamic"), "Dynamic"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kDynamic));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_negative"), "Negative"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kNegative));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags(T(TKEY("tes_flag_flicker"), "Flicker"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlicker));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_flicker_slow"), "Flicker Slow"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kFlickerSlow));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_pulse"), "Pulse"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulse));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_pulse_slow"), "Pulse Slow"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPulseSlow));
		ImGui::CheckboxFlags(T(TKEY("tes_flag_hemi_shadow"), "Hemi Shadow"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags(T(TKEY("tes_flag_omni_shadow"), "Omni Shadow"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow));
		if (!lpInfo.isLPLight)
			ImGui::CheckboxFlags(T(TKEY("tes_flag_portal_strict"), "Portal Strict"), flags, static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kPortalStrict));

		ImGui::EndDisabled();
	}

	if (isAttaching)
		ImGui::EndDisabled();
}

static constexpr std::string_view kPopupPrefsPath =
	R"(Data\SKSE\Plugins\CommunityShaders\LightEditorPrefs.json)";

void LightEditor::SavePopupPrefs() const
{
	nlohmann::ordered_json j;
	j["addConfigSearch"] = addConfigSearch;
	j["addAttachMode"] = addAttachMode;
	j["addLighSearch"] = addLighSearch;
	j["addPopupMode"] = addPopupMode;
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
		addConfigSearch[sizeof(addConfigSearch) - 1] = '\0';  // strncpy won't terminate an oversized source
	}
	if (auto it = j.find("addAttachMode"); it != j.end() && it->is_number_integer())
		addAttachMode = it->get<int>();
	if (auto it = j.find("addLighSearch"); it != j.end() && it->is_string()) {
		auto s = it->get<std::string>();
		std::strncpy(addLighSearch, s.c_str(), sizeof(addLighSearch) - 1);
		addLighSearch[sizeof(addLighSearch) - 1] = '\0';
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

	if (picker.IsPicking()) {
		int pm = static_cast<int>(picker.pickMode);
		ImGui::RadioButton(T(TKEY("pick_mode_collision"), "Collision"), &pm, 0);
		ImGui::SameLine();
		ImGui::RadioButton(T(TKEY("pick_mode_effect"), "Effect mesh"), &pm, 1);
		const auto newPickMode = static_cast<LightPicker::PickMode>(pm);
		if (newPickMode != picker.pickMode) {
			picker.pickMode = newPickMode;
			picker.InvalidateHover();  // recompute the hover hit under the new mode immediately
		}
	}
}

std::vector<std::string> LightEditor::ScanLPConfigPaths() const
{
	std::vector<std::string> paths;
	// Mirrors LightPlacer's USVFS-safe scanning: relative path (not GetDataPath), throwing iterator (no
	// error_code), is_directory()/extension() only (is_regular_file(ec) hits an unhookable API, hiding virtual files).
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
			// Use path ops, not character-count stripping: lexically_relative removes the prefix by
			// component (robust to USVFS casing/format variation) and stem() strips the extension cleanly.
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

bool LightEditor::MatchesComboFilter(std::string_view filter, const std::string& text)
{
	return filter.empty() || Util::StringMatchesSearch(text, std::string(filter));
}

bool LightEditor::BeginSearchableCombo(const char* label, const char* preview, const char* searchId,
	char* searchBuf, size_t searchBufSize, std::string_view& filterOut, bool openNow)
{
	filterOut = {};
	// SetNextItemOpen is ignored for combos (they open via the derived popup id), so open it directly to
	// show the list without an extra click. One-shot: openNow is true only the frame the mode is entered.
	if (openNow)
		ImGui::OpenPopup(ImHashStr("##ComboPopup", 0, ImGui::GetID(label)));
	if (!ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge))
		return false;
	if (ImGui::IsWindowAppearing())
		ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputText(searchId, searchBuf, searchBufSize);
	ImGui::Separator();
	filterOut = searchBuf;
	return true;
}

void LightEditor::NotifyResult(bool ok, const char* okMsg, const char* failMsg)
{
	EditorWindow::GetSingleton()->ShowNotification(ok ? okMsg : failMsg,
		ok ? Util::Colors::GetSuccess() : Util::Colors::GetError());
}

void LightEditor::ReloadLPAndReselect()
{
	QueueReselectCurrentLP();
	ScheduleConsoleCommand("reloadlp");
	previous = {};
	waitFrames = 3;
}

int LightEditor::DrawAttachedBulbCombo(const char* searchId, bool openNow)
{
	int clicked = -1;
	const char* preview = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size()) ? attachedBulbs[addSelectedBulb].lightEDID.c_str() : T(TKEY("select_a_bulb"), "Select a bulb");
	std::string_view filter;
	if (BeginSearchableCombo(T(TKEY("attached_bulb"), "Attached bulb"), preview, searchId, addBulbSearch, sizeof(addBulbSearch), filter, openNow)) {
		for (int i = 0; i < (int)attachedBulbs.size(); ++i) {
			const auto& bulb = attachedBulbs[i];
			const std::string label = fmt::format("{}[{}]  ({})", bulb.lightEDID, bulb.index, bulb.configPath);
			if (!MatchesComboFilter(filter, label))
				continue;
			const bool isSel = (i == addSelectedBulb);
			if (ImGui::Selectable(label.c_str(), isSel)) {
				addSelectedBulb = i;
				clicked = i;
			}
			if (isSel)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return clicked;
}

void LightEditor::DrawLightRecordCombo(const char* searchId)
{
	EnsureLighFormListBuilt();
	const char* preview = T(TKEY("select_a_light"), "Select a light");
	if (const auto* edid = LighEdidPtrForFormId(addSelectedLighFormId))
		preview = edid->c_str();
	std::string_view filter;
	if (BeginSearchableCombo(T(TKEY("light_record"), "Light record"), preview, searchId, addLighSearch, sizeof(addLighSearch), filter, false)) {
		for (auto& [edid, ligh] : s_lighFormList) {
			if (!MatchesComboFilter(filter, edid))
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

void LightEditor::BeginAttachSequence(const std::string& configPath)
{
	attachConfigPath = configPath;
	pendingSelectConfigPath = configPath;
	RestoreOriginal();
	previous = {};
	ScheduleConsoleCommand("reloadlp");
	attachPendingRefr = pickedMesh.refrHandle;
	attachPhase = AttachPhase::WaitingForReload;
	attachPhaseStart = std::chrono::steady_clock::now();
	SavePopupPrefs();
	ImGui::CloseCurrentPopup();
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

		auto notifyAddFailed = [] {
			EditorWindow::GetSingleton()->ShowNotification(
				T(TKEY("add_light_failed"), "Failed to add light \xE2\x80\x94 see log"),
				Util::Colors::GetError());
		};

		// "Selectable button": disabled+tooltip when unavailable, info-styled when active, else clickable.
		// Unavailability is checked first so a stale remembered-active option renders disabled, not clickable.
		auto selectableButton = [&](const char* label, bool active, bool available, const char* unavailTip) -> bool {
			if (!available) {
				{
					auto _d = Util::DisableGuard(true);
					ImGui::Button(label);
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", unavailTip);
				return false;
			}
			if (active) {
				auto _s = Util::StatusButtonStyle(Util::Colors::GetInfo());
				ImGui::Button(label);
				return false;
			}
			return ImGui::Button(label);
		};

		const bool hasBulbs = !attachedBulbs.empty();
		auto drawModeBtn = [&](const char* label, int mode, bool available, const char* unavailTip) {
			if (selectableButton(label, addPopupMode == mode, available, unavailTip)) {
				addPopupMode = mode;
				// Auto-open the bulb list when entering multi-bulb Edit Bulb mode (saves a click).
				if (mode == ModeEditBulb)
					editBulbComboPendingOpen = true;
			}
		};

		const char* noBulbsTip = T(TKEY("no_attached_bulbs"), "This mesh has no attached Light Placer bulbs.");
		drawModeBtn(T(TKEY("mode_add_light"), "Add Light"), ModeAddLight, true, "");
		ImGui::SameLine();
		// Single bulb: "Edit Bulb" fires immediately without entering the mode.
		if (hasBulbs && attachedBulbs.size() == 1) {
			if (ImGui::Button(T(TKEY("mode_edit_bulb"), "Edit Bulb"))) {
				const auto& bulb = attachedBulbs[0];
				pendingSelectRefrId = bulb.refrId;
				pendingSelectConfigPath = bulb.configPath;
				pendingSelectLighEdid = bulb.lightEDID;
				pendingAutoSelect = true;
				pendingAutoSelectTTL = 10;
				filterOption = FilterOption::AttachedLights;
				SavePopupPrefs();
				ImGui::CloseCurrentPopup();
			}
		} else {
			drawModeBtn(T(TKEY("mode_edit_bulb"), "Edit Bulb"), ModeEditBulb, hasBulbs, noBulbsTip);
		}
		ImGui::SameLine();
		drawModeBtn(T(TKEY("add_to_whitelist"), "Add to Whitelist"), ModeWhitelist, hasBulbs, noBulbsTip);
		ImGui::SameLine();
		drawModeBtn(T(TKEY("add_to_blacklist"), "Add to Blacklist"), ModeBlacklist, hasBulbs, noBulbsTip);
		ImGui::SameLine();
		drawModeBtn(T(TKEY("mode_remove_from_list"), "Remove from List"), ModeRemoveFromList,
			!filterListEntries.empty(),
			T(TKEY("no_filter_list_entries"), "This mesh has no whitelist or blacklist entries."));
		ImGui::Separator();

		if (addPopupMode == ModeAddLight) {
			auto drawSubModeBtn = [&](const char* label, int subMode) {
				if (selectableButton(label, addLightSubMode == subMode, true, nullptr))
					addLightSubMode = subMode;
			};

			if (hasBulbs) {
				drawSubModeBtn(T(TKEY("sub_mode_new_point"), "Add new point"), SubModeNewPoint);
				ImGui::SameLine();
				drawSubModeBtn(T(TKEY("sub_mode_to_entry"), "Add to entry"), SubModeToEntry);
				ImGui::SameLine();
				drawSubModeBtn(T(TKEY("sub_mode_new_entry"), "Add new entry"), SubModeNewEntry);
				ImGui::Separator();
			}

			if (!hasBulbs || addLightSubMode == SubModeNewEntry) {
				const char* configPreview = (addSelectedConfig >= 0 && addSelectedConfig < (int)lpConfigPaths.size()) ? lpConfigPaths[addSelectedConfig].c_str() : T(TKEY("select_a_config"), "Select a config");
				std::string_view cfgFilter;
				if (BeginSearchableCombo(T(TKEY("target_json"), "Target JSON"), configPreview, "##cfg_search", addConfigSearch, sizeof(addConfigSearch), cfgFilter, false)) {
					if (lpConfigPaths.empty())
						ImGui::TextDisabled("%s", T(TKEY("no_configs_found"), "No configs found in Data\\LightPlacer\\"));
					for (int i = 0; i < (int)lpConfigPaths.size(); ++i) {
						if (!MatchesComboFilter(cfgFilter, lpConfigPaths[i]))
							continue;
						const bool isSel = (i == addSelectedConfig);
						if (ImGui::Selectable(lpConfigPaths[i].c_str(), isSel))
							addSelectedConfig = i;
						if (isSel)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				if (addSelectedConfig >= 0) {
					ImGui::Text("%s", T(TKEY("attach_by"), "Attach by:"));
					ImGui::SameLine();

					auto drawAttachBtn = [&](const char* label, int mode, bool available, const char* unavailTip) {
						if (selectableButton(label, addAttachMode == mode, available, unavailTip))
							addAttachMode = mode;
					};

					drawAttachBtn(T(TKEY("attach_model"), "Model"), 0, !pickedMesh.modelPath.empty(), T(TKEY("no_model_path"), "No model path on this object."));
					ImGui::SameLine();
					drawAttachBtn(T(TKEY("attach_form_id"), "FormID"), 1, !pickedMesh.sourcePlugin.empty(), T(TKEY("no_source_plugin"), "No source plugin on this object."));
					ImGui::SameLine();
					drawAttachBtn(T(TKEY("attach_editor_id"), "EditorID"), 2, !pickedMesh.editorId.empty(), T(TKEY("no_editor_id_on_object"), "No EditorID on this object."));
				}

				if (addSelectedConfig >= 0 && addAttachMode >= 0)
					DrawLightRecordCombo("##ligh_search");

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
						if (AddBulbToConfig()) {
							pendingSelectLighEdid = LighEdidForFormId(addSelectedLighFormId);
							pendingSelectRefrId = 0;
							if (auto refr = pickedMesh.refrHandle.get())
								pendingSelectRefrId = refr->GetFormID();
							BeginAttachSequence(lpConfigPaths[addSelectedConfig]);
						} else {
							notifyAddFailed();
						}
					}
				}
			}

			if (hasBulbs && addLightSubMode == SubModeNewPoint) {
				DrawAttachedBulbCombo("##bulb_search_pt", false);

				ImGui::Separator();
				const bool canAddPt = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
				ImGui::BeginDisabled(!canAddPt);
				const bool clickedPt = ImGui::Button(T(TKEY("add_point"), "Add Point"));
				ImGui::EndDisabled();

				if (clickedPt && canAddPt) {
					const auto& bulb = attachedBulbs[addSelectedBulb];
					if (AddPointToConfig(bulb)) {
						pendingSelectLighEdid = bulb.lightEDID;
						pendingSelectRefrId = bulb.refrId;
						BeginAttachSequence(bulb.configPath);
					} else {
						notifyAddFailed();
					}
				}
			}

			if (hasBulbs && addLightSubMode == SubModeToEntry) {
				// Bulb picker: identifies the parent top-level entry
				DrawAttachedBulbCombo("##bulb_search_te", false);
				DrawLightRecordCombo("##ligh_search_te");

				ImGui::Separator();
				const bool bulbOk = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
				const std::string teEdid = LighEdidForFormId(addSelectedLighFormId);
				const bool lightOk = !teEdid.empty();
				const bool isDupe = bulbOk && lightOk &&
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
					if (AddLightToExistingEntry(bulb, teEdid)) {
						pendingSelectLighEdid = teEdid;
						pendingSelectRefrId = bulb.refrId;
						BeginAttachSequence(bulb.configPath);
					} else {
						notifyAddFailed();
					}
				}
			}
		}

		if (addPopupMode == ModeEditBulb) {
			// Multi-bulb: combo fires immediately on selection. It returns the clicked index so the modal
			// is closed after the combo, not inside it (closing inside would close the combo, not the modal).
			const bool openNow = editBulbComboPendingOpen;
			editBulbComboPendingOpen = false;
			const int clickedBulb = DrawAttachedBulbCombo("##bulb_search", openNow);
			if (clickedBulb >= 0) {
				const auto& bulb = attachedBulbs[clickedBulb];
				pendingSelectRefrId = bulb.refrId;
				pendingSelectConfigPath = bulb.configPath;
				pendingSelectLighEdid = bulb.lightEDID;
				pendingAutoSelect = true;
				pendingAutoSelectTTL = 10;
				filterOption = FilterOption::AttachedLights;
				SavePopupPrefs();
				ImGui::CloseCurrentPopup();
			}
		} else if (addPopupMode == ModeWhitelist || addPopupMode == ModeBlacklist) {
			DrawAttachedBulbCombo("##bulb_search", false);

			// Entry type: what string to write into the filter list.
			{
				auto* refr = PickedRefr();
				std::string cellEdid;
				if (refr)
					if (auto* cell = refr->GetParentCell())
						cellEdid = cell->GetFormEditorID();

				ImGui::Text("%s", T(TKEY("filter_entry_type"), "Add as:"));
				ImGui::SameLine();
				auto drawEntryTypeBtn = [&](const char* label, int type, bool available, const char* unavailTip) {
					if (selectableButton(label, addFilterEntryType == type, available, unavailTip))
						addFilterEntryType = type;
				};
				drawEntryTypeBtn(T(TKEY("entry_type_reference"), "Reference"), 0, true, "");
				ImGui::SameLine();
				drawEntryTypeBtn(T(TKEY("entry_type_cell"), "Cell"), 1,
					!cellEdid.empty(),
					T(TKEY("no_cell_editor_id"), "This mesh's cell has no EditorID."));
			}

			ImGui::Separator();
			const bool bulbChosen = (addSelectedBulb >= 0 && addSelectedBulb < (int)attachedBulbs.size());
			// Append ##confirm to the label so the button has a unique ImGui ID distinct
			// from the identically-labelled mode selector button rendered in the same popup.
			const std::string confirmLabel = std::string(addPopupMode == ModeWhitelist ? T(TKEY("add_to_whitelist"), "Add to Whitelist") : T(TKEY("add_to_blacklist"), "Add to Blacklist")) + "##confirm";
			ImGui::BeginDisabled(!bulbChosen);
			const bool confirm = ImGui::Button(confirmLabel.c_str());
			ImGui::EndDisabled();

			if (confirm && bulbChosen) {
				const auto& bulb = attachedBulbs[addSelectedBulb];
				auto* refr = PickedRefr();

				std::string entryStr;  // built from the chosen entry type
				if (addFilterEntryType == 1) {
					if (refr)
						if (auto* cell = refr->GetParentCell())
							entryStr = cell->GetFormEditorID();
				}
				if (entryStr.empty())
					entryStr = FormatOwnerFormEntry(refr);

				const MatchContext ctx = MakePickedContext(bulb.lightEDID);
				const bool isWhite = (addPopupMode == ModeWhitelist);
				const bool ok = ModifyLPFilterListFor(bulb.configPath, ctx, entryStr, isWhite, true);
				if (ok)
					ScheduleConsoleCommand("reloadlp");
				NotifyResult(ok,
					isWhite ? T(TKEY("added_to_whitelist"), "Added to whitelist") : T(TKEY("added_to_blacklist"), "Added to blacklist"),
					T(TKEY("filter_update_failed"), "Filter update failed \xe2\x80\x94 see log"));
				SavePopupPrefs();
				ImGui::CloseCurrentPopup();
			}
		}

		if (addPopupMode == ModeRemoveFromList) {
			const char* filterPreview = (addSelectedFilterEntry >= 0 && addSelectedFilterEntry < (int)filterListEntries.size()) ? filterListEntries[addSelectedFilterEntry].lightEDID.c_str() : T(TKEY("select_a_bulb"), "Select a bulb");
			std::string_view filterSv;
			if (BeginSearchableCombo(T(TKEY("attached_bulb"), "Attached bulb"), filterPreview, "##filter_search", addFilterSearch, sizeof(addFilterSearch), filterSv, false)) {
				for (int i = 0; i < (int)filterListEntries.size(); ++i) {
					const auto& fe = filterListEntries[i];
					const std::string lbl = fmt::format("[{}]  {}  ({})  \"{}\"",
						fe.isWhiteList ? "whitelist" : "blacklist",
						fe.lightEDID, fe.configPath,
						fe.matchedEntry);
					if (!MatchesComboFilter(filterSv, lbl))
						continue;
					const bool isSel = (i == addSelectedFilterEntry);
					if (ImGui::Selectable(lbl.c_str(), isSel))
						addSelectedFilterEntry = i;
					if (isSel)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::Separator();
			const bool entryChosen = (addSelectedFilterEntry >= 0 && addSelectedFilterEntry < (int)filterListEntries.size());
			ImGui::BeginDisabled(!entryChosen);
			const bool clickedRemove = ImGui::Button(T(TKEY("remove_btn"), "Remove"));
			ImGui::EndDisabled();

			if (clickedRemove && entryChosen) {
				const auto& fe = filterListEntries[addSelectedFilterEntry];
				const MatchContext ctx = MakePickedContext(fe.lightEDID);
				const bool ok = ModifyLPFilterListFor(fe.configPath, ctx, fe.matchedEntry, fe.isWhiteList, false);
				if (ok)
					ScheduleConsoleCommand("reloadlp");
				NotifyResult(ok,
					T(TKEY("removed_from_list"), "Removed from list"),
					T(TKEY("filter_update_failed"), "Filter update failed \xe2\x80\x94 see log"));
				SavePopupPrefs();
				ImGui::CloseCurrentPopup();
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
			return LightPicker::FormatFormEntry(pickedMesh.baseFormId, pickedMesh.sourcePlugin);
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

	reasonOut.clear();
	return true;
}

bool LightEditor::AddBulbToConfig()
{
	if (addSelectedConfig < 0 || addSelectedConfig >= (int)lpConfigPaths.size())
		return false;

	const std::string lighEdid = LighEdidForFormId(addSelectedLighFormId);
	if (lighEdid.empty())
		return false;

	const std::string target = AddEntryTargetString();
	if (target.empty())
		return false;

	const auto configPath = lpConfigPaths[addSelectedConfig];
	const auto filePath = LPConfigFilePath(configPath);

	// Create the config if it doesn't exist yet (LoadConfigArray would reject a missing file).
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

	nlohmann::ordered_json newEntry;
	newEntry[addAttachMode == 0 ? "models" : "formIDs"] = nlohmann::ordered_json::array({ target });
	newEntry["lights"] = nlohmann::ordered_json::array({ MakeLightObject(lighEdid) });

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
	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(bulb.configPath, configArray)) {
		logger::warn("[LightEditor] AddPointToConfig: cannot load {}", bulb.configPath);
		return false;
	}

	const MatchContext ctx = MakePickedContext(bulb.lightEDID);

	auto* lightEntry = FindMatchingLightEntry(configArray, ctx, false);
	if (!lightEntry) {
		logger::warn("[LightEditor] AddPointToConfig: no matching entry for '{}' in {}",
			bulb.lightEDID, bulb.configPath);
		return false;
	}

	auto& points = (*lightEntry)["points"];
	if (!points.is_array())
		points = nlohmann::ordered_json::array();
	points.push_back(nlohmann::ordered_json::array({ 0, 0, 1 }));

	if (!WriteLPConfig(LPConfigFilePath(bulb.configPath), configArray)) {
		logger::warn("[LightEditor] AddPointToConfig: write failed for {}", bulb.configPath);
		return false;
	}
	logger::info("[LightEditor] AddPointToConfig: added point to '{}' in {}", bulb.lightEDID, bulb.configPath);
	return true;
}

bool LightEditor::LightAlreadyInEntry(const AttachedBulb& bulb, const std::string& lighEdid) const
{
	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(bulb.configPath, configArray))
		return false;

	// A matching light entry (model/formID match + same light EDID) means it is already present.
	return FindMatchingLightEntry(configArray, MakePickedContext(lighEdid), false) != nullptr;
}

bool LightEditor::AddLightToExistingEntry(const AttachedBulb& bulb, const std::string& lighEdid)
{
	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(bulb.configPath, configArray)) {
		logger::warn("[LightEditor] AddLightToExistingEntry: cannot load {}", bulb.configPath);
		return false;
	}

	const MatchContext ctx = MakePickedContext(lighEdid);

	nlohmann::ordered_json* parentEntry = nullptr;
	for (auto& entry : configArray)
		if (EntryMatchesContext(entry, ctx)) {
			parentEntry = &entry;
			break;
		}

	if (!parentEntry) {
		logger::warn("[LightEditor] AddLightToExistingEntry: no matching top-level entry in {}", bulb.configPath);
		return false;
	}

	// Duplicate guard (should be checked by UI already, but be safe)
	if (EntryContainsLight(*parentEntry, lighEdid))
		return false;

	auto& lightsArr = (*parentEntry)["lights"];
	if (!lightsArr.is_array())
		lightsArr = nlohmann::ordered_json::array();
	lightsArr.push_back(MakeLightObject(lighEdid));

	if (!WriteLPConfig(LPConfigFilePath(bulb.configPath), configArray)) {
		logger::warn("[LightEditor] AddLightToExistingEntry: write failed for {}", bulb.configPath);
		return false;
	}
	logger::info("[LightEditor] AddLightToExistingEntry: added '{}' to existing entry in {}", lighEdid, bulb.configPath);
	return true;
}

bool LightEditor::HasShadowFlags(uint32_t tesFlags)
{
	return (tesFlags & (static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kHemiShadow) |
						   static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kOmniShadow) |
						   static_cast<uint32_t>(RE::TES_LIGHT_FLAGS::kSpotShadow))) != 0;
}

std::string LightEditor::GetLightName(const LightInfo& lightInfo)
{
	if (lightInfo.isRef)
		return fmt::format("0x{:08X} - {}", lightInfo.id, lightInfo.name.c_str());
	if (lightInfo.isAttached)
		return fmt::format("0x{:08X}|{} - {}", lightInfo.id, lightInfo.index, lightInfo.name.c_str());
	return fmt::format("{:p} - {}", lightInfo.ptr, lightInfo.name.c_str());
}

void LightEditor::GatherLights()
{
	// Attach-to-mesh timed flow: runs every frame so the timer isn't gated behind ShouldSwallowInput
	// (menu focus can drop when the popup closes). Each step waits kAttachStepDelay: disable, enable, finalize.
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
					I18n::GetSingleton()->Format(TKEY("added_light_to_config"), { { "path", attachConfigPath } }, "Added light to {path}").c_str(),
					Util::Colors::GetSuccess());
				break;
			default:
				break;
			}
		}
	}

	// Fire any deferred Enable (light-flag rebuild) before the early-outs below, so a pending
	// re-enable still runs while resampling is paused or the menu has lost input focus.
	UpdateRefRefresh();

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
		lpConfigPaths = ScanLPConfigPaths();
		GatherAttachedBulbs(PickedRefr());
		ScanFilterListEntries(PickedRefr(), lpConfigPaths);
		addSelectedConfig = -1;
		addAttachMode = -1;
		addSelectedLighFormId = 0;
		addPopupMode = -1;
		addLightSubMode = -1;
		editBulbComboPendingOpen = false;
		// Filter-flow selections are mesh-specific: reset them so a remembered "Cell" type or filter
		// index from the previous pick can't persist for a mesh that can't offer it.
		addFilterEntryType = 0;  // Reference, always available
		addSelectedFilterEntry = -1;
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

		LightInfo info;
		RE::TESObjectLIGH* ligh = nullptr;

		const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
		const auto refr = niLight->GetUserData();
		if (refr) {
			if (refr->IsDisabled())
				return;
			if (auto* objRef = refr->GetObjectReference()) {
				if (objRef->GetFormType() == RE::FormType::Light)
					ligh = objRef->As<RE::TESObjectLIGH>();
				info.id = refr->GetFormID();
				info.name = clib_util::editorID::get_editorID(objRef);
				info.index = lightsAttached[refr]++;
			}
		}

		info.isRef = ligh != nullptr;

		if (!info.isRef && runtimeData->lighFormId != 0) {
			if (auto* lighForm = RE::TESForm::LookupByID(runtimeData->lighFormId))
				ligh = lighForm->As<RE::TESObjectLIGH>();
		}

		info.isSpotlight = ligh && ligh->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow);
		const bool isShadow = ligh && HasShadowFlags(ligh->data.flags.underlying());

		totalLightCount++;
		if (isShadow)
			activeShadowLightCount++;

		if ((shadowsOnly) && (!ligh || !isShadow)) {
			return;
		}

		info.isAttached = !info.isRef && refr != nullptr;
		// Spotlights are always grouped under "Other" (their falloff isn't editable here).
		info.isOther = (!info.isRef && !info.isAttached) || (info.isSpotlight);

		const bool isRefMatch = (info.isRef && !info.isSpotlight) && filterOption == FilterOption::RefLights;
		const bool isAttachedMatch = info.isAttached && filterOption == FilterOption::AttachedLights;
		const bool isOtherMatch = info.isOther && filterOption == FilterOption::OtherLights;

		if (!(isRefMatch || isAttachedMatch || isOtherMatch))
			return;

		if (info.isRef) {
			info.position = refr->GetPosition();
			info.hasPosition = true;
		} else if (niLight->parent) {
			info.position = niLight->parent->world.translate;
			info.hasPosition = true;
		}
		if (info.isOther) {
			info.ptr = reinterpret_cast<void*>(niLight);
			if (info.name.empty())
				info.name = niLight->name.c_str();
			info.index = 0;
		}

		// Match a queued re-selection by stable identity so it survives reloadlp's index changes.
		if (pendingAutoSelect && info.isAttached && info.id == pendingSelectRefrId) {
			const auto parsedName = ParseLPLightName(niLight->name.c_str());
			if (parsedName.isLPLight && parsedName.configPath == pendingSelectConfigPath && parsedName.lightEDID == pendingSelectLighEdid) {
				selected = info;
				pendingAutoSelect = false;
			}
		}

		info.isSelected = selected == info;

		lights.push_back(info);

		// Capture the NiLight for hover-flash on the first frame this light is hovered.
		if (comboHoveredLight.id != 0 && info == comboHoveredLight && !hoverFlashNiLight) {
			hoverFlashNiLight.reset(niLight);
			const auto* rd = ISLCommon::RuntimeLightDataExt::Get(niLight);
			hoverFlashOriginalFade = (rd && rd->fade > 0.f) ? rd->fade : 1.f;
		}

		if (!info.isSelected)
			return;
		selected = info;
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
		// Count every light owned by this ref (not just LP bulbs) so the index matches the main Lights
		// combo, which increments per owner over all its lights.
		const uint32_t ownerIndex = running[owner]++;
		const auto parsed = ParseLPLightName(niLight->name.c_str());
		if (!parsed.isLPLight)
			return;
		AttachedBulb bulb;
		bulb.lightEDID = parsed.lightEDID;
		bulb.configPath = parsed.configPath;
		bulb.refrId = refr->GetFormID();
		bulb.index = ownerIndex;
		attachedBulbs.push_back(std::move(bulb));
	};

	for (auto& light : shadowSceneNode->GetRuntimeData().activeLights)
		collect(light);
	for (auto& light : shadowSceneNode->GetRuntimeData().activeShadowLights)
		collect(light);
}

void LightEditor::ScanFilterListEntries(RE::TESObjectREFR* refr, const std::vector<std::string>& configPaths)
{
	filterListEntries.clear();
	addSelectedFilterEntry = -1;
	addFilterSearch[0] = '\0';
	addFilterEntryType = 0;
	if (!refr)
		return;

	const std::string refEntry = FormatOwnerFormEntry(refr);

	// Collect all entry strings that could match this reference.
	std::vector<std::string> entriesToScan;
	if (!refEntry.empty())
		entriesToScan.push_back(refEntry);
	if (auto* cell = refr->GetParentCell()) {
		std::string cellEdid = cell->GetFormEditorID();
		if (!cellEdid.empty())
			entriesToScan.push_back(std::move(cellEdid));
	}

	if (entriesToScan.empty())
		return;

	for (const auto& configPath : configPaths) {
		nlohmann::ordered_json configArray;
		if (!LoadConfigArray(configPath, configArray))
			continue;

		for (const auto& entry : configArray) {
			auto lightsIt = entry.find("lights");
			if (lightsIt == entry.end() || !lightsIt->is_array())
				continue;
			for (const auto& le : *lightsIt) {
				const auto* edidPtr = GetLightEntryEdid(le);
				if (!edidPtr)
					continue;
				const std::string& lightEDID = *edidPtr;

				auto checkList = [&](bool isWhiteList) {
					const char* key = isWhiteList ? "whiteList" : "blackList";
					if (auto* list = GetArrayMember(le, key)) {
						for (const auto& item : *list) {
							if (!item.is_string())
								continue;
							const std::string& itemStr = item.get_ref<const std::string&>();
							for (const auto& candidate : entriesToScan) {
								if (itemStr == candidate) {
									filterListEntries.push_back({ lightEDID, configPath, candidate, isWhiteList });
									return;
								}
							}
						}
					}
				};
				checkList(true);
				checkList(false);
			}
		}
	}
	logger::info("[LightEditor] ScanFilterListEntries: found {} entries", filterListEntries.size());
}

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
		// The hover-flash may have blinked fade to 0; snapshotting mid-blink would freeze 0 as the base, so
		// recover the stashed pre-flash value. Covers non-LP refs; LP lights get this from RefreshLPJsonState.
		if (hoverFlashNiLight && niLight == hoverFlashNiLight.get())
			original.data.fade = hoverFlashOriginalFade;
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

		// Reset emittance state; populated below from the JSON (LP) or the runtime extra (non-LP).
		externalEmittanceEdid = {};
		useExternalEmittance = false;
		activeEmittanceSource = nullptr;
		emittanceColorActive = false;  // recomputed by UpdateEmittanceColor for the new bulb
		// LP bulbs author emittance in the JSON (no reliable runtime extra), read below by
		// RefreshLPJsonState; non-LP bulbs carry it as extra data.
		if (!lpInfo.isLPLight && refr) {
			if (const auto* extra = refr->extraList.GetByType<RE::ExtraEmittanceSource>())
				if (extra->source) {
					externalEmittanceEdid = clib_util::editorID::get_editorID(extra->source);
					activeEmittanceSource = extra->source;
				}
			useExternalEmittance = !externalEmittanceEdid.empty();
		}

		lpMatchFound = lpInfo.isLPLight && SaveToLightPlacer(false, true);
		if (lpInfo.isLPLight) {
			RefreshLPJsonState();
			originalLpFlagSet = lpFlagSet;
		}

		originalEmittanceSource = activeEmittanceSource;
		originalExternalEmittanceEdid = externalEmittanceEdid;
		originalUseExternalEmittance = useExternalEmittance;

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

	// Only non-LP lights apply TES-flag edits (mutating the base LIGH form + rebuilding the ref); LP flags
	// go via JSON/reloadlp. Running this for LP lights would mutate the shared form and vanish the mesh.
	if (!selected.isOther && !lpInfo.isLPLight && refr && tesFlags && current.tesFlags.underlying() != tesFlags->underlying()) {
		*tesFlags = static_cast<RE::TES_LIGHT_FLAGS>(current.tesFlags.underlying());
		RequestRefRefresh(refr);
	}

	UpdateEmittanceColor();

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
	// Hovered (not selected) light: blink its fade so it flashes in the combo list.
	if (hoverFlashNiLight && niLight == hoverFlashNiLight.get() && niLight != activeNiLight.get()) {
		runtimeData->fade = hoverFlashVisible ? hoverFlashOriginalFade : 0.f;
		return true;
	}

	if (niLight != activeNiLight.get())
		return false;

	runtimeData->lighFormId = current.data.lighFormId;
	// Use the emittance source's live color while one drives this bulb (see UpdateEmittanceColor), else the editor color.
	runtimeData->diffuse = emittanceColorActive ? emittanceColor : current.data.diffuse;
	runtimeData->fade = current.data.fade;
	runtimeData->cutoffOverride = current.data.cutoffOverride;
	runtimeData->size = current.data.size;

	if (current.data.flags.any(LightLimitFix::LightFlags::InverseSquare)) {
		runtimeData->flags.set(LightLimitFix::LightFlags::InverseSquare);
	} else {
		runtimeData->flags.reset(LightLimitFix::LightFlags::InverseSquare);
		// Restore the authoritative radius: ProcessLight's IS branch writes a computed value into the shared
		// runtimeData->radius that the non-IS branch only reads, so a stale inflated value would stick forever.
		runtimeData->radius = current.data.radius;
	}

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

	// Mirror the non-LP gate in UpdateSelectedLight: only non-LP lights ever mutated the base form's
	// flags, so only they need the revert + rebuild here.
	if (!lpInfo.isLPLight && activeLigh && activeRefr && current.tesFlags.underlying() != original.tesFlags.underlying()) {
		activeLigh->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(original.tesFlags.underlying());
		RequestRefRefresh(activeRefr);
	}

	if (auto* shadowLight = AsShadowLight(activeBsLight.get()))
		shadowLight->GetRuntimeData().shadowBiasScale = originalShadowDepthBias;

	activeNiLight.reset();
	activeBsLight.reset();
	activeRefr = nullptr;
	activeLigh = nullptr;
	activeIsRef = false;
	activeEmittanceSource = nullptr;
	emittanceColorActive = false;
}

void LightEditor::RequestRefRefresh(RE::TESObjectREFR* refr)
{
	if (!refr)
		return;
	// Flush a still-pending refresh on a different reference so it doesn't stay disabled.
	if (pendingRefreshFrames > 0) {
		if (auto prev = pendingRefreshRefr.get(); prev && prev.get() != refr)
			prev->Enable(false);
	}
	refr->Disable();
	pendingRefreshRefr = RE::ObjectRefHandle(refr);
	pendingRefreshFrames = kRefreshEnableDelay;
	// Hold off resampling until after the deferred Enable so we don't read a mid-rebuild reference.
	waitFrames = std::max(waitFrames, kRefreshEnableDelay + 1);
}

void LightEditor::UpdateRefRefresh()
{
	if (pendingRefreshFrames <= 0)
		return;
	if (--pendingRefreshFrames == 0) {
		if (auto refr = pendingRefreshRefr.get())
			refr->Enable(false);
		pendingRefreshRefr = {};
	}
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
	ctx.ownerEditorId = lpInfo.ownerEditorId;
	ctx.baseFormId = (activeRefr && activeRefr->GetObjectReference()) ? activeRefr->GetObjectReference()->formID : 0;
	ctx.lightEDID = lpInfo.lightEDID;
	ctx.refr = activeRefr;
	return ctx;
}

LightEditor::MatchContext LightEditor::MakePickedContext(const std::string& lightEDID) const
{
	MatchContext ctx;
	ctx.ownerModelPath = pickedMesh.modelPath;
	ctx.ownerEditorId = pickedMesh.editorId;
	ctx.baseFormId = pickedMesh.baseFormId;
	ctx.lightEDID = lightEDID;
	ctx.refr = PickedRefr();
	return ctx;
}

bool LightEditor::MatchesLPFilters(const nlohmann::ordered_json& lightEntry, RE::TESObjectREFR* refr)
{
	if (!refr)
		return true;

	auto matchesEntry = [&](const std::string& entry) -> bool {
		if (entry.find('~') != std::string::npos || HasHexPrefix(entry)) {
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
	if (!LoadConfigArray(lpInfo.configPath, out)) {
		logger::warn("[LightEditor] Could not load Light Placer config: {}", lpInfo.configPath);
		return false;
	}
	return true;
}

bool LightEditor::EntryMatchesContext(const nlohmann::ordered_json& entry, const MatchContext& ctx)
{
	const std::string normalizedOwner = NormalizeModelPath(ctx.ownerModelPath);
	if (auto* models = GetArrayMember(entry, "models"); !normalizedOwner.empty() && models)
		for (const auto& v : *models)
			if (v.is_string() && NormalizeModelPath(v.get<std::string>()) == normalizedOwner)
				return true;
	if (auto* formIDs = GetArrayMember(entry, "formIDs"); formIDs)
		for (const auto& v : *formIDs) {
			if (!v.is_string())
				continue;
			const std::string s = v.get<std::string>();
			if (s.find('~') == std::string::npos && !HasHexPrefix(s)) {
				if (!ctx.ownerEditorId.empty() && s == ctx.ownerEditorId)
					return true;
			} else if (ctx.baseFormId != 0) {
				const RE::FormID resolved = ResolveFormEntry(s);
				if (resolved != 0 && resolved == ctx.baseFormId)
					return true;
			}
		}
	return false;
}

nlohmann::ordered_json* LightEditor::FindMatchingLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, bool applyFilters) const
{
	for (auto& entry : configArray) {
		auto lightsIt = entry.find("lights");
		if (lightsIt == entry.end() || !lightsIt->is_array())
			continue;

		if (!EntryMatchesContext(entry, ctx))
			continue;

		for (auto& lightEntry : *lightsIt) {
			const auto* edid = GetLightEntryEdid(lightEntry);
			if (!edid || *edid != ctx.lightEDID)
				continue;
			if (applyFilters && !MatchesLPFilters(lightEntry, ctx.refr))
				continue;
			return &lightEntry;
		}
	}
	return nullptr;
}

bool LightEditor::LocateLightEntry(nlohmann::ordered_json& configArray, const MatchContext& ctx, LightEntryLocation& out) const
{
	for (size_t t = 0; t < configArray.size(); ++t) {
		auto& topEntry = configArray[t];
		auto lightsIt = topEntry.find("lights");
		if (lightsIt == topEntry.end() || !lightsIt->is_array())
			continue;
		if (!EntryMatchesContext(topEntry, ctx))
			continue;
		for (size_t i = 0; i < lightsIt->size(); ++i) {
			auto& le = (*lightsIt)[i];
			const auto* edid = GetLightEntryEdid(le);
			if (!edid || *edid != ctx.lightEDID)
				continue;
			if (!MatchesLPFilters(le, ctx.refr))
				continue;
			out.topEntry = &topEntry;
			out.topIdx = t;
			out.lightsArr = &*lightsIt;
			out.lightIdx = i;
			return true;
		}
	}
	return false;
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

	auto& data = (*matchedEntry)["data"];
	data = BuildEditedData(data, includeColor);

	SetFirstPointFromPos(*matchedEntry, current.pos);

	NormalizeConfig(configArray);

	const auto filePath = LPConfigFilePath(lpInfo.configPath);
	if (!WriteLPConfig(filePath, configArray))
		return false;

	original.pos = current.pos;
	logger::info("[LightEditor] Saved light settings to {}", filePath.string());
	return true;
}

nlohmann::ordered_json LightEditor::BuildEditedData(const nlohmann::ordered_json& existingData, bool includeColor) const
{
	const bool isInvSq = lpFlagSet.contains("InverseSquare");
	std::string newFlags;
	for (const auto& flag : lpFlagSet) {
		if (!newFlags.empty())
			newFlags += "|";
		newFlags += flag;
	}

	nlohmann::ordered_json newData;
	// Only mutate color when the user opts in via the Save checkbox; otherwise leave the existing color untouched.
	if (includeColor) {
		// Light Placer stores color as 0-255 integers; current.data.diffuse is normalized 0-1.
		auto toByte = [](float c) { return static_cast<int>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); };
		newData["color"] = { toByte(current.data.diffuse.red), toByte(current.data.diffuse.green), toByte(current.data.diffuse.blue) };
	} else if (existingData.contains("color")) {
		newData["color"] = existingData["color"];
	}
	// Persist the edited bulb type (LIGH form); fall back to the existing value if it has no EditorID.
	const std::string editedLighEdid = LighEdidForFormId(current.data.lighFormId);
	newData["light"] = editedLighEdid.empty() ? existingData.at("light") : nlohmann::ordered_json(editedLighEdid);
	newData["fade"] = current.data.fade;
	if (isInvSq) {
		newData["size"] = current.data.size;
		newData["cutoff"] = current.data.cutoffOverride;
	} else {
		newData["radius"] = current.data.radius;
	}
	if (existingData.contains("shadowDepthBias"))
		newData["shadowDepthBias"] = shadowDepthBias;
	// Write the source only when set; otherwise omit it so the line is removed (None or
	// NoExternalEmittance both clear it). The editor re-reads it from the JSON on selection.
	if (useExternalEmittance && !externalEmittanceEdid.empty())
		newData["externalEmittance"] = externalEmittanceEdid;
	if (!newFlags.empty())
		newData["flags"] = newFlags;
	if (existingData.contains("offset"))
		newData["offset"] = existingData["offset"];
	if (existingData.contains("rotation"))
		newData["rotation"] = existingData["rotation"];
	for (auto& [key, val] : existingData.items())
		if (std::ranges::find(kManagedDataKeys, key) == kManagedDataKeys.end())
			newData[key] = val;
	return newData;
}

void LightEditor::NormalizeConfig(nlohmann::ordered_json& configArray)
{
	// Normalise key order for every data and lightEntry in the file so the whole config is consistent.
	auto normalizeData = [](nlohmann::ordered_json& d) {
		nlohmann::ordered_json nd;
		for (const char* key : kManagedDataKeys)
			if (d.contains(key))
				nd[key] = d[key];
		for (auto& [key, val] : d.items())
			if (std::ranges::find(kManagedDataKeys, key) == kManagedDataKeys.end())
				nd[key] = val;
		d = std::move(nd);
	};

	auto normalizeLightEntry = [&normalizeData](nlohmann::ordered_json& le) {
		if (le.contains("data"))
			normalizeData(le["data"]);
		nlohmann::ordered_json newEntry;
		if (le.contains("data"))
			newEntry["data"] = le["data"];
		if (le.contains("points"))
			newEntry["points"] = le["points"];
		if (le.contains("nodes"))
			newEntry["nodes"] = le["nodes"];
		if (le.contains("whiteList"))
			newEntry["whiteList"] = le["whiteList"];
		if (le.contains("blackList"))
			newEntry["blackList"] = le["blackList"];
		for (auto& [key, val] : le.items())
			if (std::ranges::find(kManagedEntryKeys, key) == kManagedEntryKeys.end())
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
}

bool LightEditor::SaveAsSeparateEntry(bool includeColor)
{
	if (!lpInfo.isLPLight || !activeRefr)
		return false;

	const std::string ownerEntry = FormatOwnerFormEntry(activeRefr);
	if (ownerEntry.empty())
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadLPConfig(configArray))
		return false;

	const MatchContext ctx = MakeSelectedContext();

	// Locate the governing light entry (+ parent array/index) so the fork can be inserted as the next sibling.
	LightEntryLocation loc;
	if (!LocateLightEntry(configArray, ctx, loc)) {
		logger::warn("[LightEditor] SaveAsSeparateEntry: no matching entry for model '{}' with light EDID '{}' in {}.json",
			lpInfo.ownerModelPath, lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	nlohmann::ordered_json* lightsArr = loc.lightsArr;
	const size_t lightIdx = loc.lightIdx;
	auto& sourceEntry = (*lightsArr)[lightIdx];

	// Track whether this ref is whitelisted, and whether the whiteList is its alone (a prior fork);
	// a shared whiteList is not a fork and may still be split off.
	bool ownerWhitelisted = false;
	if (auto* wl = GetArrayMember(sourceEntry, "whiteList")) {
		ownerWhitelisted = ArrayContainsString(*wl, ownerEntry);
		if (ownerWhitelisted && wl->size() == 1) {
			logger::info("[LightEditor] SaveAsSeparateEntry: {} already has its own whitelisted entry", ownerEntry);
			return false;
		}
	}

	// Fork: deep copy, apply the current editor edits, whitelist only this reference.
	nlohmann::ordered_json forkedEntry = sourceEntry;
	forkedEntry["data"] = BuildEditedData(sourceEntry["data"], includeColor);

	SetFirstPointFromPos(forkedEntry, current.pos);

	forkedEntry.erase("blackList");
	forkedEntry["whiteList"] = nlohmann::ordered_json::array({ ownerEntry });

	// Exclude this ref from the source so it resolves to the fork: drop from a shared whiteList, else blacklist.
	if (ownerWhitelisted)
		MutateFilterList(sourceEntry, "whiteList", ownerEntry, false);
	else
		MutateFilterList(sourceEntry, "blackList", ownerEntry, true);

	lightsArr->insert(lightsArr->begin() + lightIdx + 1, std::move(forkedEntry));

	NormalizeConfig(configArray);

	const auto filePath = LPConfigFilePath(lpInfo.configPath);
	if (!WriteLPConfig(filePath, configArray))
		return false;

	logger::info("[LightEditor] SaveAsSeparateEntry: forked {} into its own whitelisted entry in {}", ownerEntry, filePath.string());
	return true;
}

bool LightEditor::DeleteFromLightPlacer()
{
	if (!lpInfo.isLPLight)
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadLPConfig(configArray))
		return false;

	LightEntryLocation loc;
	if (!LocateLightEntry(configArray, MakeSelectedContext(), loc)) {
		logger::warn("[LightEditor] DeleteFromLightPlacer: no matching entry for model '{}' with light EDID '{}' in {}.json",
			lpInfo.ownerModelPath, lpInfo.lightEDID, lpInfo.configPath);
		return false;
	}

	// Removing the only light would leave a dangling models/formIDs block, so drop the whole top-level
	// entry; otherwise remove just this light.
	if (loc.lightsArr->size() <= 1)
		configArray.erase(configArray.begin() + loc.topIdx);
	else
		loc.lightsArr->erase(loc.lightsArr->begin() + loc.lightIdx);

	const auto filePath = LPConfigFilePath(lpInfo.configPath);
	if (!WriteLPConfig(filePath, configArray))
		return false;

	logger::info("[LightEditor] Deleted light entry (model '{}', light '{}') from {}", lpInfo.ownerModelPath, lpInfo.lightEDID, filePath.string());
	return true;
}

void LightEditor::DrawDeleteConfirmation()
{
	if (deleteConfirmPopupRequested) {
		ImGui::OpenPopup(T(TKEY("delete_entry"), "Delete"));
		deleteConfirmPopupRequested = false;
	}

	if (auto popup = Util::CenteredPopupModal(T(TKEY("delete_entry"), "Delete"))) {
		ImGui::Text("%s", T(TKEY("confirm_delete_entry"), "Delete this light entry from the Light Placer JSON?"));
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (Util::ErrorButton(T(TKEY("yes_delete"), "Yes, Delete"))) {
			const bool ok = DeleteFromLightPlacer();
			if (ok) {
				RestoreOriginal();
				selected = {};
				previous = {};
				waitFrames = 3;
				ScheduleConsoleCommand("reloadlp");
			}
			NotifyResult(ok,
				T(TKEY("deleted_entry"), "Deleted entry"),
				T(TKEY("delete_failed"), "Delete failed \xe2\x80\x94 see log"));
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("cancel"), "Cancel")))
			ImGui::CloseCurrentPopup();
	}
}

void LightEditor::SortLights()
{
	// Other lights have no FormID/EditorID, so those sort modes fall back to None.
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
	// Single source of truth lives in the picker, which owns reference-identity resolution.
	return LightPicker::FormatRefFormEntry(refr);
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

	// Apply WL/BL filters to resolve the entry that actually governs this reference; without them a
	// model/formID with two entries for the same light resolves to whichever appears first (wrong fade/flags).
	const auto* lightEntry = FindMatchingLightEntry(configArray, MakeSelectedContext(), true);
	if (!lightEntry)
		return;

	if (!ownerEntry.empty()) {
		auto containsEntry = [&](const char* listKey) {
			const auto* list = GetArrayMember(*lightEntry, listKey);
			return list && ArrayContainsString(*list, ownerEntry);
		};
		lpInWhitelist = containsEntry("whiteList");
		lpInBlacklist = containsEntry("blackList");
	}

	const auto dataIt = lightEntry->find("data");
	if (dataIt != lightEntry->end() && dataIt->is_object()) {
		// Base intensity from the LP JSON, not the runtime snapshot: the live fade is Flicker-modulated, so a
		// snapshot freezes a random point. Applied to original (Reset) and current (the Intensity slider).
		if (const auto fadeIt = dataIt->find("fade"); fadeIt != dataIt->end() && fadeIt->is_number()) {
			const float jsonFade = fadeIt->get<float>();
			original.data.fade = jsonFade;
			current.data.fade = jsonFade;
		}

		// Likewise radius from JSON, not the snapshot (ProcessLight inflates runtimeData->radius for IS bulbs).
		// JSON "radius" is the authored non-IS value; IS bulbs omit it and persist size/cutoff instead.
		if (const auto radiusIt = dataIt->find("radius"); radiusIt != dataIt->end() && radiusIt->is_number()) {
			const float jsonRadius = radiusIt->get<float>();
			original.data.radius = jsonRadius;
			current.data.radius = jsonRadius;
		}

		const auto flagsIt = dataIt->find("flags");
		if (flagsIt != dataIt->end() && flagsIt->is_string()) {
			std::istringstream ss(flagsIt->get<std::string>());
			std::string flag;
			while (std::getline(ss, flag, '|')) {
				if (!flag.empty())
					lpFlagSet.insert(flag);
			}
		}

		// Emittance source is authored in the JSON (no reliable runtime extra); resolving it to a form lets
		// color tracking follow its live emittanceColor. NoExternalEmittance suppresses it (show None).
		if (const auto emitIt = dataIt->find("externalEmittance");
			!lpFlagSet.contains("NoExternalEmittance") && emitIt != dataIt->end() && emitIt->is_string()) {
			if (const std::string entry = emitIt->get<std::string>(); !entry.empty()) {
				EnsureEmittanceFormListBuilt();
				RE::TESForm* form = nullptr;
				for (auto& [edid, f] : s_emittanceFormList)
					if (edid == entry) {
						form = f;
						break;
					}
				if (!form)
					if (const auto formId = ResolveFormEntry(entry); formId != 0)
						form = RE::TESForm::LookupByID(formId);
				activeEmittanceSource = form;
				externalEmittanceEdid = form ? clib_util::editorID::get_editorID(form) : entry;
				useExternalEmittance = true;
			}
		}
	}

	SyncLPFlagsToRuntime();

	// SyncLPFlagsToRuntime only updates `current`; apply the JSON InverseSquare/Linear state to `original`
	// too, so RestoreOriginal can't re-assert a stale IS bit (which makes ProcessLight re-inflate the radius).
	ApplyLPFalloffFlags(original.data, lpFlagSet);
}

void LightEditor::ApplyLPFalloffFlags(ISLCommon::RuntimeLightDataExt& data, const std::set<std::string>& lpFlagSet)
{
	auto apply = [&](LightLimitFix::LightFlags bit, const char* name) {
		if (lpFlagSet.contains(name))
			data.flags.set(bit);
		else
			data.flags.reset(bit);
	};
	apply(LightLimitFix::LightFlags::InverseSquare, "InverseSquare");
	apply(LightLimitFix::LightFlags::Linear, "Linear");
}

void LightEditor::SyncLPFlagsToRuntime()
{
	if (!lpInfo.isLPLight)
		return;

	ApplyLPFalloffFlags(current.data, lpFlagSet);

	auto& tesUnderlying = reinterpret_cast<uint32_t&>(current.tesFlags);
	auto syncTesBit = [&](RE::TES_LIGHT_FLAGS bit, bool val) {
		const auto mask = static_cast<uint32_t>(bit);
		if (val)
			tesUnderlying |= mask;
		else
			tesUnderlying &= ~mask;
	};
	syncTesBit(RE::TES_LIGHT_FLAGS::kFlicker, lpFlagSet.contains("Flicker"));
	syncTesBit(RE::TES_LIGHT_FLAGS::kOmniShadow, lpFlagSet.contains("Shadow"));
	syncTesBit(RE::TES_LIGHT_FLAGS::kPortalStrict, lpFlagSet.contains("PortalStrict"));
}

void LightEditor::MutateFilterList(nlohmann::ordered_json& lightEntry, const char* listKey, const std::string& ownerEntry, bool add)
{
	if (add) {
		auto& list = lightEntry[listKey];
		if (!list.is_array())
			list = nlohmann::ordered_json::array();
		if (ArrayContainsString(list, ownerEntry))
			return;
		list.push_back(ownerEntry);
	} else {
		// Use find, not operator[]: indexing a missing key would materialize a stray "listKey": null
		// on this entry. Removal only ever runs on an entry already known to hold the value.
		const auto it = lightEntry.find(listKey);
		if (it == lightEntry.end() || !it->is_array())
			return;
		auto& list = *it;
		list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& elem) {
			return elem.is_string() && elem.template get<std::string>() == ownerEntry;
		}),
			list.end());
		if (list.empty())
			lightEntry.erase(listKey);
	}
}

bool LightEditor::ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, const std::string& entryStr, bool isWhiteList, bool add)
{
	if (entryStr.empty())
		return false;

	nlohmann::ordered_json configArray;
	if (!LoadConfigArray(configPath, configArray))
		return false;

	const char* listKey = isWhiteList ? "whiteList" : "blackList";

	nlohmann::ordered_json* lightEntry = nullptr;
	if (add) {
		// Prefer the entry that governs this ref (peer forks can share a model/formID + light EDID with
		// disjoint whiteLists); fall back to the first match when the ref isn't covered yet.
		lightEntry = FindMatchingLightEntry(configArray, ctx, true);
		if (!lightEntry)
			lightEntry = FindMatchingLightEntry(configArray, ctx, false);
	} else {
		// Removal must target the entry that actually holds entryStr in listKey. Several entries can exist
		// for the same light, so the first match often removes nothing and leaves a stray empty list.
		for (auto& entry : configArray) {
			auto lightsIt = entry.find("lights");
			if (lightsIt == entry.end() || !lightsIt->is_array())
				continue;
			if (!EntryMatchesContext(entry, ctx))
				continue;
			for (auto& le : *lightsIt) {
				const auto* edid = GetLightEntryEdid(le);
				if (!edid || *edid != ctx.lightEDID)
					continue;
				const auto* list = GetArrayMember(le, listKey);
				if (list && ArrayContainsString(*list, entryStr)) {
					lightEntry = &le;
					break;
				}
			}
			if (lightEntry)
				break;
		}
	}

	if (!lightEntry)
		return false;

	MutateFilterList(*lightEntry, listKey, entryStr, add);
	return WriteLPConfig(LPConfigFilePath(configPath), configArray);
}

bool LightEditor::ModifyLPFilterListFor(const std::string& configPath, const MatchContext& ctx, bool isWhiteList, bool add)
{
	if (!ctx.refr)
		return false;
	const std::string ownerEntry = FormatOwnerFormEntry(ctx.refr);
	return ModifyLPFilterListFor(configPath, ctx, ownerEntry, isWhiteList, add);
}

bool LightEditor::ModifyLPFilterList(bool isWhiteList, bool add)
{
	if (!lpInfo.isLPLight || !activeRefr)
		return false;
	return ModifyLPFilterListFor(lpInfo.configPath, MakeSelectedContext(), isWhiteList, add);
}
