#pragma once

#include "FeatureCategories.h"
#include "FeatureConstraints.h"
#include "FeatureVersions.h"
#include "I18n/I18n.h"
#ifdef TRACY_ENABLE
#	include <Tracy/Tracy.hpp>
#	include <Tracy/TracyD3D11.hpp>
#endif

struct Feature
{
	// For global settings search
	struct SettingSearchEntry
	{
		std::string label;
		std::string description;
		std::function<void()> focusCallback;  // Called to focus/highlight this setting in the UI
		std::string featureName;              // For display context
	};
	// Override in features to expose settings for search
	virtual std::vector<SettingSearchEntry> GetSettingsSearchEntries() { return {}; }

	// Nexus Mods base URL for Skyrim Special Edition
	static constexpr std::string_view NEXUS_BASE_URL = "https://www.nexusmods.com/skyrimspecialedition/mods/";
	bool loaded = false;
	std::string version;
	std::string failedLoadedMessage;

	virtual std::string GetName() = 0;
	virtual std::string GetShortName() = 0;
	virtual std::string GetDisplayName() { return GetName(); }
	std::string GetDisplayCategory() const;
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
	 * Whether the feature is a CORE feature
	 * This will place it under "Core Features" in UI
	 * If "CORE" file is present in the root of the feature folder,
	 * it will be merged into main cs zip file and automatically considered core
	 */
	virtual bool IsCore() const
	{
		return FeatureVersions::FEATURE_CORE_NAMES.contains(const_cast<Feature*>(this)->GetShortName());
	}

	/**
	 * Get the category for UI grouping (e.g., "Terrain", "Lighting", "Characters", etc.)
	 * Core features will be distributed to their respective categories
	 */
	virtual std::string_view GetCategory() const { return FeatureCategories::kOther; }

	/**
	 * Release maturity stage, declared via the feature .ini (Alpha/Beta flags) and
	 * baked into FeatureVersions.h at build time. Alpha takes precedence over Beta.
	 */
	enum class ReleaseStage
	{
		Release,
		Beta,
		Alpha
	};

	virtual ReleaseStage GetReleaseStage() const
	{
		const auto name = const_cast<Feature*>(this)->GetShortName();
		if (FeatureVersions::FEATURE_ALPHA_NAMES.contains(name))
			return ReleaseStage::Alpha;
		if (FeatureVersions::FEATURE_BETA_NAMES.contains(name))
			return ReleaseStage::Beta;
		return ReleaseStage::Release;
	}

	bool IsAlpha() const { return GetReleaseStage() == ReleaseStage::Alpha; }
	bool IsBeta() const { return GetReleaseStage() == ReleaseStage::Beta; }

	/**
	 * Localized stage marker shown after the feature name ("[ALPHA]", "[BETA]"),
	 * empty for release features. Takes the stage so callers that already resolved
	 * it (see GetReleaseStage) avoid a redundant lookup.
	 */
	static std::string GetReleaseStageTag(ReleaseStage stage);

	/**
	 * Whether the feature is disabled at boot by default (before any user override).
	 * Alpha and Beta features start disabled on first install; users can still enable
	 * them via the "Disable at Boot" menu.
	 */
	virtual bool IsDisabledByDefault() const { return GetReleaseStage() != ReleaseStage::Release; }

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

	/**
	 * @brief Called during disk-cache shader loading to generate additional shader permutations.
	 *
	 * Invoked once per BSShader load when the shader cache is in disk-cache mode.
	 * Features can override this to inject custom permutation descriptors into the
	 * shader cache so that feature-specific technique variants are compiled and stored.
	 * This is a cold path (disk I/O, not per-frame); performance is not critical here.
	 *
	 * @param shader The BSShader being loaded.
	 */
	virtual void GenerateShaderPermutations(RE::BSShader*) {}

	virtual void Load() {}  // Called during SKSE Load - earliest hook point only for critical hooks like d3d
	virtual void DataLoaded() {}
	virtual void PostPostLoad() {}

	void Load(json& o_json);
	void Save(json& o_json);

	virtual void SaveSettings(json&) {}
	virtual void LoadSettings(json&) {}

	virtual void RestoreDefaultSettings() {}
	virtual bool ToggleAtBootSetting();

	/**
	 * @brief Reapplies override settings for this feature if available
	 * @return True if overrides were found and applied, false otherwise
	 */
	virtual bool ReapplyOverrideSettings();

	/**
	 * Weather analysis configuration for features that want to provide weather analysis.
	 * If sectionName is empty, the feature will not appear in weather analysis UI.
	 * Features should populate this struct to opt-in to weather analysis display.
	 */
	struct WeatherAnalysisConfig
	{
		std::string sectionName;             // Display name for the collapsible section (empty = no weather analysis)
		std::function<void()> drawFunction;  // Custom draw function for weather analysis content

		// Constructor for easy initialization
		WeatherAnalysisConfig() = default;
		WeatherAnalysisConfig(const std::string& name, std::function<void()> drawFunc) :
			sectionName(name), drawFunction(std::move(drawFunc)) {}
	};

	/**
	 * Get weather analysis configuration for this feature.
	 * Returns empty sectionName by default (no weather analysis).
	 * Features should override this to provide their weather analysis section name and draw function.
	 */
	virtual WeatherAnalysisConfig GetWeatherAnalysisConfig() const { return {}; }

	/**
	 * @brief Called during feature initialization to register weather-controllable variables
	 * Features should register their weather variables here using the WeatherVariables::GlobalWeatherRegistry
	 * The weather system will automatically handle save/load/lerp for all registered variables
	 */
	virtual void RegisterWeatherVariables() {}

	/**
	 * @brief Returns constraints this feature imposes on other features' settings
	 *
	 * Features override this to declare runtime incompatibilities with other features.
	 * The constraint system will automatically:
	 * - Force the target setting to the specified value
	 * - Disable the UI control for the constrained setting
	 * - Show a tooltip explaining which features caused the constraint
	 *
	 * @return Vector of constraints this feature currently imposes (empty if none)
	 */
	virtual std::vector<FeatureConstraints::Constraint> GetActiveConstraints() const { return {}; }

	virtual bool ValidateCache(CSimpleIniA& a_ini);
	virtual void WriteDiskCacheInfo(CSimpleIniA& a_ini);
	virtual void ClearShaderCache() {}

	static const std::vector<Feature*>& GetFeatureList();

	/**
	 * @brief Finds a loaded feature by its short name.
	 *
	 * @param shortName The short name to search for.
	 * @return Pointer to the feature if found and loaded, nullptr otherwise.
	 */
	static Feature* FindFeatureByShortName(const std::string& shortName);

	/**
	 * @brief Gets sorted short names of all loaded features that appear in the menu.
	 *
	 * @return Sorted vector of short name strings.
	 */
	static std::vector<std::string> GetLoadedFeatureNames();

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

	/**
	 * @brief Execute a callable for each loaded feature with optional Tracy CPU profiling
	 *
	 * Iterates through all loaded features and calls the provided function with automatic
	 * CPU profiling zones (ZoneScoped/ZoneName via Tracy) when TRACY_ENABLE is defined.
	 * Thread-local string formatting is used to minimize per-call overhead.
	 *
	 * Usage:
	 *   Feature::ForEachLoadedFeature("Reset", [](Feature* feature) { feature->Reset(); });
	 *   Feature::ForEachLoadedFeature("Prepass", [](Feature* feature) { feature->Prepass(); });
	 *
	 * @param methodName Name of the method being called (used for Tracy zone naming)
	 * @param callback Callable that receives (Feature*) and performs the operation
	 */
	// Called once from State after TracyD3D11Context is created so ForEachLoadedFeature
	// can emit GPU timer zones without pulling in State headers here.
#ifdef TRACY_ENABLE
	inline static TracyD3D11Ctx s_tracyCtx = nullptr;
	static void SetTracyCtx(TracyD3D11Ctx ctx) noexcept { s_tracyCtx = ctx; }
#endif

	template <typename Func>
	static inline void ForEachLoadedFeature(std::string_view methodName, Func&& callback, bool emitGpuZone = false)
	{
		for (auto* feature : GetFeatureList()) {
			if (feature->loaded) {
#ifdef TRACY_ENABLE
				{
					const auto zoneName = std::format("{}::{}", feature->GetShortName(), methodName);
					ZoneTransientN(___tracy_feature_zone, zoneName.c_str(), true);
					if (emitGpuZone) {
						TracyD3D11ZoneTransientS(s_tracyCtx, ___tracy_d3d11_feature_zone, zoneName.c_str(), 0, s_tracyCtx != nullptr);
						callback(feature);
					} else {
						callback(feature);
					}
				}
#else
				callback(feature);
#endif
			}
		}
	}
};
