#pragma once

#include <string_view>
#include <vector>

namespace SceneSettingsPolicy
{
	/// Catalog address prefix: feature short name, then setting path segments, then the key.
	/// A bare feature name covers everything the feature exposes.
	using SettingPolicyPath = std::vector<std::string_view>;

	/// Settings that must never be scene-overridden, matched by catalog address prefix.
	inline const std::vector<SettingPolicyPath> kSettingBlacklist = {
		{ "ExponentialHeightFog", "volumetricGridPixelSize" },
		{ "ExponentialHeightFog", "volumetricGridSizeZ" },
		{ "ExponentialHeightFog", "volumetricShadowBias" },
		{ "ExponentialHeightFog", "volumetricDepthDistributionScale" },
		{ "ExponentialHeightFog", "volumetricHistoryWeight" },
		{ "ExponentialHeightFog", "volumetricHistoryMissSampleCount" },
		{ "ExponentialHeightFog", "volumetricSampleJitterMultiplier" },
		{ "ExponentialHeightFog", "volumetricUpsampleJitterMultiplier" },
	};

	inline const std::vector<SettingPolicyPath> kLocationFeatureWhitelist = {
		{ "ExponentialHeightFog" },
		{ "ImageBasedLighting" },
		{ "ScreenSpaceGI" },
		{ "SubsurfaceScattering" },
	};

	inline const std::vector<SettingPolicyPath> kTimeOfDayFeatureWhitelist = {
		{ "CloudShadows" },
		{ "ExponentialHeightFog" },
		{ "GrassLighting" },
		{ "ImageBasedLighting" },
		{ "Skylighting" },
		{ "SubsurfaceScattering" },
		{ "WetnessEffects" },
	};
}
