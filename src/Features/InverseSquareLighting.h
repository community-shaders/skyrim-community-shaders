#pragma once
#include "Features/InverseSquareLighting/LightEditor.h"
#include "LightLimitFix.h"

struct InverseSquareLighting : Feature
{
private:
	static constexpr std::string_view MOD_ID = "153542";

public:
	virtual inline std::string GetName() override { return "Inverse Square Lighting"; }

	virtual inline std::string GetShortName() override { return "InverseSquareLighting"; }

	virtual inline std::string_view GetShaderDefineName() override { return "ISL"; }

	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Implements an additional inverse square falloff for lighting which allows for a more physically accurate and realistic looking light attenuation.",
			{ "Automatic light radius calculation based on intensity",
				"Lights smoothly fade out at a configurable cutoff, solving the infinite distance problem",
				"Does not modify any existing lighting",
				"Requires the use of mods with lights enabled for inverse square falloff.",
				"Full integration with Light Placer",
				"Built in Light Editor for mod authors to preview lighting changes in real-time" }
		};
	}

	inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	virtual void DrawSettings() override;

	virtual void EarlyPrepass() override;

	virtual bool SupportsVR() override { return true; }

	virtual void PostPostLoad() override;

	static float CalculateRadius(float intensity, bool shadowCaster, float cutoffOverride, float size);

	void ProcessLight(LightLimitFix::LightData& light, RE::BSLight* bsLight, RE::NiLight* niLight) const;

	static float GetAttenuation(float distance, float radius, float size);

	struct CreatePointLight
	{
		static RE::NiPointLight* thunk(RE::TESObjectLIGH* ligh, RE::TESObjectREFR* refr, RE::NiAVObject* root, bool forceDynamic, bool useLightRadius, bool affectRequesterOnly);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLight_GetLuminance
	{
		static float thunk(RE::BSLight* bsLight, RE::NiPoint3* targetPosition, RE::NiLight* refLight);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	virtual bool IsCore() const override { return true; };

private:
	LightEditor editor = LightEditor();

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