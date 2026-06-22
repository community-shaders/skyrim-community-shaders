#include "SettingsPatches.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Features/Effect11/Effects/Effect.h"

namespace Util::SettingsPatches
{
	static std::vector<Entry> entries;
	static bool loaded = false;

	void Load()
	{
		entries.clear();
		loaded = true;

		std::filesystem::path path = "Data\\Shaders\\Effect11\\SettingsPatches.json";
		std::ifstream ifs(path);
		if (!ifs.is_open())
			return;

		try {
			nlohmann::json root = nlohmann::json::parse(ifs);
			for (auto& item : root) {
				Entry entry;
				entry.file = item.at("file").get<std::string>();
				for (auto& p : item.at("patches")) {
					Patch patch;
					patch.variable = p.at("variable").get<std::string>();
					patch.value = p.at("value").get<std::string>();
					entry.patches.push_back(std::move(patch));
				}
				if (item.contains("hiddenGroups") && item["hiddenGroups"].is_array()) {
					for (auto& g : item["hiddenGroups"]) {
						if (g.is_string())
							entry.hiddenGroups.push_back(g.get<std::string>());
					}
				}
				entries.push_back(std::move(entry));
			}
		} catch (const std::exception& e) {
			logger::error("[SettingsPatches] Failed to parse {}: {}", path.string(), e.what());
		}

		if (!entries.empty())
			logger::info("[SettingsPatches] Loaded {} entries", entries.size());
	}

	static bool FilenameMatches(const std::string& effectName, const std::string& pattern)
	{
		return std::equal(effectName.begin(), effectName.end(), pattern.begin(), pattern.end(), [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
	}

	static std::string GetUniqueKey(const Effect::UIVariable& uiVar)
	{
		if (!uiVar.uniqueName.empty())
			return uiVar.uniqueName;
		return uiVar.group.empty() ? uiVar.displayName : uiVar.group + "." + uiVar.displayName;
	}

	void Apply(Effect& effect)
	{
		if (!loaded)
			Load();

		for (auto& entry : entries) {
			if (!FilenameMatches(effect.GetName(), entry.file))
				continue;

			for (auto& hiddenGroup : entry.hiddenGroups) {
				for (auto& uiVar : effect.uiVariables) {
					if (uiVar.group == hiddenGroup || uiVar.group.starts_with(hiddenGroup + "."))
						uiVar.isHidden = true;
				}
			}

			for (auto& patch : entry.patches) {
				bool found = false;
				for (auto& uiVar : effect.uiVariables) {
					if (uiVar.isLabel)
						continue;

					if (GetUniqueKey(uiVar) != patch.variable)
						continue;

					found = true;
					bool patched = false;
					switch (uiVar.type) {
					case Effect::UIVariableType::Float:
						try {
							float val = std::stof(patch.value);
							if (uiVar.effectVariable && SUCCEEDED(uiVar.effectVariable->AsScalar()->SetFloat(val))) {
								uiVar.floatValue = val;
								patched = true;
							}
						} catch (...) {}
						break;
					case Effect::UIVariableType::Int:
						try {
							int val = std::stoi(patch.value);
							if (uiVar.effectVariable && SUCCEEDED(uiVar.effectVariable->AsScalar()->SetInt(val))) {
								uiVar.intValue = val;
								patched = true;
							}
						} catch (...) {}
						break;
					case Effect::UIVariableType::Bool:
						{
							std::string lv = patch.value;
							std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
							if (lv == "true" || lv == "1" || lv == "false" || lv == "0") {
								bool val = (lv == "true" || lv == "1");
								if (uiVar.effectVariable && SUCCEEDED(uiVar.effectVariable->AsScalar()->SetBool(val))) {
									uiVar.boolValue = val;
									patched = true;
								}
							}
						}
						break;
					default:
						break;
					}

					if (patched) {
						uiVar.isReadOnly = true;
						logger::debug("[SettingsPatches] Patched '{}' in '{}' to '{}'",
							patch.variable, effect.GetName(), patch.value);
					}
					break;
				}
				if (!found) {
					logger::debug("[SettingsPatches] No match for '{}' in '{}'",
						patch.variable, effect.GetName());
				}
			}
		}
	}
}
