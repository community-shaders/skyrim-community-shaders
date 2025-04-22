#pragma once
#include "Features/InverseSquareLighting/LightEditor.h"
#include "LightLimitFix.h"

struct InverseSquareLighting : Feature
{
	static InverseSquareLighting* GetSingleton()
	{
		static InverseSquareLighting singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Inverse Square Lighting"; }

	virtual inline std::string GetShortName() override { return "InverseSquareLighting"; }

	virtual inline std::string_view GetShaderDefineName() override { return "ISL"; }

	inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	virtual void DrawSettings() override;

	virtual void EarlyPrepass() override;

	virtual bool SupportsVR() override { return true; }

	virtual void PostPostLoad() override;

	static float CalculateRadius(float intensity, bool shadowCaster, float cutoffOverride);

	void ProcessLight(LightLimitFix::LightData& light, RE::BSLight* bsLight, RE::NiLight* niLight) const;

	static float GetAttenuation(float distance, float radius);

	struct CreatePointLight
	{
		static RE::NiPointLight* thunk(RE::TESObjectLIGH* ligh, RE::TESObjectREFR* refr, RE::NiAVObject* root, bool forceDynamic, bool useLightRadius, bool affectRequesterOnly);
		static inline REL::Relocation<decltype(thunk)> func;
	};

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