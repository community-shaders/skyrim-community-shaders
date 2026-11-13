#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#include "Common/Color.hlsli"
#include "Common/ColorManagement.hlsli"
#include "Common/Game.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/ViewAtmosphere.hlsli"
#include "PhysicalSky/ShadowVolume.hlsli"

#if defined(CLOUD_SHADOWS)
#	include "CloudShadows/CloudShadows.hlsli"
#endif

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#endif

#ifndef OMIT_PS_NAMESPACE
namespace PhysSky
{
#endif

#ifdef PS_PREPASS_SAMPLERS
	SamplerState SampTr : register(s0);  // in lighting, use shadow
	SamplerState SampSv : register(s1);  // in lighting, use color
	SamplerState SampNoise : register(s2);
#endif

#ifdef PS_SKY_SAMPLERS
	SamplerState SampTr : register(s3);
	SamplerState SampSv : register(s4);
#endif

#ifdef PS_DEFERRED_SAMPLERS
	SamplerState SampSv : register(s1);
#endif

#ifdef PS_PREPASS_RSRCS
#	ifndef PS_NO_RSRCS
	Texture2D<float4> TexTrLut : register(t0);
	Texture2D<float4> TexMsLut : register(t1);
	Texture2D<float4> TexSvLut : register(t2);
	Texture3D<float4> TexApLut : register(t3);
#	endif
#elif defined(PS_DEFERRED_RSRCS)
Texture3D<float4> TexApLut : register(t16);
Texture2D<unorm float> TexApShadow : register(t17);
#else
Texture2D<float4> TexTrLut : register(t61);
Texture2D<float4> TexSvLut : register(t62);
Texture3D<float4> TexApLut : register(t63);
Texture2D<unorm float> TexApShadow : register(t64);
Texture2D<float4> TexSkyTail : register(t66);
#endif

	static const float RCP_PI = 1 / Math::PI;  // PI
	static const float AP_MAX_DIST = 40000.0f / GAME_UNIT_TO_M;
	static const uint3 CLOUD_DIM = uint3(512, 512, 64);
	static const float3 CLOUD_RANGE = float3(4000.f, 4000.f, 500.f) / GAME_UNIT_TO_M;
	static const float3 CLOUD_RANGE_M = float3(4.f, 4.f, .5f) * 1e3;

	// Concentrate AP slices near the camera, where scattering changes fastest.
	float ApSliceDistance(float slice, float sliceCount)
	{
		const float t = slice / (sliceCount - 1.0);
		return AP_MAX_DIST * t * t;
	}

	float ApDepthUv(float dist, uint sliceCount)
	{
		const float clampedDist = clamp(dist, 0.0, AP_MAX_DIST);
		const float slice = min(floor(sqrt(clampedDist / AP_MAX_DIST) * (sliceCount - 1.0)), sliceCount - 2.0);
		const float nearDist = ApSliceDistance(slice, sliceCount);
		const float farDist = ApSliceDistance(slice + 1.0, sliceCount);
		// Interpolate in physical distance, not sqrt(distance), including the zero-distance slice.
		const float weight = saturate((clampedDist - nearDist) / (farDist - nearDist));
		return (slice + weight + 0.5) / sliceCount;
	}

#ifndef ISNAN
#	define ISNAN(x) (!(x < 0.f || x > 0.f || x == 0.f))
#endif

	float Remap(float x, float in_a, float in_b, float out_a, float out_b)
	{
		if (in_a == in_b)
			return out_a;
		return lerp(out_a, out_b, saturate((x - in_a) / (in_b - in_a)));
	}

	float3 PosWs2Planet(float3 posWorld)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;
		return posWorld - float3(FrameBuffer::CameraPosAdjust.xy, data.zBottom - data.rPlanet);
	}

	// return distance to sphere surface
	// url: https://viclw17.github.io/2018/07/16/raytracing-ray-sphere-intersection
	float RayIntersectSphere(float3 orig, float3 dir, float3 center, float r)
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

	// Near and far distance to a planet-centred sphere (planet centre at the origin).
	// Returns (-1, -1) when the ray misses. Requires a normalized direction.
	float2 IntersectSpherePair(float3 origin, float3 dir, float radius)
	{
		const float b = dot(origin, dir);
		const float discriminant = b * b - dot(origin, origin) + radius * radius;
		if (discriminant < 0.0)
			return -1.0;
		const float root = sqrt(discriminant);
		return float2(-b - root, -b + root);
	}

	float3 SphericalDir(float azimuth, float zenith)
	{
		float cosZenith, sinZenith, cosAzimuth, sinAzimuth;
		sincos(zenith, sinZenith, cosZenith);
		sincos(azimuth, sinAzimuth, cosAzimuth);
		return float3(sinZenith * cosAzimuth, sinZenith * sinAzimuth, cosZenith);
	}

	float HorizonZenithCos(float r)
	{
		float sinZenith = SharedData::physSkyData.rPlanet / r;
		return -sqrt(1 - sinZenith * sinZenith);
	}

	// Multiple scattering retains its original affine (height, solar zenith) map.
	float2 MsLutUv(float r, float cosSunZenith)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;
		// float cosHorZenith = HorizonZenithCos(r);
		const float cosHorZenith = -0.414;
		float2 uv = float2(
			saturate((cosSunZenith - cosHorZenith) / (1 - cosHorZenith)),
			saturate((r - data.rPlanet) / (data.rAtmosphere - data.rPlanet)));
		// Match PhysicalSky::kMsLutW/H (32x32), not the transmittance LUT.
		// The reflection sampler wraps azimuth: keep its linear filter footprint
		// inside this non-periodic LUT so night never blends with the midday edge.
		uv = clamp(uv, 0.5 / 32.0, 1.0 - 0.5 / 32.0);
		return uv;
	}

	// Bruneton's transmittance parameterization resolves low altitudes and long
	// grazing light paths without increasing the 256x64 texture dimensions.
	// https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html
	float2 TrLutUv(float r, float cosSunZenith)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;
		r = clamp(r, data.rPlanet, data.rAtmosphere);
		const float atmosphereHorizon = sqrt((data.rAtmosphere - data.rPlanet) * (data.rAtmosphere + data.rPlanet));
		const float localHorizon = sqrt(max(0.0, (r - data.rPlanet) * (r + data.rPlanet)));
		const float mu = max(cosSunZenith, -localHorizon / r);
		const float rMu = r * mu;
		const float distance = max(0.0, sqrt(max(0.0, rMu * rMu + (data.rAtmosphere - r) * (data.rAtmosphere + r))) - rMu);
		const float nearest = data.rAtmosphere - r;
		const float farthest = localHorizon + atmosphereHorizon;
		const float2 unitUv = saturate(float2((distance - nearest) / max(farthest - nearest, 1.0), localHorizon / atmosphereHorizon));
		const float2 texel = 1.0 / float2(256, 64);
		return 0.5 * texel + unitUv * (1.0 - texel);
	}

	void TrLutParameters(float2 uv, out float r, out float mu)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;
		const float2 texel = 1.0 / float2(256, 64);
		const float2 unitUv = saturate((uv - 0.5 * texel) / (1.0 - texel));
		const float horizonSquared = (data.rAtmosphere - data.rPlanet) * (data.rAtmosphere + data.rPlanet);
		const float localHorizon = sqrt(horizonSquared) * unitUv.y;
		r = sqrt(data.rPlanet * data.rPlanet + localHorizon * localHorizon);
		r = clamp(r, data.rPlanet, data.rAtmosphere);
		const float distance = lerp(data.rAtmosphere - r, localHorizon + sqrt(horizonSquared), unitUv.x);
		mu = distance > 0.0 ? (horizonSquared - localHorizon * localHorizon - distance * distance) / (2.0 * r * distance) : 1.0;
		mu = clamp(mu, -localHorizon / r, 1.0);
	}

	float3 SampleAtmosphereLightTr(Texture2D<float4> transmittance, SamplerState samp, float3 planetPos, float3 lightDir)
	{
		const float radius = length(planetPos);
		const float sinHorizon = saturate(SharedData::physSkyData.rPlanet / max(radius, 1.0));
		const float cosHorizon = -sqrt(max(0.0, 1.0 - sinHorizon * sinHorizon));
		const float mu = dot(planetPos, lightDir) / max(radius, 1.0);
		// Approximate the visible fraction of a solar disc (0.266 degree radius).
		// Keep planet visibility separate from the smooth, unoccluded LUT.
		const float discWidth = max(0.00465 * sinHorizon, 1e-6);
		const float visibility = smoothstep(-discWidth, discWidth, mu - cosHorizon);
		[branch] if (visibility <= 0.0 || radius < SharedData::physSkyData.rPlanet) return 0.0;
		return transmittance.SampleLevel(samp, TrLutUv(radius, mu), 0).rgb * visibility;
	}

	float2 TrLutUvPlanet(float3 pos, float3 sunDir)
	{
		float r = length(pos);
		float3 up = pos / r;
		return TrLutUv(r, dot(sunDir, up));
	}

	float SkyViewHorizonZenith()
	{
		const float radius = SharedData::physSkyData.rPlanet;
		const float viewRadius = max(SharedData::physSkyData.zCameraPlanet, radius);
		const float horizonDistance = sqrt(max(0.0, (viewRadius - radius) * (viewRadius + radius)));
		return acos(clamp(-horizonDistance / viewRadius, -1.0, 0.0));
	}

	float2 SkyViewLutUv(float3 rayDir, uint lutHeight)
	{
		const float u = atan2(rayDir.y, rayDir.x) * 0.5 * RCP_PI;
		const float zenith = acos(clamp(rayDir.z, -1.0, 1.0));
		const float horizon = SkyViewHorizonZenith();
		const float v = zenith <= horizon ?
		                    0.5 * (1.0 - sqrt(saturate(1.0 - zenith / horizon))) :
		                    0.5 + 0.5 * sqrt(saturate((zenith - horizon) / (Math::PI - horizon)));
		return float2(frac(u), (0.5 + v * (lutHeight - 1.0)) / lutHeight);
	}

	// url: http://www.physics.hmc.edu/faculty/esin/a101/limbdarkening.pdf
	float3 LimbDarkenHestroffer(float norm_dist)
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

	float3 InvSkyViewLutUv(float2 uv, uint lutHeight)
	{
		const float azimuth = uv.x * 2.0 * Math::PI;
		const float v = saturate((uv.y * lutHeight - 0.5) / (lutHeight - 1.0));
		const float horizon = SkyViewHorizonZenith();
		const float vm = 1.0 - 2.0 * v;
		const float zenith = v <= 0.5 ? horizon * (1.0 - vm * vm) :
		                                horizon + (Math::PI - horizon) * vm * vm;
		return SphericalDir(azimuth, zenith);
	}

	void SampleAtmosphere(
		float altitude,
		out float rayleighDensity,
		out float aerosolDensity,
		out float ozoneDensity)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;
		rayleighDensity = exp(-altitude * data.rayleighFalloff);
		aerosolDensity = exp(-altitude * data.aerosolFalloff);
		ozoneDensity = max(0.f, 1 - abs(altitude - data.ozoneAltitude) / (data.ozoneThickness * 0.5f));
	}

	namespace Phase
	{
		float HG(float cos_theta, float g)
		{
			static const float scale = .25 * RCP_PI;
			const float g2 = g * g;

			float num = (1.0 - g2);
			float denom = pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5);

			return scale * num / denom;
		}

		float HGDualLobe(float cos_theta, float g_0, float g_1, float w)
		{
			return lerp(HG(cos_theta, g_0), HG(cos_theta, g_1), w);
		}

		float CornetteShanks(float cos_theta, float g)
		{
			static const float scale = .375 * RCP_PI;
			const float g2 = g * g;

			float num = (1.0 - g2) * (1.0 + cos_theta * cos_theta);
			float denom = (2.0 + g2) * pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5);

			return scale * num / denom;
		}

		float Draine(float cos_theta, float g, float alpha)
		{
			static const float scale = .25 * RCP_PI;
			const float g2 = g * g;

			float num = (1.0 - g2) * (1.0 + alpha * cos_theta * cos_theta);
			float denom = pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5) * (1.0 + alpha * (1.0 + 2.0 * g2) * .333333333333333);

			return scale * num / denom;
		}

		// https://www.shadertoy.com/view/tl33Rn
		float smoothstep_unchecked(float x) { return (x * x) * (3.0 - x * 2.0); }
		float smoothbump(float a, float r, float x) { return 1.0 - smoothstep_unchecked(min(abs(x - a), r) / r); }
		float powerful_scurve(float x, float p1, float p2) { return pow(1.0 - pow(1.0 - saturate(x), p2), p1); }
		float3 CloudFit(float cos_theta)
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
		float ThomasSchander(float costh)
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

		float Rayleigh(float cos_theta)
		{
			const float k = .1875 * RCP_PI;
			return k * (1.0 + cos_theta * cos_theta);
		}
	}

#ifndef PS_PREPASS_RSRCS

	float GetApShadow(uint2 pxCoord)
	{
		if (ViewAtmosphere::Enabled())
			return 0.0;
		return TexApShadow[pxCoord];
	}

#	ifndef PS_DEFERRED_RSRCS
	float3 SampleSky(float3 viewDir, float shadow, SamplerState sampSv)
	{
		uint2 skyDims;
		TexSvLut.GetDimensions(skyDims.x, skyDims.y);
		const float2 skyLutUv = SkyViewLutUv(viewDir, skyDims.y);
		float3 skyColor = TexSvLut.SampleLevel(sampSv, skyLutUv, 0).rgb;

		skyColor *= 1 - shadow;

		return skyColor;
	}

	float3 SampleSky(float3 viewDir, uint2 pxCoord, SamplerState sampSv)
	{
		return SampleSky(viewDir, GetApShadow(pxCoord), sampSv);
	}

	float3 SampleViewSky(float3 viewDir, float shadow, SamplerState samp)
	{
		if (!ViewAtmosphere::Enabled())
			return SampleSky(viewDir, shadow, samp);
		uint width, height;
		TexSkyTail.GetDimensions(width, height);
		return TexSkyTail.SampleLevel(samp, SkyViewLutUv(viewDir, height), 0).rgb;
	}

	float3 SampleTr(float3 lightDir, SamplerState sampSv)
	{
		SharedData::PhysSkyData data = SharedData::physSkyData;

		if (data.trMix < 1e-8)
			return 1;

		float3 tr = SampleAtmosphereLightTr(TexTrLut, sampSv, float3(0, 0, data.zCameraPlanet), lightDir);
		tr = lerp(1, tr, data.trMix);

		return tr;
	}

	float3 SampleViewCelestialTr(float3 direction, SamplerState samp)
	{
		if (ViewAtmosphere::ReflectionEnabled()) {
			const ViewAtmosphere::Medium medium = ViewAtmosphere::SampleReflection(SkyViewLutUv(direction, 32u), AP_MAX_DIST, samp);
			const float3 origin = float3(0, 0, max(SharedData::physSkyData.zCameraPlanet, SharedData::physSkyData.rPlanet));
			return medium.transmittance * lerp(1.0, SampleAtmosphereLightTr(TexTrLut, samp, origin + direction * AP_MAX_DIST, direction), SharedData::physSkyData.trMix);
		}
		if (!ViewAtmosphere::Enabled())
			return SampleTr(direction, samp);
		const SharedData::PhysSkyData data = SharedData::physSkyData;
		const float3 origin = float3(0, 0, max(data.zCameraPlanet, data.rPlanet));
		return lerp(1.0, SampleAtmosphereLightTr(TexTrLut, samp, origin + direction * AP_MAX_DIST, direction), data.trMix);
	}

#		if defined(CLOUD_SHADOWS) && defined(PS_SKY_SAMPLERS)
	float3 RelightCloud(float4 baseColor, float3 viewDir, float3 cloudPosWS, SamplerState sampTr, SamplerState sampCube)
	{
		if (baseColor.w <= 0)
			return baseColor.rgb;

		SharedData::PhysSkyData data = SharedData::physSkyData;

		// TODO: use proper light Dir
		float3 dirLightDir = SharedData::DirLightDirection.xyz;
		float3 dirLightColor = SharedData::DirLightColor;

		// TODO: planet shadowing

		float3 trAtmos = SampleAtmosphereLightTr(TexTrLut, sampTr, cloudPosWS + float3(0, 0, data.zCameraPlanet), dirLightDir);
		trAtmos = lerp(1, trAtmos, data.trMix);
		dirLightColor *= trAtmos;

		float u = dot(viewDir, dirLightDir);
		float phaseCloud = Remap(
							   baseColor.a,
							   data.silverLiningSpread > 0 ? data.silverLiningSpread : 0,
							   data.silverLiningSpread < 0 ? 1 + data.silverLiningSpread : 1,
							   lerp(0.25 * RCP_PI, Phase::ThomasSchander(u), data.silverLiningMix),
							   0.25 * RCP_PI) *
		                   2 * Math::PI * data.cloudRelightMix;

		float3 cloudColor = baseColor.rgb * data.cloudOriginalMix;

		if (baseColor.a > 0.0) {
			float rayStep = 1.0 / 32.0;
			float rayPos = rayStep * 0.5;
			float4 rayShadow = 0.0;

			float3 PoissonDisc[] = {
				float3(0.460921f, 0.615192f, 0.887539f),
				float3(0.757347f, 0.911008f, 0.189581f),
				float3(0.548753f, 0.145482f, 0.0548723f),
				float3(0.90051f, 0.157048f, 0.623493f)
			};

			[unroll] for (int i = 0; i < 4; i++)
			{
				float3 raySample = normalize(lerp(viewDir, SharedData::DirLightDirection.xyz, rayPos));

				raySample += (PoissonDisc[i] * 2.0 - 1.0) * 0.01;

				if (raySample.z < 0.0)
					rayShadow[i % 4] += -raySample.z;  // World shadow
				else
					rayShadow[i % 4] = max(rayShadow[i % 4], CloudShadows::CloudSelfShadowTexture.SampleLevel(sampCube, raySample, 0).x);

				rayPos += rayStep;
			}

			float directVisibility = 1.0 - saturate(dot(rayShadow, 0.25));

			float cloudOpticalDepth = -log(max(1.0 - baseColor.a, 1e-3));
			float forwardG = lerp(0.82, 0.94, saturate(1.0 - abs(data.silverLiningSpread)));
			float forwardMieCore = max(0.0, Phase::HG(u, forwardG) - 0.25 * RCP_PI);
			float forwardMieAureole = max(0.0, Phase::Draine(u, 0.78, 2.0) - 0.25 * RCP_PI);
			float forwardMiePhase = forwardMieCore + 0.45 * forwardMieAureole;
			float silverEdgeMask = smoothstep(0.08, 0.35, baseColor.a) * (1.0 - smoothstep(0.45, 0.85, baseColor.a));
			float silverSingleScatter = 1.35 * silverEdgeMask * (1.0 - exp(-cloudOpticalDepth)) * exp(-0.5 * cloudOpticalDepth);
			float directSingleScatter = 0.9 * cloudOpticalDepth * exp(-0.75 * cloudOpticalDepth);
			cloudColor += baseColor.xyz * dirLightColor * forwardMiePhase * silverSingleScatter * directVisibility * data.silverLiningMix * data.cloudRelightMix;

#			if defined(IBL)
			if (SharedData::iblSettings.EnableIBL) {
				float3 cloudAmbientDir = normalize(-viewDir);
				float skyVisibility = saturate(cloudAmbientDir.z * 0.5 + 0.5) * exp(-cloudOpticalDepth);
				float3 vanillaAmbient = Color::Ambient(max(0.0, SharedData::GetAmbient(cloudAmbientDir)));

				float3 linIblAmbient = 0.0;
				if (SharedData::iblSettings.DALCMode >= 2)
					linIblAmbient += ColorManagement::SceneToLinear(vanillaAmbient * SharedData::iblSettings.DALCAmount);
				else
					linIblAmbient += ImageBasedLighting::GetEnvIBLColor(cloudAmbientDir);
				float3 linReflectionAmbient = ImageBasedLighting::GetSkyIBLColorOccluded(cloudAmbientDir, skyVisibility);
				linIblAmbient += linReflectionAmbient;
				linIblAmbient = ImageBasedLighting::ApplyIBLReflectionFallback(linIblAmbient, linReflectionAmbient, cloudPosWS);

				float3 iblAmbient = ColorManagement::LinearToScene(linIblAmbient);
				float iblFill = baseColor.a * exp(-0.35 * cloudOpticalDepth) * lerp(0.25, 1.0, 1.0 - directVisibility);
				cloudColor += baseColor.xyz * iblAmbient * iblFill * data.cloudRelightMix;
			}
#			endif

			cloudColor += baseColor.xyz * phaseCloud * directVisibility * directSingleScatter * dirLightColor;
		}

		return cloudColor;
	}
#		endif
#	endif

	float4 SampleAp(float3 viewDir, float dist, float shadow, SamplerState sampSv)
	{
		if (ViewAtmosphere::Enabled())
			return float4(0, 0, 0, 1);
		SharedData::PhysSkyData data = SharedData::physSkyData;

		uint3 apDims;
		TexApLut.GetDimensions(apDims.x, apDims.y, apDims.z);
		const float2 skyLutUv = SkyViewLutUv(viewDir, apDims.y);
		const float depth_slice = ApDepthUv(dist, apDims.z);
		float4 apColor = TexApLut.SampleLevel(sampSv, float3(skyLutUv, depth_slice), 0);
		apColor.rgb *= 1.0 - saturate(shadow);

		apColor.rgb = lerp(0, apColor.rgb, data.apLumMix);
		apColor.a = lerp(1, apColor.a, data.apTrMix);

		return apColor;
	}

	float4 SampleAp(float3 viewDir, uint2 pxCoord, float dist, SamplerState sampSv)
	{
		if (ViewAtmosphere::Enabled())
			return float4(0, 0, 0, 1);
		return SampleAp(viewDir, dist, GetApShadow(pxCoord), sampSv);
	}

#	ifndef PS_PREPASS_RSRCS
	Texture2D<float4> TexLightningChannels : register(t113);
	TextureCube<float4> TexLightningReflection : register(t109);
	// Volumetric cloud main-view result and shadow volume. Pixel shaders use t110-t112 to avoid feature texture conflicts.
#		if defined(PS_DEFERRED_RSRCS)
	Texture2D<float> TexVolTr : register(t18);
	Texture2D<float4> TexVolLum : register(t19);
	Texture3D<float> TexShadowVolume : register(t20);
#		else
	Texture2D<float> TexVolTr : register(t110);
	Texture2D<float4> TexVolLum : register(t111);
	Texture3D<float> TexShadowVolume : register(t112);
	TextureCube<float> TexVolCubeTr : register(t114);
	TextureCube<float4> TexVolCubeLum : register(t115);
	TextureCube<float4> TexVolCubeDepth : register(t67);
#		endif

	float3 CompositeVolumetricClouds(float3 color, uint2 pxCoord)
	{
		const float4 cloud = TexVolLum[pxCoord];
		const float3 channel = !ViewAtmosphere::Enabled() && SharedData::physSkyData.lightningColorIntensity.w > 0.0 ? TexLightningChannels[pxCoord].rgb : 0.0;
		return ColorManagement::SceneColor::ScaleAndAddLinear(color, TexVolTr[pxCoord], cloud.rgb + channel);
	}

	float3 CompositeVolumetricCloudsUvDr(float3 color, float2 screenUvDr, SamplerState samp)
	{
		const float4 cloud = TexVolLum.SampleLevel(samp, screenUvDr, 0);
		const float3 channel = !ViewAtmosphere::Enabled() && SharedData::physSkyData.lightningColorIntensity.w > 0.0 ? TexLightningChannels.SampleLevel(samp, screenUvDr, 0).rgb : 0.0;
		return ColorManagement::SceneColor::ScaleAndAddLinear(color, TexVolTr.SampleLevel(samp, screenUvDr, 0), cloud.rgb + channel);
	}

	float3 CompositeVolumetricCloudsUv(float3 color, float2 screenUv, SamplerState samp)
	{
		return CompositeVolumetricCloudsUvDr(color, FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(screenUv), samp);
	}

	float3 ApplyVolumetricCloudTransmittanceUv(float3 color, float2 screenUv, SamplerState samp)
	{
		const float3 volTr = TexVolTr.SampleLevel(samp, FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(screenUv), 0);
		return ColorManagement::SceneColor::ScaleByLinear(color, volTr);
	}

#		ifndef PS_DEFERRED_RSRCS
	float3 CompositeVolumetricCloudsCube(float3 color, float3 viewDir, SamplerState samp)
	{
		// Celestial layers are drawn over the already-composited sky. Only occlude
		// their emission; adding the cloud source again would duplicate its energy.
		if (ViewAtmosphere::ReflectionEnabled())
			return ColorManagement::SceneColor::ScaleByLinear(color, TexVolCubeTr.SampleLevel(samp, viewDir, 0));
		const float4 cloud = TexVolCubeLum.SampleLevel(samp, viewDir, 0);
		const float3 flash = SharedData::physSkyData.lightningColorIntensity.w > 0.0 ?
		                         TexLightningReflection.SampleLevel(samp, viewDir, 0).rgb * SharedData::physSkyData.lightningColorIntensity.w :
		                         0.0;
		const float cloudT = TexVolCubeTr.SampleLevel(samp, viewDir, 0);
		const float depth = TexVolCubeDepth.SampleLevel(samp, viewDir, 0).x * 1000.0 / GAME_UNIT_TO_M;
		const float4 air = SampleAp(viewDir, depth, 0.0, samp);
		return ColorManagement::SceneColor::ScaleAndAddLinear(color, cloudT, (cloud.rgb + flash) * air.a + air.rgb * (1.0 - cloudT));
	}

	ViewAtmosphere::Medium SampleReflectionMedium(float3 viewDir, float distance, SamplerState samp, bool skyRay)
	{
		const float2 uv = SkyViewLutUv(viewDir, 32u);
		ViewAtmosphere::Medium medium = ViewAtmosphere::SampleReflection(uv, distance, samp);
		if (SharedData::physSkyData.enableVolumetricClouds) {
			const float depth = TexVolCubeDepth.SampleLevel(samp, viewDir, 0).x * 1000.0 / GAME_UNIT_TO_M;
			const float visibility = skyRay ? 1.0 : saturate((distance - depth) * GAME_UNIT_TO_M * 0.025);
			const float cloudT = lerp(1.0, TexVolCubeTr.SampleLevel(samp, viewDir, 0), visibility);
			const ViewAtmosphere::Medium front = ViewAtmosphere::SampleReflection(uv, min(depth, distance), samp);
			const float3 flash = SharedData::physSkyData.lightningColorIntensity.w > 0.0 ?
			                         TexLightningReflection.SampleLevel(samp, viewDir, 0).rgb * SharedData::physSkyData.lightningColorIntensity.w :
			                         0.0;
			const float3 cloudL = (TexVolCubeLum.SampleLevel(samp, viewDir, 0).rgb + flash) * visibility;
			medium.luminance = lerp(front.luminance, medium.luminance, cloudT) + front.transmittance * cloudL;
			medium.transmittance *= cloudT;
		}
		return medium;
	}

	float3 CompositeReflectionMedium(float3 color, float3 viewDir, float distance, SamplerState samp, bool skyRay)
	{
		const ViewAtmosphere::Medium medium = SampleReflectionMedium(viewDir, distance, samp, skyRay);
		return ColorManagement::SceneColor::ScaleAndAddLinear(color, medium.transmittance, medium.luminance);
	}
#		endif

	float3 CompositeAerialPerspective(float3 color, float3 viewDir, uint2 pxCoord, float dist, SamplerState sampSv)
	{
		if (ViewAtmosphere::Enabled())
			return color;
		const float4 apSample = SampleAp(viewDir, pxCoord, dist, sampSv);
		color = color * apSample.w + apSample.xyz;

		if (SharedData::physSkyData.enableVolumetricClouds && !SharedData::PostWaterComposite)
			return CompositeVolumetricClouds(color, pxCoord);

		return color;
	}

	float3 CompositeAerialPerspective(float3 color, float3 viewDir, float2 screenPos, float2 screenUv, float dist, SamplerState sampSv)
	{
		if (ViewAtmosphere::Enabled())
			return color;
		const float4 apSample = SampleAp(viewDir, uint2(screenPos), dist, sampSv);
		color = color * apSample.w + apSample.xyz;

		if (SharedData::physSkyData.enableVolumetricClouds && !SharedData::PostWaterComposite)
			return CompositeVolumetricCloudsUv(color, screenUv, sampSv);

		return color;
	}

#		ifndef PS_DEFERRED_RSRCS
	float3 CompositeAerialPerspectiveReflection(float3 color, float3 viewDir, float dist, SamplerState sampSv)
	{
		if (ViewAtmosphere::ReflectionEnabled())
			return CompositeReflectionMedium(color, viewDir, dist, sampSv, false);
		const float4 apSample = SampleAp(viewDir, dist, 0.0, sampSv);
		color = color * apSample.w + apSample.xyz;

		if (SharedData::physSkyData.enableVolumetricClouds)
			return CompositeVolumetricCloudsCube(color, viewDir, sampSv);

		return color;
	}
#		endif

#	endif

#endif

	float3 GetCloudShadowLightDirection()
	{
		if (SharedData::physSkyData.volCloudUseSun != 0)
			return SharedData::physSkyData.sunDir;
		const float3 direction = SharedData::DirLightDirection.xyz;
		const float lengthSq = dot(direction, direction);
		return lengthSq > 1e-6 ? direction * rsqrt(lengthSq) : SharedData::physSkyData.sunDir;
	}

	// Clouded / clear incident radiance, computed before reflection fog. This
	// diffuse transport estimate is independent of the finite direct-shadow box.
	Texture2D<float4> TexCloudDiffuseTransport : register(t70);
	float3 GetCloudDiffuseTransmittance(float3 worldPosAbs)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;
		if (!data.enableVolumetricClouds || data.volCloudLowThickness <= 0.0)
			return 1.0;
		const float4 transport = TexCloudDiffuseTransport.Load(int3(0, 1, 0));
		if (transport.w == 0.0)
			return 1.0;
		const float altitude = worldPosAbs.z - data.zBottom;
		const float aboveCloud = smoothstep(data.volCloudLowBottom, data.volCloudLowBottom + data.volCloudLowThickness, altitude);
		return lerp(saturate(transport.rgb), 1.0, aboveCloud);
	}

#if !defined(PS_DEFERRED_RSRCS) && (!defined(PS_PREPASS_RSRCS) || defined(PS_ENABLE_DIRLIGHT_TRANSMITTANCE))
	float4 GetShadowVolumeSample(float3 posRelative, float3 sunDir)
	{
		SharedData::PhysSkyData data = SharedData::physSkyData;
		uint3 dimensions;
		TexShadowVolume.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
		if (any(dimensions == 0u))
			return -1.0;
		const float2 center = CloudShadowVolume::GridCenter(FrameBuffer::CameraPosAdjust.xy, data.shadowVolumeRange, dimensions.xy);
		float3 boundsMin = float3(center - 0.5 * data.shadowVolumeRange, data.volCloudLowBottom);
		float3 boundsMax = float3(center + 0.5 * data.shadowVolumeRange, data.volCloudLowBottom + data.volCloudLowThickness);

		return CloudShadowVolume::GetSample(posRelative, sunDir, boundsMin, boundsMax, dimensions.xy);
	}

	float3 GetDirlightTransmittance(float3 worldPosAbs, SamplerState samp, bool useFarField = false)
	{
		SharedData::PhysSkyData data = SharedData::physSkyData;
		float3 transmittance = 1.0;

		if (!data.enableVolumetricClouds || data.volCloudLowThickness <= 0)
			return transmittance;

		float3 posRelative = worldPosAbs;
		posRelative.z -= data.zBottom;

		float4 shadowSample = GetShadowVolumeSample(posRelative, GetCloudShadowLightDirection());
		float meanTransmittance = 1.0;
		float localWeight = 1.0;
		if (useFarField) {
			uint3 dimensions;
			TexShadowVolume.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
			meanTransmittance = CloudShadowVolume::FarTransmittance(posRelative, GetCloudShadowLightDirection(), shadowSample, dimensions.xy, localWeight);
			shadowSample.w *= localWeight;
		}
#	ifdef PS_LINEAR_SHADOW_SAMPLER
		const float cloudTransmittance = CloudShadowVolume::SampleTransmittance(TexShadowVolume, samp, shadowSample, meanTransmittance);
#	else
		const float cloudTransmittance = CloudShadowVolume::SampleTransmittance(TexShadowVolume, shadowSample, meanTransmittance);
#	endif

		return cloudTransmittance.xxx;
	}
#endif

	float3 GetMediumPlanetPosition(float3 position)
	{
		return position + float3(0, 0, max(SharedData::physSkyData.zCameraPlanet, SharedData::physSkyData.rPlanet));
	}

	void GetMediumCoefficients(float3 position, out float3 rayleigh, out float3 mie, out float3 extinction)
	{
		const SharedData::PhysSkyData data = SharedData::physSkyData;
		const float radius = length(GetMediumPlanetPosition(position));
		if (radius < data.rPlanet || radius > data.rAtmosphere) {
			rayleigh = mie = extinction = 0.0;
			return;
		}
		float r, m, o;
		SampleAtmosphere(max(radius - data.rPlanet, 0.0), r, m, o);
		// Per-kilometre coefficients preserve weak sources in half precision.
		rayleigh = r * data.rayleighScatter * (1000.0 / GAME_UNIT_TO_M);
		mie = m * data.aerosolScatter * (1000.0 / GAME_UNIT_TO_M);
		extinction = rayleigh + mie + (m * data.aerosolAbsorption + o * data.ozoneAbsorption) * (1000.0 / GAME_UNIT_TO_M);
	}

#ifndef OMIT_PS_NAMESPACE
}
#endif

#endif
