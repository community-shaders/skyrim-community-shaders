#include "Common/Color.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "PhysicalSky/gpu_noise_lib.hlsl"

struct CloudLayer
{
	// placement
	float bottom;
	float thickness;
	//ndf
	float2 ndf_freq;
	// noise
	float noise_scale_or_freq;
	float3 noise_offset_or_speed;

	float power;
	// density
	float3 scatter;
	float3 absorption;

	// visual
	float average_density;

	float ms_mult;
	float ms_transmittance_power;
	float ms_height_power;

	float ambient_mult;
};

struct PhySkyBufferContent
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
};

#ifdef SKY_SAMPLERS
SamplerState TileableSampler : register(s2);
SamplerState TransmittanceSampler : register(s3);  // in lighting, use shadow
SamplerState SkyViewSampler : register(s4);        // in lighting, use color
#endif

#if defined(LUTGEN) || defined(PHYS_VOLS)
StructuredBuffer<PhySkyBufferContent> PhysSkyBuffer : register(t0);
Texture2D<float4> TexTransmittance : register(t1);
Texture2D<float4> TexMultiScatter : register(t2);
Texture3D<float4> TexAerialPerspective : register(t3);
#else
StructuredBuffer<PhySkyBufferContent> PhysSkyBuffer : register(t100);
Texture2D<float4> TexTransmittance : register(t101);
Texture2D<float4> TexMultiScatter : register(t102);
Texture2D<float4> TexSkyView : register(t103);
Texture3D<float4> TexAerialPerspective : register(t104);
Texture2D<float4> TexMasser : register(t105);
Texture2D<float4> TexSecunda : register(t106);
Texture3D<float> TexShadowVolume : register(t107);
#endif

#ifndef RCP_PI
#	define RCP_PI 0.3183099
#endif

/*-------- GEOMETRIC --------*/
float3x3 quaternionToMatrix(float4 q)  // https://www.euclideanspace.com/maths/geometry/rotations/conversions/quaternionToMatrix/index.htm
{
	float s = dot(q, q);                                                                                    // just in case, not needed for uniform quaternions
	return 1.0 + 2.0 * transpose(float3x3(                                                                  //
						   float3(-q.y * q.y - q.z * q.z, +q.x * q.y - q.z * q.w, +q.x * q.z + q.y * q.w),  //
						   float3(+q.x * q.y + q.z * q.w, -q.x * q.x - q.z * q.z, +q.y * q.z - q.x * q.w),  //
						   float3(+q.x * q.z - q.y * q.w, +q.y * q.z + q.x * q.w, -q.x * q.x - q.y * q.y)   //
						   )) /
	                 s;
}

float4 randomUnitQuaternion(float3 r)  // r in [0,1] https://stackoverflow.com/questions/31600717/how-to-generate-a-random-quaternion-quickly
{
	return float4(
		sqrt(1.0 - r.x) * sin(2.0 * Math::PI * r.y),
		sqrt(1.0 - r.x) * cos(2.0 * Math::PI * r.y),
		sqrt(r.x) * sin(2.0 * Math::PI * r.z),
		sqrt(r.x) * cos(2.0 * Math::PI * r.z));
}

// return distance to sphere surface
// url: https://viclw17.github.io/2018/07/16/raytracing-ray-sphere-intersection
float rayIntersectSphere(float3 orig, float3 dir, float3 center, float r)
{
	float3 oc = orig - center;
	float b = dot(oc, dir);
	float c = dot(oc, oc) - r * r;
	float discr = b * b - c;
	if (discr < 0.0)
		return -1.0;
	// Special case: inside sphere, use far discriminant
	return (discr > b * b) ? (-b + sqrt(discr)) : (-b - sqrt(discr));
}
float rayIntersectSphere(float3 orig, float3 dir, float r)
{
	return rayIntersectSphere(orig, dir, float3(0, 0, 0), r);
}

float inBetweenSphereDistance(float3 orig, float3 dir, float r_inner, float r_outer)
{
	float inner_dist = rayIntersectSphere(orig, dir, r_inner);
	float outer_dist = rayIntersectSphere(orig, dir, r_outer);
	inner_dist = max(inner_dist, 0);
	outer_dist = max(outer_dist, 0);
	return abs(outer_dist - inner_dist);
}

float inBetweenSphereDistance(float3 orig, float3 dir, float r_inner, float r_outer, float dist_cap)
{
	float inner_dist = rayIntersectSphere(orig, dir, r_inner);
	float outer_dist = rayIntersectSphere(orig, dir, r_outer);
	inner_dist = clamp(inner_dist, 0, dist_cap);
	outer_dist = clamp(outer_dist, 0, dist_cap);
	return abs(outer_dist - inner_dist);
}

// https://gist.github.com/DomNomNom/46bb1ce47f68d255fd5d
// compute the near and far intersections of the cube (stored in the x and y components) using the slab method
// no intersection means vec.x > vec.y (really tNear > tFar)
float2 rayIntersectAABB(float3 ray_origin, float3 ray_dir, float3 box_min, float3 box_max)
{
	float3 t_min = (box_min - ray_origin) / ray_dir;
	float3 t_max = (box_max - ray_origin) / ray_dir;
	float3 t1 = min(t_min, t_max);
	float3 t2 = max(t_min, t_max);
	float t_near = max(max(t1.x, t1.y), t1.z);
	float t_far = min(min(t2.x, t2.y), t2.z);
	return float2(t_near, t_far);
};

float3 getShadowVolumeSampleUvw(float3 pos, float3 ray_dir)
{
	float3 shadow_bounds_min = float3(FrameBuffer::CameraPosAdjust[0].xy - 0.5 * PhysSkyBuffer[0].shadow_volume_range, PhysSkyBuffer[0].cloud_layer.bottom);
	float3 shadow_bounds_max = float3(FrameBuffer::CameraPosAdjust[0].xy + 0.5 * PhysSkyBuffer[0].shadow_volume_range, PhysSkyBuffer[0].cloud_layer.bottom + PhysSkyBuffer[0].cloud_layer.thickness);

	float3 shadow_sample_pos = pos;
	if (any(pos < shadow_bounds_min) || any(pos > shadow_bounds_max)) {
		// outside
		float2 hit_dists = rayIntersectAABB(pos, ray_dir, shadow_bounds_min, shadow_bounds_max);
		if (hit_dists.x > hit_dists.y)
			return -1;
		shadow_sample_pos += (hit_dists.x + 128) * ray_dir;
	}

	float3 shadow_sample_uvw = shadow_sample_pos - float3(FrameBuffer::CameraPosAdjust[0].xy, PhysSkyBuffer[0].cloud_layer.bottom);
	shadow_sample_uvw /= float3(PhysSkyBuffer[0].shadow_volume_range.xx, PhysSkyBuffer[0].cloud_layer.thickness);
	shadow_sample_uvw.xy += 0.5;
	return shadow_sample_uvw;
}

float3 sphericalDir(float azimuth, float zenith)
{
	float cos_zenith, sin_zenith, cos_azimuth, sin_azimuth;
	sincos(zenith, sin_zenith, cos_zenith);
	sincos(azimuth, sin_azimuth, cos_azimuth);
	return float3(sin_zenith * cos_azimuth, sin_zenith * sin_azimuth, cos_zenith);
}

float getHorizonZenithCos(float altitude)
{
	float sin_zenith = PhysSkyBuffer[0].planet_radius / altitude;
	return -sqrt(1 - sin_zenith * sin_zenith);
}

float2 getHeightZenithLutUv(float altitude, float3 up, float3 sun_dir)
{
	float hor_cos_zenith = getHorizonZenithCos(altitude);
	float sun_cos_zenith = dot(sun_dir, up);
	float2 uv = float2(
		saturate((sun_cos_zenith - hor_cos_zenith) / (1 - hor_cos_zenith)),
		saturate((altitude - PhysSkyBuffer[0].planet_radius) / PhysSkyBuffer[0].atmos_thickness));
	return uv;
}

float2 getHeightZenithLutUv(float altitude, float3 sun_dir)
{
	return getHeightZenithLutUv(altitude, float3(0, 0, 1), sun_dir);
}

float2 getHeightZenithLutUv(float3 pos, float3 sun_dir)
{
	float altitude = length(pos);
	float3 up = pos / altitude;
	return getHeightZenithLutUv(altitude, up, sun_dir);
}

float2 cylinderMapAdjusted(float3 ray_dir)
{
	float azimuth = atan2(ray_dir.y, ray_dir.x);
	float u = azimuth * .5 * RCP_PI;  // sampler wraps around so ok
	float zenith = asin(ray_dir.z);
	float v = 0.5 - 0.5 * sign(zenith) * sqrt(abs(zenith) * 2 * RCP_PI);
	v = max(v, 0.01);
	return frac(float2(u, v));
}

float3 invCylinderMapAdjusted(float2 uv)
{
	float azimuth = uv.x * 2 * Math::PI;
	float vm = 1 - 2 * uv.y;
	float zenith = Math::PI * .5 * (1 - sign(vm) * vm * vm);
	return sphericalDir(azimuth, zenith);
}

const static float orthographic_range = 20 / 1.428e-5f;  // km
void orthographicRay(float2 uv, float3 eye, float3 target, float3 up, out float3 orig, out float3 dir)
{
	uv = uv * 2 - 1;

	float3 cw = normalize(target - eye);
	float3 cu = normalize(cross(cw, up));
	float3 cv = normalize(cross(cu, cw));

	orig = eye + (cu * uv.x + cv * uv.y) * orthographic_range;
	dir = cw;
}

void getOrthographicUV(float3 pos, float3 eye, float3 target, float3 up, out float2 uv, out float depth)
{
	float3 cw = normalize(target - eye);
	float3 cu = normalize(cross(cw, up));
	float3 cv = normalize(cross(cu, cw));

	float3 diff = pos - eye;
	uv = float2(dot(diff, cu), dot(diff, cv)) / (orthographic_range * 2) + 0.5;
	depth = dot(diff, cw);
}

void getOrthographicUV(float3 pos, out float2 uv, out float depth)
{
	const float3 target = FrameBuffer::CameraPosAdjust[0].xyz - float3(0, 0, PhysSkyBuffer[0].bottom_z);
	const float3 eye = target + PhysSkyBuffer[0].dirlight_dir * 1000 / 1.428e-5f;
	const float3 up = abs(PhysSkyBuffer[0].dirlight_dir.z) == 1 ? float3(1, 0, 0) : float3(0, 0, 1);
	getOrthographicUV(pos, eye, target, up, uv, depth);
}

/*-------- VOLUMES --------*/
void sampleAtmostphere(
	float altitude,
	out float3 rayleigh_scatter,
	out float3 aerosol_scatter,
	out float3 extinction)
{
	float rayleigh_density = exp(-altitude * PhysSkyBuffer[0].rayleigh_decay);
	rayleigh_scatter = PhysSkyBuffer[0].rayleigh_scatter * rayleigh_density;
	float3 rayleigh_absorp = PhysSkyBuffer[0].rayleigh_absorption * rayleigh_density;

	float aerosol_density = exp(-altitude * PhysSkyBuffer[0].aerosol_decay);
	aerosol_scatter = PhysSkyBuffer[0].aerosol_scatter * aerosol_density;
	float3 aerosol_absorp = PhysSkyBuffer[0].aerosol_absorption * aerosol_density;

	float ozone_density = max(0, 1 - abs(altitude - PhysSkyBuffer[0].ozone_altitude) / (PhysSkyBuffer[0].ozone_thickness * 0.5));
	float3 ozone_absorp = PhysSkyBuffer[0].ozone_absorption * ozone_density;

	extinction = rayleigh_scatter + rayleigh_absorp + aerosol_scatter + aerosol_absorp + ozone_absorp;
}

void sampleExponentialFog(
	float altitude,
	out float3 scatter,
	out float3 extinction)
{
	if (altitude < 0 || altitude > PhysSkyBuffer[0].fog_h_max) {
		scatter = extinction = 0;
	} else {
		float density = exp(-altitude * PhysSkyBuffer[0].fog_decay);
		scatter = PhysSkyBuffer[0].fog_scatter * density;
		float3 absorp = PhysSkyBuffer[0].fog_absorption * density;
		extinction = scatter + absorp;
	}
}

// https://iquilezles.org/articles/fog/
float3 analyticFogTransmittance(float3 start_pos, float3 end_pos)
{
	float3 dir_vec = end_pos - start_pos;
	float dist = length(dir_vec);
	if (dist < 1e-8)
		return 1.0;
	dir_vec /= dist;

	float b = PhysSkyBuffer[0].fog_decay;
	float fog_amount = exp(-start_pos.z * b) * (1.0 - exp(-dist * dir_vec.z * b)) / (b * dir_vec.z);
	return exp(-fog_amount * (PhysSkyBuffer[0].fog_scatter + PhysSkyBuffer[0].fog_absorption));
}

float3 analyticFogTransmittance(float altitude)
{
	if (altitude < PhysSkyBuffer[0].fog_h_max) {
		altitude = clamp(altitude, 0, PhysSkyBuffer[0].fog_h_max);
		float3 pos = float3(0, 0, altitude);
		float3 pos_ceil = pos + clamp(PhysSkyBuffer[0].dirlight_dir * (PhysSkyBuffer[0].fog_h_max - altitude) / PhysSkyBuffer[0].dirlight_dir.z, 0, 10);
		return analyticFogTransmittance(pos, pos_ceil);
	} else
		return 1.0;
}

/*-------- PHASE FUNCS --------*/
float miePhaseHenyeyGreenstein(float cos_theta, float g)
{
	static const float scale = .25 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2);
	float denom = pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}

float miePhaseHenyeyGreensteinDualLobe(float cos_theta, float g_0, float g_1, float w)
{
	return lerp(miePhaseHenyeyGreenstein(cos_theta, g_0), miePhaseHenyeyGreenstein(cos_theta, g_1), w);
}

float miePhaseCornetteShanks(float cos_theta, float g)
{
	static const float scale = .375 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2) * (1.0 + cos_theta * cos_theta);
	float denom = (2.0 + g2) * pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}

float miePhaseDraine(float cos_theta, float g, float alpha)
{
	static const float scale = .25 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2) * (1.0 + alpha * cos_theta * cos_theta);
	float denom = pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5) * (1.0 + alpha * (1.0 + 2.0 * g2) * .333333333333333);

	return scale * num / denom;
}

float miePhaseJendersieDEon(float cos_theta, float d)  // d = particle diameter / um
{
	float g_hg, g_d, alpha_d, w_d;
	if (d >= 5) {
		g_hg = exp(-0.09905670 / (d - 1.67154));
		g_d = exp(-2.20679 / (d + 3.91029) - 0.428934);
		alpha_d = exp(3.62489 - 8.29288 / (d + 5.52825));
		w_d = exp(-0.599085 / (d - 0.641583) - 0.665888);
	} else if (d >= 1.5) {
		float logd = log(d);
		float loglogd = log(logd);
		g_hg = 0.0604931 * loglogd + 0.940256;
		g_d = 0.500411 - 0.081287 / (-2 * logd + tan(logd) + 1.27551);
		alpha_d = 7.30354 * logd + 6.31675;
		w_d = 0.026914 * (logd - cos(5.68947 * (loglogd - 0.0292149))) + 0.376475;
	} else if (d > .1) {
		float logd = log(d);
		g_hg = 0.862 - 0.143 * logd * logd;
		g_d = 0.379685 * cos(1.19692 * cos((logd - 0.238604) * (logd + 1.00667) / (0.507522 - 0.15677 * logd)) + 1.37932 * logd + 0.0625835) + 0.344213;
		alpha_d = 250;
		w_d = 0.146209 * cos(3.38707 * logd + 2.11193) + 0.316072 + 0.0778917 * logd;
	} else {
		g_hg = 13.58 * d * d;
		g_d = 1.1456 * d * sin(9.29044 * d);
		alpha_d = 250;
		w_d = 0.252977 - 312.983 * pow(d, 4.3);
	}
	return lerp(miePhaseHenyeyGreenstein(cos_theta, g_hg), miePhaseDraine(cos_theta, g_d, alpha_d), w_d);
}

// https://www.shadertoy.com/view/tl33Rn
float smoothstep_unchecked(float x) { return (x * x) * (3.0 - x * 2.0); }
float smoothbump(float a, float r, float x) { return 1.0 - smoothstep_unchecked(min(abs(x - a), r) / r); }
float powerful_scurve(float x, float p1, float p2) { return pow(1.0 - pow(1.0 - saturate(x), p2), p1); }
float3 miePhaseCloudFit(float cos_theta)
{
	float x = acos(cos_theta);
	float x2 = max(0., x - 2.45) / (Math::PI - 2.15);
	float x3 = max(0., x - 2.95) / (Math::PI - 2.95);
	float y = (exp(-max(x * 1.5 + 0.0, 0.0) * 30.0)         // front peak
			   + smoothstep(1.7, 0., x) * 0.45 * 0.8        // front ramp
			   + smoothbump(0.4, 0.5, cos_theta) * 0.02     // front bump middle
			   - smoothstep(1., 0.2, x) * 0.06              // front ramp damp wave
			   + smoothbump(2.18, 0.20, x) * 0.06           // first trail wave
			   + smoothstep(2.28, 2.45, x) * 0.18           // trailing piece
			   - powerful_scurve(x2 * 4.0, 3.5, 8.) * 0.04  // trail
			   + x2 * -0.085 + x3 * x3 * 0.1);              // trail peak

	float3 ret = y;
	// spectralize a bit
	ret = lerp(ret, ret + 0.008 * 2., smoothstep(0.94, 1., cos_theta) * sin(x * 10. * float3(8, 4, 2)));
	ret = lerp(ret, ret - 0.008 * 2., smoothbump(-0.7, 0.14, cos_theta) * sin(x * 20. * float3(8, 4, 2)));   // fogbow
	ret = lerp(ret, ret - 0.008 * 5., smoothstep(-0.994, -1., cos_theta) * sin(x * 30. * float3(3, 4, 2)));  // glory

	// scale and offset should be tweaked so integral on sphere is 1
	ret += 0.13 * 1.4;
	return ret * 3.9 * 0.25 * RCP_PI;  // Edit: additonal 1/4pi to be consistent with the rest
}

// numerical fit https://www.shadertoy.com/view/4sjBDG
float miePhaseThomasSchander(float costh)
{
	// This function was optimized to minimize (delta*delta)/reference in order to capture
	// the low intensity behavior.
	float bestParams[10];
	bestParams[0] = 9.805233e-06;
	bestParams[1] = -6.500000e+01;
	bestParams[2] = -5.500000e+01;
	bestParams[3] = 8.194068e-01;
	bestParams[4] = 1.388198e-01;
	bestParams[5] = -8.370334e+01;
	bestParams[6] = 7.810083e+00;
	bestParams[7] = 2.054747e-03;
	bestParams[8] = 2.600563e-02;
	bestParams[9] = -4.552125e-12;

	float p1 = costh + bestParams[3];
	float4 expValues = exp(float4(bestParams[1] * costh + bestParams[2], bestParams[5] * p1 * p1, bestParams[6] * costh, bestParams[9] * costh));
	float4 expValWeight = float4(bestParams[0], bestParams[4], bestParams[7], bestParams[8]);
	return dot(expValues, expValWeight) * 0.25;
}

float rayleighPhase(float cos_theta)
{
	const float k = .1875 * RCP_PI;
	return k * (1.0 + cos_theta * cos_theta);
}

/*-------- SUN LIMB DARKENING --------*/
// url: http://articles.adsabs.harvard.edu/cgi-bin/nph-iarticle_query?1994SoPh..153...91N&defaultprint=YES&filetype=.pdf
float3 limbDarkenNeckel(float norm_dist)
{
	float3 u = 1.0;                          // some models have u !=1
	float3 a = float3(0.397, 0.503, 0.652);  // coefficient for RGB wavelength (680, 550, 440)

	float mu = sqrt(1.0 - norm_dist * norm_dist);

	float3 factor = 1.0 - u * (1.0 - pow(mu, a));
	return factor;
}

// url: http://www.physics.hmc.edu/faculty/esin/a101/limbdarkening.pdf
float3 limbDarkenHestroffer(float norm_dist)
{
	float mu = sqrt(1.0 - norm_dist * norm_dist);

	// coefficient for RGB wavelength (680, 550, 440)
	float3 a0 = float3(0.34685, 0.26073, 0.15248);
	float3 a1 = float3(1.37539, 1.27428, 1.38517);
	float3 a2 = float3(-2.04425, -1.30352, -1.49615);
	float3 a3 = float3(2.70493, 1.47085, 1.99886);
	float3 a4 = float3(-1.94290, -0.96618, -1.48155);
	float3 a5 = float3(0.55999, 0.26384, 0.44119);

	float mu2 = mu * mu;
	float mu3 = mu2 * mu;
	float mu4 = mu2 * mu2;
	float mu5 = mu4 * mu;

	float3 factor = a0 + a1 * mu + a2 * mu2 + a3 * mu3 + a4 * mu4 + a5 * mu5;
	return factor;
}

#if !defined(LUTGEN) && !defined(PHYS_VOLS)
float3 getDirlightTransmittance(float3 world_pos_abs, SamplerState samp)
{
	float3 transmittance = 1.0;

	world_pos_abs.z -= PhysSkyBuffer[0].bottom_z;

	// atmo
	if (PhysSkyBuffer[0].enable_sky && PhysSkyBuffer[0].dirlight_transmittance_mix > 1e-3) {
		float2 lut_uv = getHeightZenithLutUv(PhysSkyBuffer[0].cam_height_planet + world_pos_abs.z, PhysSkyBuffer[0].dirlight_dir);  // ignore height change
		float3 transmit_sample = TexTransmittance.SampleLevel(samp, lut_uv, 0).rgb;
		transmit_sample = lerp(1, transmit_sample, PhysSkyBuffer[0].dirlight_transmittance_mix);
		transmittance *= transmit_sample;
	}

	// fog
	transmittance *= analyticFogTransmittance(world_pos_abs.z);

	// // shadow volume
	// float3 pos_sample_shadow_uvw = getShadowVolumeSampleUvw(world_pos_abs, PhysSkyBuffer[0].dirlight_dir);
	// float cloud_density = 0;
	// if (all(pos_sample_shadow_uvw.xyz > 0))
	// 	cloud_density = TexShadowVolume.SampleLevel(samp, pos_sample_shadow_uvw.xyz, 0);
	// else
	// 	cloud_density += inBetweenSphereDistance(
	// 						 world_pos_abs + float3(-FrameBuffer::CameraPosAdjust[0].xy, PhysSkyBuffer[0].planet_radius),
	// 						 PhysSkyBuffer[0].dirlight_dir,
	// 						 PhysSkyBuffer[0].planet_radius + PhysSkyBuffer[0].cloud_layer.bottom,
	// 						 PhysSkyBuffer[0].planet_radius + PhysSkyBuffer[0].cloud_layer.bottom + PhysSkyBuffer[0].cloud_layer.thickness) *
	// 	                 PhysSkyBuffer[0].cloud_layer.average_density;

	// transmittance *= exp(-(PhysSkyBuffer[0].cloud_layer.scatter + PhysSkyBuffer[0].cloud_layer.absorption) * cloud_density);

	return transmittance;
}
#endif  // LUTGEN

#ifdef SKY_SHADER
void DrawPhysicalSky(inout float4 color, PS_INPUT input)
{
	float3 cam_pos = float3(0, 0, PhysSkyBuffer[0].cam_height_planet);
	float3 view_dir = normalize(input.WorldPosition.xyz);

	float2 transmit_uv = getHeightZenithLutUv(PhysSkyBuffer[0].cam_height_planet, view_dir);
	float2 sky_lut_uv = cylinderMapAdjusted(view_dir);

	// Sky
#	if defined(DITHER) && !defined(TEX)
	color = float4(0, 0, 0, 1);

	bool is_sky = rayIntersectSphere(cam_pos, view_dir, PhysSkyBuffer[0].planet_radius) < 0;
	if (is_sky) {
		// celestials
		float cos_sun_view = clamp(dot(PhysSkyBuffer[0].sun_dir, view_dir), -1, 1);
		float cos_masser_view = clamp(dot(PhysSkyBuffer[0].masser_dir, view_dir), -1, 1);
		float cos_secunda_view = clamp(dot(PhysSkyBuffer[0].secunda_dir, view_dir), -1, 1);

		bool is_sun = cos_sun_view > PhysSkyBuffer[0].sun_aperture_cos;
		bool is_masser = cos_masser_view > PhysSkyBuffer[0].masser_aperture_cos;
		bool is_secunda = cos_secunda_view > PhysSkyBuffer[0].secunda_aperture_cos;

		if (is_sun) {
			color.rgb = PhysSkyBuffer[0].sun_disc_color;

			float tan_sun_view = sqrt(1 - cos_sun_view * cos_sun_view) / cos_sun_view;
			float norm_dist = tan_sun_view * PhysSkyBuffer[0].sun_aperture_cos * PhysSkyBuffer[0].sun_aperture_rcp_sin;
			float3 darken_factor = limbDarkenHestroffer(norm_dist);
			color.rgb *= darken_factor;
		}
		if (is_masser) {
			float3 rightvec = cross(PhysSkyBuffer[0].masser_dir, PhysSkyBuffer[0].masser_upvec);
			float3 disp = normalize(view_dir - PhysSkyBuffer[0].masser_dir);
			float2 uv = normalize(float2(dot(rightvec, disp), dot(-PhysSkyBuffer[0].masser_upvec, disp)));
			uv *= sqrt(1 - cos_masser_view * cos_masser_view) * rsqrt(1 - PhysSkyBuffer[0].masser_aperture_cos * PhysSkyBuffer[0].masser_aperture_cos);  // todo: put it in cpu
			uv = uv * .5 + .5;

			float4 samp = TexMasser.Sample(SampBaseSampler, uv);
			color.rgb = lerp(color.rgb, samp.rgb * PhysSkyBuffer[0].masser_brightness, samp.w);
		}
		if (is_secunda) {
			float3 rightvec = cross(PhysSkyBuffer[0].secunda_dir, PhysSkyBuffer[0].secunda_upvec);
			float3 disp = normalize(view_dir - PhysSkyBuffer[0].secunda_dir);
			float2 uv = normalize(float2(dot(rightvec, disp), dot(-PhysSkyBuffer[0].secunda_upvec, disp)));
			uv *= sqrt(1 - cos_secunda_view * cos_secunda_view) * rsqrt(1 - PhysSkyBuffer[0].secunda_aperture_cos * PhysSkyBuffer[0].secunda_aperture_cos);
			uv = uv * .5 + .5;

			float4 samp = TexSecunda.Sample(SampBaseSampler, uv);
			color.rgb = lerp(color.rgb, samp.rgb * PhysSkyBuffer[0].secunda_brightness, samp.w);
		}
	}

	if (any(color.rgb > 0))
		color.rgb *= TexTransmittance.SampleLevel(TransmittanceSampler, transmit_uv, 0).rgb;
	color.rgb += PhysSkyBuffer[0].dirlight_color * TexSkyView.SampleLevel(SkyViewSampler, sky_lut_uv, 0).rgb;
#	endif

	// Other vanilla meshes
#	if defined(MOONMASK)
	discard;
#	endif

#	if defined(TEX)
#		if defined(CLOUDS)  // cloud
	if (!PhysSkyBuffer[0].enable_vanilla_clouds)
		discard;
	if (PhysSkyBuffer[0].override_dirlight_color) {
		float3 dirLightColor = PhysSkyBuffer[0].dirlight_color;

		float cloud_dist = rayIntersectSphere(cam_pos, view_dir, PhysSkyBuffer[0].cam_height_planet + PhysSkyBuffer[0].cloud_height);  // planetary
		float3 cloud_pos = cam_pos + cloud_dist * view_dir;
		// light transmit
		float2 lut_uv = getHeightZenithLutUv(cloud_pos, PhysSkyBuffer[0].dirlight_dir);
		{
			float3 transmit_sample = TexTransmittance.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb;
			dirLightColor *= transmit_sample;
		}

		// manual adjustment
		dirLightColor = lerp(dot(dirLightColor, float3(0.2125, 0.7154, 0.0721)), dirLightColor, PhysSkyBuffer[0].cloud_saturation) * PhysSkyBuffer[0].cloud_mult;

		// hitting the cloud
		float3 cloud_color = color.rgb;
		color.rgb *= dirLightColor;

		// phase
		float vdl = dot(view_dir, PhysSkyBuffer[0].dirlight_dir);
		// float vdu = dot(view_dir, cloud_pos);
		// float ldu = dot(PhysSkyBuffer[0].dirlight_dir, cloud_pos);

		// thicker cloud = more multiscatter
		// brighter cloud = artist said it refracts more so that it is
		float scatter_factor = saturate(lerp(1, PhysSkyBuffer[0].cloud_alpha_heuristics, color.w)) *
		                       saturate(lerp(PhysSkyBuffer[0].cloud_color_heuristics, 1, dot(cloud_color, float3(0.2125, 0.7154, 0.0721))));
		float scatter_strength = 4 * Math::PI * scatter_factor *
		                         ((miePhaseHenyeyGreensteinDualLobe(vdl, PhysSkyBuffer[0].cloud_phase_g0, -PhysSkyBuffer[0].cloud_phase_g1, PhysSkyBuffer[0].cloud_phase_w) +
									 miePhaseHenyeyGreensteinDualLobe(vdl * .5, PhysSkyBuffer[0].cloud_phase_g0, -PhysSkyBuffer[0].cloud_phase_g1, PhysSkyBuffer[0].cloud_phase_w) * scatter_factor));
		float3 multiscatter_value = 0.f;
		if (PhysSkyBuffer[0].cloud_atmos_scatter)
			multiscatter_value = TexMultiScatter.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb * dirLightColor * PhysSkyBuffer[0].cloud_atmos_scatter;
		color.rgb = (color.rgb + multiscatter_value) * scatter_strength;

		// ap
		uint3 ap_dims;
		TexAerialPerspective.GetDimensions(ap_dims.x, ap_dims.y, ap_dims.z);

		float depth_slice = lerp(.5 / ap_dims.z, 1 - .5 / ap_dims.z, saturate(cloud_dist / PhysSkyBuffer[0].aerial_perspective_max_dist));
		float4 ap_sample = TexAerialPerspective.SampleLevel(SkyViewSampler, float3(cylinderMapAdjusted(view_dir), depth_slice), 0);
		ap_sample.rgb *= PhysSkyBuffer[0].dirlight_color;

		color.rgb = color.rgb * ap_sample.w + ap_sample.rgb;
	}

#		elif defined(DITHER)      //  glare
	discard;
#		elif !defined(HORIZFADE)  // not stars
	discard;
#		endif
#	endif
}
#endif  // SKY_SHADER
