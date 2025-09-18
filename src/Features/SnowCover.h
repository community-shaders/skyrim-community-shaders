#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/FormIdParser.h"

struct SnowCover : Feature
{
private:
	static constexpr float DEFAULT_FOLIGE_EFFECT_OFFSET = -2048.0; // color foliage earlier/later than snow appears
	static constexpr float DEFAULT_UV_SCALE = 0.5;
	static constexpr float DEFAULT_PEAK_MAIN_ANGLE = 0.45; // material is strongest at this angle
	static constexpr float DEFAULT_PEAK_ALT_ANGLE = 0.9;    // material is strongest at this angle
	static constexpr float DEFAULT_MIN_ANGLE = 0.3;        // lowest angle snow appears at
	static constexpr float DEFAULT_MAX_ANGLE = 0.9;         // angle for full opacity snow
	static constexpr float DEFULAT_MAIN_SPEC = 0.02;       // specular for main material
	static constexpr float DEFULAT_ALT_SPEC = 0.02;         // specular for alt material
	static constexpr float DEFULAT_MAP_ZSCALE = 75000.0f;  // vertical scale of the map of 'altitude offsets'
	static constexpr float DEFULAT_GLINT_1 = 1.2f;	// glint values based on Faultier's snow
	static constexpr float DEFULAT_GLINT_2 = 33.f;
	static constexpr float DEFULAT_GLINT_3 = .15f;
	static constexpr float DEFULAT_GLINT_4 = 2.f;
	static constexpr float DEFAULT_BLEND_SMOOTHNESS = 5000.0f;  // range in game units in which the snow transition gradually happens
	static constexpr float2 DEFAULT_MAP_MIN = float2(-233472.0, 208896.0);  // one corner of skyrim map (where cells end)
	static constexpr float2 DEFAULT_MAP_MAX = float2(253952.0, -176128.0);  // other corner of skyrim map
	static constexpr float DEFAULT_SUMMER_HEIGHT_OFFSET = 20000.0f; // how high snow is in summer (in game units)
	static constexpr float DEFAULT_WINTER_HEIGHT_OFFSET = -20000.0f; // how high snow is in winter (in game units)
	static constexpr uint DEFAULT_PEAK_SUMMER_MONTH = 6;
	static constexpr uint DEFAULT_PEAK_WINTER_MONTH = 0;

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

	struct UserSettings
	{
		uint EnableExpensiveFoliage = 1;
		float SnowHeightOffset = 0.0f;
		uint pad[2];
	};
	static_assert(sizeof(UserSettings) % 16 == 0);

	struct WorldSettings
	{
		uint EnableSnowCover = false;
		uint AffectGrassTint = true;
		uint AffectTreeTint = true;
		float FoliageHeightOffset = DEFAULT_FOLIGE_EFFECT_OFFSET;

		float UVScale = DEFAULT_UV_SCALE;
		float PeakMainAngle = DEFAULT_PEAK_MAIN_ANGLE;
		float PeakAltAngle = DEFAULT_PEAK_ALT_ANGLE;
		float MinAngle = DEFAULT_MIN_ANGLE;

		float MaxAngle = DEFAULT_MAX_ANGLE;
		float MainSpec = DEFULAT_MAIN_SPEC;
		float AltSpec = DEFULAT_ALT_SPEC;
		float mapZscale = DEFULAT_MAP_ZSCALE;

		float2 mapScale;
		float2 mapOffset;

		//glint
		float ScreenSpaceScale = DEFULAT_GLINT_1;
		float LogMicrofacetDensity = DEFULAT_GLINT_2;
		float MicrofacetRoughness = DEFULAT_GLINT_3;
		float DensityRandomization = DEFULAT_GLINT_4;

		float4 MainTint = float4(1.0f, 1.0f, 1.0f, 1.0f);
		float4 AltTint = float4(1.0f, 1.0f, 1.0f, 1.0f);

		float BlendSmoothness = DEFAULT_BLEND_SMOOTHNESS;
		uint pad[3];
	};
	static_assert(sizeof(WorldSettings) % 16 == 0);

	struct alignas(16) PerFrame
	{
		float Month;
		float TimeSnowing;
		float SnowingDensity;
		float SeasonalAltitude;

		UserSettings settings;
		WorldSettings wsettings;
	};
	static_assert(sizeof(PerFrame) % 16 == 0);

	UserSettings settings;
	WorldSettings wsettings;
	PerFrame perFrame;

	PerFrame GetCommonBufferData();

	std::array<ID3D11ShaderResourceView*, 7> views;

	std::string status;
	const char* last_worldspace = nullptr;
	std::filesystem::path map_tex;
	std::filesystem::path main_tex;
	std::filesystem::path alt_tex;
	char mapbuf[256] = "";
	char tbuf[256] = "";
	char altbuf[256] = "";

	float snowing_speed = 0.0f;
	float melting_speed = 0.0f;
	float2 mapMin = DEFAULT_MAP_MIN;
	float2 mapMax = DEFAULT_MAP_MAX;
	uint MaxSummerMonth = DEFAULT_PEAK_SUMMER_MONTH;
	uint MaxWinterMonth = DEFAULT_PEAK_WINTER_MONTH;
	float SummerHeightOffset = DEFAULT_SUMMER_HEIGHT_OFFSET;
	float WinterHeightOffset = DEFAULT_WINTER_HEIGHT_OFFSET;

	float lastHour = 12;
	float timeSnowing = 0.0f;
	float snowingDensity = 0.0f;
	const char* debug_text = nullptr;
	std::unordered_set<std::uint64_t> whitelist;
	std::unordered_set<std::uint64_t> blacklist;

	float GetSeasonalAltitude()
	{
		float maxMonth = static_cast<float>(std::max(MaxSummerMonth, MaxWinterMonth));
		float minMonth = static_cast<float>(std::min(MaxSummerMonth, MaxWinterMonth));
		float summerToWinter;
		auto month = (maxMonth + minMonth) / 2.0f; // fallback value if calendar not exist
		if (auto calendar = RE::Calendar::GetSingleton()) {
			auto time = calendar->GetTime();
			month = static_cast<float>(time.tm_mon + (time.tm_mday + (time.tm_hour + (time.tm_min + time.tm_sec / 60.0) / 60.0) / 24.0) / 32.0);
		}
		if (month > maxMonth) {
			summerToWinter = (month - maxMonth) / (minMonth + 12.0f - maxMonth);
			if (MaxWinterMonth > MaxSummerMonth)
				summerToWinter = 1.0f - summerToWinter;
		} else if (month < minMonth) {
			summerToWinter = (12.0f - maxMonth + month) / (minMonth + 12.0f - maxMonth);
			if (MaxSummerMonth > MaxWinterMonth)
				summerToWinter = 1.0f - summerToWinter;
		} else {
			summerToWinter = (month - minMonth) / (maxMonth - minMonth);
			if (MaxSummerMonth > MaxWinterMonth)
				summerToWinter = 1.0f - summerToWinter;
		}

		return -std::lerp(SummerHeightOffset, WinterHeightOffset, summerToWinter);
	}

	virtual void SetupResources();
	virtual void Reset();
	virtual void Prepass() override;

	virtual void DrawSettings();

	//virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);

	virtual void Load(json& o_json);
	virtual void Save(json& o_json);

	virtual void RestoreDefaultSettings();
	void Reload();
	void SaveConfig();

	virtual inline void PostPostLoad() override { Hooks::Install(); }

	void BSLightingShader_Setup(RE::BSRenderPass* Pass);

	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
			logger::info("[SnowCover] Installed hooks");
		}
	};

	bool SupportsVR() override { return true; };
};
