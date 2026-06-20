#pragma once
#include "LightLimitFix.h"

struct InverseSquareLighting : Feature
{
public:
	virtual inline std::string GetName() override { return "Inverse Square Lighting"; }
	virtual std::string GetDisplayName() override { return T("feature.inverse_square_lighting.name", "Inverse Square Lighting"); }

	/** @brief Returns the short identifier used for file paths and logging. */
	virtual inline std::string GetShortName() override { return "InverseSquareLighting"; }

	virtual inline std::string_view GetShaderDefineName() override { return "ISL"; }

	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.inverse_square_lighting.description", "Implements an additional inverse square falloff for lighting which allows for a more physically accurate and realistic looking light attenuation."),
			{ T("feature.inverse_square_lighting.key_feature_1", "Automatic light radius calculation based on intensity"),
				T("feature.inverse_square_lighting.key_feature_2", "Lights smoothly fade out at a configurable cutoff, solving the infinite distance problem"),
				T("feature.inverse_square_lighting.key_feature_3", "Does not modify any existing lighting"),
				T("feature.inverse_square_lighting.key_feature_4", "Requires the use of mods with lights enabled for inverse square falloff."),
				T("feature.inverse_square_lighting.key_feature_5", "Full integration with Light Placer") } };
	}

	inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };


	/** @brief Installs hooks for point light creation and luminance calculation. */
	virtual void PostPostLoad() override;

	/**
	 * @brief Calculates the effective radius of an inverse-square light based on its intensity and cutoff.
	 * @param intensity The light's intensity value.
	 * @param shadowCaster Whether the light casts shadows (uses a tighter cutoff).
	 * @param cutoffOverride Per-light cutoff override from the light form data.
	 * @param size The physical size of the light source.
	 * @return The computed light radius in game units.
	 */
	static float CalculateRadius(float intensity, bool shadowCaster, float cutoffOverride, float size);

	/**
	 * @brief Processes a light's runtime data to populate the LightData struct with inverse-square parameters.
	 * @param light The output light data struct to populate.
	 * @param bsLight The game's BSLight instance.
	 * @param niLight The underlying NiLight with runtime extension data.
	 */
	void ProcessLight(LightLimitFix::LightData& light, RE::BSLight* bsLight, RE::NiLight* niLight) const;

	/**
	 * @brief Computes the inverse-square attenuation at a given distance with smooth fade-out.
	 * @param distance Distance from the light source.
	 * @param radius The effective light radius.
	 * @param size The physical size of the light source.
	 * @return The attenuation factor in [0, 1].
	 */
	static float GetAttenuation(float distance, float radius, float size);

	/** @brief Hook that intercepts point light creation to inject inverse-square light extension data. */
	struct CreatePointLight
	{
		static RE::NiPointLight* thunk(RE::TESObjectLIGH* ligh, RE::TESObjectREFR* refr, RE::NiAVObject* root, bool forceDynamic, bool useLightRadius, bool affectRequesterOnly);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that overrides BSLight luminance calculation to use inverse-square attenuation. */
	struct BSLight_GetLuminance
	{
		static float thunk(RE::BSLight* bsLight, RE::NiPoint3* targetPosition, RE::NiLight* refLight);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	virtual bool IsCore() const override { return true; };

private:
	static constexpr float DefaultCutoff = 0.05f;
	static constexpr float DefaultShadowCasterCutoff = 0.022f;

	static constexpr float Scale = 0.8f;
	static constexpr float MetresToUnits = 70.f;
	static constexpr float MetresToUnitsSq = MetresToUnits * MetresToUnits;
	static constexpr float ScaledUnitsSq = Scale * MetresToUnitsSq;
	static constexpr float FadeZoneBase = 4.5f * Scale * MetresToUnits;

	static void SetExtLightData(RE::NiLight* niLight, const RE::TESObjectLIGH* ligh);

	static inline float SmoothStep(float edge0, float edge1, float x);
};