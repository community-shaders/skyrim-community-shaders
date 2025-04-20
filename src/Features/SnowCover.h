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

	struct UserSettings
	{
		float SnowHeightOffset = 0.0f;
		uint pad[3];
	};
	static_assert(sizeof(UserSettings) % 16 == 0);

	struct WorldSettings
	{
		uint EnableSnowCover = false;
		uint AffectFoliageColor = true;
		float FoliageHeightOffset = -512.0f;
		float UVScale = 1;

		uint MaxSummerMonth = 6;
		uint MaxWinterMonth = 0;
		float SummerHeightOffset = 0.0f;
		float WinterHeightOffset = -10000.0f;

		float Equation[9];
		float PeakMainAngle = 1.0f;
		float PeakAltAngle = 0.0f;
		float MinAngle = 0.0f;

		float MaxAngle = 1.0f;
		uint pad[3];

		//glint
		float ScreenSpaceScale = 1.2f;
		float LogMicrofacetDensity = 33.f;
		float MicrofacetRoughness = .15f;
		float DensityRandomization = 2.f;

		float4 MainTint = float4(1.0f, 1.0f, 1.0f, 1.0f);
		float4 AltTint = float4(1.0f, 1.0f, 1.0f, 1.0f);
	};
	static_assert(sizeof(WorldSettings) % 16 == 0);

	struct alignas(16) PerFrame
	{
		float Month;
		float TimeSnowing;
		float SnowingDensity;
		uint pad;

		UserSettings settings;
		WorldSettings wsettings;
	};
	static_assert(sizeof(PerFrame) % 16 == 0);

	UserSettings settings;
	WorldSettings wsettings;

	PerFrame GetCommonBufferData();

	std::array<ID3D11ShaderResourceView*, 6> views;

	std::string status;
	std::string last_worldspace;
	std::string main_tex;
	std::string alt_tex;
	float snowing_speed;
	float melting_speed;
	char tbuf[256] = "";
	char altbuf[256] = "";

	float lastHour = 12;
	float timeSnowing = 0.0f;
	float snowingDensity = 0.0f;

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
