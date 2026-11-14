#ifndef SNOW_DEFORMATION_SNOWPARALLAX_HLSLI
#define SNOW_DEFORMATION_SNOWPARALLAX_HLSLI

#if defined(DOMAINSHADER) || defined(PSHADER) || defined(SNOW_SURFACE_ONLY)
float2 StochasticHash(float2 cell)
{
	float3 p3 = frac(float3(cell.x, cell.y, cell.x) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return frac(float2((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y));
}

struct SnowTaps
{
	float2 uv0, uv1, uv2;
	float3 weights;
	float2 duvdx, duvdy;
};

SnowTaps ComputeSnowTapsNoGrad(float2 uv, float2 worldXY)
{
	float2 lattice = mul(float2x2(1.0, -0.57735027, 0.0, 1.15470054), worldXY * (0.6 / 256.0));
	float2 cellBase = floor(lattice);
	float2 f = frac(lattice);

	float2 v0, v1, v2;
	float3 bary;
	if (f.x + f.y < 1.0) {
		v0 = cellBase;
		v1 = cellBase + float2(1, 0);
		v2 = cellBase + float2(0, 1);
		bary = float3(1.0 - f.x - f.y, f.x, f.y);
	} else {
		v0 = cellBase + float2(1, 1);
		v1 = cellBase + float2(0, 1);
		v2 = cellBase + float2(1, 0);
		bary = float3(f.x + f.y - 1.0, 1.0 - f.x, 1.0 - f.y);
	}

	bary = pow(bary, 4.0);
	bary /= dot(bary, 1.0);

	SnowTaps taps;
	taps.uv0 = uv + StochasticHash(v0);
	taps.uv1 = uv + StochasticHash(v1);
	taps.uv2 = uv + StochasticHash(v2);
	taps.weights = bary;
	taps.duvdx = 0.0.xx;
	taps.duvdy = 0.0.xx;
	return taps;
}

float SampleSnowHeight(SnowTaps taps, float2 uvOffset, float mip)
{
	return taps.weights.x * SnowHeightMap.SampleLevel(SnowSampler, taps.uv0 + uvOffset, mip).x +
	       taps.weights.y * SnowHeightMap.SampleLevel(SnowSampler, taps.uv1 + uvOffset, mip).x +
	       taps.weights.z * SnowHeightMap.SampleLevel(SnowSampler, taps.uv2 + uvOffset, mip).x;
}
#endif

#ifdef PSHADER

float4 SampleSnowMap(Texture2D<float4> tex, SnowTaps taps)
{
	return taps.weights.x * tex.SampleGrad(SnowSampler, taps.uv0, taps.duvdx, taps.duvdy) +
	       taps.weights.y * tex.SampleGrad(SnowSampler, taps.uv1, taps.duvdx, taps.duvdy) +
	       taps.weights.z * tex.SampleGrad(SnowSampler, taps.uv2, taps.duvdx, taps.duvdy);
}

float SnowHeightMipGrad(float2 duvdx, float2 duvdy)
{
	float2 dims;
	SnowHeightMap.GetDimensions(dims.x, dims.y);
	float2 dx = duvdx * dims;
	float2 dy = duvdy * dims;
	return floor(max(0.5 * log2(max(min(dot(dx, dx), dot(dy, dy)), 1e-8)) + SharedData::MipBias, 0.0));
}

float4 SampleSnowPlanar(Texture2D<float4> tex, SnowTaps topTaps, SnowTaps sideTaps, float sideWeight)
{
	float4 c = SampleSnowMap(tex, topTaps);
	[branch] if (sideWeight > 0.001)
		c = lerp(c, SampleSnowMap(tex, sideTaps), sideWeight);
	return c;
}

#	define SNOW_RMAOS(roughness, f0, ao, topTaps, sideTaps, sideWeight)                   \
		[branch] if (HasSnowRmaos > 0.5)                                                  \
		{                                                                                 \
			float4 rmaos = SampleSnowPlanar(SnowRmaosMap, topTaps, sideTaps, sideWeight); \
			roughness = clamp(rmaos.x * SnowRoughnessScale, 0.05, 1.0);                   \
			ao = rmaos.z;                                                                 \
			f0 = rmaos.w * SnowSpecularLevel;                                             \
		}

float3 SampleSnowNormalOffset(Texture2D<float4> tex, SnowTaps topTaps, SnowTaps sideTaps, float sideWeight, float3 n,
	float3 topT, float3 topB, bool sideDropsX)
{
	float2 top = SampleSnowMap(tex, topTaps).xy * 2.0 - 1.0;
	float3 o = topT * top.x + topB * top.y;
	[branch] if (sideWeight > 0.001)
	{
		float2 side = SampleSnowMap(tex, sideTaps).xy * 2.0 - 1.0;
		float3 sideT = sideDropsX ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
		float3 sideB = float3(0.0, 0.0, 1.0);
		sideT = normalize(sideT - n * dot(n, sideT));
		sideB = normalize(sideB - n * dot(n, sideB));
		o = lerp(o, sideT * side.x + sideB * side.y, sideWeight);
	}
	return o;
}

float SnowParallaxOcclusion(SnowTaps taps, float2 lightUV, float mip, float quality, float noise, DisplacementParams params)
{
	uint tapCount = ExtendedMaterials::ParallaxShadowTapCount(quality);
	float shadowStrength = ExtendedMaterials::ShadowIntensity * (4.0 / tapCount);
	float2 rayDir = lightUV * 0.1 * params.HeightScale;
	float4 multipliers = rcp(float4(1, 2, 3, 4) + noise);

	float sh0 = ExtendedMaterials::AdjustDisplacementNormalized(SampleSnowHeight(taps, 0.0.xx, mip), params);
	float4 sh = sh0.xxxx;
	sh.x = ExtendedMaterials::AdjustDisplacementNormalized(SampleSnowHeight(taps, rayDir * multipliers.x, mip), params);
	if (quality > 0.25)
		sh.y = ExtendedMaterials::AdjustDisplacementNormalized(SampleSnowHeight(taps, rayDir * multipliers.y, mip), params);
	if (quality > 0.5)
		sh.z = ExtendedMaterials::AdjustDisplacementNormalized(SampleSnowHeight(taps, rayDir * multipliers.z, mip), params);
	if (quality > 0.75)
		sh.w = ExtendedMaterials::AdjustDisplacementNormalized(SampleSnowHeight(taps, rayDir * multipliers.w, mip), params);
	return dot(max(0.0, sh - sh0), shadowStrength);
}

float SnowParallaxOcclusionPlanar(SnowTaps topTaps, SnowTaps sideTaps, float sideWeight,
	float2 lightUVTop, float2 lightUVSide, float mipTop, float mipSide,
	float quality, float noise, DisplacementParams params)
{
	float o = SnowParallaxOcclusion(topTaps, lightUVTop, mipTop, quality, noise, params);
	[branch] if (sideWeight > 0.001)
		o = lerp(o, SnowParallaxOcclusion(sideTaps, lightUVSide, mipSide, quality, noise, params), sideWeight);
	return o;
}

DisplacementParams SnowDisplacementParams()
{
	DisplacementParams params;
	params.DisplacementScale = 1.0;
	params.DisplacementOffset = 0.0;
	params.HeightScale = SnowParallax.x;
	params.FlattenAmount = 0.0;
	return params;
}

float SnowParallaxQuality(float viewDist)
{
	return viewDist < ExtendedMaterials::ParallaxCheapDistance ?
	           ExtendedMaterials::ParallaxNearShadowQuality :
	           ExtendedMaterials::ParallaxFarShadowQuality;
}

SnowTaps OffsetSnowTaps(SnowTaps taps, float2 uvOffset)
{
	taps.uv0 += uvOffset;
	taps.uv1 += uvOffset;
	taps.uv2 += uvOffset;
	return taps;
}

static SnowTaps g_snowParallaxTaps;
static float2 g_snowParallaxBaseUV;

float EMParallaxCustomHeight(float2 uv, float mip)
{
#	ifdef SNOW_COAT_PARALLAX
	[branch] if (gObjectParallaxMarch)
		return ObjectParallaxHeight(uv, mip);
	else
		return SampleSnowHeight(g_snowParallaxTaps, uv - g_snowParallaxBaseUV, mip);
#	else
	return SampleSnowHeight(g_snowParallaxTaps, uv - g_snowParallaxBaseUV, mip);
#	endif
}

float2 SnowParallaxOffset(SnowTaps taps, float2 baseUV, float3 viewWS, float3x3 tbn, float pixelDist, float mip, float noise, DisplacementParams params)
{
	g_snowParallaxTaps = taps;
	g_snowParallaxBaseUV = baseUV;
	float2 surfaceHeightUnused;
	float2 resultCoords = ExtendedMaterials::GetParallaxCoords(pixelDist, baseUV, mip, viewWS, tbn, noise,
		SnowHeightMap, SnowSampler, 0, params, false, (StochasticOffsets)0, surfaceHeightUnused);
	return resultCoords - baseUV;
}
#endif

#endif
