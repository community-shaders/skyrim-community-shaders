#pragma once

#include "Buffer.h"
#include "Feature.h"

/** @brief Replaces the vanilla sun texture with an angular procedural disc. */
struct ProceduralSun : Feature
{
	/** @brief Persisted Procedural Sun settings. */
	struct Settings
	{
		uint enabled = false;
		float sunDiskAngularRadius = DirectX::XMConvertToRadians(0.53f);
		float diskIntensity = 4.8f;
		float edgeSoftness = 0.125f;
		uint haloEnabled = true;
		float haloAngularWidth = DirectX::XMConvertToRadians(4.0f);
		float haloIntensity = 0.4f;
		float haloFalloff = 10.0f;
		float cloudOcclusionStrength = 1.0f;
	};

	/** @brief Per-frame Procedural Sun settings shared with HLSL. */
	struct alignas(16) PerFrameData
	{
		uint enabled;
		float sunDiskCos;
		float diskIntensity;
		float edgeSoftness;

		uint haloEnabled;
		float sunHaloCos;
		float haloIntensity;
		float haloFalloff;

		float cloudOcclusionStrength;
		float sunQuadModelRadius = 0.0f;
		float pad[2] = {};
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrameData);
	static_assert(sizeof(PerFrameData) == 48);

	Settings settings;

	/** @brief Returns the stable feature name. */
	virtual inline std::string GetName() override { return "Procedural Sun"; }
	/** @brief Returns the localized feature name. */
	virtual std::string GetDisplayName() override { return T("feature.procedural_sun.name", "Procedural Sun"); }
	/** @brief Returns the shader configuration name. */
	virtual inline std::string GetShortName() override { return "ProceduralSun"; }
	/** @brief Returns the menu category. */
	virtual std::string_view GetCategory() const override { return FeatureCategories::kSky; }
	/** @brief Returns the shader permutation define. */
	virtual inline std::string_view GetShaderDefineName() override { return "PROCEDURAL_SUN"; }
	/** @brief Enables the feature define for sky shaders. */
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Sky; }
	/** @brief Returns the localized feature summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.procedural_sun.description", "Replaces the vanilla sun texture with an angular procedural disc while preserving vanilla weather tint and glare."),
			{ T("feature.procedural_sun.key_feature_1", "Angular sun disc with spectral limb darkening"),
				T("feature.procedural_sun.key_feature_2", "Vanilla sun position, weather tint, and glare"),
				T("feature.procedural_sun.key_feature_3", "Independent of atmospheric lookup tables and Physical Sky") }
		};
	}

	/** @brief Draws Procedural Sun settings. */
	virtual void DrawSettings() override;
	/** @brief Loads and validates Procedural Sun settings. */
	virtual void LoadSettings(json& o_json) override;
	/** @brief Saves Procedural Sun settings. */
	virtual void SaveSettings(json& o_json) override;
	/** @brief Restores default Procedural Sun settings. */
	virtual void RestoreDefaultSettings() override;

	/** @brief Returns true while the Effects 11 procedural sun is enabled, which then replaces this feature's sun. */
	bool IsManagedByENB() const;

	/** @brief Returns the settings uploaded to the shared feature buffer. */
	PerFrameData GetCommonBufferData() const;
};
