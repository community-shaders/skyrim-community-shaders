#pragma once

#include "FeatureVersions.h"
#include "Menu.h"
#include "Utils/Format.h"

struct Feature
{
	// Nexus Mods base URL for Skyrim Special Edition
	static constexpr std::string_view NEXUS_BASE_URL = "https://www.nexusmods.com/skyrimspecialedition/mods/";
	bool loaded = false;
	std::string version;
	std::string failedLoadedMessage;

	virtual std::string GetName() = 0;
	virtual std::string GetShortName() = 0;
	virtual std::string GetFeatureModLink() { return ""; }
	virtual std::string_view GetShaderDefineName() { return ""; }
	virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() { return {}; }

protected:
	// Helper method to construct Nexus Mods URL from mod ID
	static std::string MakeNexusModURL(std::string_view modId) noexcept
	{
		std::string url;
		url.reserve(NEXUS_BASE_URL.size() + modId.size());
		url.append(NEXUS_BASE_URL);
		url.append(modId);
		return url;
	}

public:
	virtual bool HasShaderDefine(RE::BSShader::Type) { return false; }
	/**
	 * Whether the feature supports VR.
	 *
	 * \return true if VR supported; else false
	 */
	virtual bool SupportsVR() { return false; }

	/**
	 * Whether the feature is a CORE feature
	 * This will place it under "Core Features" in UI
	 * Also need to create a file named "CORE" in the root of the feature folder
	 * if it should be merged into main cs zip file
	 */
	virtual bool IsCore() const { return false; }

	/**
	 * Get the category for UI grouping (e.g., "Terrain", "Lighting", "Characters", etc.)
	 * Core features will be distributed to their respective categories
	 */
	virtual std::string_view GetCategory() const { return "Other"; }

	/**
	 * Whether the feature will show up in the GUI menu
	 */
	virtual bool IsInMenu() const { return true; }

	/**
	 * Whether to print the INI version missing message when this feature is unloaded
	 */
	virtual bool DrawFailLoadMessage() const { return true; }

	/**
	 * Get feature summary and key features for hover tooltip and unloaded UI
	 *
	 * \return Pair containing feature summary description and vector of key feature bullet points
	 */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() { return {}; }

	virtual void SetupResources() {}
	virtual void Reset() {}

	virtual void DrawSettings() {}
	virtual void DrawUnloadedUI()
	{
		// Prioritize detailed failure message if available
		if (!failedLoadedMessage.empty()) {
			// Use error color for all failure messages
			auto& themeSettings = Menu::GetSingleton()->GetTheme();
			ImGui::TextColored(themeSettings.StatusPalette.Error, failedLoadedMessage.c_str());
			return;
		}

		// Fallback: Always show missing file message when no specific failure message exists
		auto& themeSettings = Menu::GetSingleton()->GetTheme();
		auto ini_filename = std::format("{}.ini", GetShortName());

		// Get the minimum required version to include in the error message
		std::string requiredVersion = "unknown";
		std::string shortName = GetShortName();
		if (!shortName.empty()) {
			auto iter = FeatureVersions::FEATURE_MINIMAL_VERSIONS.find(shortName);
			if (iter != FeatureVersions::FEATURE_MINIMAL_VERSIONS.end()) {
				requiredVersion = Util::GetFormattedVersion(iter->second);
			}
		}

		auto missingFileMessage = std::format("The {} file is missing. This feature is not installed! Version required: {}", ini_filename, requiredVersion);
		ImGui::TextColored(themeSettings.StatusPalette.Error, missingFileMessage.c_str());

		// Also show feature summary if available
		auto [description, keyFeatures] = GetFeatureSummary();
		if (!description.empty()) {
			ImGui::Spacing();
			ImGui::TextWrapped("%s", description.c_str());
		}

		if (!keyFeatures.empty()) {
			if (description.empty()) {
				ImGui::Spacing();
			}
			ImGui::TextWrapped("Key features:");
			for (const auto& feature : keyFeatures) {
				ImGui::BulletText("%s", feature.c_str());
			}
		}
	}

	virtual void ReflectionsPrepass() {};
	virtual void Prepass() {}
	virtual void EarlyPrepass() {}

	virtual void DataLoaded() {}
	virtual void PostPostLoad() {}

	void Load(json& o_json);
	void Save(json& o_json);

	virtual void SaveSettings(json&) {}
	virtual void LoadSettings(json&) {}

	virtual void RestoreDefaultSettings() {}
	virtual bool ToggleAtBootSetting();

	virtual bool ValidateCache(CSimpleIniA& a_ini);
	virtual void WriteDiskCacheInfo(CSimpleIniA& a_ini);
	virtual void ClearShaderCache() {}

	static const std::vector<Feature*>& GetFeatureList();
};