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
	weatherVarSlots.clear();
	weatherSlotOfVariable.clear();
	parsedWeatherData.clear();
	weatherCacheEffect = nullptr;
	weatherCacheVariableCount = 0;
	timeOfDayGroups.clear();
	timeOfDayCacheEffect = nullptr;
	timeOfDayCacheVariableCount = 0;
	Effect::Unload();
}

/** @brief Component count of a Float or FloatN variable. */
static int GetComponentCount(Effect::UIVariableType type)
{
	return type == Effect::UIVariableType::Float ? 1 : type == Effect::UIVariableType::Float2 ? 2 :
	                                               type == Effect::UIVariableType::Float3     ? 3 :
	                                                                                            4;
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

int ExtendedEffect::GetPeriodIndex(const std::string& period)
{
	static constexpr std::string_view names[] = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "Interior" };
	const auto it = std::ranges::find(names, period);
	return it != std::end(names) ? static_cast<int>(it - std::begin(names)) : -1;
}

void ExtendedEffect::EnsureTimeOfDayGroups()
{
	if (timeOfDayCacheEffect != effect.get() || timeOfDayCacheVariableCount != uiVariables.size())
		RebuildTimeOfDayGroups();
}

void ExtendedEffect::RebuildTimeOfDayGroups()
{
	timeOfDayGroups.clear();
	timeOfDayCacheEffect = effect.get();
	timeOfDayCacheVariableCount = uiVariables.size();

	// The first variable seen for a base decides the group's type and separation
	std::unordered_map<std::string, size_t> groupOfBase;

	for (size_t i = 0; i < uiVariables.size(); ++i) {
		auto& uiVar = uiVariables[i];
		if (uiVar.timePeriod.empty() || !uiVar.effectVariable)
			continue;
		const auto& name = uiVar.name;
		const auto& period = uiVar.timePeriod;
		if (name.size() <= period.size() || name.compare(name.size() - period.size(), period.size(), period) != 0)
			continue;

		std::string baseName = name.substr(0, name.size() - period.size());
		auto groupIt = groupOfBase.find(baseName);
		if (groupIt == groupOfBase.end()) {
			auto baseVarIt = variables.find(baseName);
			if (baseVarIt == variables.end())
				continue;
			auto* baseVar = baseVarIt->second.get();
			if (!baseVar || !baseVar->IsValid())
				continue;

			groupIt = groupOfBase.emplace(std::move(baseName), timeOfDayGroups.size()).first;
			timeOfDayGroups.push_back({ baseVar, GetComponentCount(uiVar.type), uiVar.separation == "ExteriorWeather", {} });
		}
		timeOfDayGroups[groupIt->second].entries.push_back({ i, GetPeriodIndex(period) });
	}
}

void ExtendedEffect::ApplyTimeOfDayInterpolation()
{
	EnsureTimeOfDayGroups();
	if (timeOfDayGroups.empty())
		return;

	const auto& cd = EffectManager::GetSingleton().commonData;
	// Order matches GetPeriodIndex
	const float periodWeights[] = {
		cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Dawn)],
		cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunrise)],
		cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Day)],
		cd.timeOfDay1[static_cast<int>(TimeOfDay1Index::Sunset)],
		cd.timeOfDay2[static_cast<int>(TimeOfDay2Index::Dusk)],
		cd.timeOfDay2[static_cast<int>(TimeOfDay2Index::Night)],
		cd.eInteriorFactor
	};
	auto weightOf = [&](const TimeOfDayEntry& entry) { return entry.period >= 0 ? periodWeights[entry.period] : 0.0f; };
	const bool interior = cd.eInteriorFactor > 0.0f;

	for (const auto& group : timeOfDayGroups) {
		if (interior && group.exteriorWeather)
			continue;

		float totalWeight = 0.0f;
		for (const auto& entry : group.entries)
			totalWeight += weightOf(entry);
		if (totalWeight <= 0.0f)
			continue;

		float result[4] = {};
		for (const auto& entry : group.entries) {
			const float weight = weightOf(entry) / totalWeight;
			const auto& uiVar = uiVariables[entry.index];
			if (group.components == 1)
				result[0] += uiVar.floatValue * weight;
			else
				for (int c = 0; c < group.components; ++c)
					result[c] += uiVar.vectorValue[c] * weight;
		}
		if (group.components == 1)
			group.baseVariable->AsScalar()->SetFloat(result[0]);
		else
			group.baseVariable->AsVector()->SetFloatVector(result);
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

	RebuildWeatherCaches();
}

void ExtendedEffect::EnsureWeatherCaches()
{
	if (weatherCacheEffect != effect.get() || weatherCacheVariableCount != uiVariables.size())
		RebuildWeatherCaches();
}

void ExtendedEffect::RebuildWeatherCaches()
{
	weatherVarSlots.clear();
	weatherSlotOfVariable.assign(uiVariables.size(), -1);
	parsedWeatherData.clear();
	weatherCacheEffect = effect.get();
	weatherCacheVariableCount = uiVariables.size();

	for (size_t i = 0; i < uiVariables.size(); ++i) {
		const auto& uiVar = uiVariables[i];
		if (uiVar.isLabel)
			continue;
		if (!uiVar.effectVariable && !uiVar.isDefine)
			continue;
		if (!IsWeatherSeparated(uiVar))
			continue;
		if (uiVar.type != UIVariableType::Float && uiVar.type != UIVariableType::Float2 && uiVar.type != UIVariableType::Float3 && uiVar.type != UIVariableType::Float4)
			continue;

		std::string iniKey = GetVariableIniKey(uiVar);
		if (iniKey.empty())
			continue;

		weatherSlotOfVariable[i] = static_cast<int>(weatherVarSlots.size());
		weatherVarSlots.push_back({ i, std::move(iniKey), GetComponentCount(uiVar.type), uiVar.type != UIVariableType::Float && IsPerComponentVector(uiVar) });
	}

	for (const auto& [weatherID, values] : weatherData) {
		auto& parsed = parsedWeatherData[weatherID];
		parsed.resize(weatherVarSlots.size());
		for (size_t slotIndex = 0; slotIndex < weatherVarSlots.size(); ++slotIndex)
			ParseWeatherValue(values, weatherVarSlots[slotIndex], parsed[slotIndex]);
	}
}

void ExtendedEffect::ParseWeatherValue(const WeatherValues& values, const WeatherVarSlot& slot, ParsedWeatherValue& out)
{
	out = {};

	auto parseComponent = [&](const std::string& text, int component) {
		try {
			out.values[component] = std::stof(text);
			out.definedMask |= static_cast<uint8_t>(1u << component);
		} catch (...) {
		}
	};

	if (slot.perComponent) {
		static const char* suffixes[] = { "X", "Y", "Z", "W" };
		for (int c = 0; c < slot.components; ++c) {
			if (auto it = values.find(slot.iniKey + suffixes[c]); it != values.end())
				parseComponent(it->second, c);
		}
		return;
	}

	auto it = values.find(slot.iniKey);
	if (it == values.end())
		return;
	std::stringstream ss(it->second);
	std::string item;
	for (int c = 0; c < slot.components && std::getline(ss, item, ','); ++c)
		parseComponent(item, c);
}

void ExtendedEffect::ApplyWeatherBlending(float blendFactor, uint32_t currentWeatherID, uint32_t lastWeatherID)
{
	EnsureWeatherCaches();

	const std::vector<ParsedWeatherValue>* currentValues = nullptr;
	const std::vector<ParsedWeatherValue>* lastValues = nullptr;
	if (!parsedWeatherData.empty() && IsMultipleWeathersEnabled()) {
		if (auto it = parsedWeatherData.find(currentWeatherID); it != parsedWeatherData.end())
			currentValues = &it->second;
		if (auto it = parsedWeatherData.find(lastWeatherID); it != parsedWeatherData.end())
			lastValues = &it->second;
	}

	// Undefined components fall back to the base value, as an absent or unparsable key did
	auto pick = [](const std::vector<ParsedWeatherValue>* parsed, size_t slotIndex, int c, float fallback) {
		return parsed && ((*parsed)[slotIndex].definedMask & (1u << c)) ? (*parsed)[slotIndex].values[c] : fallback;
	};

	for (size_t slotIndex = 0; slotIndex < weatherVarSlots.size(); ++slotIndex) {
		const auto& slot = weatherVarSlots[slotIndex];
		auto& uiVar = uiVariables[slot.index];
		float* blended = slot.components == 1 ? &uiVar.floatValue : uiVar.vectorValue;
		const float* base = slot.components == 1 ? &uiVar.baseFloatValue : uiVar.baseVectorValue;

		for (int c = 0; c < slot.components; ++c) {
			const float currentVal = pick(currentValues, slotIndex, c, base[c]);
			const float lastVal = pick(lastValues, slotIndex, c, base[c]);
			blended[c] = lastVal + blendFactor * (currentVal - lastVal);
		}

		if (!uiVar.effectVariable)
			continue;
		if (slot.components == 1)
			uiVar.effectVariable->AsScalar()->SetFloat(uiVar.floatValue);
		else
			uiVar.effectVariable->AsVector()->SetFloatVector(uiVar.vectorValue);
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

	EnsureWeatherCaches();
	const int slotIndex = weatherSlotOfVariable[index];

	for (uint32_t linkedID : entry->weatherIDs) {
		auto& values = weatherData[linkedID];
		for (const auto& [key, value] : updates)
			values[key] = value;

		if (slotIndex >= 0) {
			auto& parsed = parsedWeatherData[linkedID];
			parsed.resize(weatherVarSlots.size());
			ParseWeatherValue(values, weatherVarSlots[slotIndex], parsed[slotIndex]);
		}
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

#include "../ENBExtender.h"
#include "../UITree.h"

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
		bool performanceMode = false;
		int tableCounter = 0;

		bool BeginVarTable()
		{
			std::string tableId = "##ut_" + std::to_string(tableCounter++);
			if (ImGui::BeginTable(tableId.c_str(), 2, ImGuiTableFlags_SizingFixedFit)) {
				float w = ImGui::GetContentRegionAvail().x;
				ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, w * 0.45f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, w * 0.55f);
				return true;
			}
			return false;
		}
	};

	void RenderWidget(const std::string& label, const std::string& id,
		Effect::UIVariable& uiVar, bool readOnly, Effect* effect, size_t index,
		std::unordered_set<Effect*>& changedEffects, std::vector<std::pair<Effect*, size_t>>& changedVars)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (readOnly)
			ImGui::PushStyleColor(ImGuiCol_Text, globals::menu->GetSettings().Theme.StatusPalette.Disable);
		ImGui::Text("%s", label.c_str());

		ImGui::TableSetColumnIndex(1);
		if (readOnly)
			ImGui::BeginDisabled();
		bool changed = false;
		float floatStep = (uiVar.floatMax - uiVar.floatMin) / 100.0f;
		switch (uiVar.type) {
		case Effect::UIVariableType::Float:
			changed = ImGui::InputFloat(id.c_str(), &uiVar.floatValue, floatStep, floatStep * 10.0f, "%.3f");
			if (changed)
				uiVar.floatValue = std::clamp(uiVar.floatValue, uiVar.floatMin, uiVar.floatMax);
			break;
		case Effect::UIVariableType::Int:
			if ((uiVar.widgetType == Effect::UIWidgetType::Dropdown || uiVar.widgetType == Effect::UIWidgetType::Quality) && !uiVar.dropdownItems.empty()) {
				int di = (uiVar.widgetType == Effect::UIWidgetType::Quality) ? uiVar.intValue + 1 : uiVar.intValue;
				const char* cur = (di >= 0 && di < static_cast<int>(uiVar.dropdownItems.size())) ? uiVar.dropdownItems[di].c_str() : "";
				if (ImGui::BeginCombo(id.c_str(), cur)) {
					for (int j = 0; j < static_cast<int>(uiVar.dropdownItems.size()); ++j) {
						int iv = (uiVar.widgetType == Effect::UIWidgetType::Quality) ? (j - 1) : j;
						if (ImGui::Selectable(uiVar.dropdownItems[j].c_str(), uiVar.intValue == iv)) {
							uiVar.intValue = iv;
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
			} else {
				changed = ImGui::InputInt(id.c_str(), &uiVar.intValue, 1, 10);
				if (changed)
					uiVar.intValue = std::clamp(uiVar.intValue, uiVar.intMin, uiVar.intMax);
			}
			break;
		case Effect::UIVariableType::Bool:
			changed = ImGui::Checkbox(id.c_str(), &uiVar.boolValue);
			break;
		case Effect::UIVariableType::Float2:
			changed = ImGui::InputScalarN(id.c_str(), ImGuiDataType_Float, uiVar.vectorValue, 2, &floatStep, nullptr, "%.3f");
			if (changed)
				for (int i = 0; i < 2; ++i)
					uiVar.vectorValue[i] = std::clamp(uiVar.vectorValue[i], uiVar.floatMin, uiVar.floatMax);
			break;
		case Effect::UIVariableType::Float3:
			if (uiVar.widgetType == Effect::UIWidgetType::Color) {
				changed = ImGui::ColorEdit3(id.c_str(), uiVar.vectorValue);
			} else {
				float min3 = (uiVar.widgetType == Effect::UIWidgetType::Vector) ? -1.0f : uiVar.floatMin;
				float max3 = (uiVar.widgetType == Effect::UIWidgetType::Vector) ? 1.0f : uiVar.floatMax;
				float step3 = (max3 - min3) / 100.0f;
				changed = ImGui::InputScalarN(id.c_str(), ImGuiDataType_Float, uiVar.vectorValue, 3, &step3, nullptr, "%.3f");
				if (changed)
					for (int i = 0; i < 3; ++i)
						uiVar.vectorValue[i] = std::clamp(uiVar.vectorValue[i], min3, max3);
			}
			break;
		case Effect::UIVariableType::Float4:
			if (uiVar.widgetType == Effect::UIWidgetType::Color) {
				changed = ImGui::ColorEdit4(id.c_str(), uiVar.vectorValue);
			} else {
				changed = ImGui::InputScalarN(id.c_str(), ImGuiDataType_Float, uiVar.vectorValue, 4, &floatStep, nullptr, "%.3f");
				if (changed)
					for (int i = 0; i < 4; ++i)
						uiVar.vectorValue[i] = std::clamp(uiVar.vectorValue[i], uiVar.floatMin, uiVar.floatMax);
			}
			break;
		}
		if (changed) {
			changedEffects.insert(effect);
			changedVars.emplace_back(effect, index);
		}
		if (readOnly)
			ImGui::EndDisabled();

		if (!uiVar.separation.empty() && uiVar.separation != "None") {
			ImGui::SameLine();
			ImGui::Text("W");
		}

		if (readOnly)
			ImGui::PopStyleColor();
	}

	bool RenderVar(UITree::VarRef& ref, bool& inTable, RenderContext& ctx)
	{
		auto& uiVar = ref.effect->uiVariables[ref.index];

		if (!IsVarVisible(uiVar))
			return false;
		if (ctx.performanceMode && !uiVar.ignorePerfMode)
			return false;

		auto [bindVisible, bindReadOnly] = EvaluateBinding(uiVar, ctx.uniqueNameMap, ctx.fileUniqueNameMap);
		if (!bindVisible)
			return false;

		if (uiVar.isLabel) {
			if (inTable) { ImGui::EndTable(); inTable = false; }
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
			RenderWidget(uiVar.displayName, "##uv_" + std::to_string(ref.index) + "_" + ref.effect->GetName(),
				uiVar, bindReadOnly, ref.effect, static_cast<size_t>(ref.index), ctx.changedEffects, ctx.changedVars);
		}
		return true;
	}

	void RenderTechniqueDropdown(Effect* effect, std::unordered_set<Effect*>& changedEffects)
	{
		ImGui::Text("%s", effect->techniqueDropdown.name.c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		const char* current = effect->uiTechniques[effect->selectedTechniqueIndex].displayName.c_str();
		if (ImGui::BeginCombo(("##TECHNIQUE_" + effect->GetName()).c_str(), current)) {
			for (uint32_t i = 0; i < effect->uiTechniques.size(); ++i) {
				if (ImGui::Selectable(effect->uiTechniques[i].displayName.c_str(), effect->selectedTechniqueIndex == i)) {
					effect->selectedTechniqueIndex = i;
					changedEffects.insert(effect);
				}
				if (effect->selectedTechniqueIndex == i)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	bool HasVisibleContent(const UITree::GroupNode& node)
	{
		for (auto& item : node.items) {
			if (item.type == UITree::Item::Type::Variable) {
				auto& uiVar = item.var.effect->uiVariables[item.var.index];
				if (IsVarVisible(uiVar))
					return true;
			} else if (item.type == UITree::Item::Type::Group && item.group) {
				if (HasVisibleContent(*item.group))
					return true;
			}
		}
		return false;
	}

	void RenderGroupNode(UITree::GroupNode& node, RenderContext& ctx,
		const std::vector<std::pair<Effect*, std::string>>& techDropdowns)
	{
		for (auto& [effect, group] : techDropdowns)
			if (!group.empty() && group == node.fullPath && !effect->techniqueDropdown.topLevel)
				RenderTechniqueDropdown(effect, ctx.changedEffects);

		bool inTable = false;
		bool lastWasSeparator = false;

		for (auto& item : node.items) {
			switch (item.type) {
			case UITree::Item::Type::Variable:
				if (RenderVar(item.var, inTable, ctx))
					lastWasSeparator = false;
				break;

			case UITree::Item::Type::Separator:
				if (!lastWasSeparator) {
					if (inTable) { ImGui::EndTable(); inTable = false; }
					ImGui::Separator();
					lastWasSeparator = true;
				}
				break;

			case UITree::Item::Type::Group:
				if (!item.group || !HasVisibleContent(*item.group))
					break;
				if (inTable) { ImGui::EndTable(); inTable = false; }
				lastWasSeparator = false;

				{
					std::string displayName = item.group->name;
					ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
					auto metaIt = ctx.meta.find(item.group->fullPath);
					if (metaIt != ctx.meta.end()) {
						if (!metaIt->second.displayName.empty())
							displayName = metaIt->second.displayName;
						if (metaIt->second.defaultOpen)
							flags = ImGuiTreeNodeFlags_DefaultOpen;
					}
					if (ImGui::TreeNodeEx((displayName + "###ugrp_" + item.group->fullPath).c_str(), flags)) {
						RenderGroupNode(*item.group, ctx, techDropdowns);
						ImGui::TreePop();
					}
				}
				break;
			}
		}

		if (inTable)
			ImGui::EndTable();
	}
}

void ExtendedEffect::RenderImGui()
{
	Effect* self = this;
	RenderMergedUI({ &self, 1 });
}

void ExtendedEffect::RenderMergedUI(std::span<Effect*> effects, UITree::FilterMode filter)
{
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
	RenderContext ctx{ tree.uniqueNameMap, tree.fileUniqueNameMap, changedEffects, changedVars, tree.meta,
		EffectManager::GetSingleton().performanceMode };

	if (filter != UITree::FilterMode::TopLevelOnly) {
		for (auto& [effect, group] : techDropdowns)
			if (effect->techniqueDropdown.topLevel || group.empty())
				RenderTechniqueDropdown(effect, changedEffects);
	}

	RenderGroupNode(tree.root, ctx, techDropdowns);

	if (!changedEffects.empty()) {
		auto& cd = EffectManager::GetSingleton().commonData;
		uint32_t activeWeatherID = static_cast<uint32_t>(cd.weather[2] > 0.5f ? cd.weather[0] : cd.weather[1]);
		for (auto& [effect, index] : changedVars) {
			if (auto* ext = dynamic_cast<ExtendedEffect*>(effect))
				ext->SyncWeatherVarFromUI(index, activeWeatherID);
		}
		for (auto* effect : changedEffects)
			effect->UpdateUIVariables();
	}

	for (auto* effect : effects) {
		if (!effect->GetErrors().empty()) {
			ImGui::TextColored(globals::menu->GetSettings().Theme.StatusPalette.Error, "%s:", effect->GetName().c_str());
			for (const auto& err : effect->GetErrors())
				ImGui::TextWrapped("%s", err.c_str());
		}
	}
}

#endif
