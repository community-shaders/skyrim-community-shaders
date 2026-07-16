#pragma once

#include "Feature.h"

/**
 * @brief Core feature enabling water rendering beyond the far clip plane for HorizonFix.
 *
 * HorizonFix (an SKSE plugin) fills the horizon gap between the farthest visible water and the
 * sky with a skirt of distant water tiles. Those tiles need shader-side support: the HORIZON_FIX
 * define in Water.hlsl folds beyond-far-plane water back onto the far plane and shades it as
 * bottomless where nothing rendered behind it.
 *
 * This feature's only job is to enable that define while the HorizonFix plugin is installed. It
 * self-disables in PostPostLoad when the plugin is absent, so water keeps exact vanilla
 * far-clip behavior without it - and because that runs before shader cache validation, regular
 * feature validation recompiles the water shaders whenever HorizonFix is installed or removed.
 */
struct HorizonFix : Feature
{
	virtual inline std::string GetName() override { return "Horizon Fix"; }
	virtual inline std::string GetShortName() override { return "HorizonFix"; }
	virtual inline std::string_view GetShaderDefineName() override { return "HORIZON_FIX"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Water; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kWater; }

	/** @brief Returns a summary description for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { "Enables water rendering beyond the far clip plane in support of the HorizonFix plugin, which fills the horizon gap between the farthest visible water and the sky.",
			{ "Active only while the HorizonFix SKSE plugin is installed.",
				"Without HorizonFix, water keeps exact vanilla far clip behavior." } };
	}

	virtual void DrawSettings() override;

	/** @brief Disables the feature when the HorizonFix plugin is not installed. */
	virtual void PostPostLoad() override;

	virtual bool IsCore() const override { return true; }
};
