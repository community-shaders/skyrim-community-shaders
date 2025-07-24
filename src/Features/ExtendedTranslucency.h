#pragma once

#include "../Buffer.h"
#include "../Feature.h"

struct ExtendedTranslucency final : Feature
{
	static ExtendedTranslucency* GetSingleton();

	virtual inline std::string GetName() override { return "Extended Translucency"; }
	virtual inline std::string GetShortName() override { return "ExtendedTranslucency"; }
	virtual inline std::string_view GetShaderDefineName() override { return "EXTENDED_TRANSLUCENCY"; }
	virtual inline std::string_view GetCategory() const override { return "Lighting"; }
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return RE::BSShader::Type::Lighting == shaderType; };
	virtual void PostPostLoad() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual bool SupportsVR() override { return true; };

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;

	// Future proof function for UI refactoring
	std::string GetFeatureDescription() { return "Realistic rendering of thin fabric and other translucent materials"; }  // Feature description for settings page
	std::string GetFeatureModLink() { return "https://www.nexusmods.com/skyrimspecialedition/mods/150755"; }

	static void BSLightingShader_SetupGeometry(RE::BSRenderPass* pass);

	struct Hooks;

	// TODO: Support more material model like glasses or arcylic
	enum MaterialModel : uint32_t
	{
		Disabled = 0,           // In ExtraFeatureDescriptor, this value means 'Default' instead of 'Disabled'
		RimLight = 1,           // Similar effect like rim light
		IsotropicFabric = 2,    // 1D fabric model, respect normal map
		AnisotropicFabric = 3,  // 2D fabric model alone tangent and binormal, ignores normal map
		ForceDisabled = 4,      // In ExtraFeatureDescriptor, value >= 4 means 'Disabled'
	};

	static constexpr uint32_t ExtraFeatureDescriptorShift = 6;
	static constexpr uint32_t ExtraFeatureDescriptorMask = 7;

	struct alignas(16) MaterialParams
	{
		uint32_t AlphaMode = MaterialModel::AnisotropicFabric;
		float AlphaReduction = 0.15f;
		float AlphaSoftness = 0.f;
		float AlphaStrength = 0.f;
	};

	MaterialParams settings;
	bool SkinnedOnly = true;

	static const RE::BSFixedString NiExtraDataName_AnisotropicAlphaMaterial;
};
