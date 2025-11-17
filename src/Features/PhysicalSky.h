#pragma once

#include "Buffer.h"
#include "Feature.h"

//////////////////////////////////////////////////////////////////////////

struct WorldspaceInfo
{
	std::string name = "EDITOR ID";
	float bottom_z = -14000.f;
};

struct Orbit
{
	// in rad
	float azimuth = 0;  // rad, 0 being E-W
	float zenith = 0;   // rad, 0 being up
	// in [-1, 1]
	float drift = 7.f * RE::NI_PI / 180.f;  // rad, moving to the sides

	RE::NiPoint3 getDir(float t);  // t = fraction of a cycle, start at the bottom
	RE::NiPoint3 getTangent(float t);
};

struct Trajectory
{
	Orbit minima, maxima;
	// in days
	float period_orbital = 1;  // circling one orbit
	float offset_orbital = 0;  // add to gameDaysPassed
	float period_long = 364;   // from minima to maxima and back
	float offset_long = -10;   // start from the mean of minima and maxima

	Orbit getMixedOrbit(float gameDaysPassed);
	RE::NiPoint3 getDir(float gameDaysPassed);
	RE::NiPoint3 getTangent(float gameDaysPassed);
};

struct CloudLayer
{
	// placement
	float bottom = 0.f;
	float thickness = 2.f;
	// ndf
	float2 ndf_scale_or_freq = { 16.f, 16.f };  // km
	// noise
	float noise_scale_or_freq = 0.2f;     // km^-1
	float3 noise_offset_or_speed{ 0.f };  // moving speed in settings, offset in game

	float power = 1.0f;

	// density
	float3 scatter{ 100.f };
	float3 absorption{ 0.f };

	// visuals
	float average_density = 0.02f;

	float ms_mult = 5.0f;
	float ms_transmittance_power = 0.15f;
	float ms_height_power = 0.7f;

	float ambient_mult = 1.0f;
};

struct CloudLayerSettings
{
	CloudLayer layer;
};

struct TextureManager
{
	ankerl::unordered_dense::map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> tex_list;

	bool LoadTexture(std::filesystem::path path);
	inline ID3D11ShaderResourceView* Query(const std::string& path) const
	{
		if (tex_list.contains(path))
			return tex_list.at(path).get();
		return nullptr;
	}

	inline std::vector<std::string> ListPaths()
	{
		std::vector<std::string> retval;
		std::ranges::transform(tex_list, std::back_inserter(retval), [](auto& pair) { return pair.first; });
		return std::move(retval);
	}
};

struct PhysicalSky : public Feature
{
	constexpr static uint16_t s_transmittance_width = 256;
	constexpr static uint16_t s_transmittance_height = 64;
	constexpr static uint16_t s_multiscatter_width = 32;
	constexpr static uint16_t s_multiscatter_height = 32;
	constexpr static uint16_t s_sky_view_width = 200;
	constexpr static uint16_t s_sky_view_height = 150;
	constexpr static uint16_t s_aerial_perspective_width = 32;
	constexpr static uint16_t s_aerial_perspective_height = 32;
	constexpr static uint16_t s_aerial_perspective_depth = 32;
	constexpr static uint16_t s_shadow_volume_size = 256;
	constexpr static uint16_t s_shadow_volume_height = 64;
	constexpr static uint16_t s_noise_size = 128;  // from nubis

	static PhysicalSky* GetSingleton()
	{
		static PhysicalSky singleton;
		return std::addressof(singleton);
	}

	virtual inline std::string GetName() override { return "Physical Sky"; }
	virtual inline std::string GetShortName() override { return "PhysicalSky"; }
	virtual inline std::string_view GetShaderDefineName() override { return "PHYS_SKY"; }
	virtual bool HasShaderDefine(RE::BSShader::Type) override;

	uint32_t current_moon_phases[2];

	struct Settings
	{
		// GENRERAL
		bool enable_sky = true;

		// PERFORMANCE
		uint transmittance_step = 40;
		uint multiscatter_step = 20;
		uint multiscatter_sqrt_samples = 4;
		uint skyview_step = 30;
		float aerial_perspective_max_dist = 80;  // in km

		float shadow_volume_range = 8;  // in km

		float ray_march_range = 32;  // in km
		uint fog_max_step = 45;
		uint cloud_max_step = 97;

		// WORLD
		float planet_radius = 6.36e3f;  // 6360 km
		float atmos_thickness = 100.f;  // 20 km
		float3 ground_albedo = { .2f, .2f, .2f };

		std::vector<WorldspaceInfo> worldspace_whitelist = {
			{ "Tamriel", -14000.f }
		};

		// LIGHTING
		float3 sunlight_color = float3{ 1.0f, 0.949f, 0.937f } * 6.f;

		float3 moonlight_color = float3{ .9f, .9f, 1.f } * .1f;
		bool phase_dep_moonlight = false;
		float masser_moonlight_min = 0.1f;
		float3 masser_moonlight_color = float3{ 1.f, .7f, .7f } * .01f;
		float secunda_moonlight_min = 0.1f;
		float3 secunda_moonlight_color = float3{ .9f, .9f, 1.f } * .03f;

		float2 light_transition_angles = float2{ -10.f, -14.f } * RE::NI_PI / 180.0;

		bool enable_vanilla_clouds = false;
		float cloud_height = 4.f;  // km
		float cloud_saturation = .7f;
		float cloud_mult = 1.f;
		float cloud_atmos_scatter = 3.f;

		float cloud_phase_g0 = .42f;
		float cloud_phase_g1 = -.3f;
		float cloud_phase_w = .3f;
		float cloud_alpha_heuristics = 0.3f;
		float cloud_color_heuristics = 0.1f;

		bool override_dirlight_color = true;
		bool override_dirlight_dir = true;
		bool moonlight_follows_secunda = true;
		float dirlight_transmittance_mix = 0;

		// CELESTIALS

		struct Celestials
		{
			// - Sun
			float3 sun_disc_color = float3{ 1.f, 0.949f, 0.937f } * 6.f;
			float sun_angular_radius = .5f * RE::NI_PI / 180.0f;  // in rad

			bool override_sun_traj = true;
			Trajectory sun_trajectory{
				.minima = { .zenith = -40 * RE::NI_PI / 180.0f, .drift = -23.5f * RE::NI_PI / 180.0f },
				.maxima = { .zenith = -40 * RE::NI_PI / 180.0f, .drift = 23.5f * RE::NI_PI / 180.0f }
			};

			// - Moons
			bool override_masser_traj = true;
			Trajectory masser_trajectory = {
				.minima = { .zenith = 0, .drift = -.174f },
				.maxima = { .zenith = 0, .drift = -.174f },
				.period_orbital = .25f / 0.23958333333f,  // po3 values :)
				.offset_orbital = 0.7472f
			};
			float masser_angular_radius = 10.f * RE::NI_PI / 180.0f;
			float masser_brightness = .7f;

			bool override_secunda_traj = true;
			Trajectory secunda_trajectory = {
				.minima = { .zenith = 0, .drift = -.423f },
				.maxima = { .zenith = 0, .drift = -.423f },
				.period_orbital = .25f / 0.2375f,
				.offset_orbital = 0.3775f
			};
			float secunda_angular_radius = 4.5f * RE::NI_PI / 180.0f;
			float secunda_brightness = .7f;

			float stars_brightness = 1;
		} celestials;

		// ATMOSPHERE
		float3 rayleigh_scatter = { 6.6049f, 12.345f, 29.413f };  // in megameter^-1
		float3 rayleigh_absorption = { 0.f, 0.f, 0.f };
		float rayleigh_decay = 1 / 8.69645f;  // in km^-1

		float aerosol_phase_func_g = 0.8f;
		float3 aerosol_scatter = { 3.996f, 3.996f, 3.996f };
		float3 aerosol_absorption = { .444f, .444f, .444f };
		float aerosol_decay = 1 / 1.2f;

		float3 ozone_absorption = { 2.2911f, 1.5404f, 0 };
		float ozone_altitude = 22.3499f + 35.66071f * .5f;  // in km
		float ozone_thickness = 35.66071f;

		// OTHER VOLUMETRICS
		float3 fog_scatter = { 1.f, 1.f, 1.f };  // in km^-1
		float3 fog_absorption = { .1f, .1f, .1f };
		float fog_decay = 10.f;
		float fog_bottom = 0.f;
		float fog_thickness = .5f;
		float fog_ambient_mult = 0.5f;

		CloudLayerSettings cloud_layer = {};
	} settings;

	struct PhysSkySB
	{
		uint enable_sky;

		// PERFORMANCE
		uint transmittance_step;
		uint multiscatter_step;
		uint multiscatter_sqrt_samples;
		uint skyview_step;
		float aerial_perspective_max_dist;
		float shadow_volume_range;

		float ray_march_range;
		uint fog_max_step;
		uint cloud_max_step;

		// WORLD
		float bottom_z;
		float planet_radius;
		float atmos_thickness;
		float3 ground_albedo;

		// LIGHTING
		uint override_dirlight_color;
		float dirlight_transmittance_mix;

		uint enable_vanilla_clouds;
		float cloud_height;
		float cloud_saturation;
		float cloud_mult;
		float cloud_atmos_scatter;

		float cloud_phase_g0;
		float cloud_phase_g1;
		float cloud_phase_w;
		float cloud_alpha_heuristics;
		float cloud_color_heuristics;

		// CELESTIAL
		float3 sun_disc_color;
		float sun_aperture_cos;
		float sun_aperture_rcp_sin;

		float masser_aperture_cos;
		float masser_brightness;

		float secunda_aperture_cos;
		float secunda_brightness;

		// ATMOSPHERE
		float3 rayleigh_scatter;
		float3 rayleigh_absorption;
		float rayleigh_decay;

		float aerosol_phase_func_g;
		float3 aerosol_scatter;
		float3 aerosol_absorption;
		float aerosol_decay;

		float3 ozone_absorption;
		float ozone_altitude;
		float ozone_thickness;

		// OTHER VOLUMETRICS
		float3 fog_scatter;
		float3 fog_absorption;
		float fog_decay;
		float fog_h_max;
		float fog_ambient_mult;

		CloudLayer cloud_layer;

		// DYNAMIC
		float2 tex_dim;
		float2 rcp_tex_dim;
		float2 frame_dim;
		float2 rcp_frame_dim;

		float3 dirlight_dir;
		float3 dirlight_color;
		float3 sun_dir;
		float3 masser_dir;
		float3 masser_upvec;
		float3 secunda_dir;
		float3 secunda_upvec;

		float horizon_penumbra;

		float cam_height_planet;

	} phys_sky_sb_data;
	eastl::unique_ptr<StructuredBuffer> phys_sky_sb = nullptr;

	eastl::unique_ptr<Texture2D> transmittance_lut = nullptr;
	eastl::unique_ptr<Texture2D> multiscatter_lut = nullptr;
	eastl::unique_ptr<Texture2D> sky_view_lut = nullptr;
	eastl::unique_ptr<Texture3D> aerial_perspective_lut = nullptr;
	eastl::unique_ptr<Texture2D> main_view_tr_tex = nullptr;
	eastl::unique_ptr<Texture2D> main_view_lum_tex = nullptr;
	eastl::unique_ptr<Texture3D> shadow_volume_tex = nullptr;

	winrt::com_ptr<ID3D11ShaderResourceView> ndf_tex_srv = nullptr;
	winrt::com_ptr<ID3D11ShaderResourceView> cloud_top_lut_srv = nullptr;
	winrt::com_ptr<ID3D11ShaderResourceView> cloud_bottom_lut_srv = nullptr;
	winrt::com_ptr<ID3D11ShaderResourceView> nubis_noise_srv = nullptr;
	void LoadNDFTextures();

	winrt::com_ptr<ID3D11ComputeShader> transmittance_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> multiscatter_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> sky_view_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> aerial_perspective_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> main_view_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> shadow_volume_program = nullptr;

	winrt::com_ptr<ID3D11SamplerState> tileable_sampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> transmittance_sampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> sky_view_sampler = nullptr;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	void CompileComputeShaders();

	inline bool CheckComputeShaders()
	{
		bool result = transmittance_program && multiscatter_program && sky_view_program && aerial_perspective_program && shadow_volume_program;
		return result;
	}

	virtual void Reset() override;
	void UpdateBuffer();
	void UpdateOrbitsAndHeight();

	virtual void DrawSettings() override;
	void SettingsGeneral();
	void SettingsWorldspace();
	void SettingsLighting();
	void SettingsClouds();
	void SettingsCelestials();
	void SettingsAtmosphere();
	void SettingsLayers();
	void SettingsTextures();
	void SettingsDebug();

	virtual void Prepass() override;
	bool GenerateNoise(const std::filesystem::path& filename,
		uint type, float base_freq, uint octaves, float persistence, float lacunarity, uint seed);
	void GenerateLuts();
	void RenderShadowMapMainView();

	virtual inline void RestoreDefaultSettings() override { settings = {}; };
	virtual void ClearShaderCache() override;

	virtual inline void PostPostLoad() override { Hooks::Install(); }

	struct Hooks
	{
		struct NiPoint3_Normalize
		{
			// at Sun::Update
			static void thunk(RE::NiPoint3* a_this);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_thunk_call<NiPoint3_Normalize>(REL::RelocationID(25798, 26352).address() + REL::Relocate(0x6A8, 0x753));
		}
	};
};