#include "Feature.h"

#include "FeatureIssues.h"
#include "FeatureVersions.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/IBL.h"
#include "Features/InteriorSun.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/PerformanceOverlay.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/SkySync.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/TerrainShadows.h"
#include "Features/TerrainVariation.h"
#include "Features/VR.h"
#include "Features/VolumetricLighting.h"
#include "Features/WaterEffects.h"
#include "Features/WeatherPicker.h"
#include "Features/WetnessEffects.h"
#include "Features/ENBPostProcessing.h"

#include "Menu.h"
#include "SettingsOverrideManager.h"
#include "Utils/Format.h"

#include "State.h"

void Feature::Load(json& o_json)
{
	// Convert string to wstring
	auto ini_filename = std::format("{}.ini", GetShortName());
	std::wstring ini_filename_w;
	std::ranges::copy(ini_filename, std::back_inserter(ini_filename_w));
	auto ini_path = L"Data\\Shaders\\Features\\" + ini_filename_w;

	CSimpleIniA ini;
	ini.SetUnicode();
	SI_Error rc = ini.LoadFile(ini_path.c_str());

	if (rc < 0) {
		if (!FeatureIssues::IsObsoleteFeature(GetShortName()))
			logger::info("{} failed to load, feature disabled", ini_filename);
		loaded = false;
		return;
	}

	bool hasError = false;
	std::string errorVersion;
	FeatureIssues::FeatureIssueInfo::IssueType errorType = FeatureIssues::FeatureIssueInfo::IssueType::UNKNOWN;

	if (FeatureIssues::IsObsoleteFeature(GetShortName())) {
		hasError = true;
		errorVersion = "N/A";
		errorType = FeatureIssues::FeatureIssueInfo::IssueType::OBSOLETE;
		failedLoadedMessage = std::format("{} is an obsolete feature that has been removed", GetShortName());
	} else if (auto value = ini.GetValue("Info", "Version")) {
		try {
			REL::Version featureVersion(std::regex_replace(value, std::regex("-"), "."));

			// Check if feature exists in minimal versions
			REL::Version minimalFeatureVersion;
			if (!Feature::IsFeatureKnown(GetShortName(), &minimalFeatureVersion)) {
				hasError = true;
				errorVersion = value;
				errorType = FeatureIssues::FeatureIssueInfo::IssueType::UNKNOWN;
				failedLoadedMessage = std::format("{} {} is an unknown feature not supported by this CS version. This may be a feature from a development branch.", GetShortName(), value);
			} else {
				// Version compatibility check
				bool oldFeature = featureVersion.compare(minimalFeatureVersion) == std::strong_ordering::less;
				bool majorVersionMismatch = featureVersion.major() < minimalFeatureVersion.major();

				if (!oldFeature && !majorVersionMismatch) {
					loaded = true;
					logger::info("{} {} successfully loaded", ini_filename, value);
				} else {
					hasError = true;
					errorVersion = value;
					errorType = FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH;

					std::string minimalVersionString = Util::GetFormattedVersion(minimalFeatureVersion);

					if (majorVersionMismatch) {
						failedLoadedMessage = std::format("{} {} is too old, major version incompatibility detected. Required: {}", GetShortName(), value, minimalVersionString);
					} else {
						failedLoadedMessage = std::format("{} {} is an old feature version, required: {}", GetShortName(), value, minimalVersionString);
					}
				}
			}

			version = value;
		} catch (const std::exception& e) {
			hasError = true;
			errorVersion = value;
			errorType = FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH;
			failedLoadedMessage = std::format("{} {} has invalid version format: {}", GetShortName(), value, e.what());
		}
	} else {
		hasError = true;
		errorVersion = "unknown";
		errorType = FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH;

		// Get the minimum required version to include in the error message
		std::string requiredVersion = Feature::GetFeatureRequiredVersion(GetShortName());

		failedLoadedMessage = std::format("The {} file is missing. This feature is not installed! Version required: {}", ini_filename, requiredVersion);
	}

	if (hasError) {
		loaded = false;
		logger::warn("{}", failedLoadedMessage);

		// Guard against empty shortName to prevent bogus filesystem access
		std::string shortName = GetShortName();
		if (!shortName.empty()) {
			FeatureIssues::FeatureFileInfo fileInfo = FeatureIssues::GetFeatureFileInfo(shortName);

			// For version mismatch, also pass the minimum required version
			std::string minimumVersion;
			if (errorType == FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH) {
				minimumVersion = Feature::GetFeatureRequiredVersion(shortName);
			}

			FeatureIssues::AddFeatureIssue(shortName, errorVersion, failedLoadedMessage, errorType, fileInfo, minimumVersion);

		} else {
			logger::error("Feature has empty short name, cannot add to feature issues list");
		}
	} else {
		// No errors, load settings now
		if (o_json[GetName()].is_structured()) {
			logger::info("Loading {} settings", GetName());
			try {
				LoadSettings(o_json[GetName()]);
			} catch (...) {
				logger::warn("Invalid settings for {}, using default.", GetName());
				RestoreDefaultSettings();
			}
		} else {
			logger::info("Loading default settings for {}", GetName());
			RestoreDefaultSettings();
		}
	}
}

void Feature::Save(json& o_json)
{
	SaveSettings(o_json[GetName()]);
}

bool Feature::ValidateCache(CSimpleIniA& a_ini)
{
	auto name = GetName();
	auto ini_name = GetShortName();

	logger::info("Validating {}", name);

	auto enabledInCache = a_ini.GetBoolValue(ini_name.c_str(), "Enabled", false);
	if (enabledInCache && !loaded) {
		logger::info("Feature was uninstalled");
		return false;
	}
	if (!enabledInCache && loaded) {
		logger::info("Feature was installed");
		return false;
	}

	if (loaded) {
		auto versionInCache = a_ini.GetValue(ini_name.c_str(), "Version");
		if (strcmp(versionInCache, version.c_str()) != 0) {
			logger::info("Change in version detected. Installed {} but {} in Disk Cache", version, versionInCache);
			return false;
		} else {
			logger::info("Installed version and cached version match.");
		}
	}

	logger::info("Cached feature is valid");
	return true;
}

void Feature::WriteDiskCacheInfo(CSimpleIniA& a_ini)
{
	auto ini_name = GetShortName();
	a_ini.SetBoolValue(ini_name.c_str(), "Enabled", loaded);
	a_ini.SetValue(ini_name.c_str(), "Version", version.c_str());
}

const std::vector<Feature*>& Feature::GetFeatureList()
{
	static std::vector<Feature*> features = {
		&globals::features::grassLighting,
		&globals::features::grassCollision,
		&globals::features::screenSpaceShadows,
		&globals::features::extendedMaterials,
		&globals::features::wetnessEffects,
		&globals::features::lightLimitFix,
		&globals::features::dynamicCubemaps,
		&globals::features::cloudShadows,
		&globals::features::waterEffects,
		&globals::features::weatherPicker,
		&globals::features::performanceOverlay,
		&globals::features::subsurfaceScattering,
		&globals::features::terrainShadows,
		&globals::features::screenSpaceGI,
		&globals::features::skylighting,
		&globals::features::skySync,
		&globals::features::terrainBlending,
		&globals::features::terrainHelper,
		&globals::features::volumetricLighting,
		&globals::features::lodBlending,
		&globals::features::inverseSquareLighting,
		&globals::features::hairSpecular,
		&globals::features::interiorSun,
		&globals::features::terrainVariation,
		&globals::features::ibl,
		&globals::features::extendedTranslucency,
		&globals::features::enbPostProcessing
	};

	if (REL::Module::IsVR()) {
		// Helper function to build VR feature list
		static auto BuildVRList = []() -> std::vector<Feature*> {
			auto v = features;
			v.push_back(&globals::features::vr);

			// In developer mode, keep all features for testing
			// In production mode, filter to VR-compatible only
			if (!globals::state->IsDeveloperMode()) {
				std::erase_if(v, [](Feature* a) { return !a->SupportsVR(); });
			}
			return v;
		};

		// Cache the VR feature list but invalidate when developer mode changes
		static std::vector<Feature*> featuresVR;
		static bool cachedDevMode = false;

		bool currentDevMode = globals::state->IsDeveloperMode();
		if (featuresVR.empty() || currentDevMode != cachedDevMode) {
			featuresVR = BuildVRList();
			cachedDevMode = currentDevMode;
		}

		return featuresVR;
	} else {
		return features;
	}
}

bool Feature::ToggleAtBootSetting()
{
	auto state = globals::state;
	const std::string featureName = GetShortName();
	auto disabled = state->IsFeatureDisabled(featureName);
	state->SetFeatureDisabled(featureName, !disabled);

	return state->IsFeatureDisabled(featureName);  // Return the new state
}

bool Feature::ReapplyOverrideSettings()
{
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	if (!overrideManager || !overrideManager->HasFeatureOverrides(GetShortName())) {
		return false;
	}

	// Get current settings as JSON
	json featureJson;
	SaveSettings(featureJson);

	// Apply overrides to the current settings
	size_t appliedCount = overrideManager->ReapplyFeatureOverrides(GetShortName(), featureJson);

	if (appliedCount > 0) {
		// Load the modified settings back into the feature
		LoadSettings(featureJson);
		return true;
	}

	return false;
}

void Feature::DrawUnloadedUI()
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
	std::string requiredVersion = Feature::GetFeatureRequiredVersion(GetShortName());

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

std::string Feature::GetFeatureRequiredVersion(const std::string& shortName)
{
	if (shortName.empty()) {
		return "unknown";
	}
	auto iter = FeatureVersions::FEATURE_MINIMAL_VERSIONS.find(shortName);
	if (iter != FeatureVersions::FEATURE_MINIMAL_VERSIONS.end()) {
		return Util::GetFormattedVersion(iter->second);
	}

	return "unknown";
}

bool Feature::IsFeatureKnown(const std::string& shortName, REL::Version* outVersion)
{
	if (shortName.empty()) {
		return false;
	}

	auto iter = FeatureVersions::FEATURE_MINIMAL_VERSIONS.find(shortName);
	if (iter != FeatureVersions::FEATURE_MINIMAL_VERSIONS.end()) {
		if (outVersion) {
			*outVersion = iter->second;
		}
		return true;
	}

	return false;
}