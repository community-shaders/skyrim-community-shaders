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
		{ "GrassOptimizations" },
		{ "TerrainVariation" },
		{ "ImageBasedLighting", "DisableInWorldMap" },
		{ "ImageBasedLighting", "DisableInLoadingScreen" },
		{ "PhysicalSky", "enableAllExteriorCells" },
		{ "PhysicalSky", "forceEnableAllInteriorCells" },
		{ "PhysicalSky", "fallbackZBottom" },
		{ "PhysicalSky", "planetRadius" },
		{ "PhysicalSky", "atmosphereRadius" },
		{ "PhysicalSky", "halfResApShadow" },
		{ "PhysicalSky", "aerosolType" },
		{ "PhysicalSky", "aerosolLoading" },
		{ "PhysicalSky", "aerosolHumidity" },
		{ "PhysicalSky", "rayMarchRange" },
		{ "PhysicalSky", "shadowVolumeRange" },
		{ "PhysicalSky", "marchStepScale" },
		{ "PhysicalSky", "cloudNoise" },
		{ "PhysicalSky", "cloudLayer", "cirrus", "uvSize" },
		{ "PhysicalSky", "cloudRelightMix" },
		{ "PhysicalSky", "cloudOriginalMix" },
		{ "PhysicalSky", "silverLiningMix" },
		{ "PhysicalSky", "silverLiningSpread" },
		{ "PhysicalSky", "cloudShadowRemapRange" },
		{ "PhysicalSky", "cloudMap", "type" },
		{ "PhysicalSky", "cloudMap", "localTexelSize" },
		{ "PhysicalSky", "cloudMap", "texture" },
		{ "PhysicalSky", "cloudMap", "procedural", "noise" },
		{ "PhysicalSky", "cloudMap", "procedural", "local" },
		{ "PhysicalSky", "cloudMap", "procedural", "localMaskPath" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "heightFromCoverage" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "localBlendMode" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "primary", "noise" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "secondary", "noise" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "coverageGain", "noise" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "coverageGain", "exponent" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "modeling", "noise" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "modelingGain", "noise" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "modelingGain", "exponent" },
		{ "PhysicalSky", "cloudMap", "procedural", "parameters", "heightVariation", "noise" },
		// Debug visualisation, never a look
		{ "LightLimitFix", "EnableLightsVisualisation" },
		{ "LightLimitFix", "LightsVisualisationMode" },
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
		{ "ScreenSpaceGI", "EnableGI" },
		{ "ScreenSpaceGI", "ResolutionMode" },
		{ "ScreenSpaceGI", "EnableTemporalDenoiser" },
		{ "ScreenSpaceGI", "EnableExperimentalSpecularGI" },
		{ "VolumetricLighting" },
		{ "ViewAtmosphere" },
	};

	inline const std::vector<SettingPolicyPath> kLocationFeatureWhitelist = {
		{ "ExponentialHeightFog" },
		{ "HorizonFix" },
		{ "ImageBasedLighting" },
		{ "PhysicalSky" },
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
		{ "PhysicalSky" },
		{ "PostProcessing" },
		{ "ProceduralSun" },
		{ "ScreenSpaceGI" },
		{ "ScreenSpaceShadows" },
		{ "Skylighting" },
		{ "SubsurfaceScattering" },
		{ "WetnessEffects" },
	};
}
