#include "WetnessEffects/optimized-ggx.hlsli"

namespace WetnessEffects
{
	Texture2D<float4> TexPrecipOcclusion : register(t70);

	// https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
	float2 EnvBRDFApproxWater(float3 F0, float Roughness, float NoV)
	{
		const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
		const float4 c1 = { 1, 0.0425, 1.04, -0.04 };
		float4 r = Roughness * c0 + c1;
		float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
		float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
		return AB;
	}

	// https://github.com/BelmuTM/Noble/blob/master/LICENSE.txt

	float SmoothstepDeriv(float x)
	{
		return 6.0 * x * (1. - x);
	}

	float RainFade(float normalised_t)
	{
		const float rain_stay = .5;

		if (normalised_t < rain_stay)
			return 1.0;

		float val = lerp(1.0, 0.0, (normalised_t - rain_stay) / (1.0 - rain_stay));
		return val * val;
	}

	// https://blog.selfshadow.com/publications/blending-in-detail/
	// geometric normal s, a base normal t and a secondary (or detail) normal u
	float3 ReorientNormal(float3 u, float3 t, float3 s)
	{
		// Build the shortest-arc quaternion
		float4 q = float4(cross(s, t), dot(s, t) + 1) / sqrt(2 * (dot(s, t) + 1));

		// Rotate the normal
		return u * (q.w * q.w - dot(q.xyz, q.xyz)) + 2 * q.xyz * dot(q.xyz, u) + 2 * q.w * cross(q.xyz, u);
	}

	// for when s = (0,0,1)
	float3 ReorientNormal(float3 n1, float3 n2)
	{
		n1 += float3(0, 0, 1);
		n2 *= float3(-1, -1, 1);

		return n1 * dot(n1, n2) / n1.z - n2;
	}

	// xyz - ripple normal, w - splotches
	float4 GetRainDrops(float3 worldPos, float t, float3 normal)
	{
		const static float uintToFloat = rcp(4294967295.0);
		const float rippleBreadthRcp = rcp(SharedData::wetnessEffectsSettings.RippleBreadth);

		float2 gridUV = worldPos.xy * SharedData::wetnessEffectsSettings.RaindropGridSizeRcp;
		gridUV += normal.xy;
		int2 grid = floor(gridUV);
		gridUV -= grid;

		float3 rippleNormal = float3(0, 0, 1);
		float wetness = 0;

		if (SharedData::wetnessEffectsSettings.EnableSplashes || SharedData::wetnessEffectsSettings.EnableRipples) {
			for (int i = -1; i <= 1; i++) {
				for (int j = -1; j <= 1; j++) {
					int2 gridCurr = grid + int2(i, j);
					float tOffset = float(Random::iqint3(gridCurr)) * uintToFloat;

					// splashes
					if (SharedData::wetnessEffectsSettings.EnableSplashes) {
						float residual = t * SharedData::wetnessEffectsSettings.RaindropIntervalRcp / SharedData::wetnessEffectsSettings.SplashesLifetime + tOffset + worldPos.z * 0.001;
						uint timestep = residual;
						residual = residual - timestep;

						uint3 hash = Random::pcg3d(uint3(asuint(gridCurr), timestep));
						float3 floatHash = float3(hash) * uintToFloat;

						if (floatHash.z < (SharedData::wetnessEffectsSettings.RaindropChance)) {
							float2 vec2Centre = int2(i, j) + floatHash.xy - gridUV;
							float distSqr = dot(vec2Centre, vec2Centre);
							float drop_radius = lerp(SharedData::wetnessEffectsSettings.SplashesMinRadius, SharedData::wetnessEffectsSettings.SplashesMaxRadius,
								float(Random::iqint3(hash.yz)) * uintToFloat);
							if (distSqr < drop_radius * drop_radius)
								wetness = max(wetness, RainFade(residual));
						}
					}

					// ripples
					if (SharedData::wetnessEffectsSettings.EnableRipples) {
						float residual = t * SharedData::wetnessEffectsSettings.RaindropIntervalRcp + tOffset + worldPos.z * 0.001;
						uint timestep = residual;
						residual = residual - timestep;

						uint3 hash = Random::pcg3d(uint3(asuint(gridCurr), timestep));
						float3 floatHash = float3(hash) * uintToFloat;

						if (floatHash.z < (SharedData::wetnessEffectsSettings.RaindropChance)) {
							float2 vec2Centre = int2(i, j) + floatHash.xy - gridUV;
							float distSqr = dot(vec2Centre, vec2Centre);
							float rippleT = residual * SharedData::wetnessEffectsSettings.RippleLifetimeRcp;
							if (rippleT < 1.) {
								float ripple_r = lerp(0., SharedData::wetnessEffectsSettings.RippleRadius, rippleT);
								float ripple_inner_radius = ripple_r - SharedData::wetnessEffectsSettings.RippleBreadth;

								float band_lerp = (sqrt(distSqr) - ripple_inner_radius) * rippleBreadthRcp;
								if (band_lerp > 0. && band_lerp < 1.) {
									float deriv = (band_lerp < .5 ? SmoothstepDeriv(band_lerp * 2.) : -SmoothstepDeriv(2. - band_lerp * 2.)) *
									              lerp(SharedData::wetnessEffectsSettings.RippleStrength, 0, rippleT * rippleT);

									float3 grad = float3(normalize(vec2Centre), -deriv);
									float3 bitangent = float3(-grad.y, grad.x, 0);
									float3 normal = normalize(cross(grad, bitangent));

									rippleNormal = ReorientNormal(normal, rippleNormal);
								}
							}
						}
					}
				}
			}
		}

		wetness *= SharedData::wetnessEffectsSettings.SplashesStrength;

		return float4(rippleNormal, wetness);
	}

	float3 GetWetnessAmbientSpecular(float2 uv, float3 N, float3 VN, float3 V, float roughness)
	{
		float3 R = reflect(-V, N);
		float NoV = saturate(dot(N, V));

#if defined(DYNAMIC_CUBEMAPS)
#	if defined(DEFERRED)
		float level = roughness * 7.0;
		float3 specularIrradiance = 1.0;
#	else
		float level = roughness * 7.0;
		float3 specularIrradiance = Color::GammaToLinear(DynamicCubemaps::EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level).rgb);
#	endif
#else
		float3 specularIrradiance = 1.0;
#endif

		float2 specularBRDF = EnvBRDFApproxWater(0.02, roughness, NoV);

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float horizon = min(1.0 + dot(R, VN), 1.0);
		specularIrradiance *= horizon * horizon;

		// Roughness dependent fresnel
		// https://www.jcgt.org/published/0008/01/03/paper.pdf
		float3 Fr = max(1.0.xxx - roughness.xxx, 0.02) - 0.02;
		float3 S = 0.02 + Fr * pow(1.0 - NoV, 5.0);

		return specularIrradiance * (S * specularBRDF.x + specularBRDF.y);
	}

	float3 GetWetnessSpecular(float3 N, float3 L, float3 V, float3 lightColor, float roughness)
	{
		lightColor *= 0.01;
		return LightingFuncGGX_OPT3(N, V, L, roughness, 1.0 - roughness) * lightColor;
	}
}
