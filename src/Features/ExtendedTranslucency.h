#pragma once

#include "../Buffer.h"
#include "../Feature.h"

struct ExtendedTranslucency final : Feature
{
	virtual inline std::string GetName() override { return "Extended Translucency"; }
	virtual std::string GetDisplayName() override { return T("feature.extended_translucency.name", "Extended Translucency"); }
	virtual inline std::string GetShortName() override { return "ExtendedTranslucency"; }
	virtual inline std::string_view GetShaderDefineName() override { return "EXTENDED_TRANSLUCENCY"sv; }
	virtual inline std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.extended_translucency.description", "Extended Translucency provides realistic rendering of thin fabric and other translucent materials.\nThis feature supports multiple material models for different types of translucent surfaces."),
			{ T("feature.extended_translucency.key_feature_1", "Multiple translucency material models (rim edge, isotropic/anisotropic fabric)"),
				T("feature.extended_translucency.key_feature_2", "Realistic fabric translucency with directional light transmission"),
				T("feature.extended_translucency.key_feature_3", "Per-material override support via NIF extra data"),
				T("feature.extended_translucency.key_feature_4", "Configurable transparency and softness controls"),
				T("feature.extended_translucency.key_feature_5", "Performance-optimized translucency calculations") } };
	}
	/** @brief Returns true only for Lighting shader type. */
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return RE::BSShader::Type::Lighting == shaderType; };
	/** @brief Installs the BSLightingShader geometry setup hooks after all plugins have loaded. */
	virtual void PostPostLoad() override;
	/** @brief Draws the ImGui settings UI for translucency material model and blend options. */
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/**
	 * @brief Sets the ExtraFeatureDescriptor for translucency on the current render pass.
	 *
	 * Reads per-geometry NiExtraData to determine the material model, falling back
	 * to the global default when no override is present.
	 * @param pass The BSRenderPass being set up for rendering.
	 */
	static void BSLightingShader_SetupGeometry(RE::BSRenderPass* pass);

	struct Hooks;

	// TODO: Support more material model like glasses or arcylic
	enum MaterialModel : uint32_t
	{
		Disabled = 0,           // In user settings, 0 means 'Disabled'
		RimLight = 1,           // Similar effect like rim light
		IsotropicFabric = 2,    // 1D fabric model, respect normal map
		AnisotropicFabric = 3,  // 2D fabric model alone tangent and binormal, ignores normal map

		DescriptorUseDefault = 0,  // In ExtraFeatureDescriptor, 0 means 'UseDefault' instead of 'Disabled'
		DescriptorDisabled = 7,    // In ExtraFeatureDescriptor, value >= 5 means 'Disabled'
	};

	static constexpr uint32_t ExtraFeatureDescriptorShift = 6;
	static constexpr uint32_t ExtraFeatureDescriptorMask = 7;

	// Settings in both CPU and GPU constant buffer
	struct alignas(16) PerFrame
	{
		uint32_t AlphaMode = MaterialModel::AnisotropicFabric;
		float AlphaReduction = 0.15f;
		float AlphaSoftness = 0.f;
		float AlphaStrength = 0.f;
	};

	// Settings only in CPU
	struct Settings : PerFrame
	{
		bool SkinnedOnly = true;
	};

	Settings settings;

	/** @brief Returns the per-frame settings data for upload to the GPU constant buffer. */
	const PerFrame& GetCommonBufferData() { return settings; }

	static const RE::BSFixedString NiExtraDataName_AnisotropicAlphaMaterial;

	virtual bool IsCore() const override { return true; };
};
