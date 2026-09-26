#include "SceneSettingsInternal.h"

#include "Feature.h"
#include "Globals.h"
#include "SceneSettingsCatalog.generated.h"
#include "SceneSettingsPolicy.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/SettingsCatalog.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace SceneSettingsInternal
{
	void CombineHash(size_t& signature, size_t value)
	{
		signature ^= value + 0x9E3779B9u + (signature << 6) + (signature >> 2);
	}

	void HashSceneSettingValue(size_t& signature, const json& value)
	{
		CombineHash(signature, static_cast<size_t>(value.type()));
		if (value.is_boolean())
			CombineHash(signature, std::hash<bool>{}(value.get<bool>()));
		else if (value.is_number_unsigned())
			CombineHash(signature, std::hash<std::uint64_t>{}(value.get<std::uint64_t>()));
		else if (value.is_number_integer())
			CombineHash(signature, std::hash<std::int64_t>{}(value.get<std::int64_t>()));
		else if (value.is_number_float())
			CombineHash(signature, std::hash<double>{}(value.get<double>()));
		else if (value.is_string())
			CombineHash(signature, std::hash<std::string_view>{}(value.get_ref<const std::string&>()));
	}

	bool IsSceneSettingPrimitive(const json& value)
	{
		return value.is_boolean() || value.is_number_integer() || value.is_number_float() || value.is_string();
	}

	bool IsEntryListSceneType(SceneSettingsManager::SceneType type)
	{
		return type == SceneSettingsManager::SceneType::InteriorOnly ||
		       type == SceneSettingsManager::SceneType::TimeOfDay;
	}

	bool WriteJsonAtomically(const std::filesystem::path& path, const json& data, int indent,
		std::string_view context)
	{
		return Util::FileHelpers::WriteJsonAtomically(path, data, indent, context);
	}

	std::optional<float> ReadTimeOfDayTransitionHours(const json& object, std::string_view context)
	{
		const auto transitionIt = object.find(kTimeOfDayTransitionHoursKey);
		if (transitionIt == object.end())
			return std::nullopt;
		if (const auto hours = transitionIt->is_number() ? transitionIt->get<float>() : -1.0f;
			std::isfinite(hours) && hours >= 0.0f && hours <= SceneSettingsManager::kMaxTimeOfDayTransitionHours)
			return hours;
		logger::warn("[SceneSettings] {} in {} must be a number in 0..{}; ignoring it",
			kTimeOfDayTransitionHoursKey, context, SceneSettingsManager::kMaxTimeOfDayTransitionHours);
		return std::nullopt;
	}

	std::vector<std::filesystem::path> GetSortedDirectoryPaths(
		const std::filesystem::path& directory, bool directories, std::string_view context)
	{
		std::vector<std::filesystem::path> paths;
		std::error_code ec;
		std::filesystem::directory_iterator iterator(
			directory, std::filesystem::directory_options::skip_permission_denied, ec);
		if (ec) {
			logger::error("[SceneSettings] Failed to enumerate {} '{}': {}", context, directory.string(), ec.message());
			return paths;
		}

		const std::filesystem::directory_iterator end;
		while (iterator != end) {
			const auto& entry = *iterator;
			std::error_code statusError;
			const bool matches = directories ? entry.is_directory(statusError) : entry.is_regular_file(statusError);
			if (statusError) {
				logger::warn("[SceneSettings] Could not inspect '{}': {}", entry.path().string(), statusError.message());
			} else if (matches) {
				paths.push_back(entry.path());
			}

			iterator.increment(ec);
			if (ec) {
				logger::error("[SceneSettings] Failed while enumerating {} '{}': {}", context, directory.string(), ec.message());
				break;
			}
		}

		std::sort(paths.begin(), paths.end(), [](const auto& lhs, const auto& rhs) {
			return lhs.generic_string() < rhs.generic_string();
		});
		return paths;
	}

	std::vector<std::filesystem::path> GetSortedJsonFiles(
		const std::filesystem::path& directory, std::string_view context)
	{
		auto paths = GetSortedDirectoryPaths(directory, false, context);
		std::erase_if(paths, [](const auto& path) { return path.extension() != ".json"; });
		return paths;
	}

	std::string NormalizeLocationFormKey(std::string_view formKey)
	{
		const auto components = Util::ParseSpid(std::string(formKey));
		if (components.localFormId == 0)
			return std::string(formKey);
		if (components.pluginName.empty())
			return std::format("0x{:X}", components.localFormId);

		auto pluginName = components.pluginName;
		std::transform(pluginName.begin(), pluginName.end(), pluginName.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return std::format("0x{:X}~{}", components.localFormId, pluginName);
	}

	std::string CanonicalizeResolvedLocationFormKey(std::string_view formKey)
	{
		const auto components = Util::ParseSpid(std::string(formKey));
		if (components.pluginName.empty() || components.localFormId == 0)
			return std::string(formKey);
		if (!RE::TESDataHandler::GetSingleton())
			return std::string(formKey);
		const auto formId = Util::SpidToFormId(std::string(formKey));
		return formId != 0 ? Util::FormIdToSpid(formId) : std::string(formKey);
	}

	bool IsSafeLocationFormKey(std::string_view formKey)
	{
		return !formKey.empty() && formKey != "." && formKey != ".." &&
		       formKey.find_first_of("\\/:*?\"<>|") == std::string_view::npos;
	}

	bool ReadOptionalStringField(const json& object, std::string_view field, std::string& value,
		std::string_view context)
	{
		auto it = object.find(std::string(field));
		if (it == object.end())
			return true;
		if (!it->is_string()) {
			logger::warn("[SceneSettings] {} field '{}' must be a string", context, field);
			return false;
		}
		value = it->get<std::string>();
		return true;
	}

	bool IsSceneMetadataKey(std::string_view key)
	{
		return !key.empty() && key.front() == '_';
	}

	bool ReadBoundedSceneJson(const std::filesystem::path& path, json& data)
	{
		std::error_code ec;
		const auto fileSize = std::filesystem::file_size(path, ec);
		if (ec || fileSize > kMaxSceneOverwriteFileSize)
			return false;

		std::ifstream file(path);
		if (!file.is_open())
			return false;
		data = json::parse(file, nullptr, false);
		return data.is_object();
	}

	bool IsNumericValue(const json& value)
	{
		return value.is_number_float();
	}

	void WidenParsedIntegerToFloat(const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, json& value)
	{
		if (!value.is_number_integer())
			return;
		if (const auto* setting = FindAllowedCatalogSetting(featureShortName, settingPath, settingKey);
			setting && setting->valueType == SceneSettingsCatalog::ValueType::Float)
			value = value.get<double>();
	}

	bool IsSceneSettingPathWrapper(std::string_view token)
	{
		return token == "settings";
	}

	std::string NormalizeSceneSettingAddressToken(std::string_view token)
	{
		auto normalized = token.find(' ') == std::string_view::npos ?
		                      Util::PrettifyIdentifier(token) :
		                      std::string(token);
		std::erase_if(normalized, [](unsigned char c) { return std::isspace(c); });
		std::transform(normalized.begin(), normalized.end(), normalized.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return normalized;
	}

	bool SceneSettingAddressTokensEqual(std::string_view lhs, std::string_view rhs)
	{
		return NormalizeSceneSettingAddressToken(lhs) == NormalizeSceneSettingAddressToken(rhs);
	}

	bool IsSceneSettingPolicyPrefix(
		const std::vector<std::string>& address, const SceneSettingsPolicy::SettingPolicyPath& prefix)
	{
		if (prefix.size() > address.size())
			return false;

		for (size_t index = 0; index < prefix.size(); ++index)
			if (!SceneSettingAddressTokensEqual(address[index], prefix[index]))
				return false;
		return true;
	}

	bool MatchesSceneSettingPolicy(const std::vector<std::string>& address,
		const std::vector<SceneSettingsPolicy::SettingPolicyPath>& paths)
	{
		return std::any_of(paths.begin(), paths.end(),
			[&](const auto& prefix) { return IsSceneSettingPolicyPrefix(address, prefix); });
	}

	std::vector<std::string> GetSceneSettingAddress(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey)
	{
		std::vector<std::string> address{ featureShortName };
		address.reserve(settingPath.size() + 2);
		for (const auto& segment : settingPath)
			if (!IsSceneSettingPathWrapper(segment))
				address.push_back(segment);
		address.push_back(settingKey);
		return address;
	}

	bool IsBlacklistedSceneSetting(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey)
	{
		auto address = GetSceneSettingAddress(featureShortName, settingPath, settingKey);
		return MatchesSceneSettingPolicy(address, SceneSettingsPolicy::kSettingBlacklist);
	}

	bool HasSceneOverwriteContent(const json& data)
	{
		if (!data.is_object())
			return false;

		for (const auto& [key, _] : data.items())
			if (!IsSceneMetadataKey(key))
				return true;
		return false;
	}

	bool IsCompatibleSceneSettingValue(const json& featureValue, const json& value)
	{
		if (featureValue.type() == value.type())
			return true;
		if (featureValue.is_number() && value.is_number())
			return true;
		return false;
	}

	std::string JoinDisplayParts(const std::vector<std::string>& parts, std::string_view leaf)
	{
		std::string displayName;
		for (const auto& part : parts) {
			if (!displayName.empty())
				displayName += kSceneSettingDisplaySeparator;
			displayName += part;
		}
		if (!leaf.empty()) {
			if (!displayName.empty())
				displayName += kSceneSettingDisplaySeparator;
			displayName += leaf;
		}
		return displayName;
	}

	void ToCatalogPath(const std::vector<std::string>& path, std::string& out)
	{
		out.clear();
		// Separate on position, not emptiness: a leading empty segment from a hand-edited
		// JSON must not alias onto the address that omits it.
		bool firstPart = true;
		for (const auto& part : path) {
			if (!firstPart)
				out += '/';
			firstPart = false;
			for (const char ch : part) {
				if (ch == '~')
					out += "~0";
				else if (ch == '/')
					out += "~1";
				else
					out += ch;
			}
		}
	}

	std::vector<std::string> GetCatalogSelectorPath(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		auto parts = SplitCatalogPath(setting.selectorPath);
		auto keys = SplitCatalogPath(setting.selectorPathKeys);
		for (size_t i = 0; i < parts.size(); ++i) {
			if (i < keys.size() && keys[i] != "-")
				parts[i] = T(keys[i], parts[i].c_str());
			parts[i] = StripImGuiId(parts[i]);
		}
		return parts;
	}

	bool EqualDisplayText(std::string_view lhs, std::string_view rhs)
	{
		return std::ranges::equal(lhs, rhs, [](const char a, const char b) {
			return std::tolower(static_cast<unsigned char>(a)) ==
			       std::tolower(static_cast<unsigned char>(b));
		});
	}

	std::vector<std::string> GetCatalogContextPath(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		auto parts = GetCatalogDisplayPath(setting);
		auto selectorDefaults = GetCatalogSelectorPath(setting);
		auto rawParts = SplitCatalogPath(setting.displayPath.empty() ? setting.settingPath : setting.displayPath);
		const auto rawKeys = SplitCatalogPath(setting.displayPathKeys);
		const auto settingParts = SplitCatalogPath(setting.settingPath);
		const bool hasSelector = !selectorDefaults.empty();
		size_t rawOffset = 0;
		for (auto& part : selectorDefaults)
			part = NormalizeDisplayPart(std::move(part));
		while (!parts.empty() && !selectorDefaults.empty() &&
		       EqualDisplayText(parts.front(), selectorDefaults.front())) {
			parts.erase(parts.begin());
			selectorDefaults.erase(selectorDefaults.begin());
			++rawOffset;
		}
		if (hasSelector) {
			while (rawOffset < rawParts.size() && IsStructuralDisplayPart(rawParts[rawOffset]))
				++rawOffset;
			if (!parts.empty() && rawOffset < rawParts.size() && rawOffset < settingParts.size()) {
				const bool translated = rawOffset < rawKeys.size() && rawKeys[rawOffset] != "-";
				auto rawPart = NormalizeDisplayPart(rawParts[rawOffset]);
				auto settingPart = NormalizeDisplayPart(settingParts[rawOffset]);
				if (!translated && EqualDisplayText(parts.front(), rawPart) &&
					EqualDisplayText(rawPart, settingPart))
					parts.erase(parts.begin());
			}
		}
		return parts;
	}

	double GetCatalogNumericDisplayScale(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		return std::isfinite(setting.displayScale) && setting.displayScale > 0.0 ?
		           setting.displayScale :
		           1.0;
	}

	bool ConvertCatalogNumericStoredToDisplay(const SceneSettingsCatalog::SettingMetadata& setting,
		double storedValue, double& displayValue)
	{
		if (!std::isfinite(storedValue))
			return false;
		if (setting.editorSemantic == SceneSettingsCatalog::EditorSemantic::Generic) {
			displayValue = storedValue;
			return true;
		}
		if (setting.editorSemantic != SceneSettingsCatalog::EditorSemantic::Numeric)
			return false;

		double transformedValue = storedValue;
		switch (setting.numericTransform) {
		case SceneSettingsCatalog::NumericTransform::Identity:
			break;
		case SceneSettingsCatalog::NumericTransform::Log2:
			if (storedValue <= 0.0)
				return false;
			transformedValue = std::log2(storedValue);
			break;
		default:
			return false;
		}

		displayValue = transformedValue * GetCatalogNumericDisplayScale(setting);
		return std::isfinite(displayValue);
	}

	bool ConvertCatalogNumericDisplayToStored(const SceneSettingsCatalog::SettingMetadata& setting,
		double displayValue, double& storedValue)
	{
		if (!std::isfinite(displayValue))
			return false;
		if (setting.editorSemantic == SceneSettingsCatalog::EditorSemantic::Generic) {
			storedValue = displayValue;
			return true;
		}
		if (setting.editorSemantic != SceneSettingsCatalog::EditorSemantic::Numeric)
			return false;

		const double transformedValue = displayValue / GetCatalogNumericDisplayScale(setting);
		switch (setting.numericTransform) {
		case SceneSettingsCatalog::NumericTransform::Identity:
			storedValue = transformedValue;
			break;
		case SceneSettingsCatalog::NumericTransform::Log2:
			storedValue = std::exp2(transformedValue);
			break;
		default:
			return false;
		}
		return std::isfinite(storedValue);
	}

	bool ClampCatalogNumericValue(const SceneSettingsCatalog::SettingMetadata& setting, json& value)
	{
		if (!setting.hasNumericBounds || !value.is_number() ||
			setting.editorSemantic != SceneSettingsCatalog::EditorSemantic::Numeric)
			return false;

		double minimum = 0.0;
		double maximum = 0.0;
		if (!ConvertCatalogNumericDisplayToStored(setting, setting.minimumValue, minimum) ||
			!ConvertCatalogNumericDisplayToStored(setting, setting.maximumValue, maximum) ||
			minimum >= maximum)
			return false;

		const double storedValue = value.get<double>();
		if (!std::isfinite(storedValue) || (storedValue >= minimum && storedValue <= maximum))
			return false;

		if (!value.is_number_integer()) {
			value = std::clamp(storedValue, minimum, maximum);
			return true;
		}
		// An integer control cannot land on a fractional bound, so its range is the whole numbers inside.
		const double lowest = std::ceil(minimum);
		const double highest = std::floor(maximum);
		if (lowest > highest)
			return false;
		value = static_cast<std::int64_t>(std::clamp(storedValue, lowest, highest));
		return true;
	}

	void WarnOnceAboutClampedSceneSetting(std::string_view featureShortName,
		const SceneSettingsCatalog::SettingMetadata& setting, const json& authored, const json& clamped)
	{
		static std::set<size_t> reported;
		size_t signature = std::hash<std::string_view>{}(featureShortName);
		CombineHash(signature, std::hash<std::string_view>{}(setting.settingPath));
		CombineHash(signature, std::hash<std::string_view>{}(setting.settingKey));
		if (!reported.insert(signature).second)
			return;

		logger::warn("[SceneSettings] {}.{} clamped from {} to {} on apply; the value is outside the range "
					 "its control allows. The scene entry keeps the authored value.",
			featureShortName, setting.settingKey, authored.dump(), clamped.dump());
	}

	const SceneSettingsCatalog::SettingMetadata* FindStoredAllComponent(
		const SceneSettingsCatalog::SettingMetadata& setting)
	{
		const auto settings = SceneSettingsCatalog::GetSettings();
		static const auto storedAllComponents = [] {
			using AggregateKey = std::tuple<std::string_view, std::string_view, std::string_view,
				SceneSettingsCatalog::AggregateSemantic, std::int8_t, std::uint8_t>;
			const auto makeKey = [](const auto& candidate) {
				return AggregateKey{ candidate.featureShortName, candidate.serializedPath,
					candidate.serializedKey, candidate.aggregateSemantic,
					candidate.aggregateStart, candidate.aggregateCount };
			};
			std::map<AggregateKey, const SceneSettingsCatalog::SettingMetadata*> storedAll;
			for (const auto& candidate : SceneSettingsCatalog::GetSettings())
				if (candidate.aggregateAll)
					storedAll.try_emplace(makeKey(candidate), &candidate);
			std::vector<const SceneSettingsCatalog::SettingMetadata*> components(
				SceneSettingsCatalog::GetSettings().size(), nullptr);
			for (size_t index = 0; index < SceneSettingsCatalog::GetSettings().size(); ++index) {
				const auto& source = SceneSettingsCatalog::GetSettings()[index];
				if (auto component = storedAll.find(makeKey(source)); component != storedAll.end())
					components[index] = component->second;
			}
			return components;
		}();
		const auto index = static_cast<size_t>(&setting - settings.data());
		assert(index < storedAllComponents.size());
		return index < storedAllComponents.size() ? storedAllComponents[index] : nullptr;
	}

	SceneSettingControlType GetCatalogControlType(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		using enum SceneSettingsCatalog::AggregateSemantic;
		switch (setting.aggregateSemantic) {
		case Color:
			return FindStoredAllComponent(setting) ?
			           SceneSettingControlType::Numeric :
			           SceneSettingControlType::Color;
		case Numeric:
			return SceneSettingControlType::Numeric;
		default:
			return SceneSettingControlType::Scalar;
		}
	}

	std::string GetSettingComponentName(SceneSettingControlType type, std::int8_t componentIndex)
	{
		if (componentIndex < 0 || componentIndex > 3)
			return {};
		if (type == SceneSettingControlType::Color) {
			switch (componentIndex) {
			case 0:
				return T("feature.scene_manager.channel.red", "R");
			case 1:
				return T("feature.scene_manager.channel.green", "G");
			case 2:
				return T("feature.scene_manager.channel.blue", "B");
			default:
				return T("feature.scene_manager.channel.alpha", "A");
			}
		}
		switch (componentIndex) {
		case 0:
			return T("feature.scene_manager.channel.x", "X");
		case 1:
			return T("feature.scene_manager.channel.y", "Y");
		case 2:
			return T("feature.scene_manager.channel.z", "Z");
		default:
			return T("feature.scene_manager.channel.w", "W");
		}
	}

	std::string GetCatalogComponentDisplayName(
		const SceneSettingsCatalog::SettingMetadata& setting, SceneSettingControlType controlType)
	{
		auto displayName = StripImGuiId(setting.componentDisplayName);
		if (!setting.componentDisplayNameKey.empty())
			displayName = StripImGuiId(T(setting.componentDisplayNameKey, displayName.c_str()));
		if (!displayName.empty())
			return displayName;
		if (setting.aggregateAll)
			return T("feature.scene_manager.channel.all", "All");

		auto componentIndex = static_cast<std::int8_t>(setting.aggregateCount > 1 ?
		                                                     setting.serializedComponent - setting.aggregateStart :
		                                                     setting.serializedComponent);
		const auto* storedAll = FindStoredAllComponent(setting);
		if (storedAll && storedAll->serializedComponent < setting.serializedComponent)
			--componentIndex;
		const auto componentType = setting.aggregateSemantic == SceneSettingsCatalog::AggregateSemantic::Color ?
		                               SceneSettingControlType::Color :
		                               controlType;
		return GetSettingComponentName(componentType, componentIndex);
	}

	SceneSettingsManager::SettingControlInfo MakeSettingControlInfo(
		const SceneSettingsCatalog::SettingMetadata& setting)
	{
		SceneSettingsManager::SettingControlInfo info;
		info.controlType = GetCatalogControlType(setting);
		info.settingPath = info.controlType == SceneSettingControlType::Scalar ?
		                       SplitCatalogPath(setting.settingPath) :
		                       SplitCatalogPath(setting.serializedPath);
		info.settingKey = std::string(info.controlType == SceneSettingControlType::Scalar ?
		                                  setting.settingKey : setting.serializedKey);
		info.displayName = GetCatalogLeafDisplayName(setting);
		info.componentDisplayName = GetCatalogComponentDisplayName(setting, info.controlType);
		info.displayPath = GetCatalogContextPath(setting);
		info.componentIndex = setting.serializedComponent;
		info.aggregateAll = setting.aggregateAll;
		if (info.controlType != SceneSettingControlType::Scalar) {
			info.componentStart = setting.aggregateStart;
			info.componentCount = setting.aggregateCount;
		}
		return info;
	}

	bool IsCatalogValueCompatible(const SceneSettingsCatalog::SettingMetadata& setting, const json& value)
	{
		using enum SceneSettingsCatalog::ValueType;
		switch (setting.valueType) {
		case Boolean:
			return value.is_boolean();
		case Integer:
			return value.is_number_integer();
		case Float:
			return value.is_number_float() || value.is_number_integer();
		case String:
			return value.is_string();
		default:
			return false;
		}
	}

	bool IsSameSetting(const SceneSettingsManager::SettingEntry& entry, const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey)
	{
		return entry.featureShortName == featureShortName &&
		       entry.settingPath == settingPath &&
		       entry.settingKey == settingKey;
	}

	std::string GetSettingLogName(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey)
	{
		return JoinDisplayParts(settingPath, std::format("{}.{}", featureShortName, settingKey));
	}

	json* GetObjectAtPath(json& data, const std::vector<std::string>& path, bool create)
	{
		json* node = &data;
		for (const auto& segment : path) {
			if (!node->is_object()) {
				return nullptr;
			}

			auto it = node->find(segment);
			if (it == node->end()) {
				if (!create)
					return nullptr;
				it = node->emplace(segment, json::object()).first;
			}
			node = &*it;
		}
		return node->is_object() ? node : nullptr;
	}

	bool RemoveObjectValueAtPath(json& data, const std::vector<std::string>& path,
		size_t pathIndex, const std::string& settingKey)
	{
		if (!data.is_object())
			return false;
		if (pathIndex == path.size())
			return data.erase(settingKey) == 1;

		auto childIt = data.find(path[pathIndex]);
		if (childIt == data.end() || !childIt->is_object() ||
			!RemoveObjectValueAtPath(*childIt, path, pathIndex + 1, settingKey))
			return false;
		if (childIt->empty())
			data.erase(childIt);
		return true;
	}

	const json* GetObjectAtPath(const json& data, const std::vector<std::string>& path)
	{
		const json* node = &data;
		for (const auto& segment : path) {
			if (!node->is_object())
				return nullptr;
			auto it = node->find(segment);
			if (it == node->end())
				return nullptr;
			node = &*it;
		}
		return node->is_object() ? node : nullptr;
	}

	json* GetObjectAtPath(json& data, const std::vector<std::string>& path)
	{
		return const_cast<json*>(GetObjectAtPath(std::as_const(data), path));
	}

	bool ParseCatalogArrayIndex(std::string_view value, size_t& index)
	{
		const auto result = std::from_chars(value.data(), value.data() + value.size(), index);
		return result.ec == std::errc{} && result.ptr == value.data() + value.size();
	}

	void CollectOverwriteEntries(const json& data, const std::vector<std::string>& settingPath,
		const std::function<void(const std::vector<std::string>&, const std::string&, const json&)>& callback)
	{
		if (!data.is_object())
			return;

		for (const auto& [key, value] : data.items()) {
			if (IsSceneMetadataKey(key))
				continue;
			if (IsSceneSettingPrimitive(value)) {
				callback(settingPath, key, value);

				continue;
			}
			if (!value.is_object())
				continue;

			auto childPath = settingPath;
			childPath.push_back(key);
			CollectOverwriteEntries(value, childPath, callback);
		}
	}

	bool PolicyContainsFeature(const std::vector<SceneSettingsPolicy::SettingPolicyPath>& paths,
		std::string_view featureShortName)
	{
		return std::any_of(paths.begin(), paths.end(), [&](const auto& prefix) {
			return !prefix.empty() && SceneSettingAddressTokensEqual(prefix.front(), featureShortName);
		});
	}

	bool IsInteriorOnlyFeatureAllowed(std::string_view featureShortName)
	{
		return PolicyContainsFeature(SceneSettingsPolicy::kLocationFeatureWhitelist, featureShortName);
	}

	bool IsTimeOfDayFeatureAllowed(std::string_view featureShortName)
	{
		return PolicyContainsFeature(SceneSettingsPolicy::kTimeOfDayFeatureWhitelist, featureShortName);
	}

	bool IsSettingAllowedBySceneTypePolicy(SceneSettingsManager::SceneType type,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey)
	{
		const auto address = GetSceneSettingAddress(featureShortName, settingPath, settingKey);
		switch (type) {
		case SceneSettingsManager::SceneType::InteriorOnly:
			return MatchesSceneSettingPolicy(address, SceneSettingsPolicy::kLocationFeatureWhitelist);
		case SceneSettingsManager::SceneType::TimeOfDay:
			return MatchesSceneSettingPolicy(address, SceneSettingsPolicy::kTimeOfDayFeatureWhitelist);
		case SceneSettingsManager::SceneType::Location:
			return MatchesSceneSettingPolicy(address, SceneSettingsPolicy::kLocationFeatureWhitelist) ||
			       MatchesSceneSettingPolicy(address, SceneSettingsPolicy::kTimeOfDayFeatureWhitelist);
		default:
			return false;
		}
	}

	bool ComputeCatalogSettingAllowedByPolicy(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		return SceneSettingsCatalog::IsSceneControllable(setting) &&
		       !IsBlacklistedSceneSetting(
			       std::string(setting.featureShortName),
			       SplitCatalogPath(setting.settingPath),
			       std::string(setting.settingKey));
	}

	bool IsCatalogSettingAllowedByPolicy(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		const auto settings = SceneSettingsCatalog::GetSettings();
		static const auto allowedSettings = [] {
			std::vector<uint8_t> allowed;
			allowed.reserve(SceneSettingsCatalog::GetSettings().size());
			for (const auto& candidate : SceneSettingsCatalog::GetSettings())
				allowed.push_back(ComputeCatalogSettingAllowedByPolicy(candidate) ? 1 : 0);
			return allowed;
		}();
		const auto index = static_cast<size_t>(&setting - settings.data());
		assert(index < allowedSettings.size());
		return index < allowedSettings.size() && allowedSettings[index] != 0;
	}

	bool IsCatalogSettingAllowedForSceneType(SceneSettingsManager::SceneType type,
		const SceneSettingsCatalog::SettingMetadata& setting)
	{
		constexpr size_t sceneTypeCount = 3;
		const auto typeIndex = static_cast<size_t>(type);
		if (typeIndex >= sceneTypeCount)
			return false;

		const auto settings = SceneSettingsCatalog::GetSettings();
		static const auto allowedSettings = [] {
			std::array<std::vector<uint8_t>, sceneTypeCount> allowedByType;
			for (size_t index = 0; index < sceneTypeCount; ++index) {
				const auto sceneType = static_cast<SceneSettingsManager::SceneType>(index);
				auto& allowed = allowedByType[index];
				allowed.reserve(SceneSettingsCatalog::GetSettings().size());
				for (const auto& candidate : SceneSettingsCatalog::GetSettings()) {
					// Time-of-day blending interpolates, so only transitionable floats qualify.
					const bool transitionable = sceneType != SceneSettingsManager::SceneType::TimeOfDay ||
					                            SceneSettingsCatalog::HasFlag(candidate.flags,
													SceneSettingsCatalog::SettingFlag::Transitionable);
					allowed.push_back(IsCatalogSettingAllowedByPolicy(candidate) && transitionable &&
										  IsSettingAllowedBySceneTypePolicy(sceneType,
											  std::string(candidate.featureShortName),
											  SplitCatalogPath(candidate.settingPath),
											  std::string(candidate.settingKey)) ?
					                      1 :
					                      0);
				}
			}
			return allowedByType;
		}();

		const auto index = static_cast<size_t>(&setting - settings.data());
		assert(index < allowedSettings[typeIndex].size());
		return index < allowedSettings[typeIndex].size() && allowedSettings[typeIndex][index] != 0;
	}

	const SceneSettingsCatalog::SettingMetadata* FindAllowedCatalogSetting(
		std::string_view featureShortName, const std::vector<std::string>& settingPath,
		std::string_view settingKey, bool requireTransitionable)
	{
		// Reused across lookups so a resolve does not allocate once per configured entry.
		thread_local std::string catalogPath;
		ToCatalogPath(settingPath, catalogPath);
		auto* setting = SceneSettingsCatalog::FindSetting(
			featureShortName, catalogPath, settingKey);
		if (!setting || !IsCatalogSettingAllowedByPolicy(*setting))
			return nullptr;
		if (requireTransitionable &&
			!SceneSettingsCatalog::HasFlag(setting->flags, SceneSettingsCatalog::SettingFlag::Transitionable))
			return nullptr;
		return setting;
	}

	bool TrySaveFeatureSettings(Feature& feature, std::string_view context, json& settings)
	{
		try {
			feature.SaveSettings(settings);
			return settings.is_object();
		} catch (const std::exception& e) {
			logger::warn("[SceneSettings] Could not {} for {}: {}", context, feature.GetShortName(), e.what());
		} catch (...) {
			logger::warn("[SceneSettings] Could not {} for {}", context, feature.GetShortName());
		}
		return false;
	}

	bool GetCatalogSettingValue(
		Feature& feature, const SceneSettingsCatalog::SettingMetadata& setting, json& value)
	{
		json featureSettings;
		if (!TrySaveFeatureSettings(feature, "read settings", featureSettings))
			return false;
		const auto* serializedValue = GetCatalogSerializedValue(featureSettings, setting);
		if (!serializedValue || !IsSceneSettingPrimitive(*serializedValue))
			return false;
		value = *serializedValue;
		return true;
	}

	std::span<const SceneSettingsCatalog::SettingMetadata> GetCatalogFeatureSettings(
		std::string_view featureShortName)
	{
		static const auto featureRanges = [] {
			std::map<std::string_view, std::pair<size_t, size_t>> ranges;
			const auto allSettings = SceneSettingsCatalog::GetSettings();
			for (size_t index = 0; index < allSettings.size();) {
				const auto name = allSettings[index].featureShortName;
				size_t end = index + 1;
				while (end < allSettings.size() && allSettings[end].featureShortName == name)
					++end;
				ranges.emplace(name, std::pair{ index, end });
				index = end;
			}
			return ranges;
		}();
		auto rangeIt = featureRanges.find(featureShortName);
		if (rangeIt == featureRanges.end())
			return {};
		const auto [begin, end] = rangeIt->second;
		return SceneSettingsCatalog::GetSettings().subspan(begin, end - begin);
	}

	bool CatalogHasSceneSettings(
		std::string_view featureShortName, SceneSettingsManager::SceneType type)
	{
		for (const auto& setting : GetCatalogFeatureSettings(featureShortName))
			if (IsCatalogSettingAllowedForSceneType(type, setting))
				return true;
		return false;
	}

	std::vector<std::string> GetLoadedCatalogFeatureNames(SceneSettingsManager::SceneType type)
	{
		auto names = Feature::GetLoadedFeatureNames();
		std::erase_if(names, [&](const auto& name) { return !CatalogHasSceneSettings(name, type); });
		return names;
	}

	RE::FormID GetActiveRegionId(RE::TESObjectCELL* cell)
	{
		return cell && cell->IsExteriorCell() && globals::game::sky && globals::game::sky->region ?
		           globals::game::sky->region->GetFormID() :
		           0;
	}

	std::string GetSceneSettingDisplayName(const std::string& featureShortName,
		const std::vector<std::string>& settingPath, const std::string& settingKey)
	{
		auto* setting = FindAllowedCatalogSetting(featureShortName, settingPath, settingKey);
		if (setting) {
			auto info = MakeSettingControlInfo(*setting);
			auto displayName = info.displayName;
			if (info.controlType != SceneSettingControlType::Scalar && !info.componentDisplayName.empty())
				displayName += std::format(" ({})", info.componentDisplayName);
			return JoinDisplayParts(info.displayPath, displayName);
		}
		return SceneSettingsManager::GetSettingDisplayName(settingKey);
	}

	bool GetFeatureSettingValueForValidation(Feature& feature, const std::string& featureShortName,
		const SceneSettingsCatalog::SettingMetadata& setting,
		FeatureSettingsCache* featureSettingsCache, json& featureValue)
	{
		if (!featureSettingsCache)
			return GetCatalogSettingValue(feature, setting, featureValue);

		auto [snapshotIt, inserted] = featureSettingsCache->try_emplace(featureShortName);
		if (inserted && !TrySaveFeatureSettings(feature, "snapshot settings", snapshotIt->second))
			snapshotIt->second = nullptr;
		if (!snapshotIt->second.is_object())
			return false;

		const auto* value = GetCatalogSerializedValue(snapshotIt->second, setting);
		if (!value || !IsSceneSettingPrimitive(*value))
			return false;
		featureValue = *value;
		return true;
	}

	bool IsSceneSettingValueAllowed(const json& featureValue,
		const SceneSettingsCatalog::SettingMetadata& setting, const json& value, bool requireNumeric)
	{
		if (!IsCatalogValueCompatible(setting, featureValue) || !IsCatalogValueCompatible(setting, value))
			return false;

		if (value.is_number() && !std::isfinite(value.get<double>()))
			return false;
		if (setting.editorSemantic == SceneSettingsCatalog::EditorSemantic::Numeric) {
			double ignoredDisplayValue = 0.0;
			if (!featureValue.is_number() || !value.is_number() ||
				!ConvertCatalogNumericStoredToDisplay(setting, featureValue.get<double>(), ignoredDisplayValue) ||
				!ConvertCatalogNumericStoredToDisplay(setting, value.get<double>(), ignoredDisplayValue))
				return false;
		}

		if (SceneSettingsCatalog::HasFlag(setting.flags, SceneSettingsCatalog::SettingFlag::BooleanControl)) {
			if (setting.valueType == SceneSettingsCatalog::ValueType::Integer &&
				(!value.is_number_integer() || (value.get<std::int64_t>() != 0 && value.get<std::int64_t>() != 1)))
				return false;
			if (setting.valueType == SceneSettingsCatalog::ValueType::Boolean && !value.is_boolean())
				return false;
		}

		if (setting.choiceCount > 0) {
			if (!value.is_number_integer())
				return false;
			const auto* const choicesEnd = setting.choices + setting.choiceCount;
			if (std::find(setting.choices, choicesEnd, value.get<std::int64_t>()) == choicesEnd)
				return false;
		}

		if (requireNumeric && (!SceneSettingsCatalog::HasFlag(setting.flags, SceneSettingsCatalog::SettingFlag::Transitionable) ||
			                      !IsNumericValue(featureValue) || !IsNumericValue(value) || !std::isfinite(value.get<float>())))
			return false;
		if (!requireNumeric && !IsSceneSettingPrimitive(value))
			return false;

		return IsCompatibleSceneSettingValue(featureValue, value);
	}

	bool ValidateSceneSettingEntry(std::string_view context, SceneSettingsManager::SceneType type,
		const std::string& featureShortName, const std::vector<std::string>& settingPath,
		const std::string& settingKey, const json& value, bool requireNumeric,
		FeatureSettingsCache* featureSettingsCache)
	{
		if (!IsSettingAllowedBySceneTypePolicy(type, featureShortName, settingPath, settingKey)) {
			logger::warn("[SceneSettings] {} entry {} is not whitelisted for this scene type",
				context, GetSettingLogName(featureShortName, settingPath, settingKey));
			return false;
		}
		if (IsBlacklistedSceneSetting(featureShortName, settingPath, settingKey)) {
			logger::warn("[SceneSettings] {} entry {} is blacklisted",
				context, GetSettingLogName(featureShortName, settingPath, settingKey));
			return false;
		}

		auto* setting = FindAllowedCatalogSetting(featureShortName, settingPath, settingKey, requireNumeric);
		if (!setting) {
			logger::warn("[SceneSettings] {} entry {} is not permitted by the compiled scene settings catalog",
				context, GetSettingLogName(featureShortName, settingPath, settingKey));
			return false;
		}

		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		if (!feature) {
			logger::warn("[SceneSettings] {} entry {} - feature '{}' not found/loaded",
				context, GetSettingLogName(featureShortName, settingPath, settingKey), featureShortName);
			return false;
		}

		json featureValue;
		if (!GetFeatureSettingValueForValidation(*feature, featureShortName, *setting,
				featureSettingsCache, featureValue) ||
			!IsSceneSettingValueAllowed(featureValue, *setting, value, requireNumeric)) {
			logger::warn("[SceneSettings] {} entry {} is not a supported scene-manager setting",
				context, GetSettingLogName(featureShortName, settingPath, settingKey));
			return false;
		}
		return true;
	}

	bool ApplyEntryValueUpdates(std::string_view context, SceneSettingsManager::SceneType type,
		std::vector<SceneSettingsManager::SettingEntry>& entries,
		std::span<const SceneSettingsManager::EntryValueUpdate> updates,
		bool requireNumeric, bool& userEntriesChanged)
	{
		if (updates.empty())
			return false;

		std::set<size_t> updatedIndices;
		FeatureSettingsCache featureSettingsCache;
		for (const auto& update : updates) {
			if (update.index >= entries.size() || !updatedIndices.insert(update.index).second)
				return false;
			const auto& entry = entries[update.index];
			if (!ValidateSceneSettingEntry(context, type, entry.featureShortName, entry.settingPath,
					entry.settingKey, update.value, requireNumeric, &featureSettingsCache))
				return false;
		}

		userEntriesChanged = false;
		for (const auto& update : updates) {
			auto& entry = entries[update.index];
			entry.value = update.value;
			userEntriesChanged |= entry.source == SceneSettingsManager::EntrySource::User;
		}
		return true;
	}
}
