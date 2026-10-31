#include "SettingsCatalog.h"

#include "I18n/I18n.h"
#include "Utils/Format.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <ranges>
#include <set>

namespace Util::Settings
{
	constexpr std::string_view kImGuiIdSeparator = "##";
	constexpr std::string_view kLabelSeparator = " / ";
	/// Decimals FormatValue shows for a float, before trailing zeros are trimmed.
	constexpr int kValueDecimals = 3;

	/// Joins the non-empty parts into the "Group / Setting" form export lists show.
	static std::string JoinLabelParts(const std::vector<std::string>& parts)
	{
		return parts | std::views::filter([](const auto& part) { return !part.empty(); }) |
		       std::views::join_with(kLabelSeparator) | std::ranges::to<std::string>();
	}

	std::string StripImGuiId(std::string_view label)
	{
		return std::string(label.substr(0, label.find(kImGuiIdSeparator)));
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

	void EraseMetadataKeys(nlohmann::json& settings)
	{
		if (!settings.is_object())
			return;
		for (auto it = settings.begin(); it != settings.end();)
			it = IsMetadataKey(it.key()) ? settings.erase(it) : std::next(it);
	}

	void MergeSettings(nlohmann::json& target, const nlohmann::json& overlay)
	{
		if (!target.is_object() || !overlay.is_object()) {
			target = overlay;
			return;
		}
		for (const auto& [key, value] : overlay.items()) {
			if (const auto found = target.find(key); found != target.end())
				MergeSettings(*found, value);
			else
				target[key] = value;
		}
	}

	std::vector<ExportSetting> GetExportSettings(std::string_view featureShortName, const nlohmann::json& settings)
	{
		using json = nlohmann::json;
		std::map<std::string, std::string> labels;
		for (const auto& setting : SceneSettingsCatalog::GetSettings()) {
			if (setting.featureShortName != featureShortName ||
				!SceneSettingsCatalog::HasFlag(setting.flags, SceneSettingsCatalog::SettingFlag::Persisted))
				continue;
			json::json_pointer path;
			for (const auto& part : SplitCatalogPath(setting.serializedPath))
				path /= part;
			path /= std::string(setting.serializedKey);
			auto parts = GetCatalogDisplayPath(setting);
			parts.push_back(GetCatalogLeafDisplayName(setting));
			labels.try_emplace(path.to_string(), JoinLabelParts(parts));
		}
		std::vector<ExportSetting> result;
		const auto visit = [&](auto&& self, const json& node, const json::json_pointer& parent, const std::string& context) -> void {
			for (const auto& [key, value] : node.items()) {
				if (IsMetadataKey(key))
					continue;
				auto path = parent / key;
				auto label = JoinLabelParts({ context, NormalizeDisplayPart(key) });
				if (value.is_object()) {
					self(self, value, path, label);
				} else {
					if (const auto found = labels.find(path.to_string()); found != labels.end() && !found->second.empty())
						label = found->second;
					result.push_back({ path.to_string(), std::move(label) });
				}
			}
		};
		if (settings.is_object())
			visit(visit, settings, json::json_pointer{}, {});
		std::ranges::sort(result, [](const auto& a, const auto& b) {
			return a.label == b.label ? a.path < b.path : a.label < b.label;
		});
		return result;
	}

	nlohmann::json SelectSettingPaths(const nlohmann::json& values, const std::vector<std::string>& paths)
	{
		using json = nlohmann::json;
		const std::set<std::string> selected(paths.begin(), paths.end());
		const auto visit = [&](auto&& self, const json& node, const json::json_pointer& parent) -> json {
			json result = json::object();
			if (!node.is_object())
				return result;
			for (const auto& [key, value] : node.items()) {
				if (IsMetadataKey(key))
					continue;
				const auto path = parent / key;
				if (value.is_object()) {
					auto nested = self(self, value, path);
					if (!nested.empty())
						result[key] = std::move(nested);
				} else if (selected.contains(path.to_string())) {
					result[key] = value;
				}
			}
			return result;
		};
		return visit(visit, values, json::json_pointer{});
	}

	bool IsCompatibleSettingValue(const nlohmann::json& known, const nlohmann::json& value)
	{
		// A boolean loads into a numeric field (features store toggles as uint), but a number never into a bool.
		return known.is_null() || known.type() == value.type() ||
		       (known.is_number() && (value.is_number() || value.is_boolean()));
	}

	/** @brief Dotted path of key under prefix. */
	static std::string JoinSettingPath(const std::string& prefix, const std::string& key)
	{
		return prefix.empty() ? key : prefix + "." + key;
	}

	void CollectUnknownSettingKeys(const nlohmann::json& incoming, const nlohmann::json& known,
		const std::string& prefix, std::vector<std::string>& unknownKeys)
	{
		if (!incoming.is_object())
			return;
		for (const auto& [key, value] : incoming.items()) {
			if (IsMetadataKey(key))
				continue;
			const auto path = JoinSettingPath(prefix, key);
			const auto found = known.is_object() ? known.find(key) : known.end();
			if (!known.is_object() || found == known.end())
				unknownKeys.push_back(path);
			else if (!IsCompatibleSettingValue(*found, value))
				unknownKeys.push_back(path + " (wrong type)");
			else if (value.is_object())
				CollectUnknownSettingKeys(value, *found, path, unknownKeys);
		}
	}

	void EraseMismatchedSettings(nlohmann::json& incoming, const nlohmann::json& known,
		const std::string& prefix, std::vector<std::string>& erasedKeys)
	{
		if (!incoming.is_object() || !known.is_object())
			return;
		for (auto it = incoming.begin(); it != incoming.end();) {
			const auto found = known.find(it.key());
			if (found == known.end()) {
				++it;
				continue;
			}
			const auto path = JoinSettingPath(prefix, it.key());
			if (!IsCompatibleSettingValue(*found, it.value())) {
				erasedKeys.push_back(path);
				it = incoming.erase(it);
				continue;
			}
			EraseMismatchedSettings(it.value(), *found, path, erasedKeys);
			++it;
		}
	}

	std::string FormatValue(const nlohmann::json& value)
	{
		if (value.is_string())
			return value.get<std::string>();
		if (value.is_boolean())
			return value.get<bool>() ? T("cs_editor.scene_copy_value_on", "On") : T("cs_editor.scene_copy_value_off", "Off");
		if (value.is_array()) {
			std::string text = "[";
			for (const auto& element : value)
				text += (text.size() > 1 ? ", " : "") + FormatValue(element);
			return text + "]";
		}
		if (!value.is_number_float())
			return value.dump();
		auto text = std::format("{:.{}f}", value.get<double>(), kValueDecimals);
		text.erase(text.find_last_not_of('0') + 1);
		if (text.ends_with('.'))
			text.pop_back();
		return text;
	}
}
