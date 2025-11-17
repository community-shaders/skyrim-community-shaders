#include "../PhysicalSky.h"

#include "PSCommon.h"

#include "State.h"
#include "Util.h"

constexpr float g_game_unit_2_km = 1.428e-5f;
constexpr float g_km_2_game_unit = 1 / g_game_unit_2_km;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WorldspaceInfo, name, bottom_z)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Orbit, azimuth, zenith, drift);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Trajectory,
	minima, maxima,
	period_orbital, offset_orbital,
	period_long, offset_long);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalSky::Settings::Celestials,
	sun_disc_color,
	sun_angular_radius,
	override_sun_traj,
	sun_trajectory,
	override_masser_traj,
	masser_trajectory,
	masser_angular_radius,
	masser_brightness,
	override_secunda_traj,
	secunda_trajectory,
	secunda_angular_radius,
	secunda_brightness,
	stars_brightness)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalSky::Settings,
	enable_sky,
	transmittance_step,
	multiscatter_step,
	multiscatter_sqrt_samples,
	skyview_step,
	aerial_perspective_max_dist,
	planet_radius,
	atmos_thickness,
	ground_albedo,
	worldspace_whitelist,
	enable_vanilla_clouds,
	cloud_height,
	cloud_saturation,
	cloud_mult,
	cloud_phase_g0,
	cloud_phase_g1,
	cloud_phase_w,
	cloud_alpha_heuristics,
	cloud_color_heuristics,
	override_dirlight_color,
	override_dirlight_dir,
	moonlight_follows_secunda,
	dirlight_transmittance_mix,
	sunlight_color,
	moonlight_color,
	phase_dep_moonlight,
	masser_moonlight_min,
	masser_moonlight_color,
	secunda_moonlight_min,
	secunda_moonlight_color,
	light_transition_angles,
	celestials,
	rayleigh_scatter,
	rayleigh_absorption,
	rayleigh_decay,
	aerosol_phase_func_g,
	aerosol_scatter,
	aerosol_absorption,
	aerosol_decay,
	ozone_absorption,
	ozone_altitude,
	ozone_thickness,
	fog_scatter,
	fog_absorption,
	fog_decay,
	fog_bottom,
	fog_thickness)

RE::NiPoint3 Orbit::getDir(float t)
{
	float t_rad = t * 2 * RE::NI_PI;

	RE::NiPoint3 result = { sin(t_rad) * cos(drift), cos(t_rad) * cos(drift), sin(drift) };

	RE::NiMatrix3 rotmat = RE::NiMatrix3(0, 0, azimuth) * RE::NiMatrix3(zenith + .5f * RE::NI_PI, 0, 0);
	result = rotmat * result;

	result.Unitize();

	return result;
}

RE::NiPoint3 Orbit::getTangent(float t)
{
	float t_rad = t * 2 * RE::NI_PI;

	RE::NiPoint3 result = { cos(t_rad), 0, sin(t_rad) };

	RE::NiMatrix3 rotmat;
	rotmat.SetEulerAnglesXYZ(zenith, 0, azimuth);
	result = rotmat * result;

	return result;
}

Orbit Trajectory::getMixedOrbit(float gameDaysPassed)
{
	auto lerp = sin((gameDaysPassed + offset_long) / period_long * 2 * RE::NI_PI) * .5f + .5f;
	Orbit orbit = {
		.azimuth = std::lerp(minima.azimuth, maxima.azimuth, lerp),
		.zenith = std::lerp(minima.zenith, maxima.zenith, lerp),
		.drift = std::lerp(minima.drift, maxima.drift, lerp)
	};
	return orbit;
}

RE::NiPoint3 Trajectory::getDir(float gameDaysPassed)
{
	auto t = (gameDaysPassed + offset_orbital) / period_orbital;
	return getMixedOrbit(gameDaysPassed).getDir(t);
}

RE::NiPoint3 Trajectory::getTangent(float gameDaysPassed)
{
	auto t = (gameDaysPassed + offset_orbital) / period_orbital;
	return getMixedOrbit(gameDaysPassed).getTangent(t);
}

void PhysicalSky::LoadSettings(json& o_json)
{
	settings = o_json;
}

void PhysicalSky::SaveSettings(json& o_json)
{
	o_json = settings;
}

void PhysicalSky::UpdateBuffer()
{
	bool all_set = CheckComputeShaders();

	// check worldspace
	auto worldspace_it = std::ranges::find_if(settings.worldspace_whitelist, [](const auto& info) {
		if (auto tes = RE::TES::GetSingleton(); tes)
			if (auto worldspace = tes->GetRuntimeData2().worldSpace; worldspace)
				return info.name == worldspace->GetFormEditorID();
		return false;
	});
	all_set = worldspace_it != settings.worldspace_whitelist.end();

	float sun_aperture_cos = cos(settings.celestials.sun_angular_radius);
	float sun_aperture_rcp_sin = 1.f / sqrt(1 - sun_aperture_cos * sun_aperture_cos);

	float2 res = State::GetSingleton()->screenSize;
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	float fog_mult = exp(settings.fog_bottom * settings.fog_decay);

	phys_sky_sb_data = {
		.enable_sky = settings.enable_sky && all_set,
		.transmittance_step = settings.transmittance_step,
		.multiscatter_step = settings.multiscatter_step,
		.multiscatter_sqrt_samples = settings.multiscatter_sqrt_samples,
		.skyview_step = settings.skyview_step,
		.aerial_perspective_max_dist = settings.aerial_perspective_max_dist,
		.shadow_volume_range = settings.shadow_volume_range,
		.ray_march_range = settings.ray_march_range,
		.fog_max_step = settings.fog_max_step,
		.cloud_max_step = settings.cloud_max_step,
		.bottom_z = worldspace_it != settings.worldspace_whitelist.end() ? worldspace_it->bottom_z : 0,
		.planet_radius = settings.planet_radius,
		.atmos_thickness = settings.atmos_thickness,
		.ground_albedo = settings.ground_albedo,
		.override_dirlight_color = settings.override_dirlight_color,
		.dirlight_transmittance_mix = settings.override_dirlight_color ? 1.f : settings.dirlight_transmittance_mix,
		.enable_vanilla_clouds = settings.enable_vanilla_clouds,
		.cloud_height = settings.cloud_height,
		.cloud_saturation = settings.cloud_saturation,
		.cloud_mult = settings.cloud_mult,
		.cloud_atmos_scatter = settings.cloud_atmos_scatter,
		.cloud_phase_g0 = settings.cloud_phase_g0,
		.cloud_phase_g1 = settings.cloud_phase_g1,
		.cloud_phase_w = settings.cloud_phase_w,
		.cloud_alpha_heuristics = settings.cloud_alpha_heuristics,
		.cloud_color_heuristics = settings.cloud_color_heuristics,
		.sun_disc_color = settings.celestials.sun_disc_color,
		.sun_aperture_cos = sun_aperture_cos,
		.sun_aperture_rcp_sin = sun_aperture_rcp_sin,
		.masser_aperture_cos = cos(settings.celestials.masser_angular_radius),
		.masser_brightness = settings.celestials.masser_brightness,
		.secunda_aperture_cos = cos(settings.celestials.secunda_angular_radius),
		.secunda_brightness = settings.celestials.secunda_brightness,
		.rayleigh_scatter = settings.rayleigh_scatter * 1e-3f,  // km^-1
		.rayleigh_absorption = settings.rayleigh_absorption * 1e-3f,
		.rayleigh_decay = settings.rayleigh_decay,
		.aerosol_phase_func_g = settings.aerosol_phase_func_g,
		.aerosol_scatter = settings.aerosol_scatter * 1e-3f,
		.aerosol_absorption = settings.aerosol_absorption * 1e-3f,
		.aerosol_decay = settings.aerosol_decay,
		.ozone_absorption = settings.ozone_absorption * 1e-3f,
		.ozone_altitude = settings.ozone_altitude,
		.ozone_thickness = settings.ozone_thickness,
		.fog_scatter = settings.fog_scatter * fog_mult,
		.fog_absorption = settings.fog_absorption * fog_mult,
		.fog_decay = settings.fog_decay,
		.fog_h_max = settings.fog_bottom + settings.fog_thickness,
		.fog_ambient_mult = settings.fog_ambient_mult,
		.cloud_layer = settings.cloud_layer.layer,

		.tex_dim = res,
		.rcp_tex_dim = float2(1.0f) / res,
		.frame_dim = dynres,
		.rcp_frame_dim = float2(1.0f) / dynres,
	};

	// km to game unit
	constexpr auto resizeCloudLayer = [](CloudLayer& cl) {
		cl.bottom *= g_km_2_game_unit;
		cl.thickness *= g_km_2_game_unit;
		cl.ndf_scale_or_freq = float2(1.f / cl.ndf_scale_or_freq.x, 1.f / cl.ndf_scale_or_freq.y) * g_game_unit_2_km;
		cl.noise_scale_or_freq = 1.f / (cl.noise_scale_or_freq * g_km_2_game_unit);
		cl.noise_offset_or_speed *= -State::GetSingleton()->timer * 1e-3f * g_km_2_game_unit;
		cl.scatter *= g_game_unit_2_km;
		cl.absorption *= g_game_unit_2_km;
	};

	phys_sky_sb_data.aerial_perspective_max_dist *= g_km_2_game_unit;
	phys_sky_sb_data.shadow_volume_range *= g_km_2_game_unit;
	phys_sky_sb_data.ray_march_range *= g_km_2_game_unit;
	phys_sky_sb_data.planet_radius *= g_km_2_game_unit;
	phys_sky_sb_data.atmos_thickness *= g_km_2_game_unit;
	phys_sky_sb_data.cloud_height *= g_km_2_game_unit;
	phys_sky_sb_data.ozone_altitude *= g_km_2_game_unit;
	phys_sky_sb_data.ozone_thickness *= g_km_2_game_unit;
	phys_sky_sb_data.fog_h_max *= g_km_2_game_unit;

	phys_sky_sb_data.rayleigh_scatter *= g_game_unit_2_km;
	phys_sky_sb_data.rayleigh_absorption *= g_game_unit_2_km;
	phys_sky_sb_data.rayleigh_decay *= g_game_unit_2_km;
	phys_sky_sb_data.aerosol_scatter *= g_game_unit_2_km;
	phys_sky_sb_data.aerosol_absorption *= g_game_unit_2_km;
	phys_sky_sb_data.aerosol_decay *= g_game_unit_2_km;
	phys_sky_sb_data.ozone_absorption *= g_game_unit_2_km;
	phys_sky_sb_data.fog_scatter *= g_game_unit_2_km;
	phys_sky_sb_data.fog_absorption *= g_game_unit_2_km;
	phys_sky_sb_data.fog_decay *= g_game_unit_2_km;

	resizeCloudLayer(phys_sky_sb_data.cloud_layer);

	// DYNAMIC STUFF
	if (phys_sky_sb_data.enable_sky)
		UpdateOrbitsAndHeight();

	phys_sky_sb->Update(&phys_sky_sb_data, sizeof(phys_sky_sb_data));
}

void PhysicalSky::UpdateOrbitsAndHeight()
{
	// height part
	RE::NiPoint3 cam_pos = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
		cam_pos = cam->cameraRoot->world.translate;
		phys_sky_sb_data.cam_height_planet = (cam_pos.z - phys_sky_sb_data.bottom_z) + settings.planet_radius * g_km_2_game_unit;
	}

	// orbits
	auto sky = RE::Sky::GetSingleton();
	// auto sun = sky->sun;
	auto masser = sky->masser;
	auto secunda = sky->secunda;
	// auto stars = sky->stars;

	RE::NiPoint3 sun_dir;
	auto calendar = RE::Calendar::GetSingleton();
	if (calendar) {
		float game_days = getDayInYear();  // Last Seed = 8
		if (settings.celestials.override_sun_traj) {
			sun_dir = settings.celestials.sun_trajectory.getDir(game_days);
		} else {
			sun_dir = Orbit{}.getDir(getVanillaSunLerpFactor());
		}

		if (settings.celestials.override_masser_traj) {
			auto masser_dir = settings.celestials.masser_trajectory.getDir(game_days);
			auto masser_up = masser_dir.Cross(settings.celestials.masser_trajectory.getTangent(game_days));

			phys_sky_sb_data.masser_dir = { masser_dir.x, masser_dir.y, masser_dir.z };
			phys_sky_sb_data.masser_upvec = { masser_up.x, masser_up.y, masser_up.z };
		} else if (masser) {
			auto masser_dir = masser->moonMesh->world.translate - cam_pos;
			masser_dir.Unitize();
			auto masser_upvec = masser->moonMesh->world.rotate * RE::NiPoint3{ 0, 1, 0 };

			phys_sky_sb_data.masser_dir = { masser_dir.x, masser_dir.y, masser_dir.z };
			phys_sky_sb_data.masser_upvec = { masser_upvec.x, masser_upvec.y, masser_upvec.z };
		}

		if (settings.celestials.override_secunda_traj) {
			auto secunda_dir = settings.celestials.secunda_trajectory.getDir(game_days);
			auto secunda_up = secunda_dir.Cross(settings.celestials.secunda_trajectory.getTangent(game_days));

			phys_sky_sb_data.secunda_dir = { secunda_dir.x, secunda_dir.y, secunda_dir.z };
			phys_sky_sb_data.secunda_upvec = { secunda_up.x, secunda_up.y, secunda_up.z };
		} else if (secunda) {
			auto secunda_dir = secunda->moonMesh->world.translate - cam_pos;
			secunda_dir.Unitize();
			auto secunda_upvec = secunda->moonMesh->world.rotate * RE::NiPoint3{ 0, 1, 0 };

			phys_sky_sb_data.secunda_dir = { secunda_dir.x, secunda_dir.y, secunda_dir.z };
			phys_sky_sb_data.secunda_upvec = { secunda_upvec.x, secunda_upvec.y, secunda_upvec.z };
		}
	}
	phys_sky_sb_data.sun_dir = { sun_dir.x, sun_dir.y, sun_dir.z };

	// sun or moon
	float sun_dir_angle = asin(sun_dir.z);
	float sun_moon_transition = (sun_dir_angle - settings.light_transition_angles.x) / (settings.light_transition_angles.y - settings.light_transition_angles.x);
	if (sun_moon_transition < .5) {
		phys_sky_sb_data.dirlight_color = (1.f - std::clamp(sun_moon_transition * 2.f, 0.f, 1.f)) * settings.sunlight_color;
		phys_sky_sb_data.dirlight_dir = phys_sky_sb_data.sun_dir;
	} else {  // moon
		float3 moonlight = settings.moonlight_color;
		if (settings.phase_dep_moonlight) {
			float masser_phase = current_moon_phases[0] > 4 ? (current_moon_phases[0] - 4) * .25f : current_moon_phases[0] * .25f;
			float3 masser_light = (1 - masser_phase * (1 - settings.masser_moonlight_min)) * settings.masser_moonlight_color;
			float secunda_phase = current_moon_phases[1] > 4 ? (current_moon_phases[1] - 4) * .25f : current_moon_phases[1] * .25f;
			float3 secunda_light = (1 - secunda_phase * (1 - settings.secunda_moonlight_min)) * settings.secunda_moonlight_color;
			moonlight = masser_light + secunda_light;
		}
		phys_sky_sb_data.dirlight_color = std::clamp(sun_moon_transition * 2.f - 1, 0.f, 1.f) * moonlight;

		phys_sky_sb_data.dirlight_dir = settings.moonlight_follows_secunda ? phys_sky_sb_data.secunda_dir : phys_sky_sb_data.masser_dir;
	}

	float sin_sun_horizon = phys_sky_sb_data.dirlight_dir.z;
	float norm_dist = sin_sun_horizon * phys_sky_sb_data.sun_aperture_rcp_sin;
	if (abs(norm_dist) < 1)
		phys_sky_sb_data.horizon_penumbra = .5f + norm_dist * .5f;  // horizon penumbra, not accurate but good enough
	else
		phys_sky_sb_data.horizon_penumbra = sin_sun_horizon > 0;
}

void PhysicalSky::Hooks::NiPoint3_Normalize::thunk(RE::NiPoint3* a_this)
{
	auto feat = GetSingleton();
	if (feat->phys_sky_sb_data.enable_sky && feat->settings.override_dirlight_dir) {
		float3 mod_dirlight_dir = -feat->phys_sky_sb_data.dirlight_dir;
		*a_this = { mod_dirlight_dir.x, mod_dirlight_dir.y, mod_dirlight_dir.z };
	} else
		func(a_this);
}
