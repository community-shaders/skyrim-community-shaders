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
		{ "ExponentialHeightFog", "volumetricFarGridPixelSize" },
		{ "ExponentialHeightFog", "volumetricFarGridSizeZ" },
		{ "ExponentialHeightFog", "volumetricShadowBias" },
		{ "ExponentialHeightFog", "volumetricDepthDistributionScale" },
		{ "ExponentialHeightFog", "volumetricHistoryWeight" },
		{ "ExponentialHeightFog", "volumetricHistoryMissSampleCount" },
		{ "ExponentialHeightFog", "volumetricSampleJitterMultiplier" },
		{ "ExponentialHeightFog", "volumetricUpsampleJitterMultiplier" },
		{ "GrassOptimizations" },
		{ "TerrainVariation" },
		{ "ImageBasedLighting", "DisableInWorldMap" },
		{ "ImageBasedLighting", "DisableInLoadingScreen" },
		{ "PostProcessing", "Border" },
		// Auto-enabled each frame from its inputs, so a scene value would never be retained.
		{ "PostProcessing", "Composite" },
		{ "PostProcessing", "Depth of Field", "HighlightShape" },
		{ "PostProcessing", "Color Grading and Tone Mapping", "enabled" },
		{ "PostProcessing", "Color Grading and Tone Mapping", "enableTonemap" },
		{ "PostProcessing", "Color Grading and Tone Mapping", "useOpenDrt" },
		{ "PostProcessing", "Color Grading and Tone Mapping", "currentTonemapper" },
		{ "PostProcessing", "Color Grading and Tone Mapping", "tonemapParams" },
		{ "PostProcessing", "Motion Blur", "VelocityScale" },
		// Shader defines: a change recompiles every SSGI compute shader on the render thread.
		{ "ScreenSpaceGI", "ResolutionMode" },
		{ "ScreenSpaceGI", "EnableGI" },
		{ "ScreenSpaceGI", "EnableTemporalDenoiser" },
		{ "ScreenSpaceGI", "EnableExperimentalSpecularGI" },
		{ "VolumetricLighting" },
	};

	inline const std::vector<SettingPolicyPath> kLocationFeatureWhitelist = {
		{ "ExponentialHeightFog" },
		{ "HorizonFix" },
		{ "ImageBasedLighting" },
		{ "PostProcessing" },
		{ "ScreenSpaceGI" },
		{ "ScreenSpaceShadows" },
		{ "SubsurfaceScattering" },
	};

	inline const std::vector<SettingPolicyPath> kTimeOfDayFeatureWhitelist = {
		{ "CloudShadows" },
		{ "ExponentialHeightFog" },
		{ "GrassLighting" },
		{ "ImageBasedLighting" },
		{ "PostProcessing" },
		{ "ScreenSpaceGI" },
		{ "ScreenSpaceShadows" },
		{ "Skylighting" },
		{ "SubsurfaceScattering" },
		{ "WetnessEffects" },
	};
}
