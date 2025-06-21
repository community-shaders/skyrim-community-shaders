#include "Feature.h"

#include "FeatureIssues.h"
#include "FeatureVersions.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExtendedMaterials.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/IBL.h"
#include "Features/InteriorSunShadows.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
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
#include "Features/WetnessEffects.h"

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
			auto iter = FeatureVersions::FEATURE_MINIMAL_VERSIONS.find(GetShortName());
			if (iter == FeatureVersions::FEATURE_MINIMAL_VERSIONS.end()) {
				hasError = true;
				errorVersion = value;
				errorType = FeatureIssues::FeatureIssueInfo::IssueType::UNKNOWN;
				failedLoadedMessage = std::format("{} {} is an unknown feature not supported by this CS version. This may be a feature from a development branch.", GetShortName(), value);
			} else {
				// Version compatibility check
				auto& minimalFeatureVersion = iter->second;
				bool oldFeature = featureVersion.compare(minimalFeatureVersion) == std::strong_ordering::less;
				bool majorVersionMismatch = featureVersion.major() < minimalFeatureVersion.major();

				if (!oldFeature && !majorVersionMismatch) {
					loaded = true;
					logger::info("{} {} successfully loaded", ini_filename, value);
				} else {
					hasError = true;
					errorVersion = value;
					errorType = FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH;

					std::string minimalVersionString = minimalFeatureVersion.string();
					minimalVersionString = minimalVersionString.substr(0, minimalVersionString.size() - 2);

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
		failedLoadedMessage = std::format("{} missing version info; not successfully loaded", ini_filename);
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
				auto iter = FeatureVersions::FEATURE_MINIMAL_VERSIONS.find(shortName);
				if (iter != FeatureVersions::FEATURE_MINIMAL_VERSIONS.end()) {
					std::string minimalVersionString = iter->second.string();
					// Only remove trailing ".0" if it exists, don't truncate non-zero patch versions
					if (minimalVersionString.ends_with(".0")) {
						minimumVersion = minimalVersionString.substr(0, minimalVersionString.size() - 2);
					} else {
						minimumVersion = minimalVersionString;
					}
				}
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
		globals::features::grassLighting,
		globals::features::grassCollision,
		globals::features::screenSpaceShadows,
		globals::features::extendedMaterials,
		globals::features::wetnessEffects,
		globals::features::lightLimitFix,
		globals::features::dynamicCubemaps,
		globals::features::cloudShadows,
		globals::features::waterEffects,
		globals::features::subsurfaceScattering,
		globals::features::terrainShadows,
		globals::features::screenSpaceGI,
		globals::features::skylighting,
		globals::features::skySync,
		globals::features::terrainBlending,
		globals::features::terrainHelper,
		globals::features::volumetricLighting,
		globals::features::lodBlending,
		globals::features::inverseSquareLighting,
		globals::features::hairSpecular,
		globals::features::interiorSunShadows,
		globals::features::terrainVariation,
		globals::features::ibl
	};

	if (REL::Module::IsVR()) {
		// Helper function to build VR feature list
		static auto BuildVRList = []() -> std::vector<Feature*> {
			auto v = features;
			v.push_back(globals::features::vr);

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