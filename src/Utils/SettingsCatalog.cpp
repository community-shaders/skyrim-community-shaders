#include "SettingsCatalog.h"

#include "I18n/I18n.h"
#include "Utils/Format.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace Util::Settings
{
	constexpr std::string_view kImGuiIdSeparator = "##";

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
			std::string label;
			for (const auto& part : parts) {
				if (part.empty())
					continue;
				if (!label.empty())
					label += " / ";
				label += part;
			}
			labels.try_emplace(path.to_string(), std::move(label));
		}
		std::vector<ExportSetting> result;
		const auto visit = [&](auto&& self, const json& node, const json::json_pointer& parent, const std::string& context) -> void {
			for (const auto& [key, value] : node.items()) {
				if (key.starts_with('_'))
					continue;
				auto path = parent / key;
				auto label = context.empty() ? NormalizeDisplayPart(key) : context + " / " + NormalizeDisplayPart(key);
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
				if (key.starts_with('_'))
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

	void CollectUnknownSettingKeys(const nlohmann::json& incoming, const nlohmann::json& known,
		const std::string& prefix, std::vector<std::string>& unknownKeys)
	{
		if (!incoming.is_object())
			return;
		for (const auto& [key, value] : incoming.items()) {
			if (key.starts_with('_'))
				continue;
			const auto path = prefix.empty() ? key : prefix + "." + key;
			if (!known.is_object() || !known.contains(key))
				unknownKeys.push_back(path);
			else if (value.is_object())
				CollectUnknownSettingKeys(value, known[key], path, unknownKeys);
		}
	}
}
