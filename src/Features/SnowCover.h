#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "State.h"
#include "TruePBR.h"

struct SnowCover : Feature
{
public:
	static SnowCover* GetSingleton()
	{
		static SnowCover singleton;
		return &singleton;
	}

	virtual inline std::string GetName() { return "Snow Cover"; }
	virtual inline std::string GetShortName() { return "SnowCover"; }
	inline std::string_view GetShaderDefineName() override { return "SNOW_COVER"; }

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	struct Settings
	{
		uint EnableSnowCover = true;
		uint AffectFoliageColor = true;
		float SnowHeightOffset = 0.0f;
		float FoliageHeightOffset = -512.0f;

		uint MaxSummerMonth = 6;
		uint MaxWinterMonth = 0;
		float SummerHeightOffset = 0.0f;
		float WinterHeightOffset = -10000.0f;

		float UVScale = 1;
		float ParallaxScale = 0.1f;
		uint pad[2];
		//glint
		float screenSpaceScale = 1.2f;
		float logMicrofacetDensity = 33.f;
		float microfacetRoughness = .15f;
		float densityRandomization = 2.f;
	};
	static_assert(sizeof(Settings) % 16 == 0);

	struct alignas(16) PerFrame
	{
		float Month;
		float TimeSnowing;
		float SnowAmount;
		uint Sky;

		Settings settings;
	};
	static_assert(sizeof(PerFrame) % 16 == 0);

	Settings settings;

	PerFrame GetCommonBufferData();

	std::array<ID3D11ShaderResourceView*, 4> views;

	bool requiresUpdate = true;
	float snowDepth = 0.0f;
	float lastGameTimeValue = 0.0f;
	uint32_t currentWeatherID = 0;
	uint32_t lastWeatherID = 0;
	float previousWeatherTransitionPercentage = 0.0f;

	virtual void SetupResources();
	virtual void Reset();
	virtual void Prepass() override;

	virtual void DrawSettings();

	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);

	virtual void Load(json& o_json);
	virtual void Save(json& o_json);

	virtual void RestoreDefaultSettings();
	float CalculateWeatherTransitionPercentage(float skyCurrentWeatherPct, float beginFade, bool fadeIn);
	void CalculateWetness(RE::TESWeather* weather, RE::Sky* sky, float seconds, float& wetness);

	virtual inline void PostPostLoad() override { Hooks::Install(); }

	struct Hooks
	{
		static void Install()
		{
		}
	};

	bool SupportsVR() override { return true; };
};
