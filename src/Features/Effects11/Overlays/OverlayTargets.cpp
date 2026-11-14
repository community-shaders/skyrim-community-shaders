#include "OverlayTargets.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <span>

#include "Features/Effects11/EffectManager.h"
#include "Features/Effects11/Effects/ExtendedEffect.h"
#include "Features/Effects11/IniSerialization.h"
#include "Features/Effects11/PresetManager.h"
#include "Features/Effects11/SettingManager.h"
#include "Features/Effects11/WeatherManager.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"

namespace Overlays
{
	namespace
	{
		static_assert(static_cast<int>(std::size(Effect::kComponentSuffixes)) == kMaxComponents);
		/** @brief Interior* periods first, so "InteriorDay" wins over "Day". */
		constexpr int kSuffixOrder[TimeOfDayValue::Total] = { 6, 7, 0, 1, 3, 4, 5, 2 };

		/** @brief Effects whose weather caches need re-parsing after weather writes. */
		std::set<Effect*> pendingWeatherFlush;
		/** @brief WeatherEntry::fileName -> IDs of every weather using that file; built by RebuildWeatherIndex. */
		std::unordered_map<std::string, std::vector<uint32_t>> weatherIndex;

		/** @brief Whether a variable holds a saved value an op can target. */
		bool IsTargetable(const Effect::UIVariable& a_var)
		{
			return !a_var.isLabel && !a_var.isPatched && (a_var.effectVariable || a_var.isDefine);
		}

		/** @brief Whether a variable type is Float or FloatN. */
		bool IsFloatType(Effect::UIVariableType a_type)
		{
			return a_type != Effect::UIVariableType::Int && a_type != Effect::UIVariableType::Bool;
		}

		/** @brief ASCII-uppercased copy of the text. */
		std::string ToUpperAscii(std::string a_text)
		{
			std::transform(a_text.begin(), a_text.end(), a_text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_text;
		}

		/** @brief The file name part of a path, as UTF-8. */
		std::string FileNameOnly(const std::string& a_path)
		{
			return Util::PathHelpers::ToUtf8(std::filesystem::path(a_path).filename());
		}

		/** @brief IDs of every weather using this file; empty only when the file is not indexed. */
		std::span<const uint32_t> WeatherIDsOf(const std::string& a_weatherFile)
		{
			const auto it = weatherIndex.find(a_weatherFile);
			assert(it != weatherIndex.end() && "weather slot outside the indexed weather list");
			return it != weatherIndex.end() ? std::span<const uint32_t>(it->second) : std::span<const uint32_t>();
		}

		/** @brief The first weather ID using this file; 0 for the base file. */
		uint32_t WeatherIDOf(const std::string& a_weatherFile)
		{
			if (a_weatherFile.empty())
				return 0;
			const auto weatherIDs = WeatherIDsOf(a_weatherFile);
			return weatherIDs.empty() ? 0 : weatherIDs.front();
		}

		/** @brief Every indexed weather file name. */
		std::vector<std::string> IndexedWeatherFiles()
		{
			std::vector<std::string> files;
			files.reserve(weatherIndex.size());
			for (const auto& [file, weatherIDs] : weatherIndex)
				files.push_back(file);
			return files;
		}

		/** @brief The ini key of a variable, or of one component of a per-component vector. */
		std::string WeatherKey(const Effect::UIVariable& a_var, int8_t a_part)
		{
			return Effect::GetVariableIniKey(a_var) + (a_part >= 0 ? Effect::kComponentSuffixes[a_part] : "");
		}

		/** @brief Whether the category names a shader file (e.g. ENBEFFECT.FX) rather than an engine category. */
		bool IsShaderCategory(std::string_view a_category)
		{
			return a_category.size() > 3 && Util::IEquals(a_category.substr(a_category.size() - 3), ".FX");
		}

		/** @brief The file a category's values are saved to without weathers: "<category>.ini" for shader files, else enbseries.ini. */
		std::string BaseFileOf(std::string_view a_category)
		{
			return IsShaderCategory(a_category) ? Util::ToLower(std::string(a_category)) + ".ini" : std::string(PresetManager::kEnbSeriesIniName);
		}

		/** @brief ReadSlot for a setting slot. */
		SlotValue ReadSettingSlot(const SlotKey& a_slot)
		{
			const auto stored = SettingManager::GetSingleton().GetStoredValue(a_slot.index, WeatherIDOf(a_slot.weatherFile));
			return { SettingPartValue(stored.value, a_slot.part), a_slot.part < 0 ? stored.definedMask != 0 : (stored.definedMask & (1u << a_slot.part)) != 0 };
		}

		/** @brief WriteSlot for a setting slot, to every weather sharing its file; an undefined weather value clears its period bits. */
		void WriteSettingSlot(const SlotKey& a_slot, const SlotValue& a_value)
		{
			auto& settings = SettingManager::GetSingleton();
			const auto weatherIDs = a_slot.weatherFile.empty() ? std::span<const uint32_t>() : WeatherIDsOf(a_slot.weatherFile);
			const uint32_t weatherID = weatherIDs.empty() ? 0 : weatherIDs.front();
			auto stored = settings.GetStoredValue(a_slot.index, weatherID);
			const auto& v = a_value.value.values;
			std::visit([&](auto& value) {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, bool>)
					value = v[0] != 0.0f;
				else if constexpr (std::is_same_v<T, float>)
					value = v[0];
				else if constexpr (std::is_same_v<T, TimeOfDayValue>)
					value.values[a_slot.part] = v[0];
				else
					value.values[a_slot.part] = { v[0], v[1], v[2] };
			},
				stored.value);
			if (weatherID) {
				const uint8_t bits = a_slot.part < 0 ? AllPeriodsMask : static_cast<uint8_t>(1u << a_slot.part);
				stored.definedMask = a_value.defined ? (stored.definedMask | bits) : (stored.definedMask & ~bits);
			}
			if (weatherIDs.empty())
				settings.SetStoredValue(a_slot.index, 0, stored);
			for (const uint32_t id : weatherIDs)
				settings.SetStoredValue(a_slot.index, id, stored);
		}

		/** @brief ReadSlot for a shader slot; an undefined weather value reads as the base value. */
		SlotValue ReadShaderSlot(const SlotKey& a_slot)
		{
			const auto& var = a_slot.effect->uiVariables[a_slot.index];
			if (a_slot.weatherFile.empty()) {
				const auto whole = ReadVariableValue(var, Effect::IsWeatherSeparated(var));
				return { a_slot.part >= 0 ? ComponentOf(whole, a_slot.part) : whole, true };
			}
#ifdef ENABLE_ENB_EXTENDER
			const auto* text = static_cast<ExtendedEffect*>(a_slot.effect)->GetWeatherValue(WeatherIDsOf(a_slot.weatherFile), WeatherKey(var, a_slot.part));
			SlotValue out{ ReadShaderSlot(a_slot.Base()).value, false };
			if (!text)
				return out;
			if (a_slot.part >= 0 || out.value.components == 1) {
				if (const auto value = ParseFloat(*text)) {
					out.value.values[0] = *value;
					out.defined = true;
				}
			} else if (ParseFloatList(*text, out.value.values) == out.value.components) {
				out.defined = true;
			}
			return out;
#else
			return { ReadShaderSlot(a_slot.Base()).value, false };
#endif
		}

		/** @brief WriteSlot for a shader slot; an undefined weather value removes the key. */
		void WriteShaderSlot(const SlotKey& a_slot, const SlotValue& a_value)
		{
			auto& var = a_slot.effect->uiVariables[a_slot.index];
			if (a_slot.weatherFile.empty()) {
				const bool separated = Effect::IsWeatherSeparated(var);
				auto whole = a_value.value;
				if (a_slot.part >= 0) {
					whole = ReadVariableValue(var, separated);
					whole.values[a_slot.part] = a_value.value.values[0];
				}
				WriteVariableValue(var, whole, separated);
				return;
			}
#ifdef ENABLE_ENB_EXTENDER
			const auto& v = a_value.value;
			const std::string text = v.components == 1 ? Effects11Util::Ini::FormatFloat(v.values[0]) :
			                                             Effects11Util::Ini::FormatFloatList({ v.values.data(), static_cast<size_t>(v.components) });
			static_cast<ExtendedEffect*>(a_slot.effect)->SetWeatherValue(WeatherIDsOf(a_slot.weatherFile), WeatherKey(var, a_slot.part), a_value.defined ? &text : nullptr);
			pendingWeatherFlush.insert(a_slot.effect);
#endif
		}
	}

	OverlayValue ReadVariableValue(const Effect::UIVariable& a_var, bool a_base)
	{
		OverlayValue out;
		switch (a_var.type) {
		case Effect::UIVariableType::Float:
			out.values[0] = a_base ? a_var.baseFloatValue : a_var.floatValue;
			break;
		case Effect::UIVariableType::Int:
			out.values[0] = static_cast<float>(a_var.intValue);
			out.kind = ValueKind::Int;
			break;
		case Effect::UIVariableType::Bool:
			out.values[0] = a_var.boolValue ? 1.0f : 0.0f;
			out.kind = ValueKind::Bool;
			break;
		default:
			out.components = Effect::GetComponentCount(a_var.type);
			std::copy_n(a_base ? a_var.baseVectorValue : a_var.vectorValue, out.components, out.values.begin());
			break;
		}
		return out;
	}

	void WriteVariableValue(Effect::UIVariable& a_var, const OverlayValue& a_value, bool a_alsoBase)
	{
		auto* scalar = a_var.effectVariable ? a_var.effectVariable->AsScalar() : nullptr;
		switch (a_var.type) {
		case Effect::UIVariableType::Float:
			a_var.floatValue = a_value.values[0];
			if (a_alsoBase)
				a_var.baseFloatValue = a_var.floatValue;
			if (scalar)
				scalar->SetFloat(a_var.floatValue);
			break;
		case Effect::UIVariableType::Int:
			a_var.intValue = static_cast<int>(std::lround(a_value.values[0]));
			if (scalar)
				scalar->SetInt(a_var.intValue);
			break;
		case Effect::UIVariableType::Bool:
			a_var.boolValue = a_value.values[0] != 0.0f;
			if (scalar)
				scalar->SetBool(a_var.boolValue);
			break;
		default:
			std::copy_n(a_value.values.begin(), a_value.components, a_var.vectorValue);
			if (a_alsoBase)
				std::copy_n(a_value.values.begin(), a_value.components, a_var.baseVectorValue);
			if (a_var.effectVariable)
				a_var.effectVariable->AsVector()->SetFloatVector(a_var.vectorValue);
			break;
		}
	}

	OverlayValue ComponentOf(const OverlayValue& a_value, int a_component)
	{
		return { { a_value.values[a_component] }, 1, a_value.kind };
	}

	Op SliceOperand(const Op& a_op, int a_component)
	{
		if (a_component < 0)
			return a_op;
		Op sliced = a_op;
		sliced.operand = ComponentOf(a_op.operand, a_component);
		return sliced;
	}

	void RebuildWeatherIndex()
	{
		weatherIndex.clear();
		for (const auto& [section, entry] : WeatherManager::GetSingleton().GetWeatherEntries()) {
			if (entry.weatherIDs.empty())
				continue;
			auto& weatherIDs = weatherIndex[entry.fileName];
			weatherIDs.insert(weatherIDs.end(), entry.weatherIDs.begin(), entry.weatherIDs.end());
		}
	}

	OverlayValue SettingPartValue(const SettingValue& a_value, int a_part)
	{
		return std::visit([&](const auto& value) -> OverlayValue {
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, bool>)
				return { { value ? 1.0f : 0.0f }, 1, ValueKind::Bool };
			else if constexpr (std::is_same_v<T, float>)
				return { { value }, 1, ValueKind::Float };
			else if constexpr (std::is_same_v<T, TimeOfDayValue>)
				return { { value.values[a_part] }, 1, ValueKind::Float };
			else
				return { { value.values[a_part].x, value.values[a_part].y, value.values[a_part].z }, 3, ValueKind::Float };
		},
			a_value);
	}

	bool OpTargetsBaseFile(const Op& a_op)
	{
		return a_op.fileName.empty() || MatchesPattern(a_op.fileName, BaseFileOf(a_op.category));
	}

	std::string MatchKey(std::string_view a_name)
	{
		return Util::ToLower(Effect::NormalizeIniKey(a_name));
	}

	bool HasPeriods(const Setting& a_setting)
	{
		return a_setting.type == SettingType::TimeOfDay || a_setting.type == SettingType::ColorTimeOfDay;
	}

	SlotValue ReadSlot(const SlotKey& a_slot)
	{
		return a_slot.IsShader() ? ReadShaderSlot(a_slot) : ReadSettingSlot(a_slot);
	}

	void WriteSlot(const SlotKey& a_slot, const SlotValue& a_value)
	{
		if (a_slot.IsShader())
			WriteShaderSlot(a_slot, a_value);
		else
			WriteSettingSlot(a_slot, a_value);
	}

	void FlushWeatherWrites()
	{
#ifdef ENABLE_ENB_EXTENDER
		for (auto* effect : pendingWeatherFlush)
			static_cast<ExtendedEffect*>(effect)->RefreshWeatherCaches();
#endif
		pendingWeatherFlush.clear();
	}

	OverlayValue ClampSlot(const SlotKey& a_slot, const OverlayValue& a_value)
	{
		if (a_slot.IsShader() || a_value.kind != ValueKind::Float)
			return a_value;
		const auto* setting = SettingManager::GetSingleton().GetSettingInfo(a_slot.index);
		auto clamped = a_value;
		for (int c = 0; c < clamped.components; ++c)
			clamped.values[c] = std::clamp(clamped.values[c], setting->minValue, setting->maxValue);
		return clamped;
	}

	SlotName DescribeSlot(const SlotKey& a_slot)
	{
		SlotName name;
		if (a_slot.IsShader()) {
			const auto& var = a_slot.effect->uiVariables[a_slot.index];
			name.category = ToUpperAscii(a_slot.effect->GetName());
			name.name = WeatherKey(var, a_slot.part);
			name.fileName = a_slot.weatherFile.empty() ? std::string() : FileNameOnly(a_slot.weatherFile);
		} else {
			const auto* setting = SettingManager::GetSingleton().GetSettingInfo(a_slot.index);
			name.category = setting->category;
			name.name = setting->key + (a_slot.part >= 0 ? kTimeOfDayNames[a_slot.part] : "");
			name.fileName = a_slot.weatherFile.empty() ? std::string(PresetManager::kEnbSeriesIniName) : FileNameOnly(a_slot.weatherFile);
		}
		return name;
	}

	std::vector<SlotKey> EnumerateSlots()
	{
		const auto weatherFiles = IndexedWeatherFiles();
		std::vector<SlotKey> slots;
		const auto addFiles = [&](SlotKey a_slot, bool a_weathers) {
			slots.push_back(a_slot);
			if (!a_weathers)
				return;
			for (const auto& file : weatherFiles) {
				a_slot.weatherFile = file;
				slots.push_back(a_slot);
			}
		};

		auto& settings = SettingManager::GetSingleton();
		for (const auto& category : settings.GetCategories()) {
			for (const auto& key : settings.GetSettingsByCategory(category)) {
				const auto* setting = settings.GetSettingInfo(key, category);
				ForEachSettingPart(*setting, [&](int8_t a_part) { addFiles({ nullptr, setting->id, a_part, {} }, setting->hasWeatherSupport); });
			}
		}

		for (auto* effect : EffectManager::GetSingleton().AllEffects()) {
			if (!effect->IsCompiled())
				continue;
			for (uint32_t i = 0; i < effect->uiVariables.size(); ++i) {
				const auto& var = effect->uiVariables[i];
				if (!IsTargetable(var))
					continue;
				const bool weathers = IsFloatType(var.type) && Effect::IsWeatherSeparated(var);
				const int parts = Effect::IsPerComponentVector(var) ? Effect::GetComponentCount(var.type) : 0;
				for (int part = parts ? 0 : -1; part < parts; ++part)
					addFiles({ effect, i, static_cast<int8_t>(part), {} }, weathers);
			}
		}
		return slots;
	}

	Resolver::Resolver()
	{
		for (auto* effect : EffectManager::GetSingleton().AllEffects()) {
			if (!effect->IsCompiled())
				continue;
			EffectIndex index{ effect, {} };
			for (uint32_t i = 0; i < effect->uiVariables.size(); ++i) {
				if (IsTargetable(effect->uiVariables[i]))
					index.keys.emplace(MatchKey(Effect::GetVariableIniKey(effect->uiVariables[i])), i);
			}
			effects.push_back(std::move(index));
		}
		weatherFiles = IndexedWeatherFiles();
	}

	std::vector<std::string> Resolver::MatchWeatherFiles(std::string_view a_fileName, bool a_wildcard) const
	{
		std::vector<std::string> matches;
		for (const auto& file : weatherFiles) {
			const auto name = FileNameOnly(file);
			if (a_wildcard ? MatchesPattern(a_fileName, name) : Util::IEquals(a_fileName, name))
				matches.push_back(file);
		}
		return matches;
	}

	bool Resolver::ResolveShader(const Op& a_op, std::vector<BaseTarget>& a_out, std::string& a_reason) const
	{
		const auto effectIt = std::find_if(effects.begin(), effects.end(), [&](const EffectIndex& index) { return Util::IEquals(index.effect->GetName(), a_op.category); });
		if (effectIt == effects.end()) {
			a_reason = "shader file not loaded by Effects 11";
			return false;
		}

		int component = -1;
		auto keyIt = effectIt->keys.find(MatchKey(a_op.name));
		if (keyIt == effectIt->keys.end() && a_op.name.size() > 1) {
			const std::string_view lastChar(&a_op.name.back(), 1);
			const auto suffix = std::find_if(std::begin(Effect::kComponentSuffixes), std::end(Effect::kComponentSuffixes), [&](const char* a_suffix) { return Util::IEquals(lastChar, a_suffix); });
			if (suffix != std::end(Effect::kComponentSuffixes)) {
				keyIt = effectIt->keys.find(MatchKey(std::string_view(a_op.name).substr(0, a_op.name.size() - 1)));
				component = static_cast<int>(suffix - std::begin(Effect::kComponentSuffixes));
			}
		}
		if (keyIt == effectIt->keys.end()) {
			a_reason = "no such shader parameter";
			return false;
		}

		auto* effect = effectIt->effect;
		const auto& var = effect->uiVariables[keyIt->second];
		const bool perComponent = Effect::IsPerComponentVector(var);
		if (component >= 0 && (!perComponent || component >= Effect::GetComponentCount(var.type))) {
			a_reason = "no such shader parameter";
			return false;
		}
#ifdef ENABLE_ENB_EXTENDER
		const bool weatherCapable = IsFloatType(var.type) && Effect::IsWeatherSeparated(var);
#else
		const bool weatherCapable = false;
#endif
		if (component >= 0) {
			a_out.push_back({ { effect, keyIt->second, static_cast<int8_t>(component), {} }, -1, weatherCapable });
		} else if (perComponent) {
			const int components = Effect::GetComponentCount(var.type);
			if (a_op.operand.components != 1 && a_op.operand.components != components) {
				a_reason = "operand has the wrong number of components";
				return false;
			}
			for (int c = 0; c < components; ++c)
				a_out.push_back({ { effect, keyIt->second, static_cast<int8_t>(c), {} }, a_op.operand.components == 1 ? 0 : c, weatherCapable });
		} else {
			a_out.push_back({ { effect, keyIt->second, -1, {} }, -1, weatherCapable });
		}
		return true;
	}

	bool Resolver::ResolveSetting(const Op& a_op, std::vector<BaseTarget>& a_out, std::string& a_reason) const
	{
		// Matched case-insensitively like the shader path, since the stores are keyed by exact name
		auto& settings = SettingManager::GetSingleton();
		std::vector<const Setting*> candidates;
		for (const auto& category : settings.GetCategories()) {
			if (Util::IEquals(category, a_op.category)) {
				for (const auto& key : settings.GetSettingsByCategory(category))
					candidates.push_back(settings.GetSettingInfo(key, category));
			}
		}
		const auto findSetting = [&](std::string_view a_key) -> const Setting* {
			for (const auto* setting : candidates)
				if (Util::IEquals(setting->key, a_key))
					return setting;
			for (const auto* setting : candidates)
				for (const auto& legacy : setting->legacyKeys)
					if (Util::IEquals(legacy, a_key))
						return setting;
			return nullptr;
		};

		if (const auto* setting = findSetting(a_op.name)) {
			ForEachSettingPart(*setting, [&](int8_t a_part) { a_out.push_back({ { nullptr, setting->id, a_part, {} }, -1, setting->hasWeatherSupport }); });
			return true;
		}
		for (const int period : kSuffixOrder) {
			const std::string_view suffix = kTimeOfDayNames[period];
			if (a_op.name.size() <= suffix.size() || !Util::IEquals(std::string_view(a_op.name).substr(a_op.name.size() - suffix.size()), suffix))
				continue;
			const auto* setting = findSetting(std::string_view(a_op.name).substr(0, a_op.name.size() - suffix.size()));
			if (setting && HasPeriods(*setting)) {
				a_out.push_back({ { nullptr, setting->id, static_cast<int8_t>(period), {} }, -1, setting->hasWeatherSupport });
				return true;
			}
		}
		a_reason = "setting not used by Effects 11";
		return false;
	}

	std::vector<ResolvedTarget> Resolver::Resolve(const Op& a_op, std::string& a_reason) const
	{
		std::vector<BaseTarget> bases;
		if (!(IsShaderCategory(a_op.category) ? ResolveShader(a_op, bases, a_reason) : ResolveSetting(a_op, bases, a_reason)))
			return {};

		const bool wildcard = HasWildcard(a_op.fileName);
		const bool targetsBase = OpTargetsBaseFile(a_op);
		const auto weathers = targetsBase && !wildcard ? std::vector<std::string>() : MatchWeatherFiles(a_op.fileName, wildcard);
		if (!targetsBase && weathers.empty()) {
			a_reason = std::format("weather file '{}' is not in _weatherlist.ini", a_op.fileName);
			return {};
		}

		std::vector<ResolvedTarget> targets;
		for (const auto& base : bases) {
			if (targetsBase)
				targets.push_back({ base.slot, base.operandComponent, false });
			if (weathers.empty())
				continue;
			if (!base.weatherCapable) {
				logger::debug("[Overlays] {} [{}] {}: no per-weather values, weather targets skipped", a_op.fileName, a_op.category, a_op.name);
				continue;
			}
			for (const auto& file : weathers) {
				auto slot = base.slot;
				slot.weatherFile = file;
				targets.push_back({ std::move(slot), base.operandComponent, !wildcard });
			}
		}
		if (targets.empty())
			a_reason = "value has no per-weather values";
		return targets;
	}
}
