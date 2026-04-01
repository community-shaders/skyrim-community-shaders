#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/FormIdParser.h"

struct SnowCover : Feature
{
private:
	static constexpr float DEFAULT_FOLIAGE_EFFECT_OFFSET = -3000.0f;  // color foliage earlier/later than snow appears
	static constexpr float DEFAULT_UV_SCALE = 1.0f;
	static constexpr float DEFAULT_PEAK_MAIN_ANGLE = 0.734f;  // material is strongest at this angle
	static constexpr float DEFAULT_PEAK_ALT_ANGLE = 1.0f;     // material is strongest at this angle
	static constexpr float DEFAULT_MIN_ANGLE = 0.223f;        // lowest angle snow appears at
	static constexpr float DEFAULT_MAX_ANGLE = 0.813f;        // angle for full opacity snow
	static constexpr float DEFAULT_MAIN_SPEC = 0.02;          // specular for main material
	static constexpr float DEFAULT_ALT_SPEC = 0.02;           // specular for alt material
	static constexpr float DEFAULT_MAP_ZSCALE = 75000.0f;     // vertical scale of the map of 'altitude offsets'
	static constexpr float DEFAULT_GLINT_SCREEN_SPACE_SCALE = 1.2f;
	static constexpr float DEFAULT_GLINT_LOG_MICROFACET_DENSITY = 33.f;
	static constexpr float DEFAULT_GLINT_MICROFACET_ROUGHNESS = .15f;
	static constexpr float DEFAULT_GLINT_DENSITY_RANDOMIZATION = 2.f;
	static constexpr float DEFAULT_BLEND_SMOOTHNESS = 5000.0f;              // range in game units in which the snow transition gradually happens
	static constexpr float2 DEFAULT_MAP_MIN = float2(-233472.0, 208896.0);  // one corner of skyrim map (where cells end)
	static constexpr float2 DEFAULT_MAP_MAX = float2(253952.0, -176128.0);  // other corner of skyrim map
	static constexpr float DEFAULT_SUMMER_HEIGHT_OFFSET = 20000.0f;         // how high snow is in summer (in game units)
	static constexpr float DEFAULT_WINTER_HEIGHT_OFFSET = -20000.0f;        // how high snow is in winter (in game units)
	static constexpr uint DEFAULT_PEAK_SUMMER_MONTH = 6;
	static constexpr uint DEFAULT_PEAK_WINTER_MONTH = 0;
	static constexpr float HEIGHT_OFFSET_SLIDER_MIN = -20000.0f;
	static constexpr float HEIGHT_OFFSET_SLIDER_MAX = 20000.0f;

public:
	virtual inline std::string GetName() override { return "Snow Cover"; }
	virtual inline std::string GetShortName() override { return "SnowCover"; }
	inline std::string_view GetShaderDefineName() override { return "SNOW_COVER"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return shaderType == RE::BSShader::Type::Lighting ||
		       shaderType == RE::BSShader::Type::Grass ||
		       shaderType == RE::BSShader::Type::DistantTree;
	}

	struct UserSettings
	{
		uint EnableExpensiveFoliage = 1;
		float SnowHeightOffset = 0.0f;
		uint AffectHavok = 0;
		uint pad;
	};
	static_assert(sizeof(UserSettings) % 16 == 0);

	struct WorldSettings
	{
		uint EnableSnowCover = false;
		uint AffectGrassTint = true;
		uint AffectTreeTint = true;
		float FoliageHeightOffset = DEFAULT_FOLIAGE_EFFECT_OFFSET;

		float UVScale = DEFAULT_UV_SCALE;
		float PeakMainAngle = DEFAULT_PEAK_MAIN_ANGLE;
		float PeakAltAngle = DEFAULT_PEAK_ALT_ANGLE;
		float MinAngle = DEFAULT_MIN_ANGLE;

		float MaxAngle = DEFAULT_MAX_ANGLE;
		float MainSpec = DEFAULT_MAIN_SPEC;
		float AltSpec = DEFAULT_ALT_SPEC;
		float mapZscale = DEFAULT_MAP_ZSCALE;

		float2 mapScale;
		float2 mapOffset;

		//glint
		float ScreenSpaceScale = DEFAULT_GLINT_SCREEN_SPACE_SCALE;
		float LogMicrofacetDensity = DEFAULT_GLINT_LOG_MICROFACET_DENSITY;
		float MicrofacetRoughness = DEFAULT_GLINT_MICROFACET_ROUGHNESS;
		float DensityRandomization = DEFAULT_GLINT_DENSITY_RANDOMIZATION;

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

	struct WorldConfig
	{
		uint AffectGrassTint = true;
		uint AffectTreeTint = true;
		float FoliageHeightOffset = DEFAULT_FOLIAGE_EFFECT_OFFSET;
		float UVScale = DEFAULT_UV_SCALE;
		uint MaxSummerMonth = DEFAULT_PEAK_SUMMER_MONTH;
		uint MaxWinterMonth = DEFAULT_PEAK_WINTER_MONTH;
		float SummerHeightOffset = DEFAULT_SUMMER_HEIGHT_OFFSET;
		float WinterHeightOffset = DEFAULT_WINTER_HEIGHT_OFFSET;
		std::string MapTexture;
		float MapZscale = DEFAULT_MAP_ZSCALE;
		float BlendSmoothness = DEFAULT_BLEND_SMOOTHNESS;
		float ScreenSpaceScale = DEFAULT_GLINT_SCREEN_SPACE_SCALE;
		float LogMicrofacetDensity = DEFAULT_GLINT_LOG_MICROFACET_DENSITY;
		float MicrofacetRoughness = DEFAULT_GLINT_MICROFACET_ROUGHNESS;
		float DensityRandomization = DEFAULT_GLINT_DENSITY_RANDOMIZATION;
		float2 MapMin = DEFAULT_MAP_MIN;
		float2 MapMax = DEFAULT_MAP_MAX;
		std::string MainTexture;
		std::string AltTexture;
		float4 MainTint = float4(1.0f, 1.0f, 1.0f, 1.0f);
		float4 AltTint = float4(1.0f, 1.0f, 1.0f, 1.0f);
		float SnowingSpeed = 0.0f;
		float MeltingSpeed = 0.0f;
		float PeakMainAngle = DEFAULT_PEAK_MAIN_ANGLE;
		float PeakAltAngle = DEFAULT_PEAK_ALT_ANGLE;
		float MinAngle = DEFAULT_MIN_ANGLE;
		float MaxAngle = DEFAULT_MAX_ANGLE;
		float MainSpec = DEFAULT_MAIN_SPEC;
		float AltSpec = DEFAULT_ALT_SPEC;
	};

	WorldConfig ToWorldConfig() const;
	void ApplyWorldConfig(const WorldConfig& wc);

	UserSettings settings;
	WorldSettings wsettings;
	PerFrame perFrame;

	PerFrame GetCommonBufferData();

	std::array<ID3D11ShaderResourceView*, 7> views;

	std::string status;
	std::string last_worldspace;
	std::filesystem::path map_tex;
	std::filesystem::path main_tex;
	std::filesystem::path alt_tex;
	std::string mapbuf;
	std::string tbuf;
	std::string altbuf;

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
	std::unordered_set<std::uint64_t> whitelist;
	std::unordered_set<std::uint64_t> blacklist;

	float GetSeasonalAltitude();

	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void Prepass() override;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;
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
