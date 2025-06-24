#pragma once

#include "FeatureVersions.h"

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
	virtual void DrawUnloadedUI();

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

	// Feature utility functions
	/**
	 * @brief Gets the minimum required version for a feature.
	 *
	 * This function looks up the minimum required version for a feature
	 * from FeatureVersions::FEATURE_MINIMAL_VERSIONS and returns it as a
	 * formatted string. Returns "unknown" if the feature is not found.
	 *
	 * @param shortName The short name of the feature.
	 * @return The formatted minimum required version string, or "unknown" if not found.
	 */
	static std::string GetFeatureRequiredVersion(const std::string& shortName);

	/**
	 * @brief Checks if a feature has a minimum required version defined.
	 *
	 * This function checks if a feature exists in the FeatureVersions::FEATURE_MINIMAL_VERSIONS
	 * map and optionally returns the version.
	 *
	 * @param shortName The short name of the feature.
	 * @param outVersion Pointer to REL::Version to store the version if found (optional).
	 * @return True if the feature is found, false otherwise.
	 */
	static bool IsFeatureKnown(const std::string& shortName, REL::Version* outVersion = nullptr);
};