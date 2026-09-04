#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
#include "SceneSettingsCatalog.generated.h"
#include "SceneSettingsPolicy.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/Game.h"

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
#include <numeric>
#include <set>
#include <string_view>
#include <tuple>

namespace
{
	using SceneSettingControlType = SceneSettingsManager::SettingControlType;

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

	SceneSettingsManager* sceneSettingsManagerSingleton = nullptr;

	/// RAII CPU pass for the in-game Profiling UI; ends the pass on every early-return path.
	struct ProfilerPassScope
	{
		explicit ProfilerPassScope(const std::string& name)
		{
			if (globals::profiler)
				globals::profiler->BeginPass(name);
		}
		~ProfilerPassScope()
		{
			if (globals::profiler)
				globals::profiler->EndPass();
		}
	};
}

SceneSettingsManager::SceneSettingsManager()
{
	assert(!sceneSettingsManagerSingleton);
	sceneSettingsManagerSingleton = this;
}

SceneSettingsManager::~SceneSettingsManager()
{
	if (sceneSettingsManagerSingleton == this)
		sceneSettingsManagerSingleton = nullptr;
}

SceneSettingsManager* SceneSettingsManager::GetSingleton()
{
	return sceneSettingsManagerSingleton;
}

namespace
{
	constexpr auto kOverwriteJsonIndent = 2;
	constexpr auto kMaxSceneOverwriteFileSize = 1024 * 1024;
	constexpr const char* kFeatureKey = "_feature";
	constexpr const char* kMetadataKey = "_metadata";
	constexpr const char* kMetadataDescriptionKey = "description";
	constexpr const char* kStatusKey = "status";
	constexpr const char* kStatusDeleted = "deleted";
	constexpr std::string_view kSceneSettingDisplaySeparator = " / ";
	constexpr std::string_view kImGuiIdSeparator = "##";

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
		std::string serialized;
		try {
			serialized = data.dump(indent);
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Could not serialize {} '{}': {}", context, path.string(), e.what());
			return false;
		}

		std::error_code ec;
		if (!path.parent_path().empty()) {
			std::filesystem::create_directories(path.parent_path(), ec);
			if (ec) {
				logger::error("[SceneSettings] Could not create directory for {} '{}': {}",
					context, path.string(), ec.message());
				return false;
			}
		}

		auto temporaryPath = path;
		temporaryPath += std::format(".{}.tmp", ::GetCurrentProcessId());
		{
			std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
			if (!file.is_open()) {
				logger::error("[SceneSettings] Could not open temporary {} file '{}'", context, temporaryPath.string());
				return false;
			}
			file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
			file.flush();
			if (file.fail()) {
				logger::error("[SceneSettings] Could not write temporary {} file '{}'", context, temporaryPath.string());
				file.close();
				std::filesystem::remove(temporaryPath, ec);
				return false;
			}
			file.close();
			if (file.fail()) {
				logger::error("[SceneSettings] Could not close temporary {} file '{}'", context, temporaryPath.string());
				std::filesystem::remove(temporaryPath, ec);
				return false;
			}
		}

		if (!::MoveFileExW(temporaryPath.c_str(), path.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			const auto error = ::GetLastError();
			logger::error("[SceneSettings] Could not replace {} '{}' (Win32 error {})",
				context, path.string(), error);
			std::filesystem::remove(temporaryPath, ec);
			return false;
		}
		return true;
	}

	std::string StripImGuiId(std::string_view label)
	{
		return std::string(label.substr(0, label.find(kImGuiIdSeparator)));
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

	// TOD/weather can only interpolate float settings, not integer toggles or enum values.
	bool IsNumericValue(const json& value)
	{
		return value.is_number_float();
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

	std::vector<std::string> SplitCatalogPath(std::string_view path)
	{
		std::vector<std::string> parts;
		size_t start = 0;
		while (start < path.size()) {
			auto end = path.find('/', start);
			auto part = path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
			if (!part.empty()) {
				std::string decoded(part);
				for (size_t pos = 0; (pos = decoded.find('~', pos)) != std::string::npos;) {
					if (pos + 1 < decoded.size() && decoded[pos + 1] == '1')
						decoded.replace(pos, 2, "/");
					else if (pos + 1 < decoded.size() && decoded[pos + 1] == '0')
						decoded.replace(pos, 2, "~");
					++pos;
				}
				parts.push_back(std::move(decoded));
			}
			if (end == std::string_view::npos)
				break;
			start = end + 1;
		}
		return parts;
	}

	/// Writes into a caller-owned buffer so repeated lookups can reuse one allocation.
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

	bool IsStructuralDisplayPart(std::string_view part)
	{
		std::string normalized;
		normalized.reserve(part.size());
		for (const char ch : part)
			if (std::isalnum(static_cast<unsigned char>(ch)))
				normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
		return normalized == "settings" || normalized == "values" || normalized == "baseline";
	}

	std::string NormalizeDisplayPart(std::string part)
	{
		part = StripImGuiId(part);
		if (!part.empty() && std::all_of(part.begin(), part.end(), [](const char ch) {
				return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
			}))
			part = Util::PrettifyIdentifier(part);
		return part;
	}

	std::vector<std::string> GetCatalogDisplayPath(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		auto parts = SplitCatalogPath(setting.displayPath.empty() ? setting.settingPath : setting.displayPath);
		const auto keys = SplitCatalogPath(setting.displayPathKeys);
		for (size_t index = 0; index < parts.size(); ++index) {
			if (index < keys.size() && keys[index] != "-")
				parts[index] = T(keys[index], parts[index].c_str());
			parts[index] = NormalizeDisplayPart(std::move(parts[index]));
		}
		std::erase_if(parts, [](const auto& part) { return part.empty() || IsStructuralDisplayPart(part); });
		return parts;
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

	std::string GetCatalogLeafDisplayName(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		if (setting.displayName.empty() && setting.displayNameKey.empty() &&
			setting.editorSemantic == SceneSettingsCatalog::EditorSemantic::Choice)
			return T("feature.scene_manager.selection", "Selection");

		auto displayName = StripImGuiId(setting.displayName.empty() ? setting.settingKey : setting.displayName);
		if (!setting.displayNameKey.empty())
			displayName = StripImGuiId(T(setting.displayNameKey, displayName.c_str()));
		return displayName;
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

	/// Applying a value bypasses the ImGui call that would have bounded it, so a document authored
	/// elsewhere can push a control past its range. Bounds are in display space and both transforms are
	/// monotonic, so the range converts once rather than every value round-tripping.
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

	/// An out-of-range entry re-applies every frame its scene is active, so the report is one-shot.
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

	template <class Json>
	Json* GetCatalogNodeAtPath(Json& data, const std::vector<std::string>& path)
	{
		auto* node = &data;
		for (const auto& segment : path) {
			if (node->is_object()) {
				auto it = node->find(segment);
				if (it == node->end())
					return nullptr;
				node = &*it;
				continue;
			}

			size_t index = 0;
			if (!node->is_array() || !ParseCatalogArrayIndex(segment, index) || index >= node->size())
				return nullptr;
			node = &(*node)[index];
		}
		return node;
	}

	template <class Json>
	Json* GetCatalogSerializedValue(Json& data, const SceneSettingsCatalog::SettingMetadata& setting)
	{
		auto* parent = GetCatalogNodeAtPath(data, SplitCatalogPath(setting.serializedPath));
		if (!parent)
			return nullptr;

		Json* value = nullptr;
		if (parent->is_object()) {
			auto valueIt = parent->find(setting.serializedKey);
			if (valueIt == parent->end())
				return nullptr;
			value = &*valueIt;
		} else {
			size_t index = 0;
			if (!parent->is_array() || !ParseCatalogArrayIndex(setting.serializedKey, index) ||
				index >= parent->size())
				return nullptr;
			value = &(*parent)[index];
		}

		if (setting.serializedComponent < 0)
			return value;
		const auto component = static_cast<size_t>(setting.serializedComponent);
		if (!value->is_array() || component >= value->size())
			return nullptr;
		return &(*value)[component];
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

	/// Whitelists are address prefixes, so a feature can be admitted whole or setting by setting.
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

	/// Catalog and policy are both compile-time constant, so decide every setting up front.
	/// Built once (thread-safe static init) and read-only afterwards: read from both the render
	/// thread and the menu thread.
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
		std::string_view settingKey, bool requireTransitionable = false)
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

	// Runs on the render thread, so a throwing feature must degrade to "skip the override", not crash.
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

	/// The catalog is grouped by feature, so a feature's settings are one contiguous range.
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
}

size_t SceneSettingsManager::GetCatalogUpdateSignature(std::string_view featureShortName,
	std::span<const CatalogSceneSettingUpdate> updates)
{
	size_t signature = std::hash<std::string_view>{}(featureShortName);
	for (const auto& update : updates) {
		for (const auto& segment : update.settingPath)
			CombineHash(signature, std::hash<std::string_view>{}(segment));
		CombineHash(signature, std::hash<std::string_view>{}(update.key));
	}
	return signature;
}

bool SceneSettingsManager::ApplyCatalogSceneSettings(
	Feature& feature, const std::vector<CatalogSceneSettingUpdate>& updates)
{
	if (updates.empty())
		return true;

	const auto featureShortName = feature.GetShortName();
	auto documentIt = featureApplyDocuments.find(featureShortName);
	if (documentIt == featureApplyDocuments.end()) {
		json settingsDocument;
		if (!TrySaveFeatureSettings(feature, "snapshot settings", settingsDocument))
			return false;
		documentIt = featureApplyDocuments.emplace(featureShortName, std::move(settingsDocument)).first;
	}
	auto& settingsDocument = documentIt->second;

	// Resolved up front: the document outlives this call, so a rejected update must not have touched it.
	std::vector<const SceneSettingsCatalog::SettingMetadata*> catalogSettings;
	std::vector<json*> targetValues;
	catalogSettings.reserve(updates.size());
	targetValues.reserve(updates.size());
	for (const auto& update : updates) {
		auto* setting = FindAllowedCatalogSetting(featureShortName, update.settingPath, update.key);
		auto* currentValue = setting ? GetCatalogSerializedValue(settingsDocument, *setting) : nullptr;
		if (!currentValue || !IsSceneSettingPrimitive(*currentValue) ||
			!IsSceneSettingPrimitive(update.value) ||
			!IsCompatibleSceneSettingValue(*currentValue, update.value))
			return false;
		catalogSettings.push_back(setting);
		targetValues.push_back(currentValue);
	}

	std::vector<json> originalValues;
	originalValues.reserve(updates.size());
	for (size_t index = 0; index < updates.size(); ++index) {
		auto& targetValue = *targetValues[index];
		originalValues.push_back(std::move(targetValue));
		targetValue = updates[index].value;
		if (updates[index].clampToControlRange &&
			ClampCatalogNumericValue(*catalogSettings[index], targetValue))
			WarnOnceAboutClampedSceneSetting(
				featureShortName, *catalogSettings[index], updates[index].value, targetValue);
	}

	try {
		feature.LoadSettings(settingsDocument);
		return true;
	} catch (const std::exception& e) {
		logger::warn("[SceneSettings] Failed to apply settings for {}: {}", featureShortName, e.what());
	} catch (...) {
		logger::warn("[SceneSettings] Failed to apply settings for {}", featureShortName);
	}

	try {
		for (size_t index = 0; index < updates.size(); ++index)
			*targetValues[index] = std::move(originalValues[index]);
		feature.LoadSettings(settingsDocument);
	} catch (...) {
		featureApplyDocuments.erase(featureShortName);
		logger::error("[SceneSettings] Failed to restore {} after an apply error", featureShortName);
	}
	return false;
}

void SceneSettingsManager::ScheduleApplyVerification(std::string_view featureShortName,
	const std::vector<CatalogSceneSettingUpdate>& updates, size_t signature, bool transition)
{
	pendingApplyVerifications[std::string(featureShortName)] = {
		.appliedFrame = lastUpdateFrame,
		.updates = updates,
		.signature = signature,
		.transition = transition,
	};
}

void SceneSettingsManager::VerifyPendingApplies()
{
	for (auto verificationIt = pendingApplyVerifications.begin();
		verificationIt != pendingApplyVerifications.end();) {
		const auto& [featureShortName, verification] = *verificationIt;
		// Give the feature the frame it was handed the values in before reading them back.
		if (verification.appliedFrame == lastUpdateFrame) {
			++verificationIt;
			continue;
		}

		bool retained = false;
		json actualSettings;
		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		if (feature && TrySaveFeatureSettings(*feature, "verify applied settings", actualSettings))
			retained = std::all_of(verification.updates.begin(), verification.updates.end(),
				[&](const auto& update) {
					auto* setting = FindAllowedCatalogSetting(
						featureShortName, update.settingPath, update.key);
					const auto* actual = setting ?
					                         GetCatalogSerializedValue(actualSettings, *setting) :
					                         nullptr;
					return actual && ResolvedValuesEqual(*actual, update.value);
				});

		if (!retained) {
			logger::warn("[SceneSettings] {} did not retain settings after reporting a successful apply",
				featureShortName);
			featureApplyDocuments.erase(featureShortName);
			for (const auto& update : verification.updates)
				appliedSettings.erase({ featureShortName, update.settingPath, update.key });
			PruneAppliedFeatureName(featureShortName);
			auto& failure = (verification.transition ? transitionApplyFailures : applyFailures)[featureShortName];
			failure.signature = verification.signature;
			failure.retryAfter = std::chrono::steady_clock::now() + kApplyRetryDelay;
			failure.warningLogged = true;
			resolverDirty = true;
		}
		verificationIt = pendingApplyVerifications.erase(verificationIt);
	}
}

static std::filesystem::path GetSceneOverwritePath(SceneSettingsManager::SceneType type, const SceneSettingsManager::SettingEntry& entry);
static bool RemoveSettingFromOverwriteFile(const std::filesystem::path& path,
	const std::vector<std::string>& settingPath, const std::string& settingKey);

static bool HasOverwriteEntryForPeriod(const std::vector<SceneSettingsManager::SettingEntry>& entries,
	const SceneSettingsManager::SettingEntry& candidate)
{
	return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
		return entry.source == SceneSettingsManager::EntrySource::Overwrite &&
		       entry.period == candidate.period &&
		       IsSameSetting(entry, candidate.featureShortName, candidate.settingPath, candidate.settingKey);
	});
}

static bool AddOverwriteEntryIfUnique(std::vector<SceneSettingsManager::SettingEntry>& entries,
	SceneSettingsManager::SettingEntry&& entry, std::string_view context)
{
	// Files are scanned lexicographically. The first overwrite for an address and period wins.
	if (HasOverwriteEntryForPeriod(entries, entry)) {
		logger::warn("[SceneSettings] Duplicate {} overwrite for {}.{} ({}) skipped",
			context, entry.featureShortName, entry.settingKey, entry.sourceFilename);
		return false;
	}

	entries.push_back(std::move(entry));
	return true;
}

// --- Path Resolution ---

std::string SceneSettingsManager::GetSceneTypeName(SceneType type)
{
	switch (type) {
	case SceneType::InteriorOnly:
		return "InteriorOnly";
	case SceneType::TimeOfDay:
		return "TimeOfDay";
	case SceneType::Location:
		return "Location";
	default:
		return "Unknown";
	}
}

std::filesystem::path SceneSettingsManager::GetUserSettingsFilePath()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "SceneManager.json";
}

std::filesystem::path SceneSettingsManager::GetOverwritesPath(SceneType type)
{
	// Location overwrites are keyed by form under GetLocationOverwritesDir(), not by scene type name.
	assert(IsEntryListSceneType(type));
	return Util::PathHelpers::GetSceneSettingsPath() / GetSceneTypeName(type);
}

std::filesystem::path SceneSettingsManager::GetWeatherOverwritesDir()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "Weather";
}

std::filesystem::path SceneSettingsManager::GetLocationOverwritesDir()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "Locations";
}

// --- Time of Day Period Helpers ---

const char* SceneSettingsManager::GetPeriodName(TimeOfDayPeriod period)
{
	int idx = static_cast<int>(period);
	return (idx >= 0 && idx < kPeriodCount) ? kPeriodNames[idx] : "Unknown";
}

SceneSettingsManager::TimeOfDayPeriod SceneSettingsManager::GetPeriodFromName(const std::string& name)
{
	for (auto period : kPeriods) {
		if (name == GetPeriodName(period))
			return period;
	}
	return TimeOfDayPeriod::Count;
}

namespace
{
	/// The editor can run before globals are cached, so the singleton is the fallback.
	RE::Calendar* GetCalendar()
	{
		return globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	}
}

float SceneSettingsManager::GetCurrentGameHour()
{
	// Prefer calendar (ground truth), which the Weather Editor slider writes to.
	// sky->currentGameHour may lag when timeScale is 0 (time paused).
	auto calendar = GetCalendar();
	float hour = 12.0f;
	if (calendar && calendar->gameHour)
		hour = calendar->gameHour->value;
	else if (auto sky = globals::game::sky)
		hour = sky->currentGameHour;
	if (!std::isfinite(hour))
		hour = 12.0f;

	// Normalize into [0, 24) so midnight is 0 and never 24.
	hour = std::clamp(hour, 0.0f, 24.0f);
	if (hour >= 24.0f)
		hour = 0.0f;
	return hour;
}

void SceneSettingsManager::SetGameHour(float hour)
{
	if (!std::isfinite(hour))
		return;
	auto calendar = GetCalendar();
	if (calendar && calendar->gameHour)
		calendar->gameHour->value = std::clamp(hour, 0.0f, 24.0f);
}

float SceneSettingsManager::GetPeriodMidHour(TimeOfDayPeriod period)
{
	const int index = static_cast<int>(period);
	if (index < 0 || index >= kPeriodCount)
		return GetCurrentGameHour();

	// Night ends past 24, so its middle lands after midnight.
	const float mid = (kPeriodHours[index][0] + kPeriodHours[index][1]) * 0.5f;
	return mid >= 24.0f ? mid - 24.0f : mid;
}

SceneSettingsManager::PeriodLookup SceneSettingsManager::FindPeriodForHour(float hour)
{
	for (int index = 0; index < kPeriodCount; ++index) {
		const float start = kPeriodHours[index][0];
		const float end = kPeriodHours[index][1];
		// Night ends past 24, so pre-dawn hours have to be compared against hour + 24.
		const float periodHour = (end > 24.0f && hour < start) ? hour + 24.0f : hour;
		if (periodHour >= start && periodHour < end)
			return { index, periodHour };
	}
	return {};
}

std::array<float, SceneSettingsManager::kPeriodCount> SceneSettingsManager::GetTimeOfDayFactors()
{
	std::array<float, kPeriodCount> factors{};
	const auto lookup = FindPeriodForHour(GetCurrentGameHour());
	if (lookup.index < 0) {
		factors[static_cast<int>(TimeOfDayPeriod::Day)] = 1.0f;
		return factors;
	}

	const float hoursToEnd = kPeriodHours[lookup.index][1] - lookup.hour;
	if (hoursToEnd >= kTransitionHours) {
		factors[lookup.index] = 1.0f;
		return factors;
	}

	// Inside the blend-out zone: cross-fade into the next period.
	const float weight = hoursToEnd / kTransitionHours;
	factors[lookup.index] = weight;
	factors[(lookup.index + 1) % kPeriodCount] = 1.0f - weight;
	return factors;
}

SceneSettingsManager::TimeOfDayPeriod SceneSettingsManager::GetCurrentPeriod()
{
	const auto lookup = FindPeriodForHour(GetCurrentGameHour());
	return lookup.index < 0 ? TimeOfDayPeriod::Day : static_cast<TimeOfDayPeriod>(lookup.index);
}

// --- Feature Metadata ---

bool SceneSettingsManager::IsFeatureAllowedForType(SceneType type, const std::string& featureShortName)
{
	if (!Feature::FindFeatureByShortName(featureShortName))
		return false;

	switch (type) {
	case SceneType::InteriorOnly:
		return IsInteriorOnlyFeatureAllowed(featureShortName) &&
		       CatalogHasSceneSettings(featureShortName, type);
	case SceneType::TimeOfDay:
		return IsTimeOfDayFeatureAllowed(featureShortName) &&
		       CatalogHasSceneSettings(featureShortName, type);
	case SceneType::Location:
		return (IsInteriorOnlyFeatureAllowed(featureShortName) ||
		           IsTimeOfDayFeatureAllowed(featureShortName)) &&
		       CatalogHasSceneSettings(featureShortName, type);
	default:
		return false;
	}
}

bool SceneSettingsManager::IsSettingAllowedForType(SceneType type,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey)
{
	auto* setting = FindAllowedCatalogSetting(featureShortName, settingPath, settingKey);
	return Feature::FindFeatureByShortName(featureShortName) && setting &&
	       IsCatalogSettingAllowedForSceneType(type, *setting);
}

bool SceneSettingsManager::IsSceneSettingAllowed(
	std::string_view featureShortName, std::string_view settingPath, std::string_view settingKey)
{
	auto* setting = SceneSettingsCatalog::FindSetting(featureShortName, settingPath, settingKey);
	return setting && IsCatalogSettingAllowedByPolicy(*setting);
}

std::vector<std::string> SceneSettingsManager::GetExteriorRelevantFeatureNames()
{
	return GetLoadedCatalogFeatureNames(SceneType::TimeOfDay);
}

std::vector<std::string> SceneSettingsManager::GetLocationRelevantFeatureNames()
{
	return GetLoadedCatalogFeatureNames(SceneType::Location);
}

std::string SceneSettingsManager::GetFeatureDisplayName(const std::string& featureShortName)
{
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	return feature ? feature->GetDisplayName() : featureShortName;
}

bool SceneSettingsManager::GetSettingControlInfo(const SettingEntry& entry, SettingControlInfo& info)
{
	auto* setting = FindAllowedCatalogSetting(
		entry.featureShortName, entry.settingPath, entry.settingKey);
	if (!setting)
		return false;
	info = MakeSettingControlInfo(*setting);
	return true;
}

std::string SceneSettingsManager::GetSettingDisplayName(const std::string& settingKey)
{
	return StripImGuiId(Util::PrettifyIdentifier(settingKey));
}

static std::string GetSceneSettingDisplayName(const std::string& featureShortName,
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

json SceneSettingsManager::GetFeatureSettingValue(const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey)
{
	auto* setting = FindAllowedCatalogSetting(featureShortName, settingPath, settingKey);
	if (!setting)
		return {};
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature)
		return {};

	SceneLayerGuard guard;
	json value;
	if (GetCatalogSettingValue(*feature, *setting, value))
		return value;
	return {};
}

using FeatureSettingsCache = std::map<std::string, json>;

static bool GetFeatureSettingValueForValidation(Feature& feature, const std::string& featureShortName,
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

static bool IsSceneSettingValueAllowed(const json& featureValue,
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

static bool ValidateSceneSettingEntry(std::string_view context, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, const json& value,
	bool requireNumeric, FeatureSettingsCache* featureSettingsCache = nullptr)
{
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

static bool ApplyEntryValueUpdates(std::string_view context,
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
		if (!ValidateSceneSettingEntry(context, entry.featureShortName, entry.settingPath,
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

// --- Generic Entry Management ---

std::vector<SceneSettingsManager::SettingEntry>& SceneSettingsManager::GetEntriesMut(SceneType type)
{
	assert(IsEntryListSceneType(type));
	return entries[type];
}

void SceneSettingsManager::BumpEntryPresentationRevision()
{
	++entryPresentationRevision;
	MarkSceneValuesDirty();
}

void SceneSettingsManager::MarkSceneValuesDirty()
{
	++sceneValueRevision;
	locationOverridesDirty = true;
}

const std::vector<SceneSettingsManager::SettingEntry>& SceneSettingsManager::GetEntries(SceneType type) const
{
	static const std::vector<SettingEntry> empty;
	if (!IsEntryListSceneType(type))
		return empty;
	auto it = entries.find(type);
	return (it != entries.end()) ? it->second : empty;
}

void SceneSettingsManager::MarkEntryListUserSettingsModified(SceneType type)
{
	assert(IsEntryListSceneType(type));
	if (type == SceneType::InteriorOnly)
		interiorUserSettingsModified = true;
	else
		timeOfDayUserSettingsModified = true;
}

bool SceneSettingsManager::IsEntryActive(const SettingEntry& entry) const
{
	return !entry.paused && !IsFeaturePaused(entry.featureShortName);
}

bool SceneSettingsManager::HasEntryFromSource(SceneType type, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, EntrySource source) const
{
	for (const auto& entry : GetEntries(type)) {
		if (entry.source == source && IsSameSetting(entry, featureShortName, settingPath, settingKey))
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasEntryForPeriod(const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey,
	TimeOfDayPeriod period, EntrySource source) const
{
	for (const auto& entry : GetEntries(SceneType::TimeOfDay)) {
		if (entry.source == source && entry.period == period &&
			IsSameSetting(entry, featureShortName, settingPath, settingKey))
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasDuplicateEntry(SceneType type, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, EntrySource source, TimeOfDayPeriod period) const
{
	if (!IsEntryListSceneType(type))
		return false;
	if (type == SceneType::TimeOfDay)
		return HasEntryForPeriod(featureShortName, settingPath, settingKey, period, source);
	return HasEntryFromSource(type, featureShortName, settingPath, settingKey, source);
}

void SceneSettingsManager::RemoveSetting(SceneType type, size_t index)
{
	if (!IsEntryListSceneType(type))
		return;
	auto& vec = GetEntriesMut(type);
	if (index >= vec.size())
		return;

	const auto entry = vec[index];
	if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty() &&
		!RemoveSettingFromOverwriteFile(GetSceneOverwritePath(type, entry), entry.settingPath, entry.settingKey))
		return;

	logger::info("[SceneSettings] Removed {} entry: {} (source={})", GetSceneTypeName(type),
		GetSettingLogName(entry.featureShortName, entry.settingPath, entry.settingKey),
		entry.source == EntrySource::Overwrite ? "overwrite" : "user");

	vec.erase(vec.begin() + static_cast<ptrdiff_t>(index));
	BumpEntryPresentationRevision();
	if (entry.source == EntrySource::User) {
		MarkEntryListUserSettingsModified(type);
		SaveAllUserSettings();
	}
	ReapplyIfActive();
}

/// Per-period overwrites live in a period subfolder of their scene's directory.
static std::filesystem::path GetOverwriteDir(const std::filesystem::path& baseDir,
	SceneSettingsManager::TimeOfDayPeriod period)
{
	return period != SceneSettingsManager::TimeOfDayPeriod::Count ?
	           baseDir / SceneSettingsManager::GetPeriodName(period) :
	           baseDir;
}

/// Discovered entries keep the exact file they came from; authored ones derive it from their period.
static std::filesystem::path GetOverwriteFilePath(const std::filesystem::path& baseDir,
	const SceneSettingsManager::SettingEntry& entry)
{
	if (!entry.sourcePath.empty())
		return entry.sourcePath;
	return GetOverwriteDir(baseDir, entry.period) / entry.sourceFilename;
}

static std::string GetOverwriteTypeDescription(std::string_view sceneLabel,
	SceneSettingsManager::TimeOfDayPeriod period)
{
	return period != SceneSettingsManager::TimeOfDayPeriod::Count ?
	           std::format("{} - {}", sceneLabel, SceneSettingsManager::GetPeriodName(period)) :
	           std::string(sceneLabel);
}

static std::filesystem::path GetSceneOverwritePath(SceneSettingsManager::SceneType type,
	const SceneSettingsManager::SettingEntry& entry)
{
	return GetOverwriteFilePath(SceneSettingsManager::GetOverwritesPath(type), entry);
}

static std::filesystem::path GetWeatherOverwritePath(RE::FormID weatherId, const SceneSettingsManager::SettingEntry& entry)
{
	return GetOverwriteFilePath(
		SceneSettingsManager::GetWeatherOverwritesDir() / Util::FormIdToSpid(weatherId), entry);
}

static std::filesystem::path GetLocationOverwritePath(std::string_view formKey,
	const SceneSettingsManager::SettingEntry& entry)
{
	return GetOverwriteFilePath(SceneSettingsManager::GetLocationOverwritesDir() / formKey, entry);
}

static bool WriteGroupedOverwriteFile(const std::filesystem::path& path, const std::string& featureShortName,
	const std::string& overwriteType, const std::vector<const SceneSettingsManager::SettingEntry*>& entries,
	const json& extraMetadata = json::object())
{
	std::error_code ec;
	const auto pathExists = std::filesystem::exists(path, ec);
	if (ec) {
		logger::error("[SceneSettings] WriteGroupedOverwriteFile: could not inspect '{}': {}", path.string(), ec.message());
		return false;
	}

	json data = json::object();
	if (pathExists && !ReadBoundedSceneJson(path, data)) {
		logger::error("[SceneSettings] Refusing to replace invalid overwrite file '{}'", path.string());
		return false;
	}

	if (auto featureIt = data.find(kFeatureKey); featureIt != data.end() &&
		(!featureIt->is_string() || featureIt->get<std::string>() != featureShortName)) {
		logger::error("[SceneSettings] Refusing to relabel overwrite file '{}' from another feature", path.string());
		return false;
	}
	data[kFeatureKey] = featureShortName;
	auto& metadata = data[kMetadataKey];
	if (!metadata.is_null() && !metadata.is_object()) {
		logger::error("[SceneSettings] Refusing to replace invalid metadata in overwrite file '{}'", path.string());
		return false;
	}
	if (metadata.is_null())
		metadata = json::object();
	metadata[kMetadataDescriptionKey] = std::format("{} scene settings overwrite ({})",
		SceneSettingsManager::GetFeatureDisplayName(featureShortName), overwriteType);
	if (extraMetadata.is_object())
		for (const auto& [key, value] : extraMetadata.items())
			metadata[key] = value;
	for (const auto* entry : entries) {
		auto* node = GetObjectAtPath(data, entry->settingPath, true);
		if (!node) {
			logger::error("[SceneSettings] Refusing to replace a non-object path in overwrite file '{}'",
				path.string());
			return false;
		}
		(*node)[entry->settingKey] = entry->value;
	}

	return WriteJsonAtomically(path, data, kOverwriteJsonIndent, "overwrite file");
}

static bool RemoveSettingFromOverwriteFile(const std::filesystem::path& path,
	const std::vector<std::string>& settingPath, const std::string& settingKey)
{
	if (path.empty())
		return true;

	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		return !ec;

	std::ifstream in(path);
	if (!in.is_open()) {
		logger::error("[SceneSettings] Could not open overwrite file '{}' for editing", path.string());
		return false;
	}

	auto data = json::parse(in, nullptr, false);
	if (!data.is_object()) {
		logger::error("[SceneSettings] Could not parse overwrite file '{}' for editing", path.string());
		return false;
	}

	if (!RemoveObjectValueAtPath(data, settingPath, 0, settingKey)) {
		logger::error("[SceneSettings] Overwrite setting '{}' was not found in '{}'",
			settingKey, path.string());
		return false;
	}
	if (!HasSceneOverwriteContent(data)) {
		auto removed = std::filesystem::remove(path, ec);
		if (removed || !ec)
			return true;
		logger::error("[SceneSettings] Failed to delete overwrite file '{}': {}", path.string(), ec.message());
		return false;
	}

	return WriteJsonAtomically(path, data, kOverwriteJsonIndent, "overwrite file");
}

std::vector<std::string> SceneSettingsManager::GetOverwriteModNames()
{
	// Best-effort: unlike ExportPreset, nothing is written here, so a partial load still returns
	// whatever overwrites are already known rather than reporting nothing.
	TryEnsureWeatherDataLoaded();
	TryEnsureLocationDataLoaded();

	std::vector<std::string> names;
	const auto collect = [&](const std::vector<SettingEntry>& sourceEntries) {
		for (const auto& entry : sourceEntries) {
			if (entry.source != EntrySource::Overwrite)
				continue;
			auto name = GetOverwriteModName(entry);
			if (!name.empty() && std::ranges::find(names, name) == names.end())
				names.push_back(std::move(name));
		}
	};

	for (const auto& [type, sourceEntries] : entries)
		collect(sourceEntries);
	for (const auto& [weatherId, config] : weatherSceneConfigs)
		collect(config.entries);
	for (const auto& [configKey, config] : locationSceneConfigs)
		collect(config.entries);
	return names;
}

std::vector<std::filesystem::path> SceneSettingsManager::FindPresetFiles(const std::string& modName) const
{
	std::vector<std::filesystem::path> found;
	if (modName.empty())
		return found;
	const auto prefix = modName + "_";

	const auto sweepDir = [&](const std::filesystem::path& directory) {
		std::error_code ec;
		if (!std::filesystem::exists(directory, ec))
			return;
		for (const auto& path : GetSortedJsonFiles(directory, "preset files"))
			if (path.filename().string().starts_with(prefix))
				found.push_back(path);
	};
	const auto childDirectories = [](const std::filesystem::path& root, std::string_view context) {
		std::error_code ec;
		return std::filesystem::exists(root, ec) ?
		           GetSortedDirectoryPaths(root, true, context) :
		           std::vector<std::filesystem::path>{};
	};

	sweepDir(GetOverwritesPath(SceneType::InteriorOnly));
	for (auto period : kPeriods)
		sweepDir(GetOverwriteDir(GetOverwritesPath(SceneType::TimeOfDay), period));
	for (const auto& weatherDir : childDirectories(GetWeatherOverwritesDir(), "weather overwrite directories")) {
		// A flat weather file feeds every period, so the preset has to claim it alongside its per-period files.
		sweepDir(weatherDir);
		for (auto period : kPeriods)
			sweepDir(GetOverwriteDir(weatherDir, period));
	}
	for (const auto& locationDir : childDirectories(GetLocationOverwritesDir(), "location overwrite directories"))
		sweepDir(locationDir);
	return found;
}

bool SceneSettingsManager::ExportPreset(const std::string& modName)
{
	// Weather and location configs load lazily; baking before they exist would sweep their files and
	// write nothing back.
	if (!TryEnsureWeatherDataLoaded() || !TryEnsureLocationDataLoaded()) {
		logger::error("[SceneSettings] Preset '{}' not exported: weather or location data is not loaded", modName);
		return false;
	}

	const auto safeModName = Util::FileHelpers::SanitizeFileName(modName);
	if (safeModName.empty())
		return false;

	/// One output file: the type description its metadata carries, and the entries baked into it.
	struct PresetFile
	{
		std::string typeDescription;
		json extraMetadata = json::object();
		std::vector<const SettingEntry*> entries;
	};
	std::map<std::pair<std::filesystem::path, std::string>, PresetFile> files;

	const auto bakeContext = [&](const SceneContextId& context, const std::vector<SettingEntry>& sourceEntries,
								 const std::filesystem::path& baseDir, std::string_view sceneLabel,
								 const json& extraMetadata = json::object()) {
		const auto directory = GetOverwriteDir(baseDir, context.period);
		for (const auto& [identity, entry] : BuildEffectiveContextEntries(sourceEntries, context)) {
			auto& file = files[{ directory, identity.featureShortName }];
			file.typeDescription = GetOverwriteTypeDescription(sceneLabel, context.period);
			file.extraMetadata = extraMetadata;
			file.entries.push_back(entry);
		}
	};

	bakeContext({ .type = SceneContextType::Interior, .period = TimeOfDayPeriod::Count },
		GetEntries(SceneType::InteriorOnly), GetOverwritesPath(SceneType::InteriorOnly), "Interior Only");

	for (auto period : kPeriods)
		bakeContext({ .type = SceneContextType::TimeOfDay, .period = period },
			GetEntries(SceneType::TimeOfDay), GetOverwritesPath(SceneType::TimeOfDay), "Time of Day");

	for (const auto& [weatherId, config] : weatherSceneConfigs) {
		const auto weatherDir = GetWeatherOverwritesDir() / Util::FormIdToSpid(weatherId);
		for (auto period : kPeriods)
			bakeContext({ .type = SceneContextType::Weather, .period = period, .weatherId = weatherId },
				config.entries, weatherDir, "Weather");
	}

	for (const auto& [configKey, config] : locationSceneConfigs) {
		const auto* targetDescription = GetLocationTargetTypeName(config.type);
		bakeContext({ .type = SceneContextType::Location,
						.locationType = config.type,
						.locationFormKey = config.formKey },
			config.entries, GetLocationOverwritesDir() / config.formKey, targetDescription,
			json{ { "targetType", targetDescription },
				{ "targetName", config.name },
				{ "coc", config.cocCode } });
	}

	bool wroteAll = true;
	// The sweep runs only once every output is known, so a failure above costs nothing on disk. A file
	// that survives it would be merged into rather than replaced, silently reviving a deleted setting.
	for (const auto& path : FindPresetFiles(safeModName)) {
		std::error_code ec;
		std::filesystem::remove(path, ec);
		if (ec) {
			logger::error("[SceneSettings] Could not remove stale preset file '{}': {}", path.string(), ec.message());
			wroteAll = false;
		}
	}

	for (const auto& [key, file] : files) {
		const auto& [directory, featureShortName] = key;
		const auto path = directory / std::format("{}_{}.json", safeModName, featureShortName);
		if (!WriteGroupedOverwriteFile(path, featureShortName, file.typeDescription, file.entries,
				file.extraMetadata)) {
			logger::error("[SceneSettings] Preset '{}' failed to write '{}'", safeModName, path.string());
			wroteAll = false;
		}
	}
	logger::info("[SceneSettings] Exported preset '{}' as {} file(s)", safeModName, files.size());
	return wroteAll;
}

void SceneSettingsManager::CommitSceneSettingChanges()
{
	SaveAllUserSettings();
	ReapplyIfActive();
}

void SceneSettingsManager::MarkDeferredSceneChanges()
{
	deferredSceneChangesPending = true;
	deferredSceneChangesDeadline = std::chrono::steady_clock::now() + kDeferredSaveDelay;
}

void SceneSettingsManager::HoldDeferredSceneChanges()
{
	// Only ever pushes an existing deadline out, so grabbing a control without moving it cannot
	// schedule a save of its own.
	if (deferredSceneChangesPending)
		deferredSceneChangesDeadline = std::chrono::steady_clock::now() + kDeferredSaveDelay;
}

void SceneSettingsManager::FlushDeferredSceneChanges()
{
	if (!deferredSceneChangesPending || std::chrono::steady_clock::now() < deferredSceneChangesDeadline)
		return;

	SaveAllUserSettings();
	ReapplyIfActive();
}

// --- Event Handler ---

RE::BSEventNotifyControl SceneSettingsManager::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening) {
		// Defer the transition to next frame - cell data isn't available yet.
		// The sink outlives the manager, so a menu close during shutdown must find no singleton.
		if (auto* manager = GetSingleton())
			manager->queuedLoadingTransition.store(true, std::memory_order_relaxed);
	}

	return RE::BSEventNotifyControl::kContinue;
}

// --- Scene Application ---

void SceneSettingsManager::Update()
{
	ProfilerPassScope profilerPass("SceneSettingsManager::Update");
	if (globals::state) {
		const auto frame = globals::state->frameCount;
		if (lastUpdateFrame == frame)
			return;
		lastUpdateFrame = frame;
	}
	VerifyPendingApplies();
	FlushDeferredSceneChanges();

	if (queuedLoadingTransition.exchange(false, std::memory_order_relaxed))
		OnLoadingTransition();
	else
		ResolveAndApply();
}

void SceneSettingsManager::OnLoadingTransition()
{
	resolverDirty = true;
	ResolveAndApply(true, false);
}

void SceneSettingsManager::ReapplyIfActive()
{
	activeEntryCacheDirty = true;
	resolverDirty = true;
	MarkSceneValuesDirty();
	if (!resolverSuspended)
		ResolveAndApply(true);
}

bool SceneSettingsManager::HasActiveSettingsForFeature(const std::string& featureShortName) const
{
	return appliedFeatureNames.contains(featureShortName);
}

bool SceneSettingsManager::HasAnySceneEntriesForFeature(const std::string& featureShortName) const
{
	if (configuredFeatureNamesRevision != entryPresentationRevision) {
		configuredFeatureNamesCache.clear();
		const auto collect = [&](const auto& sourceEntries) {
			for (const auto& entry : sourceEntries)
				configuredFeatureNamesCache.insert(entry.featureShortName);
		};
		for (const auto& [_, sourceEntries] : entries)
			collect(sourceEntries);
		for (const auto& [_, config] : weatherSceneConfigs)
			collect(config.entries);
		for (const auto& [_, config] : locationSceneConfigs)
			collect(config.entries);
		configuredFeatureNamesRevision = entryPresentationRevision;
	}
	return configuredFeatureNamesCache.contains(featureShortName);
}

bool SceneSettingsManager::IsActiveSceneSetting(std::string_view featureShortName,
	std::string_view settingPath, std::string_view settingKey) const
{
	return IsActiveSceneSetting(std::string(featureShortName), SplitCatalogPath(settingPath), std::string(settingKey));
}

bool SceneSettingsManager::IsActiveSceneSetting(const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey) const
{
	return appliedSettings.contains({ featureShortName, settingPath, settingKey });
}

void SceneSettingsManager::CaptureExternalFeatureChanges(Feature* feature)
{
	if (!feature)
		return;
	if (appliedSettings.empty()) {
		InvalidateFeatureSnapshot(feature->GetShortName());
		return;
	}

	json featureSettings;
	if (!TrySaveFeatureSettings(*feature, "inspect external changes", featureSettings))
		return;

	std::vector<std::pair<SettingAddress, json>> changedSettings;
	const auto featureShortName = feature->GetShortName();
	InvalidateFeatureSnapshot(featureShortName);
	for (const auto& [address, appliedValue] : appliedSettings) {
		if (address.featureShortName != featureShortName)
			continue;
		auto* setting = FindAllowedCatalogSetting(
			address.featureShortName, address.settingPath, address.settingKey);
		if (!setting)
			continue;
		const auto* value = GetCatalogSerializedValue(featureSettings, *setting);
		if (!value || !IsSceneSettingPrimitive(*value) ||
			!IsCompatibleSceneSettingValue(appliedValue, *value) || appliedValue == *value)
			continue;
		changedSettings.emplace_back(address, *value);
	}

	if (changedSettings.empty())
		return;
	for (const auto& [address, value] : changedSettings) {
		baselineSettings[address] = value;
		appliedSettings[address] = value;
	}
	resolverDirty = true;
	if (!resolverSuspended)
		ResolveAndApply(true);
}

SceneSettingsManager::SceneLayerGuard::SceneLayerGuard() :
	manager(GetSingleton())
{
	if (manager)
		manager->SuspendSceneLayer();
}

SceneSettingsManager::SceneLayerGuard::~SceneLayerGuard()
{
	// Resuming re-applies through arbitrary features, and a destructor is noexcept.
	try {
		if (manager)
			manager->ResumeSceneLayer();
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to resume the scene layer: {}", e.what());
	} catch (...) {
		logger::error("[SceneSettings] Failed to resume the scene layer");
	}
}

bool SceneSettingsManager::IsFeaturePaused(const std::string& featureShortName) const
{
	auto it = featurePauseStates.find(featureShortName);
	return it != featurePauseStates.end() && it->second;
}

void SceneSettingsManager::SetFeaturePaused(const std::string& featureShortName, bool paused)
{
	featurePauseStates[featureShortName] = paused;
	ReapplyIfActive();
}

void SceneSettingsManager::SuspendSceneLayer()
{
	if (++sceneLayerSuspendDepth > 1)
		return;

	resolverSuspended = true;
	RestoreAppliedSettings();
}

void SceneSettingsManager::ResumeSceneLayer()
{
	if (sceneLayerSuspendDepth <= 0) {
		logger::warn("[SceneSettings] ResumeSceneLayer called without a matching suspend");
		sceneLayerSuspendDepth = 0;
		return;
	}
	if (--sceneLayerSuspendDepth > 0)
		return;

	// The layer was off while suspended, so anything the caller read or wrote is the new base.
	InvalidateFeatureSnapshot();
	resolverSuspended = false;
	resolverDirty = true;
	ResolveAndApply(true);
}

void SceneSettingsManager::ResolveAndApply(bool force, bool allowLocationTransitions)
{
	if (resolverSuspended || sceneLayerSuspendDepth > 0)
		return;
	if (!locationDataLoaded)
		TryEnsureLocationDataLoaded();
	if (!weatherDataLoaded)
		TryEnsureWeatherDataLoaded();
	if (!HasActiveSceneEntriesCached()) {
		applyFailures.clear();
		if (!appliedSettings.empty())
			RestoreAppliedSettings();
		else
			ClearLocationTransitions();
		resolverDirty = !appliedSettings.empty();
		return;
	}

	if (globals::state && globals::state->IsMainOrLoadingMenuOpen()) {
		RestoreAppliedSettings();
		resolverDirty = true;
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	if (!player || !cell) {
		RestoreAppliedSettings();
		resolverDirty = true;
		return;
	}

	const bool interior = Util::IsInterior();
	const auto hour = GetCurrentGameHour();
	auto* location = player->GetCurrentLocation();
	if (!location)
		location = cell->GetLocation();
	const auto* worldspace = player->GetWorldspace();
	const auto locationId = location ? location->GetFormID() : 0;
	const auto cellId = cell->GetFormID();
	const auto worldspaceId = worldspace ? worldspace->GetFormID() : 0;
	const bool cellChanged = cellId != lastResolvedCellId;
	const bool locationContextChanged = locationId != lastResolvedLocationId || cellChanged;
	// Only walking between exterior cells of one worldspace eases; anything behind a loading screen
	// has to be in place by the time the player sees it.
	const bool walkedBetweenWorldspaceCells = allowLocationTransitions && cellChanged &&
	                                          !interior && !lastResolvedInterior &&
	                                          lastResolvedCellId != 0 &&
	                                          worldspaceId != 0 && worldspaceId == lastResolvedWorldspaceId;

	if (!interior)
		TryEnsureWeatherDataLoaded();
	RefreshBlendSnapshot(interior);
	const auto& weather = blendSnapshot.weather;

	const bool contextChanged = interior != lastResolvedInterior ||
	                            locationContextChanged ||
	                            weather.currentWeatherId != lastResolvedCurrentWeatherId ||
	                            weather.previousWeatherId != lastResolvedPreviousWeatherId ||
	                            std::abs(weather.lerp - lastResolvedWeatherLerp) >= kBlendEpsilon ||
	                            lastResolvedHour < 0.0f || std::abs(hour - lastResolvedHour) >= kHourUpdateThreshold;
	const auto now = std::chrono::steady_clock::now();
	const bool applyRetryDue = std::any_of(applyFailures.begin(), applyFailures.end(),
		[&](const auto& item) { return now >= item.second.retryAfter; });
	const auto transitionTime = GetPauseAwareTime();
	if (!force && !resolverDirty && !contextChanged && !applyRetryDue) {
		AdvanceLocationTransitions(transitionTime);
		return;
	}

	resolverDirty = false;
	const bool reconcileLocationTransitions = locationContextChanged || locationOverridesDirty;
	auto& resolved = BuildResolvedSettings(reconcileLocationTransitions, interior);
	if (reconcileLocationTransitions) {
		// Editing a value in place snaps to it, as does arriving from a loading screen.
		StartLocationTransitions(resolved, transitionTime, walkedBetweenWorldspaceCells);
		locationOverridesDirty = false;
	}
	for (const auto& [address, transition] : activeLocationTransitions)
		resolved[address] = EaseLocationTransition(transition, transitionTime);
	ApplyResolvedSettings(resolved, force);
	RetireFinishedLocationTransitions(transitionTime);

	lastResolvedInterior = interior;
	lastResolvedLocationId = locationId;
	lastResolvedCellId = cellId;
	lastResolvedWorldspaceId = worldspaceId;
	lastResolvedHour = hour;
	lastResolvedCurrentWeatherId = weather.currentWeatherId;
	lastResolvedPreviousWeatherId = weather.previousWeatherId;
	lastResolvedWeatherLerp = weather.lerp;
}

float SceneSettingsManager::GetPauseAwareTime() const
{
	return globals::state ? globals::state->timer : 0.0f;
}

float SceneSettingsManager::EaseLocationTransition(const LocationTransition& transition, float now)
{
	const auto linear = transition.duration > 0.0f ?
	                        std::clamp((now - transition.startTime) / transition.duration, 0.0f, 1.0f) :
	                        1.0f;
	const auto smooth = linear * linear * (3.0f - 2.0f * linear);
	return transition.startValue + (transition.targetValue - transition.startValue) * smooth;
}

bool SceneSettingsManager::IsLocationTransitionFinished(const LocationTransition& transition, float now)
{
	return transition.duration <= 0.0f || now - transition.startTime >= transition.duration;
}

void SceneSettingsManager::StartLocationTransitions(
	const ResolvedSettingMap& resolved, float now, bool animateChanges)
{
	ResolvedSettingMap nextOverrideValues;
	for (const auto& [address, _] : pendingLocationTransitionDurations) {
		if (auto resolvedIt = resolved.find(address);
			resolvedIt != resolved.end() && IsNumericValue(resolvedIt->second))
			nextOverrideValues.emplace(address, resolvedIt->second);
	}

	std::set<SettingAddress> candidateAddresses;
	for (const auto* values : { &lastLocationOverrideValues, &nextOverrideValues })
		for (const auto& [address, _] : *values)
			candidateAddresses.insert(address);
	for (const auto& [address, _] : activeLocationTransitions)
		candidateAddresses.insert(address);

	for (const auto& address : candidateAddresses) {
		auto previousIt = lastLocationOverrideValues.find(address);
		auto nextIt = nextOverrideValues.find(address);
		const bool membershipChanged = (previousIt == lastLocationOverrideValues.end()) !=
		                               (nextIt == nextOverrideValues.end());
		const bool valueChanged = previousIt != lastLocationOverrideValues.end() &&
		                          nextIt != nextOverrideValues.end() &&
		                          !ResolvedValuesEqual(previousIt->second, nextIt->second);
		auto previousDurationIt = lastLocationTransitionDurations.find(address);
		auto nextDurationIt = pendingLocationTransitionDurations.find(address);
		const float duration = nextDurationIt != pendingLocationTransitionDurations.end() ?
		                           nextDurationIt->second :
		                           locationTransitionSeconds;
		const bool durationChanged = previousDurationIt != lastLocationTransitionDurations.end() &&
		                             nextDurationIt != pendingLocationTransitionDurations.end() &&
		                             std::abs(previousDurationIt->second - nextDurationIt->second) >= kBlendEpsilon;
		if (!membershipChanged && !valueChanged &&
			!(durationChanged && activeLocationTransitions.contains(address)))
			continue;
		if (!animateChanges) {
			if (activeLocationTransitions.erase(address) != 0)
				locationTransitionBatchesDirty = true;
			continue;
		}

		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end() || !IsNumericValue(baselineIt->second))
			continue;

		// Retarget from wherever the value currently sits so a reversal never snaps.
		float startValue = baselineIt->second.get<float>();
		if (auto activeIt = activeLocationTransitions.find(address); activeIt != activeLocationTransitions.end())
			startValue = EaseLocationTransition(activeIt->second, now);
		else if (auto appliedIt = appliedSettings.find(address);
			appliedIt != appliedSettings.end() && IsNumericValue(appliedIt->second))
			startValue = appliedIt->second.get<float>();

		const auto resolvedIt = resolved.find(address);
		const bool restoreAtEnd = nextIt == nextOverrideValues.end() && resolvedIt == resolved.end();
		const auto& targetJson = nextIt != nextOverrideValues.end() ? nextIt->second :
		                         resolvedIt != resolved.end()      ? resolvedIt->second :
		                                                             baselineIt->second;
		if (!IsNumericValue(targetJson))
			continue;
		const auto targetValue = targetJson.get<float>();
		if (!std::isfinite(startValue) || !std::isfinite(targetValue))
			continue;
		if (duration <= 0.0f || std::abs(targetValue - startValue) < kBlendEpsilon) {
			if (activeLocationTransitions.erase(address) != 0)
				locationTransitionBatchesDirty = true;
			continue;
		}
		activeLocationTransitions.insert_or_assign(address, LocationTransition{
															   .startValue = startValue,
															   .targetValue = targetValue,
															   .startTime = now,
															   .duration = duration,
															   .restoreAtEnd = restoreAtEnd,
														   });
		locationTransitionBatchesDirty = true;
	}
	lastLocationOverrideValues = std::move(nextOverrideValues);
	lastLocationTransitionDurations = pendingLocationTransitionDurations;
}

bool SceneSettingsManager::AdvanceLocationTransitions(float now)
{
	if (activeLocationTransitions.empty())
		return false;
	// A negative delta means the timer restarted, so it must not latch the tick off forever.
	const auto sinceLastTick = now - lastLocationTransitionTick;
	if (sinceLastTick >= 0.0f && sinceLastTick < kLocationTransitionTickInterval)
		return false;
	lastLocationTransitionTick = now;
	if (locationTransitionBatchesDirty)
		RebuildLocationTransitionBatches();

	bool appliedAny = false;
	const auto retryNow = std::chrono::steady_clock::now();
	for (auto& [featureShortName, batch] : locationTransitionBatches) {
		// A batch nobody can see move is a save/load round trip for nothing. The final tick always
		// applies, so the target value still lands exactly.
		bool valuesMoved = false;
		for (size_t index = 0; index < batch.transitions.size(); ++index) {
			const auto eased = EaseLocationTransition(*batch.transitions[index], now);
			valuesMoved = valuesMoved || IsLocationTransitionFinished(*batch.transitions[index], now) ||
			              std::abs(eased - batch.updates[index].value.get<float>()) >= kBlendEpsilon;
			batch.updates[index].value = eased;
		}
		if (!valuesMoved)
			continue;

		auto failureIt = transitionApplyFailures.find(featureShortName);
		if (failureIt != transitionApplyFailures.end() && failureIt->second.signature != batch.signature) {
			transitionApplyFailures.erase(failureIt);
			failureIt = transitionApplyFailures.end();
		}
		if (failureIt != transitionApplyFailures.end() && retryNow < failureIt->second.retryAfter)
			continue;

		// Warn once per distinct batch, then back off, so a stuck feature cannot spam the log.
		const auto recordFailure = [&](std::string_view message) {
			auto& failure = transitionApplyFailures[featureShortName];
			failure.signature = batch.signature;
			if (!failure.warningLogged) {
				logger::warn("[SceneSettings] {}", message);
				failure.warningLogged = true;
			}
			failure.retryAfter = retryNow + kApplyRetryDelay;
		};

		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		if (!feature) {
			recordFailure(std::format(
				"Cannot apply location transition, feature {} is not loaded", featureShortName));
			continue;
		}
		if (!ApplyCatalogSceneSettings(*feature, batch.updates)) {
			recordFailure(std::format("Failed to apply location transition for {}", featureShortName));
			continue;
		}
		transitionApplyFailures.erase(featureShortName);
		ScheduleApplyVerification(featureShortName, batch.updates, batch.signature, true);
		appliedAny = true;

		bool restoredSetting = false;
		bool retainedSetting = false;
		for (size_t index = 0; index < batch.transitions.size(); ++index) {
			const auto& address = batch.addresses[index];
			const auto& transition = *batch.transitions[index];
			const bool finished = IsLocationTransitionFinished(transition, now);
			if (finished && transition.restoreAtEnd) {
				appliedSettings.erase(address);
				baselineSettings.erase(address);
				restoredSetting = true;
			} else {
				appliedSettings[address] = batch.updates[index].value;
				retainedSetting = true;
			}
			if (finished) {
				activeLocationTransitions.erase(address);
				locationTransitionBatchesDirty = true;
			}
		}
		if (retainedSetting)
			appliedFeatureNames.insert(featureShortName);
		else if (restoredSetting)
			PruneAppliedFeatureName(featureShortName);
	}
	if (activeLocationTransitions.empty()) {
		locationTransitionBatches.clear();
		transitionApplyFailures.clear();
		locationTransitionBatchesDirty = false;
	}
	return appliedAny;
}

void SceneSettingsManager::RetireFinishedLocationTransitions(float now)
{
	std::set<std::string> restoredFeatures;
	for (auto transitionIt = activeLocationTransitions.begin(); transitionIt != activeLocationTransitions.end();) {
		const auto& [address, transition] = *transitionIt;
		auto appliedIt = appliedSettings.find(address);
		if (!IsLocationTransitionFinished(transition, now) || appliedIt == appliedSettings.end() ||
			!IsNumericValue(appliedIt->second) ||
			std::abs(appliedIt->second.get<float>() - transition.targetValue) >= kBlendEpsilon) {
			++transitionIt;
			continue;
		}
		if (transition.restoreAtEnd) {
			appliedSettings.erase(address);
			baselineSettings.erase(address);
			restoredFeatures.insert(address.featureShortName);
		}
		transitionIt = activeLocationTransitions.erase(transitionIt);
		locationTransitionBatchesDirty = true;
	}
	for (const auto& featureShortName : restoredFeatures)
		PruneAppliedFeatureName(featureShortName);
}

void SceneSettingsManager::RebuildLocationTransitionBatches()
{
	locationTransitionBatches.clear();
	for (auto& [address, transition] : activeLocationTransitions) {
		auto& batch = locationTransitionBatches[address.featureShortName];
		if (batch.addresses.empty())
			batch.signature = std::hash<std::string_view>{}(address.featureShortName);
		batch.addresses.push_back(address);
		batch.transitions.push_back(&transition);
		batch.updates.push_back({ address.settingPath, address.settingKey, transition.startValue,
			!transition.restoreAtEnd });
		for (const auto& segment : address.settingPath)
			CombineHash(batch.signature, std::hash<std::string_view>{}(segment));
		CombineHash(batch.signature, std::hash<std::string_view>{}(address.settingKey));
		for (const auto value : { transition.startValue, transition.targetValue, transition.duration })
			HashSceneSettingValue(batch.signature, value);
		CombineHash(batch.signature, static_cast<size_t>(transition.restoreAtEnd));
	}
	std::erase_if(transitionApplyFailures,
		[&](const auto& item) { return !locationTransitionBatches.contains(item.first); });
	locationTransitionBatchesDirty = false;
}

void SceneSettingsManager::ClearLocationTransitions()
{
	activeLocationTransitions.clear();
	locationTransitionBatches.clear();
	transitionApplyFailures.clear();
	lastLocationOverrideValues.clear();
	lastLocationTransitionDurations.clear();
	pendingLocationTransitionDurations.clear();
	cachedLocationOverrides.clear();
	cachedLocationOverridesValid = false;
	locationTransitionBatchesDirty = false;
	lastLocationTransitionTick = -1.0f;
	locationOverridesDirty = true;
}

void SceneSettingsManager::RefreshBlendSnapshot(bool interior)
{
	// Indoors there is no weather to blend, and sampling one would churn the resolver's change check.
	blendSnapshot.weather = interior ? WeatherBlend{} : GetWeatherBlend();
	blendSnapshot.timeOfDayFactors = GetTimeOfDayFactors();
}

SceneSettingsManager::WeatherBlend SceneSettingsManager::GetWeatherBlend() const
{
	WeatherBlend blend;
	auto* sky = globals::game::sky;
	if (!sky)
		return blend;
	blend.currentWeatherId = sky->currentWeather ? sky->currentWeather->GetFormID() : 0;
	blend.lerp = std::isfinite(sky->currentWeatherPct) ? std::clamp(sky->currentWeatherPct, 0.0f, 1.0f) : 0.0f;
	blend.previousWeatherId = GetEffectivePreviousWeatherId(sky, blend.lerp);
	return blend;
}

SceneSettingsManager::SettingAddress SceneSettingsManager::GetEntryAddress(const SettingEntry& entry)
{
	return { entry.featureShortName, entry.settingPath, entry.settingKey };
}

bool SceneSettingsManager::IsResolvableEntry(const SettingEntry& entry, SceneType type) const
{
	const bool floatsOnly = type == SceneType::TimeOfDay;
	// A tombstone is resolvable on purpose: suppressing an address means restoring its baseline,
	// and this gate is what gets that baseline collected.
	return IsEntryActive(entry) &&
	       IsSettingAllowedForType(type, entry.featureShortName, entry.settingPath, entry.settingKey) &&
	       (!floatsOnly || IsNumericValue(entry.value));
}

bool SceneSettingsManager::HasActiveSceneEntriesCached()
{
	if (!activeEntryCacheDirty)
		return hasActiveSceneEntries;

	const auto hasResolvable = [&](const std::vector<SettingEntry>& sourceEntries, SceneType type) {
		return std::any_of(sourceEntries.begin(), sourceEntries.end(),
			[&](const SettingEntry& entry) { return IsResolvableEntry(entry, type); });
	};

	hasActiveSceneEntries =
		std::any_of(entries.begin(), entries.end(),
			[&](const auto& item) { return hasResolvable(item.second, item.first); }) ||
		std::any_of(weatherSceneConfigs.begin(), weatherSceneConfigs.end(),
			[&](const auto& item) { return hasResolvable(item.second.entries, SceneType::TimeOfDay); }) ||
		std::any_of(locationSceneConfigs.begin(), locationSceneConfigs.end(),
			[&](const auto& item) { return hasResolvable(item.second.entries, SceneType::Location); });

	activeEntryCacheDirty = false;
	return hasActiveSceneEntries;
}

SceneSettingsManager::ResolvedSettingMap& SceneSettingsManager::BuildResolvedSettings(
	bool collectLocationTransitionDurations, bool interior)
{
	// Reuse the map's nodes across frames; null marks a slot the current resolve did not fill.
	auto& resolved = resolvedSettingsScratch;
	for (auto& [_, value] : resolved)
		value = nullptr;

	std::vector<SettingAddress> requiredBaselines;
	const auto collectBaselines = [&](const std::vector<SettingEntry>& sourceEntries, SceneType type) {
		for (const auto& entry : sourceEntries)
			if (IsResolvableEntry(entry, type))
				if (auto address = GetEntryAddress(entry); !baselineSettings.contains(address))
					requiredBaselines.push_back(std::move(address));
	};
	const auto collectGroupedBaselines = [&](const PeriodSettingMap& values) {
		for (const auto& [address, _] : values)
			if (!baselineSettings.contains(address))
				requiredBaselines.push_back(address);
	};

	const PeriodSettingMap* timeOfDayValues = nullptr;
	const auto& weather = blendSnapshot.weather;
	if (interior) {
		collectBaselines(GetEntries(SceneType::InteriorOnly), SceneType::InteriorOnly);
	} else {
		timeOfDayValues = &BuildTimeOfDayValueGroups();
		collectGroupedBaselines(*timeOfDayValues);
		for (auto weatherId : { weather.currentWeatherId, weather.previousWeatherId })
			if (weatherId != 0)
				collectGroupedBaselines(BuildWeatherValueGroups(weatherId));
	}

	const auto& locationTargets = GetCurrentLocationTargets();
	const bool rebuildLocationOverrides = collectLocationTransitionDurations || !cachedLocationOverridesValid;
	if (rebuildLocationOverrides) {
		for (const auto& target : locationTargets) {
			auto it = locationSceneConfigs.find(GetLocationConfigKey(target.type, target.formKey));
			if (it != locationSceneConfigs.end())
				collectBaselines(it->second.entries, SceneType::Location);
		}
	}
	std::sort(requiredBaselines.begin(), requiredBaselines.end());
	requiredBaselines.erase(
		std::unique(requiredBaselines.begin(), requiredBaselines.end()), requiredBaselines.end());
	EnsureBaselines(requiredBaselines);

	if (interior) {
		ResolveInteriorSettings(resolved);
	} else {
		ResolveTimeOfDaySettings(resolved, *timeOfDayValues);
		ResolveWeatherSettings(resolved, *timeOfDayValues);
	}

	if (rebuildLocationOverrides) {
		pendingLocationTransitionDurations.clear();
		cachedLocationOverrides.clear();
		ResolveLocationSettings(cachedLocationOverrides, locationTargets, true);
		cachedLocationOverridesValid = true;
	}
	for (const auto& [address, value] : cachedLocationOverrides)
		resolved[address] = value;
	std::erase_if(resolved, [](const auto& item) { return item.second.is_null(); });
	return resolved;
}

void SceneSettingsManager::ApplyResolvedSettings(const ResolvedSettingMap& resolved, bool forceRetry)
{
	struct PendingUpdate
	{
		const SettingAddress* address = nullptr;
		const json* value = nullptr;
		bool restore = false;
	};

	std::map<std::string, std::vector<PendingUpdate>> pendingByFeature;
	for (const auto& [address, _] : appliedSettings) {
		if (resolved.contains(address))
			continue;
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt != baselineSettings.end())
			pendingByFeature[address.featureShortName].push_back({ &address, &baselineIt->second, true });
	}

	for (const auto& [address, value] : resolved) {
		auto appliedIt = appliedSettings.find(address);
		if (appliedIt != appliedSettings.end() && ResolvedValuesEqual(appliedIt->second, value))
			continue;
		pendingByFeature[address.featureShortName].push_back({ &address, &value, false });
	}
	std::erase_if(applyFailures, [&](const auto& item) { return !pendingByFeature.contains(item.first); });

	for (const auto& [featureShortName, pending] : pendingByFeature) {
		std::vector<CatalogSceneSettingUpdate> updates;
		updates.reserve(pending.size());
		for (const auto& update : pending)
			updates.push_back({ update.address->settingPath, update.address->settingKey, *update.value,
				!update.restore });

		// Memoized: the backoff check, the failure record and the verification all want the same hash.
		std::optional<size_t> signature;
		const auto getSignature = [&]() {
			if (!signature) {
				signature = GetCatalogUpdateSignature(featureShortName, updates);
				for (const auto& update : pending)
					CombineHash(*signature, static_cast<size_t>(update.restore));
			}
			return *signature;
		};
		// Warn once per distinct failure signature, then back off, so a stuck feature cannot spam the log.
		const auto recordApplyFailure = [&](std::chrono::steady_clock::time_point now, std::string_view message) {
			auto& failure = applyFailures[featureShortName];
			failure.signature = getSignature();
			if (!failure.warningLogged) {
				logger::warn("[SceneSettings] {}", message);
				failure.warningLogged = true;
			}
			failure.retryAfter = now + kApplyRetryDelay;
		};

		auto failureIt = applyFailures.find(featureShortName);
		if (failureIt != applyFailures.end() && failureIt->second.signature != getSignature()) {
			applyFailures.erase(failureIt);
			failureIt = applyFailures.end();
		}
		const auto now = std::chrono::steady_clock::now();
		if (!forceRetry && failureIt != applyFailures.end() && now < failureIt->second.retryAfter)
			continue;

		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		if (!feature) {
			recordApplyFailure(now,
				std::format("Cannot apply resolved settings, feature {} is not loaded", featureShortName));
			continue;
		}
		if (!ApplyCatalogSceneSettings(*feature, updates)) {
			recordApplyFailure(now, std::format("Failed to apply resolved settings for {}", featureShortName));
			continue;
		}
		applyFailures.erase(featureShortName);
		ScheduleApplyVerification(featureShortName, updates, getSignature(), false);
		restoreFailureWarnings.erase(featureShortName);

		bool restoredSetting = false;
		bool appliedSetting = false;
		for (const auto& update : pending) {
			if (update.restore) {
				// Erase the applied entry last: the address points at its key.
				baselineSettings.erase(*update.address);
				appliedSettings.erase(*update.address);
				restoredSetting = true;
			} else {
				appliedSettings[*update.address] = *update.value;
				appliedSetting = true;
			}
		}
		if (appliedSetting)
			appliedFeatureNames.insert(featureShortName);
		else if (restoredSetting)
			PruneAppliedFeatureName(featureShortName);
	}
}

void SceneSettingsManager::RestoreAppliedSettings()
{
	ClearLocationTransitions();

	struct PendingRestore
	{
		SettingAddress address;
		CatalogSceneSettingUpdate update;
	};

	std::map<std::string, std::vector<PendingRestore>> updatesByFeature;
	for (const auto& [address, _] : appliedSettings) {
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt != baselineSettings.end())
			updatesByFeature[address.featureShortName].push_back({
				address, { address.settingPath, address.settingKey, baselineIt->second, false } });
	}

	for (const auto& [featureShortName, pending] : updatesByFeature) {
		const auto now = std::chrono::steady_clock::now();
		if (auto retryIt = restoreRetryAfter.find(featureShortName);
			retryIt != restoreRetryAfter.end() && now < retryIt->second)
			continue;
		auto* feature = Feature::FindFeatureByShortName(featureShortName);
		if (!feature) {
			if (restoreFailureWarnings.insert(featureShortName).second)
				logger::warn("[SceneSettings] Cannot restore {}, feature is not loaded", featureShortName);
			restoreRetryAfter[featureShortName] = now + kApplyRetryDelay;
			continue;
		}

		std::vector<CatalogSceneSettingUpdate> updates;
		updates.reserve(pending.size());
		for (const auto& item : pending)
			updates.push_back(item.update);
		if (!ApplyCatalogSceneSettings(*feature, updates)) {
			if (restoreFailureWarnings.insert(featureShortName).second)
				logger::warn("[SceneSettings] Failed to restore base settings for {}", featureShortName);
			restoreRetryAfter[featureShortName] = now + kApplyRetryDelay;
			continue;
		}
		restoreFailureWarnings.erase(featureShortName);
		restoreRetryAfter.erase(featureShortName);
		// The restore deliberately undoes whatever the last apply put there.
		pendingApplyVerifications.erase(featureShortName);

		for (const auto& item : pending) {
			appliedSettings.erase(item.address);
			baselineSettings.erase(item.address);
		}
		appliedFeatureNames.erase(featureShortName);
	}

	if (appliedSettings.empty()) {
		baselineSettings.clear();
		appliedFeatureNames.clear();
		restoreFailureWarnings.clear();
		restoreRetryAfter.clear();
	} else {
		resolverDirty = true;
	}
}

void SceneSettingsManager::ResolveInteriorSettings(ResolvedSettingMap& resolved) const
{
	OverlayAllEntries(resolved, GetEntries(SceneType::InteriorOnly), SceneType::InteriorOnly);
}

void SceneSettingsManager::CollectPeriodValueGroups(
	const std::vector<SettingEntry>& sourceEntries, PeriodSettingMap& values) const
{
	// Shipped overwrites are the layer's defaults; the user's own entry for the same address wins.
	for (auto source : { EntrySource::Overwrite, EntrySource::User }) {
		for (const auto& entry : sourceEntries) {
			const auto periodIndex = static_cast<int>(entry.period);
			if (entry.source != source || periodIndex < 0 || periodIndex >= kPeriodCount ||
				!IsEntryActive(entry))
				continue;
			// A tombstone clears its own period rather than filling it, so the layer beneath shows through.
			if (entry.deleted) {
				if (auto valuesIt = values.find(GetEntryAddress(entry)); valuesIt != values.end())
					valuesIt->second[periodIndex].reset();
				continue;
			}
			if (!IsResolvableEntry(entry, SceneType::TimeOfDay))
				continue;
			const auto value = entry.value.get<float>();
			if (std::isfinite(value))
				values[GetEntryAddress(entry)][periodIndex] = value;
		}
	}
}

const SceneSettingsManager::PeriodSettingMap& SceneSettingsManager::BuildTimeOfDayValueGroups() const
{
	if (timeOfDayValueGroups.revision == sceneValueRevision)
		return timeOfDayValueGroups.values;
	timeOfDayValueGroups.values.clear();
	CollectPeriodValueGroups(GetEntries(SceneType::TimeOfDay), timeOfDayValueGroups.values);
	timeOfDayValueGroups.revision = sceneValueRevision;
	return timeOfDayValueGroups.values;
}

const SceneSettingsManager::PeriodSettingMap& SceneSettingsManager::BuildWeatherValueGroups(
	RE::FormID weatherId) const
{
	auto& cached = weatherValueGroups[weatherId];
	if (cached.revision == sceneValueRevision)
		return cached.values;
	cached.values.clear();
	if (auto configIt = weatherSceneConfigs.find(weatherId); configIt != weatherSceneConfigs.end())
		CollectPeriodValueGroups(configIt->second.entries, cached.values);
	cached.revision = sceneValueRevision;
	return cached.values;
}

void SceneSettingsManager::ResolveTimeOfDaySettings(
	ResolvedSettingMap& resolved, const PeriodSettingMap& values) const
{
	const auto& factors = blendSnapshot.timeOfDayFactors;
	for (const auto& [address, periodValues] : values) {
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end() || !IsNumericValue(baselineIt->second))
			continue;
		const auto baseline = baselineIt->second.get<float>();
		float result = 0.0f;
		for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex)
			result += factors[periodIndex] * periodValues[periodIndex].value_or(baseline);
		resolved[address] = result;
	}
}

void SceneSettingsManager::ResolveWeatherSettings(
	ResolvedSettingMap& resolved, const PeriodSettingMap& timeOfDayValues) const
{
	const auto& weather = blendSnapshot.weather;
	if (weather.currentWeatherId == 0)
		return;

	const auto& factors = blendSnapshot.timeOfDayFactors;
	const auto& currentValues = BuildWeatherValueGroups(weather.currentWeatherId);
	const auto& previousValues = BuildWeatherValueGroups(weather.previousWeatherId);

	const auto resolveWeather = [&](const SettingAddress& address, const PeriodSettingMap& weatherValues,
								   float baseline) -> std::optional<float> {
		auto weatherIt = weatherValues.find(address);
		if (weatherIt == weatherValues.end())
			return std::nullopt;
		auto timeOfDayIt = timeOfDayValues.find(address);
		float result = 0.0f;
		for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex) {
			const auto lower = timeOfDayIt != timeOfDayValues.end() ?
			                       timeOfDayIt->second[periodIndex].value_or(baseline) :
			                       baseline;
			result += factors[periodIndex] * weatherIt->second[periodIndex].value_or(lower);
		}
		return result;
	};

	const auto blendAddress = [&](const SettingAddress& address) {
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end() || !IsNumericValue(baselineIt->second))
			return;
		const auto baseline = baselineIt->second.get<float>();
		// Where the time-of-day layer left this address is what an unconfigured weather blends from.
		float lowerValue = baseline;
		if (auto resolvedIt = resolved.find(address);
			resolvedIt != resolved.end() && IsNumericValue(resolvedIt->second))
			lowerValue = resolvedIt->second.get<float>();

		const auto currentValue = resolveWeather(address, currentValues, baseline);
		const auto previousValue = resolveWeather(address, previousValues, baseline);
		if (!currentValue && !previousValue)
			return;
		const auto from = previousValue.value_or(lowerValue);
		const auto to = currentValue.value_or(lowerValue);
		resolved[address] = from + (to - from) * weather.lerp;
	};

	for (const auto& [address, _] : currentValues)
		blendAddress(address);
	for (const auto& [address, _] : previousValues)
		if (!currentValues.contains(address))
			blendAddress(address);
}

void SceneSettingsManager::ResolveLocationSettings(ResolvedSettingMap& resolved,
	const std::vector<LocationTarget>& locationTargets, bool collectTransitionDurations)
{
	auto* transitionDurations = collectTransitionDurations ? &pendingLocationTransitionDurations : nullptr;
	for (const auto& target : locationTargets) {
		auto it = locationSceneConfigs.find(GetLocationConfigKey(target.type, target.formKey));
		if (it == locationSceneConfigs.end())
			continue;
		OverlayAllEntries(resolved, it->second.entries, SceneType::Location, transitionDurations);
	}
}

void SceneSettingsManager::OverlayEntries(ResolvedSettingMap& resolved, const std::vector<SettingEntry>& sourceEntries,
	SceneType type, EntrySource source, std::map<SettingAddress, float>* transitionDurations) const
{
	for (const auto& entry : sourceEntries) {
		if (entry.source != source || !IsEntryActive(entry) ||
			!IsSettingAllowedForType(type, entry.featureShortName, entry.settingPath, entry.settingKey))
			continue;
		auto address = GetEntryAddress(entry);
		if (!baselineSettings.contains(address))
			continue;
		// A tombstone supplies nothing. Nulling the slot is how this map already marks an address the
		// resolve did not fill, so BuildResolvedSettings prunes it and the baseline is restored.
		if (entry.deleted) {
			if (transitionDurations)
				transitionDurations->erase(address);
			resolved[std::move(address)] = nullptr;
			continue;
		}
		if (transitionDurations && IsNumericValue(entry.value)) {
			const auto duration = entry.transitionSeconds.value_or(locationTransitionSeconds);
			(*transitionDurations)[address] = std::clamp(
				std::isfinite(duration) ? duration : locationTransitionSeconds,
				0.0f, kMaxLocationTransitionSeconds);
		}
		resolved[std::move(address)] = entry.value;
	}
}

void SceneSettingsManager::OverlayAllEntries(ResolvedSettingMap& resolved,
	const std::vector<SettingEntry>& sourceEntries, SceneType type,
	std::map<SettingAddress, float>* transitionDurations) const
{
	// Shipped overwrites are the layer's defaults; the user's own entry for the same address wins.
	OverlayEntries(resolved, sourceEntries, type, EntrySource::Overwrite, transitionDurations);
	OverlayEntries(resolved, sourceEntries, type, EntrySource::User, transitionDurations);
}

const json* SceneSettingsManager::GetFeatureBaseSnapshot(const std::string& featureShortName)
{
	if (auto snapshotIt = featureBaseSnapshots.find(featureShortName); snapshotIt != featureBaseSnapshots.end())
		return &snapshotIt->second;

	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	json snapshot;
	if (!feature || !TrySaveFeatureSettings(*feature, "snapshot settings", snapshot))
		return nullptr;

	// SaveSettings reports the live scene layer, so fold the applied addresses back to their baselines.
	for (auto appliedIt = appliedSettings.lower_bound({ featureShortName, {}, {} });
		appliedIt != appliedSettings.end() && appliedIt->first.featureShortName == featureShortName;
		++appliedIt) {
		const auto& address = appliedIt->first;
		auto baselineIt = baselineSettings.find(address);
		if (baselineIt == baselineSettings.end())
			continue;
		auto* setting = FindAllowedCatalogSetting(
			address.featureShortName, address.settingPath, address.settingKey);
		auto* value = setting ? GetCatalogSerializedValue(snapshot, *setting) : nullptr;
		if (value && IsCompatibleSceneSettingValue(*value, baselineIt->second))
			*value = baselineIt->second;
	}

	return &featureBaseSnapshots.emplace(featureShortName, std::move(snapshot)).first->second;
}

void SceneSettingsManager::EnsureBaselines(std::span<const SettingAddress> addresses)
{
	std::map<std::string, std::vector<const SettingAddress*>> missingByFeature;
	for (const auto& address : addresses)
		if (!baselineSettings.contains(address))
			missingByFeature[address.featureShortName].push_back(&address);

	for (const auto& [featureShortName, missing] : missingByFeature) {
		const auto* snapshot = GetFeatureBaseSnapshot(featureShortName);
		if (!snapshot)
			continue;
		for (const auto* address : missing) {
			auto* setting = FindAllowedCatalogSetting(
				address->featureShortName, address->settingPath, address->settingKey);
			const auto* value = setting ? GetCatalogSerializedValue(*snapshot, *setting) : nullptr;
			if (value && IsSceneSettingPrimitive(*value))
				baselineSettings.try_emplace(*address, *value);
		}
	}
}

void SceneSettingsManager::InvalidateFeatureSnapshot(std::string_view featureShortName)
{
	cachedLocationOverridesValid = false;
	locationOverridesDirty = true;
	if (featureShortName.empty()) {
		featureBaseSnapshots.clear();
		featureApplyDocuments.clear();
		pendingApplyVerifications.clear();
		std::erase_if(baselineSettings,
			[&](const auto& item) { return !appliedSettings.contains(item.first); });
		return;
	}
	const auto featureName = std::string(featureShortName);
	featureBaseSnapshots.erase(featureName);
	featureApplyDocuments.erase(featureName);
	pendingApplyVerifications.erase(featureName);
	std::erase_if(baselineSettings, [&](const auto& item) {
		return item.first.featureShortName == featureName && !appliedSettings.contains(item.first);
	});
}

void SceneSettingsManager::PruneAppliedFeatureName(const std::string& featureShortName)
{
	if (std::none_of(appliedSettings.begin(), appliedSettings.end(),
			[&](const auto& item) { return item.first.featureShortName == featureShortName; }))
		appliedFeatureNames.erase(featureShortName);
}

json SceneSettingsManager::GetBaselineValue(const SettingAddress& address)
{
	if (!FindAllowedCatalogSetting(address.featureShortName, address.settingPath, address.settingKey))
		return {};
	EnsureBaselines(std::span{ &address, 1 });
	if (auto it = baselineSettings.find(address); it != baselineSettings.end())
		return it->second;
	return {};
}

bool SceneSettingsManager::ResolvedValuesEqual(const json& lhs, const json& rhs)
{
	if (lhs.is_number() && rhs.is_number())
		return std::abs(lhs.get<double>() - rhs.get<double>()) < kBlendEpsilon;
	return lhs == rhs;
}

// --- Debug Inspection ---

/** @brief Renders a stored setting value as a short debug string. */
static std::string FormatDebugValue(const json& value)
{
	if (value.is_null())
		return "-";
	return value.is_string() ? value.get<std::string>() : value.dump();
}

/** @brief Display name for a form, or "-" when the id is unset. */
static std::string FormatDebugFormName(RE::FormID formId)
{
	return formId ? Util::GetFormDisplayName(formId) : "-";
}

SceneSettingsManager::DebugSnapshot SceneSettingsManager::GetDebugSnapshot() const
{
	DebugSnapshot snapshot;

	snapshot.dataLoaded = dataLoaded;
	snapshot.weatherDataLoaded = weatherDataLoaded;
	snapshot.locationDataLoaded = locationDataLoaded;
	snapshot.gameDataReady = gameDataReady;
	snapshot.resolverSuspended = resolverSuspended;
	snapshot.resolverDirty = resolverDirty;
	snapshot.activeEntryCacheDirty = activeEntryCacheDirty;
	snapshot.hasActiveSceneEntries = hasActiveSceneEntries;
	snapshot.deferredSceneChangesPending = deferredSceneChangesPending;
	snapshot.sceneLayerSuspendDepth = sceneLayerSuspendDepth;

	snapshot.lastInterior = lastResolvedInterior;
	snapshot.lastCellId = lastResolvedCellId;
	snapshot.lastLocationId = lastResolvedLocationId;
	snapshot.lastHour = lastResolvedHour;
	snapshot.lastWeather = { lastResolvedCurrentWeatherId, lastResolvedPreviousWeatherId, lastResolvedWeatherLerp };
	snapshot.blendFactors = blendSnapshot.timeOfDayFactors;

	snapshot.menuOpen = globals::state && globals::state->IsMainOrLoadingMenuOpen();
	snapshot.interior = Util::IsInterior();
	snapshot.gameHour = GetCurrentGameHour();
	snapshot.period = GetCurrentPeriod();
	snapshot.timeOfDayFactors = GetTimeOfDayFactors();

	// Sampled live rather than from the resolver, so an interior still shows what the sky is doing.
	// Observing must not advance the resolver's weather tracking, which an interior would not.
	const auto trackedPreviousWeatherId = cachedPreviousWeatherId;
	snapshot.weather = GetWeatherBlend();
	cachedPreviousWeatherId = trackedPreviousWeatherId;
	snapshot.currentWeatherName = FormatDebugFormName(snapshot.weather.currentWeatherId);
	snapshot.previousWeatherName = FormatDebugFormName(snapshot.weather.previousWeatherId);

	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	snapshot.playerReady = player && cell;
	if (cell) {
		snapshot.cellId = cell->GetFormID();
		snapshot.cellName = FormatDebugFormName(snapshot.cellId);
		snapshot.cellEditorId = Util::GetFormEditorID(cell);
	}
	auto* location = player ? player->GetCurrentLocation() : nullptr;
	if (!location && cell)
		location = cell->GetLocation();
	if (location) {
		snapshot.locationId = location->GetFormID();
		snapshot.locationName = FormatDebugFormName(snapshot.locationId);
	}
	snapshot.locationTargets = GetCurrentLocationTargets();

	const auto buildEntries = [&](const std::vector<SettingEntry>& sourceEntries, SceneType type) {
		std::vector<DebugEntry> debugEntries;
		debugEntries.reserve(sourceEntries.size());
		for (const auto& entry : sourceEntries) {
			debugEntries.push_back({
				.feature = entry.featureShortName,
				.path = JoinDisplayParts(entry.settingPath, {}),
				.key = entry.settingKey,
				.value = FormatDebugValue(entry.value),
				.period = entry.period == TimeOfDayPeriod::Count ? std::string() : GetPeriodName(entry.period),
				.transitionSeconds = entry.transitionSeconds,
				.overwrite = entry.source == EntrySource::Overwrite,
				.paused = entry.paused,
				.active = IsEntryActive(entry),
				.resolvable = IsResolvableEntry(entry, type),
			});
		}
		return debugEntries;
	};

	for (auto type : { SceneType::InteriorOnly, SceneType::TimeOfDay }) {
		snapshot.sceneLayers.push_back({
			.name = GetSceneTypeName(type),
			.matchesCurrentScene = type == (snapshot.interior ? SceneType::InteriorOnly : SceneType::TimeOfDay),
			.entries = buildEntries(GetEntries(type), type),
		});
	}

	for (const auto& [weatherId, config] : weatherSceneConfigs) {
		snapshot.weatherLayers.push_back({
			.name = FormatDebugFormName(weatherId),
			.detail = std::format("{:08X}", weatherId),
			.matchesCurrentScene = weatherId == snapshot.weather.currentWeatherId ||
		                           weatherId == snapshot.weather.previousWeatherId,
			.entries = buildEntries(config.entries, SceneType::TimeOfDay),
		});
	}

	std::set<std::string> activeLocationKeys;
	for (const auto& target : snapshot.locationTargets)
		activeLocationKeys.insert(GetLocationConfigKey(target.type, target.formKey));
	for (const auto& [configKey, config] : locationSceneConfigs) {
		snapshot.locationLayers.push_back({
			.name = config.name.empty() ? configKey : config.name,
			.detail = configKey,
			.matchesCurrentScene = activeLocationKeys.contains(configKey),
			.entries = buildEntries(config.entries, SceneType::Location),
		});
	}

	const auto periodValuesFor = [](const PeriodSettingMap& values, const SettingAddress& address) {
		auto it = values.find(address);
		return it != values.end() ? it->second : PeriodValues{};
	};
	// Each group build is revision-gated, but they are loop-invariant, so hoist the lookups too.
	const auto& timeOfDayValues = BuildTimeOfDayValueGroups();
	const auto& currentWeatherValues = BuildWeatherValueGroups(snapshot.weather.currentWeatherId);
	const auto& previousWeatherValues = BuildWeatherValueGroups(snapshot.weather.previousWeatherId);
	for (const auto& [address, value] : appliedSettings) {
		DebugResolvedSetting resolvedSetting{
			.feature = address.featureShortName,
			.path = JoinDisplayParts(address.settingPath, {}),
			.key = address.settingKey,
			.baseline = "-",
			.applied = FormatDebugValue(value),
		};
		if (auto baselineIt = baselineSettings.find(address); baselineIt != baselineSettings.end())
			resolvedSetting.baseline = FormatDebugValue(baselineIt->second);
		resolvedSetting.timeOfDayValues = periodValuesFor(timeOfDayValues, address);
		resolvedSetting.currentWeatherValues = periodValuesFor(currentWeatherValues, address);
		resolvedSetting.previousWeatherValues = periodValuesFor(previousWeatherValues, address);
		snapshot.resolvedSettings.push_back(std::move(resolvedSetting));
	}

	snapshot.transitionTime = GetPauseAwareTime();
	snapshot.lastTransitionTick = lastLocationTransitionTick;
	snapshot.globalTransitionSeconds = locationTransitionSeconds;
	snapshot.transitionBatchesDirty = locationTransitionBatchesDirty;
	snapshot.transitionBatchCount = locationTransitionBatches.size();
	for (const auto& [address, transition] : activeLocationTransitions) {
		const auto elapsed = snapshot.transitionTime - transition.startTime;
		snapshot.locationTransitions.push_back({
			.feature = address.featureShortName,
			.path = JoinDisplayParts(address.settingPath, {}),
			.key = address.settingKey,
			.startValue = transition.startValue,
			.targetValue = transition.targetValue,
			.currentValue = EaseLocationTransition(transition, snapshot.transitionTime),
			.progress = transition.duration > 0.0f ? std::clamp(elapsed / transition.duration, 0.0f, 1.0f) : 1.0f,
			.duration = transition.duration,
			.restoreAtEnd = transition.restoreAtEnd,
		});
	}
	for (const auto& [featureShortName, _] : transitionApplyFailures)
		snapshot.transitionApplyFailures.push_back(featureShortName);

	for (const auto& [featureShortName, _] : applyFailures)
		snapshot.applyFailures.push_back(featureShortName);
	snapshot.restoreFailures.assign(restoreFailureWarnings.begin(), restoreFailureWarnings.end());
	for (const auto& [featureShortName, paused] : featurePauseStates)
		if (paused)
			snapshot.pausedFeatures.push_back(featureShortName);

	return snapshot;
}

// --- Unified Persistence ---

static json EntryToJson(const SceneSettingsManager::SettingEntry& entry)
{
	json item = entry.serializedTemplate.is_object() ? entry.serializedTemplate : json::object();
	item["feature"] = entry.featureShortName;
	if (!entry.settingPath.empty())
		item["path"] = entry.settingPath;
	else
		item.erase("path");
	item["setting"] = entry.settingKey;
	item["value"] = entry.value;
	item["originalValue"] = entry.originalValue;
	item["paused"] = entry.paused;
	// Only our own status is ours to erase: an unrecognised one belongs to another implementation
	// round and rides through on the serialized template.
	if (entry.deleted)
		item[kStatusKey] = kStatusDeleted;
	else if (item.value(kStatusKey, std::string{}) == kStatusDeleted)
		item.erase(kStatusKey);
	if (entry.period != SceneSettingsManager::TimeOfDayPeriod::Count)
		item["period"] = SceneSettingsManager::GetPeriodName(entry.period);
	else
		item.erase("period");
	if (entry.transitionSeconds)
		item["transitionSeconds"] = *entry.transitionSeconds;
	else if (!entry.retainSerializedTransition)
		item.erase("transitionSeconds");
	return item;
}

static json UserEntriesToArray(const std::vector<SceneSettingsManager::SettingEntry>& entries, bool transitionOnly = false)
{
	json arr = json::array();
	for (const auto& entry : entries)
		if (entry.source == SceneSettingsManager::EntrySource::User &&
			(!transitionOnly || IsNumericValue(entry.value)))
			arr.push_back(EntryToJson(entry));
	return arr;
}

static void AppendRawEntries(json& arr, const std::vector<json>& rawEntries)
{
	if (!arr.is_array())
		arr = json::array();
	for (const auto& raw : rawEntries)
		arr.push_back(raw);
}

/** @brief Writes serialized entries into a keyed section, keeping whatever it already listed.
 *  Entries this build could not resolve stay in the document rather than being written over. */
static void MergeSectionEntries(json& section, json userEntries)
{
	if (auto entriesIt = section.find("entries"); entriesIt != section.end() && entriesIt->is_array())
		for (const auto& rawEntry : *entriesIt)
			userEntries.push_back(rawEntry);
	section["entries"] = std::move(userEntries);
}

static bool ShouldSerializeUserSection(const json& data, std::string_view key, bool expectObject, bool modified)
{
	auto it = data.find(std::string(key));
	return modified || it == data.end() || (expectObject ? it->is_object() : it->is_array());
}

void SceneSettingsManager::SaveAllUserSettings()
{
	if (!userSettingsDocumentLoaded)
		LoadAllUserSettings();
	const bool weatherLoaded = TryEnsureWeatherDataLoaded();
	const bool locationLoaded = TryEnsureLocationDataLoaded();
	if (!userSettingsDocumentWritable || !preservedUserSettingsRoot.is_object()) {
		if (!userSettingsWriteBlockedWarning) {
			logger::error("[SceneSettings] Refusing to overwrite SceneManager.json because its existing document is invalid");
			userSettingsWriteBlockedWarning = true;
		}
		deferredSceneChangesPending = ++deferredSaveFailures < kMaxDeferredSaveRetries;
		deferredSceneChangesDeadline = std::chrono::steady_clock::now() + kDeferredSaveRetryDelay;
		return;
	}

	auto path = GetUserSettingsFilePath();
	json data = preservedUserSettingsRoot;
	if (ShouldSerializeUserSection(data, "interiorOnly", false, interiorUserSettingsModified)) {
		data["interiorOnly"] = UserEntriesToArray(GetEntries(SceneType::InteriorOnly));
		AppendRawEntries(data["interiorOnly"], unresolvedUserEntries[SceneType::InteriorOnly]);
	}
	if (ShouldSerializeUserSection(data, "timeOfDay", false, timeOfDayUserSettingsModified)) {
		data["timeOfDay"] = UserEntriesToArray(GetEntries(SceneType::TimeOfDay), true);
		AppendRawEntries(data["timeOfDay"], unresolvedUserEntries[SceneType::TimeOfDay]);
	}

	// Weather entries (keyed by SPID)
	if (weatherLoaded && ShouldSerializeUserSection(data, "weather", true, weatherUserSettingsModified)) {
		json weatherObj = unresolvedWeatherUserSettings.is_object() ?
		                      unresolvedWeatherUserSettings : json::object();
		std::set<RE::FormID> weatherIds;
		for (const auto& [weatherId, _] : weatherSceneConfigs)
			weatherIds.insert(weatherId);
		for (const auto& [weatherId, _] : weatherShowTimeOfDay)
			weatherIds.insert(weatherId);

		for (auto weatherId : weatherIds) {
			if (weatherId == 0)
				continue;
			const auto spid = Util::FormIdToSpid(weatherId);
			auto configIt = weatherSceneConfigs.find(weatherId);
			auto userEntries = configIt != weatherSceneConfigs.end() ?
			                       UserEntriesToArray(configIt->second.entries, true) : json::array();
			auto showIt = weatherShowTimeOfDay.find(weatherId);
			const bool hasShowPreference = showIt != weatherShowTimeOfDay.end();

			auto rawIt = weatherObj.find(spid);
			const bool hasRaw = rawIt != weatherObj.end();
			if (userEntries.empty() && !hasShowPreference && !hasRaw)
				continue;
			if (hasRaw && !rawIt->is_object()) {
				if (userEntries.empty() && !hasShowPreference)
					continue;
				*rawIt = json::object();
			}

			json weatherEntry = hasRaw ? *rawIt : json::object();
			// A weather with no entries of its own leaves whatever the document listed untouched.
			if (!userEntries.empty())
				MergeSectionEntries(weatherEntry, std::move(userEntries));
			if (hasShowPreference)
				weatherEntry["showTimeOfDay"] = showIt->second;
			weatherObj[spid] = std::move(weatherEntry);
		}
		data["weather"] = std::move(weatherObj);
	}

	if (locationLoaded && ShouldSerializeUserSection(data, "location", true, locationUserSettingsModified)) {
		json locationObj = unresolvedLocationUserSettings.is_object() ?
		                       unresolvedLocationUserSettings : json::object();
		if (locationTransitionModified)
			locationObj["transitionSeconds"] = locationTransitionSeconds;
		for (const auto& [_, config] : locationSceneConfigs) {
			auto userEntries = UserEntriesToArray(config.entries);
			// A target the user took on is worth remembering even before it has a single setting.
			if (userEntries.empty() && !config.userAuthored)
				continue;
			const auto* sectionName = GetLocationSectionName(config.type);
			auto& section = locationObj[sectionName];
			if (!section.is_object())
				section = json::object();
			auto& rawConfig = section[config.formKey];
			json locationEntry = rawConfig.is_object() ? rawConfig : json::object();
			MergeSectionEntries(locationEntry, std::move(userEntries));
			locationEntry["type"] = GetLocationTargetTypeName(config.type);
			locationEntry["name"] = config.name;
			locationEntry["editorId"] = config.editorId;
			locationEntry["coc"] = config.cocCode;
			rawConfig = std::move(locationEntry);
		}
		data["location"] = std::move(locationObj);
	}

	const bool saved = WriteJsonAtomically(path, data, kOverwriteJsonIndent, "SceneManager.json");
	if (saved) {
		preservedUserSettingsRoot = data;
		if (locationLoaded && locationTransitionModified) {
			if (!unresolvedLocationUserSettings.is_object())
				unresolvedLocationUserSettings = json::object();
			unresolvedLocationUserSettings["transitionSeconds"] = locationTransitionSeconds;
		}
		interiorUserSettingsModified = false;
		timeOfDayUserSettingsModified = false;
		weatherUserSettingsModified = false;
		locationUserSettingsModified = false;
		locationTransitionModified = false;
		userSettingsWriteBlockedWarning = false;
		deferredSaveFailures = 0;
		logger::info("[SceneSettings] Saved SceneManager.json");
		deferredSceneChangesPending = false;
		return;
	}

	if (++deferredSaveFailures >= kMaxDeferredSaveRetries) {
		logger::error("[SceneSettings] Giving up on SceneManager.json after {} attempts; changes stay in memory",
			deferredSaveFailures);
		deferredSceneChangesPending = false;
		return;
	}
	deferredSceneChangesPending = true;
	deferredSceneChangesDeadline = std::chrono::steady_clock::now() + kDeferredSaveRetryDelay;
}

static bool LoadEntryFromJson(const nlohmann::json& item, SceneSettingsManager::SettingEntry& entry,
	bool requirePeriod, const char* typeName,
	std::optional<SceneSettingsManager::SceneType> allowedSceneType = std::nullopt,
	bool requireNumericValue = false, FeatureSettingsCache* featureSettingsCache = nullptr)
{
	using SSM = SceneSettingsManager;

	if (!item.contains("feature") || !item.contains("setting") || !item.contains("value")) {
		logger::warn("[SceneSettings] {} entry missing feature/setting/value fields", typeName);
		return false;
	}
	if (!item["feature"].is_string() || !item["setting"].is_string()) {
		logger::warn("[SceneSettings] {} entry feature/setting not strings", typeName);
		return false;
	}

	entry.featureShortName = item["feature"].get<std::string>();
	entry.settingPath.clear();
	if (auto it = item.find("path"); it != item.end()) {
		if (!it->is_array()) {
			logger::warn("[SceneSettings] {} entry path is not an array", typeName);
			return false;
		}
		for (const auto& part : *it) {
			if (!part.is_string()) {
				logger::warn("[SceneSettings] {} entry path contains a non-string component", typeName);
				return false;
			}
			entry.settingPath.push_back(part.get<std::string>());
		}
	}
	entry.settingKey = item["setting"].get<std::string>();
	entry.value = item["value"];
	entry.originalValue = item.value("originalValue", entry.value);
	entry.serializedTemplate = item.is_object() ? item : json::object();
	if (auto pausedIt = item.find("paused"); pausedIt != item.end() && !pausedIt->is_boolean()) {
		logger::warn("[SceneSettings] {} entry paused field is not boolean", typeName);
		return false;
	}
	entry.paused = item.value("paused", false);
	entry.deleted = false;
	if (auto statusIt = item.find(kStatusKey); statusIt != item.end()) {
		if (!statusIt->is_string()) {
			logger::warn("[SceneSettings] {} entry status field is not a string", typeName);
			return false;
		}
		entry.deleted = statusIt->get<std::string>() == kStatusDeleted;
	}
	entry.source = SSM::EntrySource::User;

	auto sceneType = allowedSceneType.value_or(requirePeriod ? SSM::SceneType::TimeOfDay : SSM::SceneType::InteriorOnly);
	// An unusable transition costs the entry its blend, never its value: the setting still has to be
	// honored, and the raw field still has to survive the round trip.
	if (auto transitionIt = item.find("transitionSeconds"); transitionIt != item.end()) {
		const auto seconds = transitionIt->is_number() ? transitionIt->get<float>() : 0.0f;
		if (sceneType != SSM::SceneType::Location || !transitionIt->is_number()) {
			logger::warn("[SceneSettings] {} entry transitionSeconds is not valid for this scene type; applying it without a transition", typeName);
			entry.retainSerializedTransition = true;
		} else if (!std::isfinite(seconds) || seconds < 0.0f || seconds > SSM::kMaxLocationTransitionSeconds) {
			logger::warn("[SceneSettings] {} entry transitionSeconds is outside 0..{}; applying it without a transition",
				typeName, SSM::kMaxLocationTransitionSeconds);
			entry.retainSerializedTransition = true;
		} else {
			entry.transitionSeconds = seconds;
		}
	}
	if (!SSM::IsFeatureAllowedForType(sceneType, entry.featureShortName)) {
		logger::warn("[SceneSettings] {} entry feature '{}' is not allowed for this scene type", typeName, entry.featureShortName);
		return false;
	}

	if (requirePeriod) {
		if (!item.contains("period") || !item["period"].is_string()) {
			logger::warn("[SceneSettings] {} entry {}.{} missing period - skipping", typeName, entry.featureShortName, entry.settingKey);
			return false;
		}
		entry.period = SSM::GetPeriodFromName(item["period"].get<std::string>());
		if (entry.period == SSM::TimeOfDayPeriod::Count) {
			logger::warn("[SceneSettings] {} entry {}.{} has invalid period '{}' - skipping", typeName, entry.featureShortName, entry.settingKey, item["period"].get<std::string>());
			return false;
		}
	}

	// Per-period entries always blend as floats, so they carry the same requirement as float-only scenes.
	const bool requireNumeric = requirePeriod || requireNumericValue;
	if (requireNumeric && (!IsNumericValue(entry.value) || !IsNumericValue(entry.originalValue) ||
		!std::isfinite(entry.value.get<float>()))) {
		logger::warn("[SceneSettings] {} entry {} is not a finite float setting - skipping",
			typeName, GetSettingLogName(entry.featureShortName, entry.settingPath, entry.settingKey));
		return false;
	}

	if (!ValidateSceneSettingEntry(typeName, entry.featureShortName, entry.settingPath, entry.settingKey,
			entry.value, requireNumeric, featureSettingsCache) ||
		!ValidateSceneSettingEntry(typeName, entry.featureShortName, entry.settingPath, entry.settingKey,
			entry.originalValue, requireNumeric, featureSettingsCache))
		return false;
	if (entry.transitionSeconds &&
		(!IsNumericValue(entry.value) || !FindAllowedCatalogSetting(
			entry.featureShortName, entry.settingPath, entry.settingKey, true))) {
		logger::warn("[SceneSettings] {} entry {} has a transition on a discrete setting; applying it instantly",
			typeName, GetSettingLogName(entry.featureShortName, entry.settingPath, entry.settingKey));
		entry.transitionSeconds.reset();
		entry.retainSerializedTransition = true;
	}

	entry.displayName = GetSceneSettingDisplayName(entry.featureShortName, entry.settingPath, entry.settingKey);
	return true;
}

void SceneSettingsManager::LoadAllUserSettings()
{
	auto path = GetUserSettingsFilePath();
	logger::info("[SceneSettings] Loading user settings from: {}", path.string());
	for (auto type : { SceneType::InteriorOnly, SceneType::TimeOfDay })
		std::erase_if(entries[type], [](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	unresolvedUserEntries[SceneType::InteriorOnly].clear();
	unresolvedUserEntries[SceneType::TimeOfDay].clear();
	BumpEntryPresentationRevision();
	interiorUserSettingsModified = false;
	timeOfDayUserSettingsModified = false;
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		userSettingsDocumentLoaded = true;
		userSettingsDocumentWritable = !ec;
		preservedUserSettingsRoot = json::object();
		if (ec)
			logger::error("[SceneSettings] Could not inspect SceneManager.json: {}", ec.message());
		else
			logger::info("[SceneSettings] SceneManager.json not found at {}", path.string());
		return;
	}

	try {
		std::ifstream file(path);
		if (!file.is_open()) {
			userSettingsDocumentLoaded = true;
			userSettingsDocumentWritable = false;
			logger::error("[SceneSettings] Could not open SceneManager.json for reading");
			return;
		}

		json data = json::parse(file, nullptr, false);
		userSettingsDocumentLoaded = true;
		preservedUserSettingsRoot = data;
		if (!data.is_object()) {
			userSettingsDocumentWritable = false;
			logger::error("[SceneSettings] SceneManager.json must contain a valid JSON object; automatic saves are blocked");
			return;
		}
		userSettingsDocumentWritable = true;
		FeatureSettingsCache featureSettingsCache;

		struct EntryListSection
		{
			const char* key;
			const char* typeName;
			SceneType type;
			/// TimeOfDay entries carry a period; interior ones do not.
			bool requirePeriod;
		};
		for (const auto& [sectionKey, typeName, sceneType, requirePeriod] : {
				 EntryListSection{ "interiorOnly", "InteriorOnly", SceneType::InteriorOnly, false },
				 EntryListSection{ "timeOfDay", "TimeOfDay", SceneType::TimeOfDay, true },
			 }) {
			auto sectionIt = data.find(sectionKey);
			if (sectionIt == data.end())
				continue;
			if (!sectionIt->is_array()) {
				logger::warn("[SceneSettings] Preserving non-array {} section", sectionKey);
				continue;
			}

			auto& vec = GetEntriesMut(sceneType);
			auto& unresolved = unresolvedUserEntries[sceneType];
			int loaded = 0;
			for (const auto& item : *sectionIt) {
				SettingEntry entry;
				if (!LoadEntryFromJson(item, entry, requirePeriod, typeName, std::nullopt, false,
						&featureSettingsCache) ||
					HasDuplicateEntry(sceneType, entry.featureShortName, entry.settingPath,
						entry.settingKey, EntrySource::User, entry.period)) {
					unresolved.push_back(item);
					continue;
				}
				vec.push_back(std::move(entry));
				loaded++;
			}
			if (loaded > 0)
				logger::info("[SceneSettings] Loaded {} {} user settings", loaded, typeName);
		}

		// Weather and location are loaded lazily once game data is available.

		logger::info("[SceneSettings] Loaded SceneManager.json (non-weather)");
	} catch (const std::exception& e) {
		userSettingsDocumentLoaded = true;
		userSettingsDocumentWritable = false;
		logger::error("[SceneSettings] Failed to load SceneManager.json: {}", e.what());
	}
}

void SceneSettingsManager::LoadLocationUserSettings(const json& data)
{
	for (auto& [_, config] : locationSceneConfigs)
		std::erase_if(config.entries, [](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	unresolvedLocationUserSettings = json::object();
	locationUserSettingsModified = false;
	locationTransitionSeconds = kDefaultLocationTransitionSeconds;
	locationTransitionModified = false;
	auto locationIt = data.find("location");
	if (locationIt == data.end())
		return;
	if (!locationIt->is_object()) {
		logger::warn("[SceneSettings] Preserving non-object location section");
		return;
	}
	unresolvedLocationUserSettings = *locationIt;
	if (auto transitionIt = locationIt->find("transitionSeconds"); transitionIt != locationIt->end()) {
		if (!transitionIt->is_number())
			logger::warn("[SceneSettings] Location transitionSeconds must be numeric; preserving it");
		else if (const auto seconds = transitionIt->get<float>();
			std::isfinite(seconds) && seconds >= 0.0f && seconds <= kMaxLocationTransitionSeconds)
			locationTransitionSeconds = seconds;
		else
			logger::warn("[SceneSettings] Location transitionSeconds is outside 0..{}; preserving it",
				kMaxLocationTransitionSeconds);
	}
	FeatureSettingsCache featureSettingsCache;

	const auto loadSection = [&](const char* sectionName, LocationTargetType type) {
		auto sectionIt = locationIt->find(sectionName);
		if (sectionIt == locationIt->end() || !sectionIt->is_object())
			return;
		json preservedSection = json::object();

		for (const auto& [formKey, rawConfig] : sectionIt->items()) {
			if (formKey.empty()) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			const auto canonicalFormKey = CanonicalizeResolvedLocationFormKey(formKey);
			if (!rawConfig.is_object()) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			const auto configContext = std::format("Location config '{}'", formKey);
			std::string name;
			std::string persistedType;
			std::string cocCode;
			std::string editorId;
			const auto* expectedType = GetLocationTargetTypeName(type);
			persistedType = expectedType;
			if (!ReadOptionalStringField(rawConfig, "name", name, configContext) ||
				!ReadOptionalStringField(rawConfig, "type", persistedType, configContext) ||
				!ReadOptionalStringField(rawConfig, "editorId", editorId, configContext) ||
				!ReadOptionalStringField(rawConfig, "coc", cocCode, configContext)) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			if (persistedType != expectedType) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			// Presence in the user document is what makes a target theirs, entries or not.
			auto& config = EnsureAuthoredLocationConfig(type, canonicalFormKey, name, cocCode, editorId);
			auto entriesIt = rawConfig.find("entries");
			if (entriesIt == rawConfig.end()) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			if (!entriesIt->is_array()) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			auto preservedConfig = rawConfig;
			preservedConfig["entries"] = json::array();
			bool hasValidEntry = false;

			for (const auto& item : *entriesIt) {
				SettingEntry entry;
				if (!LoadEntryFromJson(item, entry, false, "Location", SceneType::Location, false,
						&featureSettingsCache)) {
					preservedConfig["entries"].push_back(item);
					continue;
				}
				hasValidEntry = true;
				if (HasLocationEntry(type, canonicalFormKey, entry.featureShortName, entry.settingPath,
						entry.settingKey, EntrySource::User)) {
					preservedConfig["entries"].push_back(item);
					continue;
				}
				config.entries.push_back(std::move(entry));
			}
			// The canonical key carries the metadata and every loaded entry, so the author's spelling
			// only has to survive when it still holds entries this build rejected. Emitting it
			// unconditionally would leave an empty duplicate target beside the canonical one.
			if (hasValidEntry && formKey != canonicalFormKey) {
				if (preservedConfig["entries"].empty())
					continue;
				preservedConfig.erase("type");
				preservedConfig.erase("name");
				preservedConfig.erase("editorId");
				preservedConfig.erase("coc");
			}
			preservedSection[formKey] = std::move(preservedConfig);
		}
		unresolvedLocationUserSettings[sectionName] = std::move(preservedSection);
	};

	for (const auto type : { LocationTargetType::Region, LocationTargetType::Location,
			 LocationTargetType::Cell })
		loadSection(GetLocationSectionName(type), type);
}

void SceneSettingsManager::LoadWeatherUserSettings()
{
	for (auto& [_, config] : weatherSceneConfigs)
		std::erase_if(config.entries, [](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	weatherShowTimeOfDay.clear();
	unresolvedWeatherUserSettings = json::object();
	weatherUserSettingsModified = false;
	if (!userSettingsDocumentLoaded || !userSettingsDocumentWritable || !preservedUserSettingsRoot.is_object())
		return;

	try {
		auto weatherIt = preservedUserSettingsRoot.find("weather");
		if (weatherIt == preservedUserSettingsRoot.end())
			return;
		if (!weatherIt->is_object()) {
			logger::warn("[SceneSettings] Preserving non-object weather section");
			return;
		}

		FeatureSettingsCache featureSettingsCache;
		logger::info("[SceneSettings] Weather section found with {} entries", weatherIt->size());
		for (const auto& [spidKey, weatherData] : weatherIt->items()) {
			RE::FormID weatherId = Util::SpidToFormId(spidKey);
			if (weatherId == 0) {
				unresolvedWeatherUserSettings[spidKey] = weatherData;
				logger::warn("[SceneSettings] Weather SPID '{}' could not be resolved - skipping", spidKey);
				continue;
			}
			if (!weatherData.is_object()) {
				unresolvedWeatherUserSettings[spidKey] = weatherData;
				logger::warn("[SceneSettings] Weather config '{}' is not an object - preserving", spidKey);
				continue;
			}
			auto preservedWeather = weatherData;

			if (auto showIt = weatherData.find("showTimeOfDay"); showIt != weatherData.end()) {
				if (!showIt->is_boolean()) {
					logger::warn("[SceneSettings] Weather config '{}' showTimeOfDay is not boolean - preserving", spidKey);
				} else {
					weatherShowTimeOfDay[weatherId] = showIt->get<bool>();
					preservedWeather.erase("showTimeOfDay");
				}
			}

			auto entriesIt = weatherData.find("entries");
			if (entriesIt == weatherData.end()) {
				unresolvedWeatherUserSettings[spidKey] = std::move(preservedWeather);
				continue;
			}
			if (!entriesIt->is_array()) {
				unresolvedWeatherUserSettings[spidKey] = preservedWeather;
				logger::warn("[SceneSettings] Weather config '{}' entries is not an array - preserving", spidKey);
				continue;
			}
			preservedWeather["entries"] = json::array();

			auto& config = GetWeatherConfigMut(weatherId);
			int loaded = 0;
			for (const auto& item : *entriesIt) {
				SettingEntry entry;
				if (!LoadEntryFromJson(item, entry, true, "Weather", std::nullopt, false,
						&featureSettingsCache)) {
					preservedWeather["entries"].push_back(item);
					continue;
				}
				if (HasWeatherEntryForPeriod(weatherId, entry.featureShortName, entry.settingPath,
						entry.settingKey, entry.period, EntrySource::User)) {
					preservedWeather["entries"].push_back(item);
					continue;
				}
				config.entries.push_back(std::move(entry));
				loaded++;
			}
			if (loaded > 0)
				logger::info("[SceneSettings] Loaded {} weather entries for {}", loaded, spidKey);
			unresolvedWeatherUserSettings[spidKey] = std::move(preservedWeather);
		}

		logger::info("[SceneSettings] Loaded weather user settings");
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load weather user settings: {}", e.what());
	}
}

void SceneSettingsManager::DiscoverOverwrites(SceneType type)
{
	if (!IsEntryListSceneType(type))
		return;
	const auto previousEntryCount = GetEntries(type).size();
	const auto basePath = GetOverwritesPath(type);
	if (type == SceneType::TimeOfDay) {
		for (auto period : kPeriods)
			DiscoverOverwritesInDir(type, GetOverwriteDir(basePath, period), period);
	} else {
		DiscoverOverwritesInDir(type, basePath);
	}

	if (GetEntries(type).size() != previousEntryCount)
		BumpEntryPresentationRevision();
}

static bool ParseOverwriteFileEntries(const std::filesystem::path& filePath,
	SceneSettingsManager::SceneType allowedType, bool requireNumeric,
	std::vector<SceneSettingsManager::SettingEntry>& outEntries, FeatureSettingsCache* featureSettingsCache)
{
	using SSM = SceneSettingsManager;

	json data;
	if (!ReadBoundedSceneJson(filePath, data))
		return false;

	std::string featureShortName = data.value(kFeatureKey, "");
	if (featureShortName.empty()) {
		auto stem = filePath.stem().string();
		auto lastUnderscore = stem.rfind('_');
		if (lastUnderscore != std::string::npos)
			featureShortName = stem.substr(lastUnderscore + 1);
	}

	auto* featurePtr = Feature::FindFeatureByShortName(featureShortName);
	if (!featurePtr || !SSM::IsFeatureAllowedForType(allowedType, featureShortName))
		return false;

	bool foundAny = false;
	CollectOverwriteEntries(data, {}, [&](const auto& settingPath, const auto& key, const auto& value) {
		if (!ValidateSceneSettingEntry("Overwrite", featureShortName, settingPath, key, value,
				requireNumeric, featureSettingsCache))
			return;

		SSM::SettingEntry entry;
		entry.featureShortName = featureShortName;
		entry.settingPath = settingPath;
		entry.settingKey = key;
		entry.displayName = GetSceneSettingDisplayName(featureShortName, settingPath, key);
		entry.value = value;
		entry.originalValue = entry.value;
		entry.source = SSM::EntrySource::Overwrite;
		entry.sourceFilename = filePath.filename().string();
		entry.sourcePath = filePath;
		outEntries.push_back(std::move(entry));
		foundAny = true;
	});
	return foundAny;
}

void SceneSettingsManager::DiscoverOverwritesInDir(SceneType type, const std::filesystem::path& dir, TimeOfDayPeriod period)
{
	auto typeName = GetSceneTypeName(type);

	std::error_code ec;
	if (!std::filesystem::exists(dir, ec))
		return;

	logger::info("[SceneSettings] Discovering {} overwrites in: {}", typeName, dir.string());

	bool requireNumeric = (type == SceneType::TimeOfDay);
	auto& vec = GetEntriesMut(type);
	int filesFound = 0, overwritesLoaded = 0;
	FeatureSettingsCache featureSettingsCache;

	for (const auto& filePath : GetSortedJsonFiles(dir, std::format("{} overwrite files", typeName))) {
		filesFound++;
		try {
			std::vector<SettingEntry> parsedEntries;
			if (!ParseOverwriteFileEntries(filePath, type, requireNumeric, parsedEntries, &featureSettingsCache))
				continue;
			for (auto& entry : parsedEntries) {
				entry.period = period;
				if (AddOverwriteEntryIfUnique(vec, std::move(entry), typeName))
					overwritesLoaded++;
			}
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Failed to load {} overwrite '{}': {}", typeName, filePath.filename().string(), e.what());
		}
	}

	if (filesFound > 0)
		logger::info("[SceneSettings] {} overwrite scan: {} files, {} loaded", typeName, filesFound, overwritesLoaded);
}

void SceneSettingsManager::LoadAll()
{
	if (!dataLoaded) {
		dataLoaded = true;
		DiscoverOverwrites(SceneType::InteriorOnly);
		DiscoverOverwrites(SceneType::TimeOfDay);
		LoadAllUserSettings();
		BumpEntryPresentationRevision();
		activeEntryCacheDirty = true;
		resolverDirty = true;
	}
	TryEnsureLocationDataLoaded();
}

void SceneSettingsManager::ReloadOverwrites()
{
	// Nothing has been discovered yet, so the first load will read the current files anyway.
	if (!dataLoaded)
		return;

	const auto dropOverwrites = [](std::vector<SettingEntry>& sourceEntries) {
		std::erase_if(sourceEntries,
			[](const SettingEntry& entry) { return entry.source == EntrySource::Overwrite; });
	};
	for (auto& [type, sourceEntries] : entries)
		dropOverwrites(sourceEntries);
	for (auto& [weatherId, config] : weatherSceneConfigs)
		dropOverwrites(config.entries);
	for (auto& [configKey, config] : locationSceneConfigs)
		dropOverwrites(config.entries);

	DiscoverOverwrites(SceneType::InteriorOnly);
	DiscoverOverwrites(SceneType::TimeOfDay);
	// Discovery for a layer that never loaded would run without its user settings, so it waits.
	if (weatherDataLoaded)
		DiscoverWeatherOverwrites();
	if (locationDataLoaded)
		DiscoverLocationOverwrites();

	BumpEntryPresentationRevision();
	ReapplyIfActive();
}

void SceneSettingsManager::OnDataLoaded()
{
	gameDataReady = true;
	if (dataLoaded)
		TryEnsureLocationDataLoaded();
}

bool SceneSettingsManager::TryEnsureLocationDataLoaded()
{
	if (locationDataLoaded)
		return true;
	if (!gameDataReady || !RE::TESDataHandler::GetSingleton())
		return false;
	if (!userSettingsDocumentLoaded)
		LoadAllUserSettings();

	try {
		DiscoverLocationOverwrites();
		if (userSettingsDocumentLoaded && userSettingsDocumentWritable && preservedUserSettingsRoot.is_object())
			LoadLocationUserSettings(preservedUserSettingsRoot);
		locationDataLoaded = true;
		locationTargetsCached = false;
		BumpEntryPresentationRevision();
		activeEntryCacheDirty = true;
		resolverDirty = true;
		return true;
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load location settings: {}", e.what());
		return false;
	}
}

bool SceneSettingsManager::TryEnsureWeatherDataLoaded()
{
	if (weatherDataLoaded)
		return true;
	if (!globals::game::sky || !RE::TESDataHandler::GetSingleton())
		return false;
	if (!userSettingsDocumentLoaded)
		LoadAllUserSettings();

	weatherDataLoaded = true;
	LoadWeatherData();
	BumpEntryPresentationRevision();
	activeEntryCacheDirty = true;
	resolverDirty = true;
	return true;
}

void SceneSettingsManager::LoadWeatherData()
{
	DiscoverWeatherOverwrites();
	LoadWeatherUserSettings();
}

RE::FormID SceneSettingsManager::GetEffectivePreviousWeatherId(const RE::Sky* sky, float weatherLerp) const
{
	if (!sky)
		return 0;
	if (weatherLerp >= 1.0f) {
		if (sky->currentWeather)
			cachedPreviousWeatherId = sky->currentWeather->GetFormID();
		return 0;
	}
	if (sky->lastWeather)
		cachedPreviousWeatherId = sky->lastWeather->GetFormID();
	return cachedPreviousWeatherId;
}

// --- Per-Weather Scene Settings ---

SceneSettingsManager::WeatherSceneConfig& SceneSettingsManager::GetWeatherConfigMut(RE::FormID weatherId)
{
	return weatherSceneConfigs[weatherId];
}

bool SceneSettingsManager::HasWeatherConfig(RE::FormID weatherId)
{
	if (!TryEnsureWeatherDataLoaded())
		return false;

	auto it = weatherSceneConfigs.find(weatherId);
	return it != weatherSceneConfigs.end() && std::any_of(it->second.entries.begin(), it->second.entries.end(),
		[](const auto& entry) { return IsNumericValue(entry.value); });
}

void SceneSettingsManager::PrepareWeatherUserSettingsMutation(RE::FormID weatherId, bool replaceMalformedEntries)
{
	weatherUserSettingsModified = true;
	if (!unresolvedWeatherUserSettings.is_object())
		unresolvedWeatherUserSettings = json::object();
	const auto canonicalSpid = Util::FormIdToSpid(weatherId);
	const auto normalizedSpid = NormalizeLocationFormKey(canonicalSpid);
	if (replaceMalformedEntries) {
		for (auto& [rawSpid, rawWeather] : unresolvedWeatherUserSettings.items()) {
			if (!rawWeather.is_object() || NormalizeLocationFormKey(rawSpid) != normalizedSpid)
				continue;
			auto entriesIt = rawWeather.find("entries");
			if (entriesIt != rawWeather.end() && !entriesIt->is_array())
				*entriesIt = json::array();
		}
	}

	auto& rawWeather = unresolvedWeatherUserSettings[canonicalSpid];
	if (!rawWeather.is_object())
		rawWeather = json::object();
	if (replaceMalformedEntries) {
		auto entriesIt = rawWeather.find("entries");
		if (entriesIt != rawWeather.end() && !entriesIt->is_array())
			*entriesIt = json::array();
	}
}

std::optional<float> SceneSettingsManager::ResolveWeatherLowerValue(RE::FormID weatherId,
	const SettingAddress& address, TimeOfDayPeriod period, EntrySource selectedSource)
{
	const auto periodIndex = static_cast<int>(period);
	if (periodIndex < 0 || periodIndex >= kPeriodCount)
		return std::nullopt;
	auto baseline = GetBaselineValue(address);
	if (!IsNumericValue(baseline))
		return std::nullopt;
	const auto baselineValue = baseline.get<float>();
	if (!std::isfinite(baselineValue))
		return std::nullopt;

	float lowerValue = GetTimeOfDayPeriodFallbackFloat(baselineValue,
		address.featureShortName, address.settingPath, address.settingKey, periodIndex);
	// Only a user entry has the weather overwrite layer beneath it. A capture deliberately passes
	// the overwrite layer here so it resolves to the value that applies without any mod.
	if (selectedSource != EntrySource::User)
		return lowerValue;

	auto configIt = weatherSceneConfigs.find(weatherId);
	if (configIt == weatherSceneConfigs.end())
		return lowerValue;
	for (const auto& entry : configIt->second.entries) {
		if (entry.source != EntrySource::Overwrite || entry.period != period || !IsEntryActive(entry) ||
			!IsNumericValue(entry.value) ||
			!IsSameSetting(entry, address.featureShortName, address.settingPath, address.settingKey))
			continue;
		const auto value = entry.value.get<float>();
		if (std::isfinite(value))
			lowerValue = value;
	}
	return lowerValue;
}

void SceneSettingsManager::RemoveWeatherSetting(RE::FormID weatherId, size_t index)
{
	if (!TryEnsureWeatherDataLoaded())
		return;

	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	const auto previousSize = it->second.entries.size();
	const auto entry = it->second.entries[index];
	if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty()) {
		const auto backingPath = GetWeatherOverwritePath(weatherId, entry);
		if (!RemoveSettingFromOverwriteFile(backingPath, entry.settingPath, entry.settingKey))
			return;
		std::erase_if(it->second.entries, [&](const auto& candidate) {
			return candidate.source == EntrySource::Overwrite &&
			       GetWeatherOverwritePath(weatherId, candidate) == backingPath &&
			       IsSameSetting(candidate, entry.featureShortName, entry.settingPath, entry.settingKey);
		});
	} else {
		it->second.entries.erase(it->second.entries.begin() + static_cast<ptrdiff_t>(index));
		PrepareWeatherUserSettingsMutation(weatherId, false);
		SaveAllUserSettings();
	}
	if (it->second.entries.size() != previousSize)
		BumpEntryPresentationRevision();
	ReapplyIfActive();
}

bool SceneSettingsManager::HasWeatherEntryForPeriod(RE::FormID weatherId, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, TimeOfDayPeriod period, std::optional<EntrySource> source)
{
	if (!TryEnsureWeatherDataLoaded())
		return false;

	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end())
		return false;
	for (const auto& e : it->second.entries)
		if (IsSameSetting(e, featureShortName, settingPath, settingKey) && e.period == period &&
			(!source || e.source == *source))
			return true;
	return false;
}

// --- Per-Weather Persistence ---

bool SceneSettingsManager::IsWeatherShowTimeOfDay(RE::FormID weatherId)
{
	if (!TryEnsureWeatherDataLoaded())
		return false;

	auto it = weatherShowTimeOfDay.find(weatherId);
	return it != weatherShowTimeOfDay.end() && it->second;
}

// --- Per-Location Scene Settings ---

const SceneSettingsManager::LocationSceneConfig SceneSettingsManager::kEmptyLocationConfig{};

std::string SceneSettingsManager::GetLocationConfigKey(LocationTargetType type, std::string_view formKey)
{
	return std::format("{}:{}", GetLocationTargetTypeName(type), NormalizeLocationFormKey(formKey));
}

const char* SceneSettingsManager::GetLocationSectionName(LocationTargetType type)
{
	switch (type) {
	case LocationTargetType::Region:
		return "regions";
	case LocationTargetType::Location:
		return "locations";
	case LocationTargetType::Cell:
		return "cells";
	default:
		return "invalid";
	}
}

const char* SceneSettingsManager::GetLocationTargetTypeName(LocationTargetType type)
{
	switch (type) {
	case LocationTargetType::Region:
		return "Region";
	case LocationTargetType::Location:
		return "Location";
	case LocationTargetType::Cell:
		return "Cell";
	default:
		return "Invalid";
	}
}

namespace
{
	bool IsValidLocationTargetType(SceneSettingsManager::LocationTargetType type)
	{
		return type == SceneSettingsManager::LocationTargetType::Region ||
		       type == SceneSettingsManager::LocationTargetType::Location ||
		       type == SceneSettingsManager::LocationTargetType::Cell;
	}

	template <class Form>
	std::string GetLocationTargetDisplayName(const Form* form)
	{
		if (const char* fullName = form->GetFullName(); fullName && fullName[0] != '\0')
			return std::string(fullName);
		return Util::GetFormDisplayName(form->GetFormID());
	}

	/// Regions carry no full name, so their editor ID is the only readable label they have.
	std::string GetRegionTargetName(const RE::TESRegion* region)
	{
		auto name = Util::PrettifyIdentifier(Util::GetFormEditorID(region));
		return name.empty() ? Util::GetFormDisplayName(region->GetFormID()) : name;
	}

	/// Build the broadest-to-narrowest target chain for a location and the cell that resolved it.
	std::vector<SceneSettingsManager::LocationTarget> BuildLocationTargetChain(
		RE::BGSLocation* location, RE::TESObjectCELL* cell)
	{
		using LocationTargetType = SceneSettingsManager::LocationTargetType;
		const auto cocCode = cell ? Util::GetFormEditorID(cell) : std::string{};

		std::vector<RE::BGSLocation*> locationChain;
		std::set<RE::FormID> visited;
		for (auto* current = location; current && visited.insert(current->GetFormID()).second; current = current->parentLoc)
			locationChain.push_back(current);
		std::reverse(locationChain.begin(), locationChain.end());

		std::vector<SceneSettingsManager::LocationTarget> targets;
		// The sky knows which of an exterior cell's overlapping regions actually won, but only for the
		// cell the player is standing in; anywhere else the cell's own first region is the best guess.
		if (cell && cell->IsExteriorCell()) {
			RE::TESRegion* region = nullptr;
			if (auto* player = RE::PlayerCharacter::GetSingleton();
				player && player->GetParentCell() == cell && globals::game::sky)
				region = globals::game::sky->region;
			if (!region) {
				if (auto* regions = cell->GetRegionList(false)) {
					auto regionIt = std::find_if(regions->begin(), regions->end(),
						[](auto* candidate) { return candidate != nullptr; });
					if (regionIt != regions->end())
						region = *regionIt;
				}
			}
			if (region) {
				targets.push_back({
					.type = LocationTargetType::Region,
					.formKey = Util::GetFormFileKey(region),
					.name = GetRegionTargetName(region),
					.editorId = Util::GetFormEditorID(region),
					.cocCode = cocCode,
					.formId = region->GetFormID(),
				});
			}
		}

		for (auto* current : locationChain) {
			targets.push_back({
				.type = LocationTargetType::Location,
				.formKey = Util::GetFormFileKey(current),
				.name = GetLocationTargetDisplayName(current),
				.editorId = Util::GetFormEditorID(current),
				.cocCode = cocCode,
				.formId = current->GetFormID(),
			});
		}

		if (cell) {
			targets.push_back({
				.type = LocationTargetType::Cell,
				.formKey = Util::GetFormFileKey(cell),
				.name = GetLocationTargetDisplayName(cell),
				.editorId = cocCode,  // The coc code is the cell's own editor ID.
				.cocCode = cocCode,
				.formId = cell->GetFormID(),
			});
		}
		return targets;
	}

	RE::TESForm* ResolveLocationTargetForm(std::string_view formKey)
	{
		const auto parsed = Util::ParseSpid(std::string(formKey));
		if (parsed.localFormId == 0)
			return nullptr;
		const auto formId = parsed.pluginName.empty() ? parsed.localFormId : Util::SpidToFormId(std::string(formKey));
		return formId != 0 ? RE::TESForm::LookupByID(formId) : nullptr;
	}

	/// Resolve the chain an arbitrary target belongs to, preferring the player's own chain when it matches.
	std::vector<SceneSettingsManager::LocationTarget> ResolveLocationTargetChain(
		SceneSettingsManager::LocationTargetType type, std::string_view formKey)
	{
		if (auto* manager = SceneSettingsManager::GetSingleton()) {
			const auto& currentTargets = manager->GetCurrentLocationTargets();
			const auto normalizedKey = NormalizeLocationFormKey(formKey);
			if (std::any_of(currentTargets.begin(), currentTargets.end(), [&](const auto& target) {
					return target.type == type && NormalizeLocationFormKey(target.formKey) == normalizedKey;
				}))
				return currentTargets;
		}
		auto* form = ResolveLocationTargetForm(formKey);
		if (!form)
			return {};
		// A region is reached through the cells it covers, so away from them it is a chain of itself.
		if (type == SceneSettingsManager::LocationTargetType::Region) {
			auto* region = form->As<RE::TESRegion>();
			if (!region)
				return {};
			return { {
				.type = SceneSettingsManager::LocationTargetType::Region,
				.formKey = Util::GetFormFileKey(region),
				.name = GetRegionTargetName(region),
				.editorId = Util::GetFormEditorID(region),
				.formId = region->GetFormID(),
			} };
		}
		if (type == SceneSettingsManager::LocationTargetType::Location)
			return BuildLocationTargetChain(form->As<RE::BGSLocation>(), nullptr);
		auto* cell = form->As<RE::TESObjectCELL>();
		return cell ? BuildLocationTargetChain(cell->GetLocation(), cell) :
		              std::vector<SceneSettingsManager::LocationTarget>{};
	}
}

const std::vector<SceneSettingsManager::LocationTarget>& SceneSettingsManager::GetCurrentLocationTargets() const
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	if (!player || !cell) {
		cachedTargetLocationId = 0;
		cachedTargetCellId = 0;
		cachedTargetRegionId = 0;
		locationTargetsCached = false;
		cachedLocationTargets.clear();
		return cachedLocationTargets;
	}

	auto* location = player->GetCurrentLocation();
	if (!location)
		location = cell->GetLocation();
	const auto locationId = location ? location->GetFormID() : 0;
	const auto cellId = cell->GetFormID();
	// Regions overlap within a cell, so the winning one can change without the cell changing.
	const auto regionId = cell->IsExteriorCell() && globals::game::sky && globals::game::sky->region ?
	                          globals::game::sky->region->GetFormID() :
	                          0;
	if (locationTargetsCached && cachedTargetLocationId == locationId &&
		cachedTargetCellId == cellId && cachedTargetRegionId == regionId)
		return cachedLocationTargets;

	cachedLocationTargets = BuildLocationTargetChain(location, cell);
	cachedTargetRegionId = regionId;
	cachedTargetLocationId = locationId;
	cachedTargetCellId = cellId;
	locationTargetsCached = true;
	return cachedLocationTargets;
}

std::vector<SceneSettingsManager::LocationTarget> SceneSettingsManager::GetAuthoredLocationTargets() const
{
	std::vector<LocationTarget> targets;
	for (const auto& [configKey, config] : locationSceneConfigs) {
		if (!config.userAuthored)
			continue;
		// formId is left unresolved: an authored target may name content that is not loaded, and
		// SpidToFormId warns on every miss. Callers that need the live form resolve it themselves.
		targets.push_back({
			.type = config.type,
			.formKey = config.formKey,
			.name = config.name.empty() ? configKey : config.name,
			.editorId = config.editorId,
			.cocCode = config.cocCode,
		});
	}
	std::ranges::sort(targets, [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
	return targets;
}

bool SceneSettingsManager::AddLocationTarget(const LocationTarget& target)
{
	if (!TryEnsureLocationDataLoaded() || target.formKey.empty() ||
		IsLocationTargetAuthored(target.type, target.formKey))
		return false;

	EnsureAuthoredLocationConfig(target.type, target.formKey, target.name, target.cocCode, target.editorId);
	PrepareLocationUserSettingsMutation(target.type, target.formKey, false);
	SaveAllUserSettings();
	return true;
}

bool SceneSettingsManager::IsLocationTargetAuthored(LocationTargetType type, std::string_view formKey) const
{
	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	return it != locationSceneConfigs.end() && it->second.userAuthored;
}

void SceneSettingsManager::RemoveLocationTarget(LocationTargetType type, const std::string& formKey)
{
	if (!TryEnsureLocationDataLoaded())
		return;

	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	if (it == locationSceneConfigs.end())
		return;

	const auto removedEntries = std::erase_if(it->second.entries,
		[](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	// Shipped overwrites keep the config alive; without them nothing is left to remember.
	if (it->second.entries.empty())
		locationSceneConfigs.erase(it);
	else
		it->second.userAuthored = false;

	// The loader treats presence in the user document as ownership, so the target has to leave it
	// entirely. Deleting only its entries would resurrect the target on the next launch.
	PrepareLocationUserSettingsMutation(type, formKey, false);
	const auto* sectionName = GetLocationSectionName(type);
	if (auto sectionIt = unresolvedLocationUserSettings.find(sectionName);
		sectionIt != unresolvedLocationUserSettings.end())
		for (const auto& rawFormKey : MatchingRawLocationKeys(*sectionIt, type, formKey))
			sectionIt->erase(rawFormKey);

	if (removedEntries != 0)
		BumpEntryPresentationRevision();
	SaveAllUserSettings();
	ReapplyIfActive();
}

SceneSettingsManager::LocationSceneConfig& SceneSettingsManager::GetLocationConfigMut(
	LocationTargetType type, const std::string& formKey, const std::string& name)
{
	const auto canonicalFormKey = CanonicalizeResolvedLocationFormKey(formKey);
	auto& config = locationSceneConfigs[GetLocationConfigKey(type, canonicalFormKey)];
	config.type = type;
	config.formKey = canonicalFormKey;
	if (!name.empty())
		config.name = name;
	return config;
}

SceneSettingsManager::LocationSceneConfig& SceneSettingsManager::EnsureAuthoredLocationConfig(
	LocationTargetType type, const std::string& formKey, const std::string& name, const std::string& cocCode,
	const std::string& editorId)
{
	auto& config = GetLocationConfigMut(type, formKey, name);
	if (!cocCode.empty())
		config.cocCode = cocCode;
	if (!editorId.empty())
		config.editorId = editorId;
	config.userAuthored = true;
	return config;
}

const SceneSettingsManager::LocationSceneConfig& SceneSettingsManager::GetLocationConfig(
	LocationTargetType type, std::string_view formKey) const
{
	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	return it != locationSceneConfigs.end() ? it->second : kEmptyLocationConfig;
}

std::optional<json> SceneSettingsManager::ResolveLocationLowerValue(LocationTargetType type,
	std::string_view formKey, const SettingAddress& address, EntrySource selectedSource)
{
	auto baseline = GetBaselineValue(address);
	if (!IsSceneSettingPrimitive(baseline))
		return std::nullopt;
	auto lowerLayers = BuildLocationLowerLayers(type, formKey, selectedSource);
	if (!lowerLayers)
		return std::nullopt;
	if (auto valueIt = lowerLayers->find(address); valueIt != lowerLayers->end() &&
		IsSceneSettingPrimitive(valueIt->second))
		return valueIt->second;
	return baseline;
}

std::optional<SceneSettingsManager::ResolvedSettingMap> SceneSettingsManager::BuildLocationLowerLayers(
	LocationTargetType type, std::string_view formKey, std::optional<EntrySource> selectedSource)
{
	ResolvedSettingMap lowerLayers;
	const bool interior = Util::IsInterior();
	RefreshBlendSnapshot(interior);
	if (interior) {
		ResolveInteriorSettings(lowerLayers);
	} else {
		const auto& timeOfDayValues = BuildTimeOfDayValueGroups();
		ResolveTimeOfDaySettings(lowerLayers, timeOfDayValues);
		ResolveWeatherSettings(lowerLayers, timeOfDayValues);
	}

	bool targetFound = false;
	const auto selectedTargetKey = GetLocationConfigKey(type, formKey);
	for (const auto& target : ResolveLocationTargetChain(type, formKey)) {
		const auto targetKey = GetLocationConfigKey(target.type, target.formKey);
		auto configIt = locationSceneConfigs.find(targetKey);
		if (targetKey == selectedTargetKey) {
			targetFound = true;
			// A user entry sits above the overwrite it shadows, so the overwrite is its lower layer.
			// A capture deliberately passes the overwrite layer here to resolve beneath it instead.
			if (selectedSource == EntrySource::User && configIt != locationSceneConfigs.end())
				OverlayEntries(
					lowerLayers, configIt->second.entries, SceneType::Location, EntrySource::Overwrite);
			break;
		}
		if (configIt != locationSceneConfigs.end())
			OverlayAllEntries(lowerLayers, configIt->second.entries, SceneType::Location);
	}
	if (!targetFound)
		return std::nullopt;
	return lowerLayers;
}

void SceneSettingsManager::PrepareLocationUserSettingsMutation(LocationTargetType type,
	std::string_view formKey, bool replaceMalformedEntries)
{
	locationUserSettingsModified = true;
	if (!unresolvedLocationUserSettings.is_object())
		unresolvedLocationUserSettings = json::object();
	const auto* sectionName = GetLocationSectionName(type);
	auto& section = unresolvedLocationUserSettings[sectionName];
	if (!section.is_object())
		section = json::object();
	const auto canonicalFormKey = CanonicalizeResolvedLocationFormKey(formKey);
	const auto targetKey = GetLocationConfigKey(type, canonicalFormKey);
	if (replaceMalformedEntries) {
		for (auto& [rawFormKey, rawConfig] : section.items()) {
			if (!rawConfig.is_object() ||
				GetLocationConfigKey(type, CanonicalizeResolvedLocationFormKey(rawFormKey)) != targetKey)
				continue;
			auto entriesIt = rawConfig.find("entries");
			if (entriesIt != rawConfig.end() && !entriesIt->is_array())
				*entriesIt = json::array();
			if (rawFormKey != canonicalFormKey) {
				rawConfig.erase("type");
				rawConfig.erase("name");
				rawConfig.erase("editorId");
				rawConfig.erase("coc");
			}
		}
	}

	auto& rawConfig = section[canonicalFormKey];
	if (!rawConfig.is_object())
		rawConfig = json::object();
	if (replaceMalformedEntries) {
		auto entriesIt = rawConfig.find("entries");
		if (entriesIt != rawConfig.end() && !entriesIt->is_array())
			*entriesIt = json::array();
	}
}

void SceneSettingsManager::RemoveLocationSetting(LocationTargetType type, const std::string& formKey, size_t index)
{
	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	if (it == locationSceneConfigs.end() || index >= it->second.entries.size())
		return;

	const auto entry = it->second.entries[index];
	const bool userEntry = entry.source == EntrySource::User;
	if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty() &&
		!RemoveSettingFromOverwriteFile(GetLocationOverwritePath(formKey, entry), entry.settingPath, entry.settingKey))
		return;
	it->second.entries.erase(it->second.entries.begin() + static_cast<ptrdiff_t>(index));
	BumpEntryPresentationRevision();
	if (userEntry) {
		PrepareLocationUserSettingsMutation(type, formKey, false);
		SaveAllUserSettings();
	}
	ReapplyIfActive();
}

std::vector<std::string> SceneSettingsManager::MatchingRawLocationKeys(const json& section,
	LocationTargetType type, std::string_view formKey)
{
	std::vector<std::string> keys;
	if (!section.is_object())
		return keys;
	const auto targetKey = GetLocationConfigKey(type, formKey);
	for (const auto& [rawFormKey, rawConfig] : section.items())
		if (rawConfig.is_object() &&
			GetLocationConfigKey(type, CanonicalizeResolvedLocationFormKey(rawFormKey)) == targetKey)
			keys.push_back(rawFormKey);
	return keys;
}

void SceneSettingsManager::SetLocationTransitionSeconds(float seconds, bool deferSave)
{
	if (!std::isfinite(seconds) || !TryEnsureLocationDataLoaded())
		return;
	seconds = std::clamp(seconds, 0.0f, kMaxLocationTransitionSeconds);
	if (std::abs(locationTransitionSeconds - seconds) < kBlendEpsilon)
		return;
	locationTransitionSeconds = seconds;
	locationTransitionModified = true;
	locationUserSettingsModified = true;
	if (deferSave)
		MarkDeferredSceneChanges();
	else
		SaveAllUserSettings();
	ReapplyIfActive();
}

std::optional<float> SceneSettingsManager::GetLocationEntryTransitionSeconds(
	LocationTargetType type, std::string_view formKey, size_t index) const
{
	const auto& config = GetLocationConfig(type, formKey);
	return index < config.entries.size() ? config.entries[index].transitionSeconds : std::nullopt;
}

namespace
{
	/// Identity of the logical control a stored setting belongs to. Aggregate members share one key.
	using CopyGroupKey = std::tuple<std::string, std::vector<std::string>, std::string,
		std::int8_t, std::uint8_t, SceneSettingsManager::SettingControlType>;

	CopyGroupKey GetCopyGroupKey(const SceneSettingsManager::SettingIdentity& identity)
	{
		SceneSettingsManager::SettingEntry entry{
			.featureShortName = identity.featureShortName,
			.settingPath = identity.settingPath,
			.settingKey = identity.settingKey,
		};
		SceneSettingsManager::SettingControlInfo info;
		const bool aggregate = SceneSettingsManager::GetSettingControlInfo(entry, info) &&
		                       info.controlType != SceneSettingsManager::SettingControlType::Scalar;
		return { identity.featureShortName,
			aggregate ? info.settingPath : identity.settingPath,
			aggregate ? info.settingKey : identity.settingKey,
			aggregate ? info.componentStart : -1,
			aggregate ? info.componentCount : 0,
			aggregate ? info.controlType : SceneSettingsManager::SettingControlType::Scalar };
	}

	bool IsValidCopyConflictPolicy(SceneSettingsManager::CopyConflictPolicy policy)
	{
		return policy == SceneSettingsManager::CopyConflictPolicy::SkipExisting ||
		       policy == SceneSettingsManager::CopyConflictPolicy::OverwriteExisting ||
		       policy == SceneSettingsManager::CopyConflictPolicy::Cancel;
	}

	const char* GetCopyPeriodName(SceneSettingsManager::TimeOfDayPeriod period)
	{
		switch (period) {
		case SceneSettingsManager::TimeOfDayPeriod::Dawn:
			return T("feature.scene_manager.period.dawn", "Dawn");
		case SceneSettingsManager::TimeOfDayPeriod::Sunrise:
			return T("feature.scene_manager.period.sunrise", "Sunrise");
		case SceneSettingsManager::TimeOfDayPeriod::Day:
			return T("feature.scene_manager.period.day", "Day");
		case SceneSettingsManager::TimeOfDayPeriod::Sunset:
			return T("feature.scene_manager.period.sunset", "Sunset");
		case SceneSettingsManager::TimeOfDayPeriod::Dusk:
			return T("feature.scene_manager.period.dusk", "Dusk");
		case SceneSettingsManager::TimeOfDayPeriod::Night:
			return T("feature.scene_manager.period.night", "Night");
		default:
			return "";
		}
	}

	const char* GetCopyLocationTypeName(SceneSettingsManager::LocationTargetType type)
	{
		switch (type) {
		case SceneSettingsManager::LocationTargetType::Region:
			return T("feature.scene_manager.location.target_region", "Region");
		case SceneSettingsManager::LocationTargetType::Location:
			return T("feature.scene_manager.location.target_location", "Location");
		case SceneSettingsManager::LocationTargetType::Cell:
			return T("feature.scene_manager.location.target_cell", "Cell");
		default:
			return "";
		}
	}

	/// Location entries carry no period, so only time and weather contexts filter on one.
	bool EntryBelongsToContext(const SceneSettingsManager::SettingEntry& entry,
		const SceneSettingsManager::SceneContextId& context)
	{
		return context.type == SceneSettingsManager::SceneContextType::Location ||
		       entry.period == context.period;
	}

	/// AllPeriods drops the period filter, which is what a flat page's fan-out amounts to.
	bool EntryCoveredByContext(const SceneSettingsManager::SettingEntry& entry,
		const SceneSettingsManager::SceneContextId& context, SceneSettingsManager::PeriodScope periodScope)
	{
		return (periodScope == SceneSettingsManager::PeriodScope::AllPeriods &&
			       SceneSettingsManager::IsPeriodicContext(context.type)) ||
		       EntryBelongsToContext(entry, context);
	}
}

void SceneSettingsManager::SetLocationEntryTransitionSeconds(LocationTargetType type,
	const std::string& formKey, std::span<const size_t> indices, std::optional<float> seconds,
	bool deferSave)
{
	auto it = locationSceneConfigs.find(GetLocationConfigKey(type, formKey));
	if (it == locationSceneConfigs.end() || indices.empty())
		return;
	if (seconds) {
		if (!std::isfinite(*seconds))
			return;
		seconds = std::clamp(*seconds, 0.0f, kMaxLocationTransitionSeconds);
	}

	// A component of an aggregate control cannot transition on its own, so the edit takes its siblings.
	auto& locationEntries = it->second.entries;
	std::set<size_t> expandedIndices;
	for (const auto index : indices) {
		if (index >= locationEntries.size())
			return;
		expandedIndices.insert(index);
		const auto& selectedEntry = locationEntries[index];
		const auto selectedGroup = GetCopyGroupKey({ selectedEntry.featureShortName,
			selectedEntry.settingPath, selectedEntry.settingKey });
		if (std::get<5>(selectedGroup) == SettingControlType::Scalar)
			continue;
		for (size_t candidateIndex = 0; candidateIndex < locationEntries.size(); ++candidateIndex) {
			const auto& candidate = locationEntries[candidateIndex];
			if (candidate.source == selectedEntry.source &&
				GetCopyGroupKey({ candidate.featureShortName, candidate.settingPath,
					candidate.settingKey }) == selectedGroup)
				expandedIndices.insert(candidateIndex);
		}
	}

	// Reject the whole edit up front: a transition only means something on a transitionable user float.
	for (const auto index : expandedIndices) {
		const auto& entry = locationEntries[index];
		if (entry.source != EntrySource::User || !IsNumericValue(entry.value) ||
			!FindAllowedCatalogSetting(entry.featureShortName, entry.settingPath, entry.settingKey, true))
			return;
	}

	bool changed = false;
	for (const auto index : expandedIndices) {
		auto& entry = locationEntries[index];
		if (entry.transitionSeconds == seconds)
			continue;
		entry.transitionSeconds = seconds;
		entry.retainSerializedTransition = false;
		changed = true;
	}
	if (!changed)
		return;

	PrepareLocationUserSettingsMutation(type, formKey, false);
	if (deferSave)
		MarkDeferredSceneChanges();
	else
		SaveAllUserSettings();
	ReapplyIfActive();
}

bool SceneSettingsManager::IsPeriodicContext(SceneContextType type)
{
	return type == SceneContextType::TimeOfDay || type == SceneContextType::Weather;
}

bool SceneSettingsManager::IsValidSceneContext(const SceneContextId& context)
{
	const auto periodIndex = static_cast<int>(context.period);
	switch (context.type) {
	case SceneContextType::Interior:
		return context.period == TimeOfDayPeriod::Count && context.weatherId == 0 &&
		       context.locationFormKey.empty() && context.locationType == LocationTargetType::Location;
	case SceneContextType::TimeOfDay:
		return periodIndex >= 0 && periodIndex < kPeriodCount && context.weatherId == 0 &&
		       context.locationFormKey.empty() && context.locationType == LocationTargetType::Location;
	case SceneContextType::Weather:
		return context.weatherId != 0 && periodIndex >= 0 && periodIndex < kPeriodCount &&
		       context.locationFormKey.empty() && context.locationType == LocationTargetType::Location;
	case SceneContextType::Location:
		return context.period == TimeOfDayPeriod::Count && context.weatherId == 0 &&
		       IsValidLocationTargetType(context.locationType) && !context.locationFormKey.empty();
	default:
		return false;
	}
}

bool SceneSettingsManager::IsSameSceneContext(const SceneContextId& lhs, const SceneContextId& rhs)
{
	if (lhs.type != rhs.type)
		return false;
	switch (lhs.type) {
	case SceneContextType::Interior:
		return true;
	case SceneContextType::TimeOfDay:
		return lhs.period == rhs.period;
	case SceneContextType::Weather:
		return lhs.weatherId == rhs.weatherId && lhs.period == rhs.period;
	case SceneContextType::Location:
		return lhs.locationType == rhs.locationType &&
		       NormalizeLocationFormKey(lhs.locationFormKey) == NormalizeLocationFormKey(rhs.locationFormKey);
	default:
		return false;
	}
}

SceneSettingsManager::EffectiveContextEntries SceneSettingsManager::BuildEffectiveContextEntries(
	const std::vector<SettingEntry>& contextEntries, const SceneContextId& context)
{
	EffectiveContextEntries effectiveEntries;
	// User last: a user entry shadows the overwrite it was authored over.
	for (auto entrySource : { EntrySource::Overwrite, EntrySource::User })
		for (const auto& entry : contextEntries)
			if (entry.source == entrySource && !entry.paused && EntryBelongsToContext(entry, context))
				effectiveEntries[{ entry.featureShortName, entry.settingPath, entry.settingKey }] = &entry;
	// A tombstone shadows the overwrite beneath it and then supplies nothing, so the address leaves the
	// set entirely: neither copy nor export may offer a value the user deleted.
	std::erase_if(effectiveEntries, [](const auto& item) { return item.second->deleted; });
	return effectiveEntries;
}

std::string SceneSettingsManager::GetOverwriteModName(const SettingEntry& entry)
{
	const auto stem = std::filesystem::path(entry.sourceFilename).stem().string();
	const auto lastUnderscore = stem.rfind('_');
	return lastUnderscore == std::string::npos ? stem : stem.substr(0, lastUnderscore);
}

SceneSettingsManager::SettingProvenance SceneSettingsManager::GetSettingProvenance(
	const SceneContextId& context, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey) const
{
	SettingProvenance provenance;
	if (!IsValidSceneContext(context))
		return provenance;

	for (const auto& entry : GetContextEntries(context)) {
		if (!IsEntryActive(entry) || !IsSameSetting(entry, featureShortName, settingPath, settingKey) ||
			!EntryBelongsToContext(entry, context))
			continue;
		if (entry.source == EntrySource::Overwrite) {
			// Discovery order is the entry order, and the last file discovered wins.
			provenance.layer = SettingLayer::Overwrite;
			continue;
		}
		// A user entry outranks every overwrite, so it settles the layer outright.
		provenance.layer = entry.deleted ? SettingLayer::Deleted : SettingLayer::User;
	}
	return provenance;
}

const std::vector<SceneSettingsManager::SettingEntry>* SceneSettingsManager::GetCopyContextEntries(
	const SceneContextId& context) const
{
	if (!IsValidSceneContext(context))
		return nullptr;
	switch (context.type) {
	case SceneContextType::Interior:
		return &GetEntries(SceneType::InteriorOnly);
	case SceneContextType::TimeOfDay:
		return &GetEntries(SceneType::TimeOfDay);
	case SceneContextType::Weather:
		if (auto configIt = weatherSceneConfigs.find(context.weatherId); configIt != weatherSceneConfigs.end())
			return &configIt->second.entries;
		return nullptr;
	case SceneContextType::Location:
		if (auto configIt = locationSceneConfigs.find(
				GetLocationConfigKey(context.locationType, context.locationFormKey));
			configIt != locationSceneConfigs.end())
			return &configIt->second.entries;
		return nullptr;
	default:
		return nullptr;
	}
}

namespace
{
	/// The stored SceneType behind a non-weather, non-location context.
	SceneSettingsManager::SceneType ContextSceneType(SceneSettingsManager::SceneContextType type)
	{
		return type == SceneSettingsManager::SceneContextType::Interior ?
		           SceneSettingsManager::SceneType::InteriorOnly :
		           SceneSettingsManager::SceneType::TimeOfDay;
	}

	/// What a context validates an entry against: the layer it lands in, whether that layer blends,
	/// and the name its validation logs carry.
	struct SceneContextRules
	{
		SceneSettingsManager::SceneType sceneType;
		bool requireNumeric;
		const char* label;
	};

	/// One rule set per context type, so an add, a copy and a tombstone all judge an address alike.
	SceneContextRules GetSceneContextRules(SceneSettingsManager::SceneContextType type)
	{
		using SceneContextType = SceneSettingsManager::SceneContextType;
		using SceneType = SceneSettingsManager::SceneType;
		switch (type) {
		case SceneContextType::Interior:
			return { SceneType::InteriorOnly, false, "InteriorOnly" };
		case SceneContextType::Location:
			return { SceneType::Location, false, "Location" };
		case SceneContextType::Weather:
			// Weather stores into the time-of-day layer but names itself in its own logs.
			return { SceneType::TimeOfDay, true, "Weather" };
		default:
			// A periodic layer blends across the period, so it only takes transitionable floats.
			return { SceneType::TimeOfDay, true, "TimeOfDay" };
		}
	}
}

std::vector<std::string> SceneSettingsManager::SplitSettingPath(std::string_view catalogPath)
{
	return SplitCatalogPath(catalogPath);
}

std::span<const SceneSettingsManager::SettingEntry> SceneSettingsManager::GetContextEntries(
	const SceneContextId& context) const
{
	const auto* contextEntries = GetCopyContextEntries(context);
	return contextEntries ? std::span{ *contextEntries } : std::span<const SettingEntry>{};
}

std::vector<SceneSettingsManager::SettingEntry>* SceneSettingsManager::GetContextEntriesMut(
	const SceneContextId& context)
{
	if (!IsValidSceneContext(context))
		return nullptr;
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		return &GetEntriesMut(ContextSceneType(context.type));
	case SceneContextType::Weather:
		if (!TryEnsureWeatherDataLoaded())
			return nullptr;
		if (auto configIt = weatherSceneConfigs.find(context.weatherId); configIt != weatherSceneConfigs.end())
			return &configIt->second.entries;
		return nullptr;
	case SceneContextType::Location:
		if (!TryEnsureLocationDataLoaded())
			return nullptr;
		if (auto configIt = locationSceneConfigs.find(
				GetLocationConfigKey(context.locationType, context.locationFormKey));
			configIt != locationSceneConfigs.end())
			return &configIt->second.entries;
		return nullptr;
	default:
		return nullptr;
	}
}

std::vector<SceneSettingsManager::SettingEntry>* SceneSettingsManager::EnsureContextEntriesMut(
	const SceneContextId& context)
{
	if (!IsValidSceneContext(context))
		return nullptr;
	switch (context.type) {
	case SceneContextType::Weather:
		if (!TryEnsureWeatherDataLoaded())
			return nullptr;
		return &GetWeatherConfigMut(context.weatherId).entries;
	case SceneContextType::Location: {
		if (!TryEnsureLocationDataLoaded())
			return nullptr;
		// Adding the first setting authors the target, so the page survives a reload with no settings.
		const auto& existing = GetLocationConfig(context.locationType, context.locationFormKey);
		const auto name = existing.name;
		const auto cocCode = existing.cocCode;
		auto& config = EnsureAuthoredLocationConfig(context.locationType, context.locationFormKey,
			name, cocCode);
		return &config.entries;
	}
	default:
		return GetContextEntriesMut(context);
	}
}

void SceneSettingsManager::MarkContextUserSettingsModified(const SceneContextId& context,
	bool replaceMalformedEntries)
{
	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		MarkEntryListUserSettingsModified(ContextSceneType(context.type));
		break;
	case SceneContextType::Weather:
		PrepareWeatherUserSettingsMutation(context.weatherId, replaceMalformedEntries);
		break;
	case SceneContextType::Location:
		PrepareLocationUserSettingsMutation(context.locationType, context.locationFormKey,
			replaceMalformedEntries);
		break;
	default:
		break;
	}
}

void SceneSettingsManager::CommitContextUserEntryMutation(const SceneContextId& context, bool deferSave,
	bool replaceMalformedEntries)
{
	BumpEntryPresentationRevision();
	MarkContextUserSettingsModified(context, replaceMalformedEntries);
	// A deferred edit holds the resolve with the save, so a drag does not run ResolveAndApply
	// mid-DrawSettings for every feature holding an active scene override.
	if (deferSave)
		MarkDeferredSceneChanges();
	else
		CommitSceneSettingChanges();
}

std::optional<json> SceneSettingsManager::CaptureContextValue(const SceneContextId& context,
	const SettingAddress& address)
{
	switch (context.type) {
	case SceneContextType::Weather:
		// Weather and location stack on lower layers, so a capture pins what applies without the mods.
		if (const auto lower = ResolveWeatherLowerValue(context.weatherId, address, context.period,
				kCaptureSourceLayer))
			return json(*lower);
		return std::nullopt;
	case SceneContextType::Location:
		return ResolveLocationLowerValue(context.locationType, context.locationFormKey, address,
			kCaptureSourceLayer);
	default: {
		// The entry lists sit directly on the feature's own value.
		auto value = GetFeatureSettingValue(address.featureShortName, address.settingPath,
			address.settingKey);
		return value.is_null() ? std::nullopt : std::optional{ std::move(value) };
	}
	}
}

std::optional<json> SceneSettingsManager::ResolveContextEntryDefault(const SceneContextId& context,
	const SettingEntry& entry)
{
	switch (context.type) {
	case SceneContextType::Weather:
		// Re-resolved rather than remembered: the layers under the entry can have moved since.
		if (const auto lower = ResolveWeatherLowerValue(context.weatherId, GetEntryAddress(entry),
				entry.period, entry.source))
			return json(*lower);
		return std::nullopt;
	case SceneContextType::Location:
		return ResolveLocationLowerValue(context.locationType, context.locationFormKey,
			GetEntryAddress(entry), entry.source);
	default:
		return entry.originalValue.is_null() ? std::nullopt : std::optional{ entry.originalValue };
	}
}

SceneSettingsManager::ContextEntrySummary SceneSettingsManager::GetContextUserEntrySummary(
	const SceneContextId& context, PeriodScope periodScope) const
{
	ContextEntrySummary summary;
	if (!IsValidSceneContext(context))
		return summary;
	for (const auto& entry : GetContextEntries(context)) {
		if (entry.source != EntrySource::User || !EntryCoveredByContext(entry, context, periodScope))
			continue;
		++summary.total;
		summary.paused += entry.paused ? 1 : 0;
	}
	return summary;
}

void SceneSettingsManager::SetContextEntriesPaused(const SceneContextId& context, bool paused,
	PeriodScope periodScope)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return;

	bool changed = false;
	for (auto& entry : *contextEntries) {
		if (entry.source != EntrySource::User || entry.paused == paused ||
			!EntryCoveredByContext(entry, context, periodScope))
			continue;
		entry.paused = paused;
		changed = true;
	}
	if (changed)
		CommitContextUserEntryMutation(context);
}

void SceneSettingsManager::ClearContextEntries(const SceneContextId& context, PeriodScope periodScope)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return;

	// Unresolved raw entries are left in the document: they belong to features this session cannot judge.
	const auto removed = std::erase_if(*contextEntries, [&](const SettingEntry& entry) {
		return entry.source == EntrySource::User && EntryCoveredByContext(entry, context, periodScope);
	});
	if (removed != 0)
		CommitContextUserEntryMutation(context);
}

bool SceneSettingsManager::HasAnyUserEntries() const
{
	const auto hasUser = [](const std::vector<SettingEntry>& sourceEntries) {
		return std::any_of(sourceEntries.begin(), sourceEntries.end(),
			[](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	};
	return std::any_of(entries.begin(), entries.end(),
			   [&](const auto& item) { return hasUser(item.second); }) ||
	       std::any_of(weatherSceneConfigs.begin(), weatherSceneConfigs.end(),
			   [&](const auto& item) { return hasUser(item.second.entries); }) ||
	       std::any_of(locationSceneConfigs.begin(), locationSceneConfigs.end(),
			   [&](const auto& item) { return hasUser(item.second.entries); });
}

void SceneSettingsManager::ClearAllUserEntries()
{
	// Unresolved raw entries stay in the document: they belong to features this session cannot judge.
	const auto clear = [](std::vector<SettingEntry>& sourceEntries) {
		return std::erase_if(sourceEntries,
				   [](const SettingEntry& entry) { return entry.source == EntrySource::User; }) != 0;
	};

	bool changed = false;
	for (auto& [type, sourceEntries] : entries)
		if (IsEntryListSceneType(type) && clear(sourceEntries)) {
			MarkEntryListUserSettingsModified(type);
			changed = true;
		}
	for (auto& [weatherId, config] : weatherSceneConfigs)
		if (clear(config.entries)) {
			PrepareWeatherUserSettingsMutation(weatherId, false);
			changed = true;
		}
	for (auto& [configKey, config] : locationSceneConfigs)
		if (clear(config.entries)) {
			locationUserSettingsModified = true;
			changed = true;
		}

	if (!changed)
		return;
	BumpEntryPresentationRevision();
	SaveAllUserSettings();
	ReapplyIfActive();
}

std::optional<size_t> SceneSettingsManager::FindContextUserEntry(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey) const
{
	if (!IsValidSceneContext(context))
		return std::nullopt;
	const auto contextEntries = GetContextEntries(context);
	for (size_t index = 0; index < contextEntries.size(); ++index) {
		const auto& entry = contextEntries[index];
		if (entry.source == EntrySource::User && entry.featureShortName == featureShortName &&
			entry.settingPath == settingPath && entry.settingKey == settingKey &&
			entry.period == context.period)
			return index;
	}
	return std::nullopt;
}

std::optional<size_t> SceneSettingsManager::AddContextSetting(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey, bool deferSave)
{
	if (!IsValidSceneContext(context))
		return std::nullopt;

	const auto rules = GetSceneContextRules(context.type);
	if (!IsSettingAllowedForType(rules.sceneType, featureShortName, settingPath, settingKey) ||
		FindContextUserEntry(context, featureShortName, settingPath, settingKey))
		return std::nullopt;

	auto value = CaptureContextValue(context, { featureShortName, settingPath, settingKey });
	if (!value || !ValidateSceneSettingEntry(rules.label, featureShortName, settingPath, settingKey,
					  *value, rules.requireNumeric))
		return std::nullopt;

	auto* contextEntries = EnsureContextEntriesMut(context);
	if (!contextEntries)
		return std::nullopt;

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingPath = settingPath;
	entry.settingKey = settingKey;
	entry.displayName = GetSceneSettingDisplayName(featureShortName, settingPath, settingKey);
	entry.originalValue = *value;
	entry.value = std::move(*value);
	entry.source = EntrySource::User;
	entry.period = context.period;
	contextEntries->push_back(std::move(entry));

	CommitContextUserEntryMutation(context, deferSave);
	return FindContextUserEntry(context, featureShortName, settingPath, settingKey);
}

void SceneSettingsManager::UpdateContextEntryValues(const SceneContextId& context,
	std::span<const EntryValueUpdate> updates, bool deferSave)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return;

	const auto rules = GetSceneContextRules(context.type);
	bool userEntriesChanged = false;
	if (!ApplyEntryValueUpdates(rules.label, *contextEntries, updates, rules.requireNumeric,
			userEntriesChanged))
		return;
	if (userEntriesChanged)
		MarkContextUserSettingsModified(context, false);

	// Only values moved, so the presentation caches still hold. The value caches must still go: a
	// resolve landing mid-drag (a weather lerp or hour tick) would otherwise re-apply the pre-edit
	// number over the value the control is dragging.
	if (deferSave) {
		MarkSceneValuesDirty();
		MarkDeferredSceneChanges();
		return;
	}
	if (userEntriesChanged)
		SaveAllUserSettings();
	ReapplyIfActive();
}

void SceneSettingsManager::RemoveContextSetting(const SceneContextId& context, size_t index)
{
	if (!IsValidSceneContext(context))
		return;

	// A mod's file is never edited from here. Suppressing a mod value goes through
	// TombstoneContextSetting; the implementations below would strip the key from the mod's own file.
	const auto contextEntries = GetContextEntries(context);
	if (index >= contextEntries.size())
		return;
	if (contextEntries[index].source != EntrySource::User) {
		assert(false && "RemoveContextSetting called with an overwrite entry");
		logger::warn("[SceneSettings] Refusing to remove overwrite entry {} through the context façade",
			GetSettingLogName(contextEntries[index].featureShortName,
				contextEntries[index].settingPath, contextEntries[index].settingKey));
		return;
	}

	switch (context.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		RemoveSetting(ContextSceneType(context.type), index);
		break;
	case SceneContextType::Weather:
		RemoveWeatherSetting(context.weatherId, index);
		break;
	case SceneContextType::Location:
		RemoveLocationSetting(context.locationType, context.locationFormKey, index);
		break;
	default:
		break;
	}
}

bool SceneSettingsManager::TombstoneContextSetting(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return false;

	// A tombstone must not land at an address where an ordinary user entry would be rejected.
	if (!IsSettingAllowedForType(GetSceneContextRules(context.type).sceneType, featureShortName, settingPath, settingKey))
		return false;

	// An existing user entry becomes the tombstone: two entries at one address would race.
	if (const auto existing = FindContextUserEntry(context, featureShortName, settingPath, settingKey)) {
		auto& entry = (*contextEntries)[*existing];
		entry.deleted = true;
		entry.paused = false;
		CommitContextUserEntryMutation(context);
		return true;
	}

	if (GetSettingProvenance(context, featureShortName, settingPath, settingKey).layer == SettingLayer::None)
		return false;
	// The base is kept for the round trip and for revert; resolution ignores it while deleted. A null
	// one would not survive a reload, so the address stays as it is rather than losing the tombstone.
	auto value = GetFeatureSettingValue(featureShortName, settingPath, settingKey);
	if (value.is_null())
		return false;

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingPath = settingPath;
	entry.settingKey = settingKey;
	entry.displayName = GetSceneSettingDisplayName(featureShortName, settingPath, settingKey);
	entry.originalValue = value;
	entry.value = std::move(value);
	entry.source = EntrySource::User;
	entry.deleted = true;
	entry.period = context.period;
	contextEntries->push_back(std::move(entry));
	CommitContextUserEntryMutation(context);
	return true;
}

void SceneSettingsManager::ClearContextTombstone(const SceneContextId& context,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries)
		return;
	const auto existing = FindContextUserEntry(context, featureShortName, settingPath, settingKey);
	if (!existing || *existing >= contextEntries->size() || !(*contextEntries)[*existing].deleted)
		return;
	contextEntries->erase(contextEntries->begin() + static_cast<ptrdiff_t>(*existing));
	CommitContextUserEntryMutation(context);
}

void SceneSettingsManager::TogglePauseContextEntry(const SceneContextId& context, size_t index)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries || index >= contextEntries->size())
		return;

	auto& entry = (*contextEntries)[index];
	entry.paused = !entry.paused;
	BumpEntryPresentationRevision();
	if (entry.source == EntrySource::User) {
		MarkContextUserSettingsModified(context, false);
		SaveAllUserSettings();
	}
	ReapplyIfActive();
}

void SceneSettingsManager::RevertContextEntryToDefault(const SceneContextId& context, size_t index)
{
	auto* contextEntries = GetContextEntriesMut(context);
	if (!contextEntries || index >= contextEntries->size())
		return;

	auto& entry = (*contextEntries)[index];
	const auto rules = GetSceneContextRules(context.type);
	const auto defaultValue = ResolveContextEntryDefault(context, entry);
	if (!defaultValue || !ValidateSceneSettingEntry(rules.label, entry.featureShortName,
							  entry.settingPath, entry.settingKey, *defaultValue, rules.requireNumeric))
		return;

	entry.value = *defaultValue;
	entry.originalValue = entry.value;
	if (entry.source == EntrySource::User) {
		MarkContextUserSettingsModified(context, false);
		SaveAllUserSettings();
	}
	ReapplyIfActive();
}

std::vector<SceneSettingsManager::CopyCandidate> SceneSettingsManager::BuildCopyCandidates(
	const SceneContextId& source, const SceneContextId& destination, PeriodScope periodScope) const
{
	std::vector<CopyCandidate> candidates;
	if (!IsValidSceneContext(source) || !IsValidSceneContext(destination))
		return candidates;
	if (destination.type == SceneContextType::Location &&
		ResolveLocationTargetChain(destination.locationType, destination.locationFormKey).empty())
		return candidates;
	if (IsSameSceneContext(source, destination))
		return candidates;
	const auto* sourceEntries = GetCopyContextEntries(source);
	if (!sourceEntries)
		return candidates;
	static const std::vector<SettingEntry> empty;
	const auto* destinationEntries = GetCopyContextEntries(destination);
	if (!destinationEntries)
		destinationEntries = &empty;

	const auto effectiveEntries = BuildEffectiveContextEntries(*sourceEntries, source);
	std::map<SettingIdentity, json> destinationUserSettings;
	std::set<SettingIdentity> destinationOverwriteSettings;
	for (const auto& entry : *destinationEntries) {
		if (!EntryCoveredByContext(entry, destination, periodScope))
			continue;
		if (entry.source == EntrySource::User && !entry.deleted)
			destinationUserSettings.try_emplace(
				SettingIdentity{ entry.featureShortName, entry.settingPath, entry.settingKey }, entry.value);
		else if (entry.source == EntrySource::Overwrite && !entry.paused)
			destinationOverwriteSettings.insert({ entry.featureShortName, entry.settingPath, entry.settingKey });
	}

	const auto destinationRules = GetSceneContextRules(destination.type);
	for (const auto& [identity, entry] : effectiveEntries) {
		auto* setting = FindAllowedCatalogSetting(
			identity.featureShortName, identity.settingPath, identity.settingKey, destinationRules.requireNumeric);
		auto rejection = CopyRejection::None;
		if (!setting)
			rejection = CopyRejection::NotInCatalog;
		else if (!IsSettingAllowedForType(destinationRules.sceneType, identity.featureShortName,
					 identity.settingPath, identity.settingKey))
			rejection = CopyRejection::NotAllowedInLayer;
		else if (!IsSceneSettingValueAllowed(entry->value, *setting, entry->value, destinationRules.requireNumeric))
			rejection = CopyRejection::ValueRejected;
		else if (destinationOverwriteSettings.contains(identity))
			rejection = CopyRejection::BlockedByOverwrite;

		const bool compatible = rejection == CopyRejection::None;
		const auto destinationIt = destinationUserSettings.find(identity);
		const bool conflicts = destinationIt != destinationUserSettings.end();
		candidates.push_back({
			.setting = identity,
			.displayName = entry->displayName.empty() ?
			                   GetSceneSettingDisplayName(identity.featureShortName, identity.settingPath, identity.settingKey) :
			                   entry->displayName,
			.value = entry->value,
			.destinationValue = conflicts ? std::optional{ destinationIt->second } : std::nullopt,
			.rejection = rejection,
			.compatible = compatible,
			.conflicts = compatible && conflicts,
		});
	}

	// An aggregate is all-or-nothing: one unusable component disqualifies the whole control.
	std::map<CopyGroupKey, std::vector<size_t>> candidateGroups;
	for (size_t index = 0; index < candidates.size(); ++index)
		candidateGroups[GetCopyGroupKey(candidates[index].setting)].push_back(index);
	for (const auto& [groupKey, indices] : candidateGroups) {
		if (std::all_of(indices.begin(), indices.end(), [&](size_t index) { return candidates[index].compatible; }))
			continue;
		for (const auto index : indices) {
			// A row with no fault of its own is out purely because a sibling is, which is worth saying.
			if (candidates[index].rejection == CopyRejection::None)
				candidates[index].rejection = CopyRejection::GroupCompanionRejected;
			candidates[index].compatible = false;
			candidates[index].conflicts = false;
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
		return std::tie(lhs.displayName, lhs.setting) < std::tie(rhs.displayName, rhs.setting);
	});
	return candidates;
}

std::vector<SceneSettingsManager::CopyCandidate> SceneSettingsManager::GetCopyCandidates(
	const SceneContextId& source, const SceneContextId& destination, PeriodScope periodScope) const
{
	return BuildCopyCandidates(source, destination, periodScope);
}

std::vector<SceneSettingsManager::CopySource> SceneSettingsManager::GetCopySources(
	const SceneContextId& destination) const
{
	if (!IsValidSceneContext(destination))
		return {};
	if (destination.type == SceneContextType::Location &&
		ResolveLocationTargetChain(destination.locationType, destination.locationFormKey).empty())
		return {};
	std::set<SettingIdentity> destinationOverwrites;
	if (const auto* destinationEntries = GetCopyContextEntries(destination))
		for (const auto& entry : *destinationEntries)
			if (entry.source == EntrySource::Overwrite && !entry.paused &&
				EntryBelongsToContext(entry, destination))
				destinationOverwrites.insert({ entry.featureShortName, entry.settingPath, entry.settingKey });

	const auto destinationRules = GetSceneContextRules(destination.type);
	const auto countCompatible = [&](const EffectiveContextEntries& effectiveEntries) {
		struct GroupCount
		{
			size_t members = 0;
			size_t compatible = 0;
		};
		std::map<CopyGroupKey, GroupCount> groups;
		for (const auto& [identity, entry] : effectiveEntries) {
			auto& group = groups[GetCopyGroupKey(identity)];
			++group.members;
			auto* metadata = FindAllowedCatalogSetting(
				identity.featureShortName, identity.settingPath, identity.settingKey, destinationRules.requireNumeric);
			if (!destinationOverwrites.contains(identity) && metadata &&
				IsSettingAllowedForType(destinationRules.sceneType, identity.featureShortName,
					identity.settingPath, identity.settingKey) &&
				IsSceneSettingValueAllowed(entry->value, *metadata, entry->value, destinationRules.requireNumeric))
				++group.compatible;
		}
		size_t count = 0;
		for (const auto& [groupKey, group] : groups)
			if (group.members == group.compatible)
				count += group.members;
		return count;
	};

	std::vector<CopySource> sources;
	const auto addSource = [&](const SceneContextId& context, const EffectiveContextEntries& effectiveEntries) {
		if (IsSameSceneContext(context, destination))
			return;
		if (const auto settingCount = countCompatible(effectiveEntries); settingCount != 0)
			sources.push_back({ context, GetSceneContextDisplayName(context), settingCount });
	};
	const auto buildPeriodMaps = [](const std::vector<SettingEntry>& sourceEntries) {
		std::array<EffectiveContextEntries, kPeriodCount> periods;
		for (auto entrySource : { EntrySource::Overwrite, EntrySource::User })
			for (const auto& entry : sourceEntries) {
				const auto periodIndex = static_cast<int>(entry.period);
				if (entry.source == entrySource && !entry.paused && periodIndex >= 0 && periodIndex < kPeriodCount)
					periods[periodIndex][{ entry.featureShortName, entry.settingPath, entry.settingKey }] = &entry;
			}
		return periods;
	};

	const SceneContextId interiorContext{ .type = SceneContextType::Interior, .period = TimeOfDayPeriod::Count };
	addSource(interiorContext,
		BuildEffectiveContextEntries(GetEntries(SceneType::InteriorOnly), interiorContext));

	const auto timeOfDayPeriods = buildPeriodMaps(GetEntries(SceneType::TimeOfDay));
	for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex)
		addSource({ .type = SceneContextType::TimeOfDay, .period = static_cast<TimeOfDayPeriod>(periodIndex) },
			timeOfDayPeriods[periodIndex]);
	for (const auto& [weatherId, config] : weatherSceneConfigs) {
		const auto weatherPeriods = buildPeriodMaps(config.entries);
		for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex)
			addSource({ .type = SceneContextType::Weather,
						  .period = static_cast<TimeOfDayPeriod>(periodIndex),
						  .weatherId = weatherId },
				weatherPeriods[periodIndex]);
	}
	for (const auto& [configKey, config] : locationSceneConfigs) {
		SceneContextId context{
			.type = SceneContextType::Location,
			.locationType = config.type,
			.locationFormKey = config.formKey,
		};
		addSource(context, BuildEffectiveContextEntries(config.entries, context));
	}
	std::sort(sources.begin(), sources.end(), [](const auto& lhs, const auto& rhs) {
		return std::tie(lhs.context.type, lhs.displayName, lhs.context) <
		       std::tie(rhs.context.type, rhs.displayName, rhs.context);
	});
	return sources;
}

std::vector<SceneSettingsManager::CopySource> SceneSettingsManager::GetCopyDestinations(
	const SceneContextId& source) const
{
	if (!IsValidSceneContext(source) || !GetCopyContextEntries(source))
		return {};

	std::vector<CopySource> destinations;
	// Every candidate is validated through BuildCopyCandidates, the same path a real copy takes, so a
	// destination is only offered when it would actually accept something.
	const auto addDestination = [&](const SceneContextId& context, std::string displayName) {
		if (IsSameSceneContext(context, source))
			return;
		const auto candidates = BuildCopyCandidates(source, context, PeriodScope::ActivePeriod);
		const auto compatibleCount = static_cast<size_t>(
			std::count_if(candidates.begin(), candidates.end(), [](const auto& candidate) { return candidate.compatible; }));
		if (compatibleCount != 0)
			destinations.push_back({ context, std::move(displayName), compatibleCount });
	};

	const SceneContextId interiorContext{ .type = SceneContextType::Interior, .period = TimeOfDayPeriod::Count };
	addDestination(interiorContext, GetSceneContextDisplayName(interiorContext));

	for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex) {
		const SceneContextId context{ .type = SceneContextType::TimeOfDay,
			.period = static_cast<TimeOfDayPeriod>(periodIndex) };
		addDestination(context, GetSceneContextDisplayName(context));
	}

	// Weather has no "already authored" cache to lean on the way locations do, so every loaded
	// weather form is a candidate destination, whether or not it holds any settings yet.
	if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
		for (auto* weather : dataHandler->GetFormArray<RE::TESWeather>()) {
			if (!weather)
				continue;
			for (int periodIndex = 0; periodIndex < kPeriodCount; ++periodIndex) {
				const SceneContextId context{ .type = SceneContextType::Weather,
					.period = static_cast<TimeOfDayPeriod>(periodIndex),
					.weatherId = weather->GetFormID() };
				addDestination(context, GetSceneContextDisplayName(context));
			}
		}
	}

	// The current chain plus everything already authored covers every location this session can
	// navigate to, the same universe the location browser offers.
	std::vector<LocationTarget> locationTargets = GetCurrentLocationTargets();
	for (auto& target : GetAuthoredLocationTargets())
		if (std::none_of(locationTargets.begin(), locationTargets.end(), [&](const auto& existing) {
				return existing.type == target.type &&
				       NormalizeLocationFormKey(existing.formKey) == NormalizeLocationFormKey(target.formKey);
			}))
			locationTargets.push_back(std::move(target));
	for (const auto& target : locationTargets) {
		const SceneContextId context{ .type = SceneContextType::Location,
			.locationType = target.type,
			.locationFormKey = target.formKey };
		addDestination(context, std::format("{} / {}", GetCopyLocationTypeName(target.type), target.name));
	}

	std::sort(destinations.begin(), destinations.end(), [](const auto& lhs, const auto& rhs) {
		return std::tie(lhs.context.type, lhs.displayName, lhs.context) <
		       std::tie(rhs.context.type, rhs.displayName, rhs.context);
	});
	return destinations;
}

std::string SceneSettingsManager::GetSceneContextDisplayName(const SceneContextId& context) const
{
	switch (context.type) {
	case SceneContextType::Interior:
		return T("feature.scene_manager.context.interior", "Interior");
	case SceneContextType::TimeOfDay:
		return GetCopyPeriodName(context.period);
	case SceneContextType::Weather:
		return std::format("{} / {}", Util::GetFormDisplayName(context.weatherId),
			GetCopyPeriodName(context.period));
	case SceneContextType::Location: {
		auto configIt = locationSceneConfigs.find(
			GetLocationConfigKey(context.locationType, context.locationFormKey));
		// The form key is the fallback identity for targets the game never named.
		const std::string* name = &context.locationFormKey;
		if (configIt != locationSceneConfigs.end())
			name = configIt->second.name.empty() ? &configIt->second.formKey : &configIt->second.name;
		return std::format("{} / {}", GetCopyLocationTypeName(context.locationType), *name);
	}
	default:
		return {};
	}
}

SceneSettingsManager::CopyResult SceneSettingsManager::CopySettingsToContext(const SceneContextId& source,
	const SceneContextId& destination, CopyConflictPolicy conflictPolicy, bool deferCommit)
{
	CopyResult result;
	if (!IsValidSceneContext(source) || !IsValidSceneContext(destination) ||
		!IsValidCopyConflictPolicy(conflictPolicy))
		return result;
	if ((source.type == SceneContextType::Weather || destination.type == SceneContextType::Weather) &&
		!TryEnsureWeatherDataLoaded())
		return result;
	if ((source.type == SceneContextType::Location || destination.type == SceneContextType::Location) &&
		!TryEnsureLocationDataLoaded())
		return result;

	const auto candidates = BuildCopyCandidates(source, destination, PeriodScope::ActivePeriod);
	if (candidates.empty())
		return result;
	std::map<CopyGroupKey, std::vector<CopyCandidate>> groups;
	for (const auto& candidate : candidates)
		groups[GetCopyGroupKey(candidate.setting)].push_back(candidate);
	const auto groupIncompatible = [](const std::vector<CopyCandidate>& group) {
		return std::any_of(group.begin(), group.end(), [](const auto& candidate) { return !candidate.compatible; });
	};
	const auto groupConflicts = [](const std::vector<CopyCandidate>& group) {
		return std::any_of(group.begin(), group.end(), [](const auto& candidate) { return candidate.conflicts; });
	};

	for (const auto& [groupKey, group] : groups) {
		if (groupIncompatible(group))
			result.incompatible += group.size();
		else
			result.hadConflicts |= groupConflicts(group);
	}
	if (conflictPolicy == CopyConflictPolicy::Cancel && result.hadConflicts) {
		result.cancelled = true;
		return result;
	}

	std::optional<LocationTarget> destinationLocationTarget;
	if (destination.type == SceneContextType::Location) {
		const auto chain = ResolveLocationTargetChain(destination.locationType, destination.locationFormKey);
		const auto destinationKey = GetLocationConfigKey(destination.locationType, destination.locationFormKey);
		auto targetIt = std::find_if(chain.begin(), chain.end(), [&](const auto& target) {
			return GetLocationConfigKey(target.type, target.formKey) == destinationKey;
		});
		if (targetIt == chain.end())
			return result;
		destinationLocationTarget = *targetIt;
	}

	// The destination list is only created once the copy is known to produce entries.
	std::vector<SettingEntry> emptyDestinationEntries;
	std::vector<SettingEntry>* destinationEntries = nullptr;
	bool destinationNeedsMaterialization = false;
	switch (destination.type) {
	case SceneContextType::Interior:
	case SceneContextType::TimeOfDay:
		destinationEntries = &GetEntriesMut(ContextSceneType(destination.type));
		break;
	case SceneContextType::Weather: {
		auto configIt = weatherSceneConfigs.find(destination.weatherId);
		destinationNeedsMaterialization = configIt == weatherSceneConfigs.end();
		destinationEntries = destinationNeedsMaterialization ? &emptyDestinationEntries : &configIt->second.entries;
		break;
	}
	case SceneContextType::Location: {
		auto configIt = locationSceneConfigs.find(
			GetLocationConfigKey(destination.locationType, destination.locationFormKey));
		destinationNeedsMaterialization = configIt == locationSceneConfigs.end();
		destinationEntries = destinationNeedsMaterialization ? &emptyDestinationEntries : &configIt->second.entries;
		break;
	}
	default:
		return {};
	}
	if (!destinationEntries)
		return result;

	// The interior and location layers are aperiodic, so entries landing there carry no period.
	const bool aperiodicDestination = destination.type == SceneContextType::Interior ||
	                                  destination.type == SceneContextType::Location;

	std::map<SettingIdentity, size_t> destinationUserIndices;
	for (size_t index = 0; index < destinationEntries->size(); ++index) {
		const auto& entry = (*destinationEntries)[index];
		if (entry.source == EntrySource::User && EntryBelongsToContext(entry, destination))
			destinationUserIndices[{ entry.featureShortName, entry.settingPath, entry.settingKey }] = index;
	}

	// New entries need a baseline to derive their original value from, so gather them in one pass.
	std::vector<SettingAddress> candidateAddresses;
	for (const auto& [groupKey, group] : groups) {
		if (groupIncompatible(group) ||
			(conflictPolicy == CopyConflictPolicy::SkipExisting && groupConflicts(group)))
			continue;
		for (const auto& candidate : group)
			if (!destinationUserIndices.contains(candidate.setting))
				candidateAddresses.push_back({ candidate.setting.featureShortName,
					candidate.setting.settingPath, candidate.setting.settingKey });
	}
	std::sort(candidateAddresses.begin(), candidateAddresses.end());
	candidateAddresses.erase(std::unique(candidateAddresses.begin(), candidateAddresses.end()),
		candidateAddresses.end());
	EnsureBaselines(candidateAddresses);

	ResolvedSettingMap lowerLayers;
	if (destination.type == SceneContextType::Location) {
		auto resolvedLowerLayers = BuildLocationLowerLayers(
			destination.locationType, destination.locationFormKey, EntrySource::User);
		if (!resolvedLowerLayers)
			return result;
		lowerLayers = std::move(*resolvedLowerLayers);
	}
	const PeriodSettingMap* timeOfDayValues = destination.type == SceneContextType::Weather ?
	                                             &BuildTimeOfDayValueGroups() :
	                                             nullptr;

	std::map<SettingIdentity, std::optional<float>> sourceTransitions;
	if (const auto* sourceEntries = GetCopyContextEntries(source))
		for (const auto& [identity, entry] : BuildEffectiveContextEntries(*sourceEntries, source))
			sourceTransitions[identity] = entry->transitionSeconds;

	struct PendingCopy
	{
		const CopyCandidate* candidate = nullptr;
		std::optional<size_t> destinationIndex;
		json originalValue;
		std::optional<float> transitionSeconds;
	};
	std::vector<PendingCopy> pending;
	for (const auto& [groupKey, group] : groups) {
		if (groupIncompatible(group))
			continue;
		if (groupConflicts(group) && conflictPolicy == CopyConflictPolicy::SkipExisting) {
			result.skipped += group.size();
			continue;
		}

		// One duration covers the whole control: the destination's own wins, else the source's.
		std::optional<float> groupTransitionSeconds;
		if (destination.type == SceneContextType::Location) {
			bool selected = false;
			for (const auto& candidate : group) {
				if (auto indexIt = destinationUserIndices.find(candidate.setting);
					indexIt != destinationUserIndices.end()) {
					groupTransitionSeconds = (*destinationEntries)[indexIt->second].transitionSeconds;
					selected = true;
					break;
				}
			}
			if (!selected)
				for (const auto& candidate : group) {
					if (auto transitionIt = sourceTransitions.find(candidate.setting);
						transitionIt != sourceTransitions.end()) {
						groupTransitionSeconds = transitionIt->second;
						break;
					}
				}
		}

		std::vector<PendingCopy> groupPending;
		bool groupValid = true;
		for (const auto& candidate : group) {
			std::optional<size_t> destinationIndex;
			if (auto indexIt = destinationUserIndices.find(candidate.setting);
				indexIt != destinationUserIndices.end())
				destinationIndex = indexIt->second;

			const SettingAddress address{ candidate.setting.featureShortName,
				candidate.setting.settingPath, candidate.setting.settingKey };
			auto baselineIt = baselineSettings.find(address);
			json originalValue;
			if (destinationIndex) {
				originalValue = (*destinationEntries)[*destinationIndex].originalValue;
			} else if (destination.type == SceneContextType::Location) {
				if (auto lowerIt = lowerLayers.find(address); lowerIt != lowerLayers.end())
					originalValue = lowerIt->second;
				else if (baselineIt != baselineSettings.end())
					originalValue = baselineIt->second;
			} else if (baselineIt != baselineSettings.end()) {
				originalValue = baselineIt->second;
				// A weather entry sits on the time-of-day layer, which is what it restores to.
				if (timeOfDayValues && IsNumericValue(originalValue)) {
					if (auto valueIt = timeOfDayValues->find(address); valueIt != timeOfDayValues->end())
						originalValue = valueIt->second[static_cast<int>(destination.period)]
						                    .value_or(baselineIt->second.get<float>());
				}
			}
			if (!destinationIndex && !IsSceneSettingPrimitive(originalValue)) {
				groupValid = false;
				break;
			}
			groupPending.push_back({ &candidate, destinationIndex, std::move(originalValue),
				groupTransitionSeconds });
		}
		if (!groupValid) {
			result.incompatible += group.size();
			continue;
		}
		pending.insert(pending.end(), std::make_move_iterator(groupPending.begin()),
			std::make_move_iterator(groupPending.end()));
	}
	if (pending.empty())
		return result;

	if (destinationNeedsMaterialization) {
		if (destination.type == SceneContextType::Weather) {
			destinationEntries = &GetWeatherConfigMut(destination.weatherId).entries;
		} else if (destination.type == SceneContextType::Location) {
			destinationEntries = &EnsureAuthoredLocationConfig(destination.locationType,
				destination.locationFormKey, destinationLocationTarget->name,
				destinationLocationTarget->cocCode, destinationLocationTarget->editorId)
			                          .entries;
		}
	}

	for (auto& copy : pending) {
		if (copy.destinationIndex) {
			auto& destinationEntry = (*destinationEntries)[*copy.destinationIndex];
			// Any user action on an address, including a copy landing on it, must clear a tombstone rather than leave it suppressing.
			const bool wasDeleted = destinationEntry.deleted;
			destinationEntry.value = copy.candidate->value;
			destinationEntry.deleted = false;
			if (destination.type == SceneContextType::Location) {
				destinationEntry.transitionSeconds = copy.transitionSeconds;
				destinationEntry.retainSerializedTransition = false;
			}
			wasDeleted ? ++result.copied : ++result.overwritten;
			continue;
		}
		destinationEntries->push_back({
			.featureShortName = copy.candidate->setting.featureShortName,
			.settingPath = copy.candidate->setting.settingPath,
			.settingKey = copy.candidate->setting.settingKey,
			.displayName = copy.candidate->displayName,
			.value = copy.candidate->value,
			.originalValue = std::move(copy.originalValue),
			.paused = false,
			.source = EntrySource::User,
			.period = aperiodicDestination ? TimeOfDayPeriod::Count : destination.period,
			.transitionSeconds = copy.transitionSeconds,
		});
		++result.copied;
	}
	if (!result.Changed() || deferCommit)
		return result;

	CommitContextUserEntryMutation(destination);
	return result;
}

SceneSettingsManager::CopyResult SceneSettingsManager::CopySettings(const SceneContextId& source,
	const SceneContextId& destination, CopyConflictPolicy conflictPolicy)
{
	return CopySettingsToContext(source, destination, conflictPolicy, false);
}

SceneSettingsManager::CopyResult SceneSettingsManager::CopySettingsAcrossPeriods(const SceneContextId& source,
	const SceneContextId& destination, CopyConflictPolicy conflictPolicy, PeriodScope periodScope)
{
	if (periodScope == PeriodScope::ActivePeriod || !IsPeriodicContext(destination.type))
		return CopySettings(source, destination, conflictPolicy);

	CopyResult result;
	// Cancelling has to be decided over the whole fan-out, or the earlier periods land before a later one refuses.
	if (conflictPolicy == CopyConflictPolicy::Cancel) {
		const auto candidates = BuildCopyCandidates(source, destination, PeriodScope::AllPeriods);
		if (std::any_of(candidates.begin(), candidates.end(),
				[](const auto& candidate) { return candidate.conflicts; })) {
			result.hadConflicts = true;
			result.cancelled = true;
			return result;
		}
	}

	for (const auto period : kPeriods) {
		auto periodDestination = destination;
		periodDestination.period = period;
		if (IsSameSceneContext(periodDestination, source))
			continue;
		const auto periodResult = CopySettingsToContext(source, periodDestination, conflictPolicy, true);
		result.copied += periodResult.copied;
		result.skipped += periodResult.skipped;
		result.overwritten += periodResult.overwritten;
		result.incompatible += periodResult.incompatible;
		result.hadConflicts |= periodResult.hadConflicts;
		result.cancelled |= periodResult.cancelled;
	}
	if (result.Changed())
		CommitContextUserEntryMutation(destination);
	return result;
}

bool SceneSettingsManager::HasLocationEntry(LocationTargetType type, std::string_view formKey,
	const std::string& featureShortName, const std::vector<std::string>& settingPath,
	const std::string& settingKey, std::optional<EntrySource> source) const
{
	const auto& config = GetLocationConfig(type, formKey);
	return std::any_of(config.entries.begin(), config.entries.end(), [&](const auto& entry) {
		return (!source || entry.source == *source) &&
		       IsSameSetting(entry, featureShortName, settingPath, settingKey);
	});
}

void SceneSettingsManager::DiscoverLocationOverwrites()
{
	const auto root = GetLocationOverwritesDir();
	std::error_code ec;
	if (!std::filesystem::exists(root, ec))
		return;
	for (const auto& directory : GetSortedDirectoryPaths(root, true, "location overwrite directories"))
		DiscoverLocationOverwritesForTarget(directory);
}

void SceneSettingsManager::DiscoverLocationOverwritesForTarget(const std::filesystem::path& targetDir)
{
	const auto formKey = targetDir.filename().string();
	if (formKey.empty())
		return;

	std::optional<LocationTargetType> resolvedType;
	std::string resolvedName;
	std::string resolvedCocCode;
	std::string canonicalFormKey = formKey;
	if (const auto formId = Util::SpidToFormId(formKey); formId != 0) {
		canonicalFormKey = Util::FormIdToSpid(formId);
		if (auto* form = RE::TESForm::LookupByID(formId)) {
			if (form->GetFormType() == RE::FormType::Region)
				resolvedType = LocationTargetType::Region;
			else if (form->GetFormType() == RE::FormType::Location)
				resolvedType = LocationTargetType::Location;
			else if (form->GetFormType() == RE::FormType::Cell)
				resolvedType = LocationTargetType::Cell;
			else {
				logger::warn("[SceneSettings] Location overwrite target '{}' is not a region, location, or cell", formKey);
				return;
			}
			resolvedName = Util::GetFormDisplayName(formId);
			if (*resolvedType == LocationTargetType::Cell)
				resolvedCocCode = Util::GetFormEditorID(form);
		}
	}

	FeatureSettingsCache featureSettingsCache;
	for (const auto& filePath : GetSortedJsonFiles(targetDir, "location overwrite files")) {
		try {
			json data;
			if (!ReadBoundedSceneJson(filePath, data)) {
				logger::warn("[SceneSettings] Location overwrite '{}' is invalid or exceeds {} bytes",
					filePath.string(), kMaxSceneOverwriteFileSize);
				continue;
			}

			std::optional<LocationTargetType> metadataType;
			std::string metadataName;
			std::string metadataCocCode;
			if (auto metadataIt = data.find(kMetadataKey); metadataIt != data.end()) {
				if (!metadataIt->is_object()) {
					logger::warn("[SceneSettings] Location overwrite '{}' metadata must be an object",
						filePath.string());
					continue;
				}
				const auto metadataContext = std::format("Location overwrite '{}' metadata", filePath.string());
				std::string targetType;
				if (!ReadOptionalStringField(*metadataIt, "targetType", targetType, metadataContext) ||
					!ReadOptionalStringField(*metadataIt, "targetName", metadataName, metadataContext) ||
					!ReadOptionalStringField(*metadataIt, "coc", metadataCocCode, metadataContext))
					continue;
				if (targetType == "Region")
					metadataType = LocationTargetType::Region;
				else if (targetType == "Location")
					metadataType = LocationTargetType::Location;
				else if (targetType == "Cell")
					metadataType = LocationTargetType::Cell;
				else if (!targetType.empty()) {
					logger::warn("[SceneSettings] {} has invalid targetType '{}'", metadataContext, targetType);
					continue;
				}
			}
			if (resolvedType && metadataType && *resolvedType != *metadataType) {
				logger::warn("[SceneSettings] Location overwrite '{}' targetType does not match resolved form '{}'",
					filePath.string(), formKey);
				continue;
			}
			const auto targetType = resolvedType ? resolvedType : metadataType;
			if (!targetType) {
				logger::warn("[SceneSettings] Location overwrite '{}' has no resolvable target type",
					filePath.string());
				continue;
			}

			auto& config = GetLocationConfigMut(*targetType, canonicalFormKey,
				!metadataName.empty() ? metadataName : resolvedName);
			if (!metadataCocCode.empty())
				config.cocCode = metadataCocCode;
			else if (!resolvedCocCode.empty())
				config.cocCode = resolvedCocCode;

			std::vector<SettingEntry> parsedEntries;
			if (!ParseOverwriteFileEntries(filePath, SceneType::Location, false, parsedEntries,
					&featureSettingsCache))
				continue;
			for (auto& entry : parsedEntries)
				AddOverwriteEntryIfUnique(config.entries, std::move(entry), "location");
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Failed to load location overwrite '{}': {}",
				filePath.filename().string(), e.what());
		}
	}
}

void SceneSettingsManager::DiscoverWeatherOverwrites()
{
	const auto countWeatherEntries = [this] {
		return std::accumulate(weatherSceneConfigs.begin(), weatherSceneConfigs.end(), size_t{ 0 },
			[](size_t total, const auto& config) { return total + config.second.entries.size(); });
	};

	const auto previousEntryCount = countWeatherEntries();
	auto baseDir = GetWeatherOverwritesDir();
	std::error_code ec;
	if (!std::filesystem::exists(baseDir, ec))
		return;

	logger::info("[SceneSettings] Discovering weather overwrites in: {}", baseDir.string());

	for (const auto& weatherDirectory : GetSortedDirectoryPaths(baseDir, true, "weather overwrite directories")) {
		auto folderName = weatherDirectory.filename().string();
		RE::FormID weatherId = Util::SpidToFormId(folderName);
		if (weatherId == 0) {
			logger::warn("[SceneSettings] Weather overwrite folder '{}' could not be resolved - skipping", folderName);
			continue;
		}

		DiscoverWeatherOverwritesForSpid(weatherId, weatherDirectory);
	}

	if (countWeatherEntries() != previousEntryCount)
		BumpEntryPresentationRevision();
}

void SceneSettingsManager::DiscoverWeatherOverwritesForSpid(RE::FormID weatherId, const std::filesystem::path& weatherDir)
{
	auto& config = GetWeatherConfigMut(weatherId);
	FeatureSettingsCache featureSettingsCache;

	const auto loadWeatherFile = [&](const std::filesystem::path& filePath, auto&& assignPeriods) {
		try {
			std::vector<SettingEntry> parsedEntries;
			if (ParseOverwriteFileEntries(filePath, SceneType::TimeOfDay, true, parsedEntries, &featureSettingsCache))
				for (auto& parsed : parsedEntries)
					assignPeriods(parsed);
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Failed to load weather overwrite '{}': {}", filePath.filename().string(), e.what());
		}
	};

	for (auto period : kPeriods) {
		const auto periodDir = weatherDir / GetPeriodName(period);
		std::error_code ec;
		if (!std::filesystem::exists(periodDir, ec))
			continue;

		for (const auto& filePath : GetSortedJsonFiles(periodDir, "weather period overwrite files"))
			loadWeatherFile(filePath, [&](SettingEntry& parsed) {
				parsed.period = period;
				AddOverwriteEntryIfUnique(config.entries, std::move(parsed), "weather");
			});
	}

	// Flat weather files are copied to every period after period-specific files are loaded.
	for (const auto& filePath : GetSortedJsonFiles(weatherDir, "flat weather overwrite files"))
		loadWeatherFile(filePath, [&](const SettingEntry& parsed) {
			for (auto period : kPeriods) {
				SettingEntry entry = parsed;
				entry.period = period;
				AddOverwriteEntryIfUnique(config.entries, std::move(entry), "weather");
			}
		});
}

float SceneSettingsManager::GetTimeOfDayPeriodFallbackFloat(float baseValue, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, int periodIndex) const
{
	const json* value = nullptr;
	EntrySource source = EntrySource::Overwrite;
	const auto period = static_cast<TimeOfDayPeriod>(periodIndex);

	for (const auto& entry : GetEntries(SceneType::TimeOfDay)) {
		// A tombstone supplies nothing; skipping it here lets a weather capture fall through to base.
		if (!IsEntryActive(entry) || entry.period != period || entry.deleted ||
			!IsSameSetting(entry, featureShortName, settingPath, settingKey))
			continue;
		if (!value || (entry.source == EntrySource::User && source != EntrySource::User)) {
			value = &entry.value;
			source = entry.source;
		}
	}

	if (!value)
		return baseValue;
	if (!IsNumericValue(*value)) {
		logger::warn("[SceneSettings] Time of day fallback value for '{}' is not a float",
			GetSettingLogName(featureShortName, settingPath, settingKey));
		return baseValue;
	}

	const float result = value->get<float>();
	return std::isfinite(result) ? result : baseValue;
}
