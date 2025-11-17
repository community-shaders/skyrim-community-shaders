#define SKY_SAMPLERS
#include "PhysicalSky/PhysicalSky.hlsli"

#if LUTGEN == 3
RWTexture3D<float4> RWTexOutput : register(u0);
#else
RWTexture2D<float4> RWTexOutput : register(u0);
#endif

void rayMarch(
	float3 pos, float3 ray_dir, float3 sun_dir,
#if LUTGEN == 0
	inout float3 transmittance
#elif LUTGEN == 1
	inout float3 transmittance,
	inout float3 lum, inout float3 lum_factor
#elif LUTGEN == 2
	inout float3 transmittance,
	inout float3 lum
#elif LUTGEN == 3
	uint2 tid, uint depth,
	inout float3 transmittance,
	inout float3 lum
#endif
)
{
#if LUTGEN == 0
	const uint nsteps = PhysSkyBuffer[0].transmittance_step;
#elif LUTGEN == 1
	const uint nsteps = PhysSkyBuffer[0].multiscatter_step;
#elif LUTGEN == 2
	const uint nsteps = PhysSkyBuffer[0].skyview_step;
#else
	const uint nsteps = depth - 1;
#endif

	float ground_dist = rayIntersectSphere(pos, ray_dir, PhysSkyBuffer[0].planet_radius);
#if LUTGEN == 0
	if (ground_dist > 0.0) {
		transmittance = 0;
		return;
	}
#endif

	float atmos_dist = rayIntersectSphere(pos, ray_dir, PhysSkyBuffer[0].planet_radius + PhysSkyBuffer[0].atmos_thickness);
#if LUTGEN == 3
	float t_max = PhysSkyBuffer[0].aerial_perspective_max_dist;
#else
	float t_max = ground_dist > 0 ? ground_dist : atmos_dist;
#endif
	float dt = t_max / float(nsteps);
	float3 v_step = dt * ray_dir;

#if LUTGEN != 0
	float cos_theta = dot(ray_dir, sun_dir);
	float aerosol_phase = miePhaseCornetteShanks(cos_theta, PhysSkyBuffer[0].aerosol_phase_func_g);
	float rayleigh_phase = rayleighPhase(cos_theta);
#endif

	float3 curr_pos = pos;
	[loop] for (uint i = 0; i < nsteps; ++i)
	{
		curr_pos += v_step;

		float3 rayleigh_scatter, aerosol_scatter, extinction;
		sampleAtmostphere(
			max(0, (length(curr_pos) - PhysSkyBuffer[0].planet_radius)),
			rayleigh_scatter, aerosol_scatter, extinction);

		float3 sample_transmittance = exp(-dt * extinction);

#if LUTGEN != 0
		float3 scatter_factor = (1 - sample_transmittance) / extinction;

		float3 scatter_no_phase = rayleigh_scatter + aerosol_scatter;
#	if LUTGEN == 1  // multiscatter
		float3 scatter_f = scatter_no_phase * scatter_factor;
		lum_factor += transmittance * scatter_f;
#	endif

		float2 lut_uv = getHeightZenithLutUv(curr_pos, sun_dir);
		float3 sun_transmittance = TexTransmittance.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb;
#	if LUTGEN != 1
		float3 psi_ms = TexMultiScatter.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb;
#	endif

		float3 rayleigh_inscatter = rayleigh_scatter * rayleigh_phase;
		float3 aerosol_inscatter = aerosol_scatter * aerosol_phase;
		float3 in_scatter = (rayleigh_inscatter + aerosol_inscatter) * sun_transmittance;
#	if LUTGEN != 1
		in_scatter += scatter_no_phase * psi_ms;
#	endif

		float3 scatter_integeral = in_scatter * scatter_factor;

		lum += scatter_integeral * transmittance;
#endif
		transmittance *= sample_transmittance;

#if LUTGEN == 3
		RWTexOutput[uint3(tid.xy, i + 1)] = float4(lum, dot(transmittance, float3(0.2126, 0.7152, 0.0722)));
#endif
	}

#if LUTGEN == 1  // multiscatter
	if (ground_dist > 0) {
		float3 hit_pos = pos + ground_dist * ray_dir;
		if (dot(pos, sun_dir) > 0) {
			hit_pos = normalize(hit_pos) * PhysSkyBuffer[0].planet_radius;
			float2 lut_uv = getHeightZenithLutUv(hit_pos, sun_dir);
			lum += transmittance * PhysSkyBuffer[0].ground_albedo * TexTransmittance.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb;
		}
	}
#endif
}

[numthreads(32, 32, 1)] void main(uint3 tid
								  : SV_DispatchThreadID) {
#if LUTGEN == 3
	RWTexOutput[uint3(tid.xy, 0)] = float4(0, 0, 0, 1);
#endif

	uint3 out_dims;
#if LUTGEN == 3
	RWTexOutput.GetDimensions(out_dims.x, out_dims.y, out_dims.z);
#else
	RWTexOutput.GetDimensions(out_dims.x, out_dims.y);
#endif
	float2 uv = (tid.xy + 0.5) / out_dims.xy;

#if LUTGEN < 2
	float altitude = PhysSkyBuffer[0].planet_radius + PhysSkyBuffer[0].atmos_thickness * uv.y;
	float3 pos = float3(0, 0, altitude);

	float hor_cos_zenith = getHorizonZenithCos(altitude);
	float cos_zenith = lerp(hor_cos_zenith, 1, uv.x);
	float3 sun_dir = float3(0, sqrt(1 - cos_zenith * cos_zenith), cos_zenith);
#else
	float3 ray_dir = invCylinderMapAdjusted(uv);
	float3 sun_dir = PhysSkyBuffer[0].dirlight_dir;
	float3 pos = float3(0, 0, PhysSkyBuffer[0].cam_height_planet);
#endif

	float3 transmittance = 1.0;
#if LUTGEN == 0
	rayMarch(pos, sun_dir, sun_dir, transmittance);
	RWTexOutput[tid.xy] = float4(transmittance, 1.0);

#elif LUTGEN == 1
	const uint sqrt_samples = PhysSkyBuffer[0].multiscatter_sqrt_samples;
	const float rcp_sqrt_samples = rcp(sqrt_samples);
	const float rcp_samples = rcp_sqrt_samples * rcp_sqrt_samples;

	float3 lum_total = 0;
	float3 f_ms = 0;
	for (uint i = 0; i < sqrt_samples; ++i)
		for (uint j = 0; j < sqrt_samples; ++j) {
			float theta = (i + 0.5) * Math::PI * rcp_sqrt_samples;
			float phi = acos(1.0 - 2.0 * (j + 0.5) * rcp_sqrt_samples);
			float3 ray_dir = sphericalDir(theta, phi);

			transmittance = 1;
			float3 lum = 0;
			float3 lum_factor = 0;
			rayMarch(pos, ray_dir, sun_dir, transmittance, lum, lum_factor);

			f_ms += lum_factor;
			lum_total += lum;
		}
	RWTexOutput[tid.xy] = float4(lum_total * rcp_samples / (1 - f_ms * rcp_samples), 1.0);

#elif LUTGEN == 2
	float3 lum = 0;
	rayMarch(pos, ray_dir, sun_dir, transmittance, lum);
	RWTexOutput[tid.xy] = float4(lum, 1.0);

#elif LUTGEN == 3
	float3 lum = 0;
	rayMarch(pos, ray_dir, sun_dir, tid.xy, out_dims.z, transmittance, lum);
#endif
}