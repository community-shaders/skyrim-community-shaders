#include "ExtendedEffect.h"

#ifdef ENABLE_ENB_EXTENDER

#include <sstream>

#include "../EffectManager.h"
#include "../PresetManager.h"
#include "../SettingManager.h"
#include "../WeatherManager.h"
#include "Globals.h"

void ExtendedEffect::Unload()
{
	weatherData.clear();
	dirtyWeatherFiles.clear();
	bindingCache.clear();
	Effect::Unload();
}

// Technique evaluation

int ExtendedEffect::ResolveTechniqueBinding(const std::string& variableName)
{
	auto cacheIt = bindingCache.find(variableName);
	if (cacheIt != bindingCache.end())
		return cacheIt->second;

	for (int i = 0; i < static_cast<int>(uiVariables.size()); ++i) {
		auto& uiVar = uiVariables[i];
		const std::string& uname = !uiVar.uniqueName.empty() ? uiVar.uniqueName
		                           : !uiVar.group.empty()    ? uiVar.group + "." + uiVar.displayName
		                                                     : uiVar.displayName;
		if (uname == variableName) {
			bindingCache[variableName] = i;
			return i;
		}
	}

	bindingCache[variableName] = -2;
	return -2;
}

bool ExtendedEffect::IsTechniqueEnabled(TechniqueInfo& info)
{
	for (auto& binding : info.bindings) {
		int idx = ResolveTechniqueBinding(binding.variableName);
		if (idx < 0)
			continue;

		auto& uiVar = uiVariables[idx];
		bool val = false;
		switch (uiVar.type) {
		case UIVariableType::Bool: val = uiVar.boolValue; break;
		case UIVariableType::Int: val = uiVar.intValue != 0; break;
		case UIVariableType::Float: val = uiVar.floatValue != 0.0f; break;
		default: val = true; break;
		}

		if (binding.inverted ? !val : val)
			continue;
		return false;
	}
	return true;
}

// Time-of-day interpolation

float ExtendedEffect::GetPeriodWeight(const std::string& period)
{
	auto& cd = EffectManager::GetSingleton().commonData;
	if (period == "Dawn") return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Dawn)];
	if (period == "Sunrise") return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunrise)];
	if (period == "Day") return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Day)];
	if (period == "Sunset") return cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunset)];
	if (period == "Dusk") return cd.timeOfDay2[static_cast<int>(TimeOfDay2Index::Dusk)];
	if (period == "Night") return cd.timeOfDay2[static_cast<int>(TimeOfDay2Index::Night)];
	if (period == "Interior") return cd.eInteriorFactor;
	return 0.0f;
}

void ExtendedEffect::ApplyTimeOfDayInterpolation()
{
	struct PeriodVar
	{
		size_t index;
		float weight;
	};
	std::unordered_map<std::string, std::vector<PeriodVar>> baseGroups;

	for (size_t i = 0; i < uiVariables.size(); ++i) {
		auto& uiVar = uiVariables[i];
		if (uiVar.timePeriod.empty() || !uiVar.effectVariable)
			continue;
		auto& name = uiVar.name;
		auto& period = uiVar.timePeriod;
		if (name.size() <= period.size() || name.compare(name.size() - period.size(), period.size(), period) != 0)
			continue;
		baseGroups[name.substr(0, name.size() - period.size())].push_back({ i, GetPeriodWeight(period) });
	}

	auto& cd = EffectManager::GetSingleton().commonData;

	for (auto& [baseName, entries] : baseGroups) {
		auto baseVarIt = variables.find(baseName);
		if (baseVarIt == variables.end())
			continue;
		auto* baseVar = baseVarIt->second.get();
		if (!baseVar || !baseVar->IsValid())
			continue;

		auto& sep = uiVariables[entries[0].index].separation;
		if (sep == "ExteriorWeather" && cd.eInteriorFactor > 0.0f)
			continue;

		float totalWeight = 0.0f;
		for (auto& e : entries)
			totalWeight += e.weight;
		if (totalWeight <= 0.0f)
			continue;

		auto& firstVar = uiVariables[entries[0].index];

		if (firstVar.type == UIVariableType::Float) {
			float result = 0.0f;
			for (auto& e : entries)
				result += uiVariables[e.index].floatValue * (e.weight / totalWeight);
			baseVar->AsScalar()->SetFloat(result);
		} else {
			int comps = (firstVar.type == UIVariableType::Float2) ? 2 : (firstVar.type == UIVariableType::Float3) ? 3 : 4;
			float result[4] = {};
			for (auto& e : entries) {
				float w = e.weight / totalWeight;
				for (int c = 0; c < comps; ++c)
					result[c] += uiVariables[e.index].vectorValue[c] * w;
			}
			baseVar->AsVector()->SetFloatVector(result);
		}
	}
}

// Weather blending

/** @brief True when the preset's per-weather overrides are active. */
static bool IsMultipleWeathersEnabled()
{
	return SettingManager::GetSingleton().GetValue<bool>(EffectManager::GetSingleton().ids.enableMultipleWeathers);
}

void ExtendedEffect::LoadWeatherData()
{
	weatherData.clear();
	dirtyWeatherFiles.clear();

	std::string section = GetName();
	std::transform(section.begin(), section.end(), section.begin(), ::toupper);

	auto& weatherManager = WeatherManager::GetSingleton();
	const auto& weatherEntries = weatherManager.GetWeatherEntries();

	for (const auto& [key, entry] : weatherEntries) {
		std::filesystem::path filePath = PresetManager::GetSingleton().GetENBSeriesPath() / entry.fileName;
		if (!std::filesystem::exists(filePath))
			continue;

		std::string filePathStr = filePath.string();

		WeatherValues values;
		for (const auto& uiVar : uiVariables) {
			if (uiVar.isLabel)
				continue;
			if (!uiVar.effectVariable && !uiVar.isDefine)
				continue;
			if (!IsWeatherSeparated(uiVar))
				continue;

			std::string iniKey = GetVariableIniKey(uiVar);
			if (iniKey.empty())
				continue;

			if (IsPerComponentVector(uiVar)) {
				static const char* suffixes[] = { "X", "Y", "Z", "W" };
				int comps = (uiVar.type == UIVariableType::Float2) ? 2 : (uiVar.type == UIVariableType::Float3) ? 3 : 4;
				for (int c = 0; c < comps; ++c) {
					std::string compKey = iniKey + suffixes[c];
					char buffer[256];
					DWORD result = GetPrivateProfileStringA(section.c_str(), compKey.c_str(), "", buffer, sizeof(buffer), filePathStr.c_str());
					if (result > 0)
						values[compKey] = buffer;
				}
			} else {
				char buffer[1024];
				DWORD result = GetPrivateProfileStringA(section.c_str(), iniKey.c_str(), "", buffer, sizeof(buffer), filePathStr.c_str());
				if (result > 0)
					values[iniKey] = buffer;
			}
		}

		if (!values.empty()) {
			for (uint32_t weatherID : entry.weatherIDs)
				weatherData[weatherID] = values;
		}
	}

	if (!weatherData.empty())
		logger::info("[ExtendedEffect] Loaded weather data for '{}' ({} weathers)", GetName(), weatherData.size());
}

void ExtendedEffect::ApplyWeatherBlending(float blendFactor, uint32_t currentWeatherID, uint32_t lastWeatherID)
{
	const WeatherValues* currentValues = nullptr;
	const WeatherValues* lastValues = nullptr;
	if (!weatherData.empty() && IsMultipleWeathersEnabled()) {
		if (auto it = weatherData.find(currentWeatherID); it != weatherData.end())
			currentValues = &it->second;
		if (auto it = weatherData.find(lastWeatherID); it != weatherData.end())
			lastValues = &it->second;
	}

	auto safeStof = [](const std::string& s, float fallback) -> float {
		try { return std::stof(s); } catch (...) { return fallback; }
	};

	for (auto& uiVar : uiVariables) {
		if (uiVar.isLabel)
			continue;
		if (!uiVar.effectVariable && !uiVar.isDefine)
			continue;
		if (!IsWeatherSeparated(uiVar))
			continue;

		std::string iniKey = GetVariableIniKey(uiVar);
		if (iniKey.empty())
			continue;

		switch (uiVar.type) {
		case UIVariableType::Float:
			{
				auto getVal = [&](const WeatherValues* vals) -> float {
					if (!vals) return uiVar.baseFloatValue;
					auto it = vals->find(iniKey);
					if (it == vals->end()) return uiVar.baseFloatValue;
					return safeStof(it->second, uiVar.baseFloatValue);
				};

				float currentVal = getVal(currentValues);
				float lastVal = getVal(lastValues);
				uiVar.floatValue = lastVal + blendFactor * (currentVal - lastVal);
				if (uiVar.effectVariable)
					uiVar.effectVariable->AsScalar()->SetFloat(uiVar.floatValue);
				break;
			}
		case UIVariableType::Float2:
		case UIVariableType::Float3:
		case UIVariableType::Float4:
			{
				int comps = (uiVar.type == UIVariableType::Float2) ? 2 : (uiVar.type == UIVariableType::Float3) ? 3 : 4;
				bool perComp = IsPerComponentVector(uiVar);

				auto parseVec = [&](const WeatherValues* vals, float* out) {
					memcpy(out, uiVar.baseVectorValue, sizeof(float) * comps);
					if (!vals)
						return;
					if (perComp) {
						static const char* suffixes[] = { "X", "Y", "Z", "W" };
						for (int c = 0; c < comps; ++c) {
							auto it = vals->find(iniKey + suffixes[c]);
							if (it != vals->end())
								out[c] = safeStof(it->second, uiVar.baseVectorValue[c]);
						}
					} else {
						auto it = vals->find(iniKey);
						if (it != vals->end()) {
							std::stringstream ss(it->second);
							std::string item;
							for (int c = 0; c < comps && std::getline(ss, item, ','); ++c)
								out[c] = safeStof(item, uiVar.baseVectorValue[c]);
						}
					}
				};

				float currentVals[4] = {}, lastVals[4] = {};
				parseVec(currentValues, currentVals);
				parseVec(lastValues, lastVals);

				for (int c = 0; c < comps; ++c)
					uiVar.vectorValue[c] = lastVals[c] + blendFactor * (currentVals[c] - lastVals[c]);

				if (uiVar.effectVariable)
					uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.vectorValue);
				break;
			}
		default:
			break;
		}
	}
}

void ExtendedEffect::SyncWeatherVarFromUI(size_t index, uint32_t weatherID)
{
	if (index >= uiVariables.size())
		return;

	auto& uiVar = uiVariables[index];
	const bool isVector = uiVar.type == UIVariableType::Float2 || uiVar.type == UIVariableType::Float3 || uiVar.type == UIVariableType::Float4;
	if (uiVar.type != UIVariableType::Float && !isVector)
		return;

	// Must match ApplyWeatherBlending: with weather overrides off, edits belong to the base value
	auto* entry = IsWeatherSeparated(uiVar) && IsMultipleWeathersEnabled() ? WeatherManager::GetSingleton().FindWeatherEntry(weatherID) : nullptr;
	std::string iniKey = GetVariableIniKey(uiVar);
	if (!entry || iniKey.empty()) {
		CaptureBaseValue(uiVar);
		return;
	}

	std::vector<std::pair<std::string, std::string>> updates;
	if (uiVar.type == UIVariableType::Float) {
		updates.emplace_back(iniKey, std::to_string(uiVar.floatValue));
	} else {
		int comps = (uiVar.type == UIVariableType::Float2) ? 2 : (uiVar.type == UIVariableType::Float3) ? 3 : 4;
		if (IsPerComponentVector(uiVar)) {
			static const char* suffixes[] = { "X", "Y", "Z", "W" };
			for (int c = 0; c < comps; ++c)
				updates.emplace_back(iniKey + suffixes[c], std::to_string(uiVar.vectorValue[c]));
		} else {
			std::string val;
			for (int c = 0; c < comps; ++c) {
				if (c > 0) val += ", ";
				val += std::to_string(uiVar.vectorValue[c]);
			}
			updates.emplace_back(iniKey, val);
		}
	}

	for (uint32_t linkedID : entry->weatherIDs) {
		auto& values = weatherData[linkedID];
		for (const auto& [key, value] : updates)
			values[key] = value;
	}

	auto& dirtyKeys = dirtyWeatherFiles[entry->fileName];
	for (const auto& [key, value] : updates)
		dirtyKeys[key] = weatherID;
}

void ExtendedEffect::SaveWeatherOverrides()
{
	if (dirtyWeatherFiles.empty())
		return;

	std::string section = GetName();
	std::transform(section.begin(), section.end(), section.begin(), ::toupper);

	for (const auto& [fileName, dirtyKeys] : dirtyWeatherFiles) {
		std::string filePath = (PresetManager::GetSingleton().GetENBSeriesPath() / fileName).string();
		for (const auto& [key, sourceWeatherID] : dirtyKeys) {
			auto valuesIt = weatherData.find(sourceWeatherID);
			if (valuesIt == weatherData.end())
				continue;
			auto it = valuesIt->second.find(key);
			if (it != valuesIt->second.end() && !WritePrivateProfileStringA(section.c_str(), key.c_str(), it->second.c_str(), filePath.c_str()))
				logger::warn("[EFFECTS11] Failed to write key '{}' to weather file '{}'", key, filePath);
		}
		WritePrivateProfileStringA(NULL, NULL, NULL, filePath.c_str());
		logger::info("[EFFECTS11] Saved {} weather override(s) to '{}' for effect '{}'", dirtyKeys.size(), filePath, GetName());
	}

	dirtyWeatherFiles.clear();
}

// Rendering

#include <format>

#include "../ENBExtender.h"
#include "../Editor/EditorWidgets.h"
#include "../UITree.h"
#include "I18n/I18n.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.effects11.params."

namespace
{
	float SafeStofLocal(const std::string& s, float fallback = 0.0f)
	{
		return ENBExtender::SafeStof(s, fallback);
	}

	bool EvaluateCondition(const std::string& condStr, float boundValue)
	{
		if (condStr.empty())
			return boundValue != 0.0f;
		size_t valueStart = 0;
		if (condStr.size() >= 2 && !std::isdigit(static_cast<unsigned char>(condStr[1])) && condStr[1] != '-')
			valueStart = 2;
		else if (condStr[0] == '<' || condStr[0] == '>')
			valueStart = 1;
		else
			return boundValue != 0.0f;
		float cmp = SafeStofLocal(condStr.substr(valueStart));
		char c0 = condStr[0], c1 = (condStr.size() >= 2) ? condStr[1] : '\0';
		if (c0 == '=' && c1 == '=') return boundValue == cmp;
		if (c0 == '!' && c1 == '=') return boundValue != cmp;
		if (c0 == '<' && c1 == '=') return boundValue <= cmp;
		if (c0 == '>' && c1 == '=') return boundValue >= cmp;
		if (c0 == '=' && c1 == '<') return boundValue <= cmp;
		if (c0 == '=' && c1 == '>') return boundValue >= cmp;
		if (c0 == '<') return boundValue < cmp;
		if (c0 == '>') return boundValue > cmp;
		return false;
	}

	using FileUniqueNameMap = std::unordered_map<std::string, std::unordered_map<std::string, UITree::VarRef>>;

	std::pair<bool, bool> EvaluateBinding(const Effect::UIVariable& var,
		const std::unordered_map<std::string, UITree::VarRef>& uniqueNameMap,
		const FileUniqueNameMap& fileUniqueNameMap)
	{
		bool visible = true, readOnly = var.isReadOnly;
		if (var.uiBindings.empty())
			return { visible, readOnly };

		for (const auto& binding : var.uiBindings) {
			const UITree::VarRef* boundRef = nullptr;
			if (!binding.file.empty()) {
				auto fileIt = fileUniqueNameMap.find(binding.file);
				if (fileIt != fileUniqueNameMap.end()) {
					auto varIt = fileIt->second.find(binding.target);
					if (varIt != fileIt->second.end())
						boundRef = &varIt->second;
				}
			} else {
				auto it = uniqueNameMap.find(binding.target);
				if (it != uniqueNameMap.end())
					boundRef = &it->second;
			}

			if (!boundRef)
				continue;

			const auto& bv = boundRef->effect->uiVariables[boundRef->index];
			float val = 0.0f;
			switch (bv.type) {
			case Effect::UIVariableType::Float: val = bv.floatValue; break;
			case Effect::UIVariableType::Int: val = static_cast<float>(bv.intValue); break;
			case Effect::UIVariableType::Bool: val = bv.boolValue ? 1.0f : 0.0f; break;
			default: break;
			}

			bool cond = EvaluateCondition(binding.condition, val);
			if (binding.inverted)
				cond = !cond;

			std::string prop = binding.property;
			std::transform(prop.begin(), prop.end(), prop.begin(), ::tolower);
			if (prop == "hidden") { if (cond) visible = false; }
			else if (prop == "visible") { if (!cond) visible = false; }
			else if (prop == "readonly") { if (cond) readOnly = true; }
			else if (prop == "readwrite") { if (!cond) readOnly = true; }
			else { if (!cond) visible = false; }
		}

		return { visible, readOnly };
	}

	bool IsVarVisible(const Effect::UIVariable& uiVar)
	{
		return !uiVar.displayName.empty() && !uiVar.isHidden;
	}

	struct RenderContext
	{
		std::unordered_map<std::string, UITree::VarRef>& uniqueNameMap;
		FileUniqueNameMap& fileUniqueNameMap;
		std::unordered_set<Effect*>& changedEffects;
		std::vector<std::pair<Effect*, size_t>>& changedVars;
		UITree::MetaMap& meta;
		UITree::ViewOptions& view;
		bool performanceMode = false;
		int tableCounter = 0;

		bool BeginVarTable()
		{
			std::string tableId = "##ut_" + std::to_string(tableCounter++);
			return Effects11UI::BeginPropertyTable(tableId.c_str());
		}

		bool Filtering() const { return !view.filter.empty(); }
	};

	bool MatchesFilter(const Effect::UIVariable& uiVar, const RenderContext& ctx)
	{
		return !ctx.Filtering() ||
		       Effects11UI::ContainsNoCase(uiVar.displayName, ctx.view.filter) ||
		       Effects11UI::ContainsNoCase(uiVar.name, ctx.view.filter);
	}

	bool IsPeriodShown(const Effect::UIVariable& uiVar, const RenderContext& ctx)
	{
		return uiVar.timePeriod.empty() || !ctx.view.showPeriod || ctx.view.showPeriod(uiVar.timePeriod);
	}

	std::string_view GroupDisplayName(const UITree::GroupNode& node, const UITree::MetaMap& meta)
	{
		auto it = meta.find(node.fullPath);
		if (it != meta.end() && !it->second.displayName.empty())
			return it->second.displayName;
		return node.name;
	}

	bool GroupMatches(const UITree::GroupNode& node, const RenderContext& ctx)
	{
		return ctx.Filtering() && Effects11UI::ContainsNoCase(GroupDisplayName(node, ctx.meta), ctx.view.filter);
	}

	bool IsColorVector(const Effect::UIVariable& uiVar)
	{
		return uiVar.widgetType == Effect::UIWidgetType::Color &&
		       (uiVar.type == Effect::UIVariableType::Float3 || uiVar.type == Effect::UIVariableType::Float4);
	}

	int ComponentCount(const Effect::UIVariable& uiVar)
	{
		switch (uiVar.type) {
		case Effect::UIVariableType::Float2:
			return 2;
		case Effect::UIVariableType::Float3:
			return 3;
		case Effect::UIVariableType::Float4:
			return 4;
		default:
			return 1;
		}
	}

	std::string FormatVector(const float* a_values, int a_count)
	{
		std::string text = "(";
		for (int i = 0; i < a_count; ++i) {
			if (i > 0)
				text += ", ";
			text += std::format("{:.3f}", a_values[i]);
		}
		return text + ")";
	}

	std::string FormatDefault(const Effect::UIVariable& uiVar)
	{
		switch (uiVar.type) {
		case Effect::UIVariableType::Float:
			return std::format("{:.3f}", uiVar.defaultFloatValue);
		case Effect::UIVariableType::Int:
			{
				const bool quality = uiVar.widgetType == Effect::UIWidgetType::Quality;
				const int item = quality ? uiVar.defaultIntValue + 1 : uiVar.defaultIntValue;
				if (!uiVar.dropdownItems.empty() && item >= 0 && item < static_cast<int>(uiVar.dropdownItems.size()))
					return uiVar.dropdownItems[item];
				return std::to_string(uiVar.defaultIntValue);
			}
		case Effect::UIVariableType::Bool:
			return uiVar.defaultBoolValue ? T(TKEY("on"), "On") : T(TKEY("off"), "Off");
		default:
			return FormatVector(uiVar.defaultVectorValue, ComponentCount(uiVar));
		}
	}

	const char* WeatherSeparatedTooltip()
	{
		return T(TKEY("weather_separated_tip"),
			"Weather-separated: each weather file keeps its own value.\n"
			"Edits change the value of the current weather.");
	}

	const char* CompileTimeTooltip()
	{
		return T(TKEY("compile_time_tip"),
			"Compile-time option: save, then click Reload Shaders to apply it.");
	}

	void DrawParameterTooltip(const Effect::UIVariable& uiVar)
	{
		if (!ImGui::BeginTooltip())
			return;
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
		ImGui::TextUnformatted(uiVar.displayName.c_str());
		Util::TextUnformattedDisabled(uiVar.name.c_str());

		const bool isDropdown = uiVar.type == Effect::UIVariableType::Int && !uiVar.dropdownItems.empty();
		if (uiVar.type == Effect::UIVariableType::Int && !isDropdown)
			ImGui::Text("%s %d - %d", T(TKEY("range"), "Range:"), uiVar.intMin, uiVar.intMax);
		else if (uiVar.type != Effect::UIVariableType::Int && uiVar.type != Effect::UIVariableType::Bool && !IsColorVector(uiVar))
			ImGui::Text("%s %.3f - %.3f", T(TKEY("range"), "Range:"), uiVar.floatMin, uiVar.floatMax);
		if (uiVar.hasDefaultValue)
			ImGui::Text("%s %s", T(TKEY("shader_default"), "Shader default:"), FormatDefault(uiVar).c_str());

		if (Effect::IsWeatherSeparated(uiVar))
			Util::Text::WrappedInfo("%s", WeatherSeparatedTooltip());
		if (uiVar.isDefine)
			Util::Text::WrappedWarning("%s", CompileTimeTooltip());
		Util::TextUnformattedDisabled(T(TKEY("context_hint"), "Right-click for reset, copy and paste."));
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	/** @return True when a menu action changed the value. */
	bool DrawParameterContextMenu(Effect::UIVariable& uiVar)
	{
		if (!ImGui::BeginPopupContextItem("##ctx"))
			return false;

		bool changed = false;
		if (uiVar.hasDefaultValue) {
			const std::string label = std::format("{} ({})", T(TKEY("reset_default"), "Reset to shader default"), FormatDefault(uiVar));
			if (ImGui::MenuItem(label.c_str()))
				changed = Effect::RestoreDefaultValue(uiVar);
		}

		if (uiVar.type == Effect::UIVariableType::Float) {
			ImGui::Separator();
			if (ImGui::MenuItem(T(TKEY("copy"), "Copy")))
				Effects11UI::Clipboard::SetFloat(uiVar.floatValue);
			if (ImGui::MenuItem(T(TKEY("paste"), "Paste"), nullptr, false, Effects11UI::Clipboard::HasFloat())) {
				uiVar.floatValue = std::clamp(Effects11UI::Clipboard::GetFloat(), uiVar.floatMin, uiVar.floatMax);
				changed = true;
			}
		} else if (IsColorVector(uiVar)) {
			ImGui::Separator();
			if (ImGui::MenuItem(T(TKEY("copy"), "Copy")))
				Effects11UI::Clipboard::SetColor(uiVar.vectorValue);
			if (ImGui::MenuItem(T(TKEY("paste"), "Paste"), nullptr, false, Effects11UI::Clipboard::HasColor())) {
				Effects11UI::Clipboard::GetColor(uiVar.vectorValue);
				for (int i = 0; i < Effects11UI::Clipboard::kColorComponents; ++i)
					uiVar.vectorValue[i] = std::clamp(uiVar.vectorValue[i], uiVar.floatMin, uiVar.floatMax);
				changed = true;
			}
		}

		if (!uiVar.hasDefaultValue && uiVar.type != Effect::UIVariableType::Float && !IsColorVector(uiVar))
			Util::TextUnformattedDisabled(T(TKEY("no_actions"), "No actions for this parameter"));

		ImGui::EndPopup();
		return changed;
	}

	void RenderWidget(UITree::VarRef& ref, bool readOnly, RenderContext& ctx)
	{
		auto& uiVar = ref.effect->uiVariables[ref.index];
		ImGui::PushID(ref.effect);
		ImGui::PushID(ref.index);

		Effects11UI::LabelBadge badge;
		if (uiVar.isDefine)
			badge = { T(TKEY("badge_compile_time"), "reload"), Util::Colors::GetWarning(), CompileTimeTooltip() };
		else if (Effect::IsWeatherSeparated(uiVar))
			badge = { T(TKEY("badge_weather"), "W"), Util::Colors::GetInfo(), WeatherSeparatedTooltip() };
		const bool labelHovered = Effects11UI::PropertyLabel(uiVar.displayName.c_str(), readOnly, badge);

		ImGui::BeginDisabled(readOnly);
		bool changed = false;
		switch (uiVar.type) {
		case Effect::UIVariableType::Float:
			changed = Effects11UI::FloatValue("##v", &uiVar.floatValue, uiVar.floatMin, uiVar.floatMax);
			break;
		case Effect::UIVariableType::Int:
			if ((uiVar.widgetType == Effect::UIWidgetType::Dropdown || uiVar.widgetType == Effect::UIWidgetType::Quality) && !uiVar.dropdownItems.empty()) {
				const bool quality = uiVar.widgetType == Effect::UIWidgetType::Quality;
				const int selected = quality ? uiVar.intValue + 1 : uiVar.intValue;
				const char* preview = (selected >= 0 && selected < static_cast<int>(uiVar.dropdownItems.size())) ? uiVar.dropdownItems[selected].c_str() : "";
				if (ImGui::BeginCombo("##v", preview)) {
					for (int j = 0; j < static_cast<int>(uiVar.dropdownItems.size()); ++j) {
						const int value = quality ? j - 1 : j;
						if (ImGui::Selectable(uiVar.dropdownItems[j].c_str(), uiVar.intValue == value)) {
							uiVar.intValue = value;
							changed = true;
						}
						if (uiVar.intValue == value)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			} else {
				changed = Effects11UI::IntValue("##v", &uiVar.intValue, uiVar.intMin, uiVar.intMax);
			}
			break;
		case Effect::UIVariableType::Bool:
			changed = ImGui::Checkbox("##v", &uiVar.boolValue);
			break;
		case Effect::UIVariableType::Float2:
			changed = Effects11UI::FloatNValue("##v", uiVar.vectorValue, 2, uiVar.floatMin, uiVar.floatMax);
			break;
		case Effect::UIVariableType::Float3:
		case Effect::UIVariableType::Float4:
			if (IsColorVector(uiVar)) {
				changed = Effects11UI::ColorValue("##v", uiVar.vectorValue, ComponentCount(uiVar), uiVar.floatMax > 1.0f);
			} else if (uiVar.widgetType == Effect::UIWidgetType::Vector && uiVar.type == Effect::UIVariableType::Float3) {
				changed = Effects11UI::FloatNValue("##v", uiVar.vectorValue, 3, -1.0f, 1.0f);
			} else {
				changed = Effects11UI::FloatNValue("##v", uiVar.vectorValue, ComponentCount(uiVar), uiVar.floatMin, uiVar.floatMax);
			}
			break;
		}

		if (!readOnly) {
			if (labelHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
				ImGui::OpenPopup("##ctx");
			changed |= DrawParameterContextMenu(uiVar);
		}
		ImGui::EndDisabled();

		if (labelHovered)
			DrawParameterTooltip(uiVar);

		if (changed) {
			ctx.changedEffects.insert(ref.effect);
			ctx.changedVars.emplace_back(ref.effect, static_cast<size_t>(ref.index));
			ctx.view.changed = true;
			if (uiVar.isDefine)
				ctx.view.compileTimeChanged = true;
		}

		ImGui::PopID();
		ImGui::PopID();
	}

	bool RenderVar(UITree::VarRef& ref, bool& inTable, bool ancestorMatched, RenderContext& ctx)
	{
		auto& uiVar = ref.effect->uiVariables[ref.index];

		if (!IsVarVisible(uiVar))
			return false;
		if (ctx.performanceMode && !uiVar.ignorePerfMode)
			return false;
		if (!IsPeriodShown(uiVar, ctx))
			return false;
		if (uiVar.isLabel ? ctx.Filtering() : !(ancestorMatched || MatchesFilter(uiVar, ctx)))
			return false;

		auto [bindVisible, bindReadOnly] = EvaluateBinding(uiVar, ctx.uniqueNameMap, ctx.fileUniqueNameMap);
		if (!bindVisible)
			return false;

		if (uiVar.isLabel) {
			if (inTable) {
				Effects11UI::EndPropertyTable();
				inTable = false;
			}
			if (uiVar.isReadOnly)
				ImGui::PushStyleColor(ImGuiCol_Text, globals::menu->GetSettings().Theme.StatusPalette.Disable);
			ImGui::TextWrapped("%s", uiVar.displayName.c_str());
			if (uiVar.isReadOnly)
				ImGui::PopStyleColor();
		} else {
			if (!inTable) {
				if (!ctx.BeginVarTable())
					return false;
				inTable = true;
			}
			RenderWidget(ref, bindReadOnly, ctx);
			ctx.view.drawn++;
		}
		return true;
	}

	void RenderTechniqueDropdown(Effect* effect, RenderContext& ctx, bool ancestorMatched)
	{
		if (ctx.Filtering() && !ancestorMatched && !Effects11UI::ContainsNoCase(effect->techniqueDropdown.name, ctx.view.filter))
			return;

		ImGui::PushID(effect);
		if (Effects11UI::BeginPropertyTable("##technique")) {
			const bool hovered = Effects11UI::PropertyLabel(effect->techniqueDropdown.name.c_str());
			const char* current = effect->uiTechniques[effect->selectedTechniqueIndex].displayName.c_str();
			if (ImGui::BeginCombo("##technique", current)) {
				for (uint32_t i = 0; i < effect->uiTechniques.size(); ++i) {
					if (ImGui::Selectable(effect->uiTechniques[i].displayName.c_str(), effect->selectedTechniqueIndex == i)) {
						effect->selectedTechniqueIndex = i;
						ctx.changedEffects.insert(effect);
						ctx.view.changed = true;
					}
					if (effect->selectedTechniqueIndex == i)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			Effects11UI::EndPropertyTable();
			if (hovered)
				ImGui::SetTooltip("%s", T(TKEY("technique_tip"), "Selects which technique of this shader file runs."));
			ctx.view.drawn++;
		}
		ImGui::PopID();
	}

	bool HasVisibleContent(const UITree::GroupNode& node, const RenderContext& ctx, bool ancestorMatched)
	{
		for (auto& item : node.items) {
			if (item.type == UITree::Item::Type::Variable) {
				auto& uiVar = item.var.effect->uiVariables[item.var.index];
				if (!IsVarVisible(uiVar) || !IsPeriodShown(uiVar, ctx))
					continue;
				if (uiVar.isLabel ? !ctx.Filtering() : (ancestorMatched || MatchesFilter(uiVar, ctx)))
					return true;
			} else if (item.type == UITree::Item::Type::Group && item.group) {
				if (HasVisibleContent(*item.group, ctx, ancestorMatched || GroupMatches(*item.group, ctx)))
					return true;
			}
		}
		return false;
	}

	void RenderGroupNode(UITree::GroupNode& node, RenderContext& ctx,
		const std::vector<std::pair<Effect*, std::string>>& techDropdowns, bool ancestorMatched)
	{
		for (auto& [effect, group] : techDropdowns)
			if (!group.empty() && group == node.fullPath && !effect->techniqueDropdown.topLevel)
				RenderTechniqueDropdown(effect, ctx, ancestorMatched);

		bool inTable = false;
		bool lastWasSeparator = false;

		for (auto& item : node.items) {
			switch (item.type) {
			case UITree::Item::Type::Variable:
				if (RenderVar(item.var, inTable, ancestorMatched, ctx))
					lastWasSeparator = false;
				break;

			case UITree::Item::Type::Separator:
				if (ctx.Filtering())
					break;
				if (!lastWasSeparator) {
					if (inTable) {
						Effects11UI::EndPropertyTable();
						inTable = false;
					}
					ImGui::Separator();
					lastWasSeparator = true;
				}
				break;

			case UITree::Item::Type::Group:
				{
					if (!item.group)
						break;
					const bool groupMatched = ancestorMatched || GroupMatches(*item.group, ctx);
					if (!HasVisibleContent(*item.group, ctx, groupMatched))
						break;
					if (inTable) {
						Effects11UI::EndPropertyTable();
						inTable = false;
					}
					lastWasSeparator = false;

					ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
					auto metaIt = ctx.meta.find(item.group->fullPath);
					if (metaIt != ctx.meta.end() && metaIt->second.defaultOpen)
						flags |= ImGuiTreeNodeFlags_DefaultOpen;
					if (ctx.Filtering())
						ImGui::SetNextItemOpen(true, ImGuiCond_Always);

					const std::string label = std::format("{}###ugrp_{}", GroupDisplayName(*item.group, ctx.meta), item.group->fullPath);
					if (ImGui::TreeNodeEx(label.c_str(), flags)) {
						RenderGroupNode(*item.group, ctx, techDropdowns, groupMatched);
						ImGui::TreePop();
					}
				}
				break;
			}
		}

		if (inTable)
			Effects11UI::EndPropertyTable();
	}
}

void ExtendedEffect::RenderImGui()
{
	Effect* self = this;
	RenderMergedUI({ &self, 1 });
}

void ExtendedEffect::RenderMergedUI(std::span<Effect*> effects, UITree::FilterMode filter, UITree::ViewOptions* options)
{
	UITree::ViewOptions defaultView;
	UITree::ViewOptions& view = options ? *options : defaultView;

	UITree::Tree tree;
	tree.Build(effects, filter);

	std::vector<std::pair<Effect*, std::string>> techDropdowns;
	for (auto* effect : effects) {
		if (!effect->IsCompiled() || effect->uiTechniques.size() <= 1 || !effect->techniqueDropdown.visible)
			continue;
		techDropdowns.push_back({ effect, effect->techniqueDropdown.group });
		if (!effect->techniqueDropdown.group.empty()) {
			auto [it, inserted] = tree.meta.try_emplace(effect->techniqueDropdown.group);
			if (inserted) {
				it->second.displayName = effect->techniqueDropdown.groupName;
				it->second.defaultOpen = effect->techniqueDropdown.groupOpen;
				it->second.ordering = effect->techniqueDropdown.ordering;
				it->second.hasOrdering = true;
			}
			UITree::TraverseGroupPath(tree.root, effect->techniqueDropdown.group, tree.meta);
		}
	}

	tree.Sort();

	std::unordered_set<Effect*> changedEffects;
	std::vector<std::pair<Effect*, size_t>> changedVars;
	RenderContext ctx{ tree.uniqueNameMap, tree.fileUniqueNameMap, changedEffects, changedVars, tree.meta, view,
		EffectManager::GetSingleton().performanceMode };

	if (filter != UITree::FilterMode::TopLevelOnly) {
		for (auto& [effect, group] : techDropdowns)
			if (effect->techniqueDropdown.topLevel || group.empty())
				RenderTechniqueDropdown(effect, ctx, false);
	}

	RenderGroupNode(tree.root, ctx, techDropdowns, false);

	if (!changedEffects.empty()) {
		const uint32_t activeWeatherID = EffectManager::GetSingleton().GetDominantWeatherID();
		for (auto& [effect, index] : changedVars) {
			if (auto* ext = dynamic_cast<ExtendedEffect*>(effect))
				ext->SyncWeatherVarFromUI(index, activeWeatherID);
		}
		for (auto* effect : changedEffects)
			effect->UpdateUIVariables();
		view.changed = true;
	}
}

#undef I18N_KEY_PREFIX

#endif
