#include "Common/SharedData.hlsli"
#if defined(PSHADER)

namespace SnowCover
{

	Texture2D<float4> SnowDiffuse : register(t73);
	Texture2D<float3> SnowNormal : register(t74);
	Texture2D<float4> SnowRMAOS : register(t75);
	Texture2D<float> SnowParallax : register(t76);

	// https://blog.selfshadow.com/publications/blending-in-detail/
	// for when s = (0,0,1)
	float3 MyReorientNormal(float3 n1, float3 n2)
	{
		n1 += float3(0, 0, 1);
		n2 *= float3(-1, -1, 1);

		return n1 * dot(n1, n2) / n1.z - n2;
	}

	// http://chilliant.blogspot.com/2010/11/rgbhsv-in-hlsl.html
	float3 Hue(float H)
	{
		float R = abs(H * 6 - 3) - 1;
		float G = 2 - abs(H * 6 - 2);
		float B = 2 - abs(H * 6 - 4);
		return saturate(float3(R, G, B));
	}

	float3 HSVtoRGB(in float3 HSV)
	{
		return ((Hue(HSV.x) - 1) * HSV.y + 1) * HSV.z;
	}

	float3 RGBtoHSV(in float3 RGB)
	{
		float3 HSV = 0;
		HSV.z = max(RGB.r, max(RGB.g, RGB.b));
		float M = min(RGB.r, min(RGB.g, RGB.b));
		float C = HSV.z - M;

		if (C != 0) {
			HSV.y = C / HSV.z;
			float3 Delta = (HSV.z - RGB) / C;
			Delta.rgb -= Delta.brg;
			Delta.rg += float2(2, 4);
			if (RGB.r >= HSV.z)
				HSV.x = Delta.b;
			else if (RGB.g >= HSV.z)
				HSV.x = Delta.r;
			else
				HSV.x = Delta.g;
			HSV.x = frac(HSV.x / 6);
		}
		return HSV;
	}

	float GetHeightMult(float3 p)
	{
		float height_tresh = p.z - snowCoverSettings.SnowHeightOffset - (p.x * 0.010569460362286 - p.y * 0.165389061732133 - p.x * p.x * 0.000000034552775 - p.x * p.y * 0.000000572526633 - p.y * p.y * 0.000000272913055 - p.x * p.x * p.x * 0.000000000001466 + p.x * p.x * p.y * 0.000000000000441 + p.x * p.y * p.y * 0.000000000003507 + p.y * p.y * p.y * 0.000000000006575);
		return height_tresh;
	}

	float GetEnvironmentalMultiplier(float3 p)
	{
		float maxMonth = max(snowCoverSettings.MaxSummerMonth, snowCoverSettings.MaxWinterMonth);
		float minMonth = min(snowCoverSettings.MaxSummerMonth, snowCoverSettings.MaxWinterMonth);
		float summerToWinter;
		if (snowCoverSettings.Month > maxMonth) {
			summerToWinter = (snowCoverSettings.Month - maxMonth) / (minMonth + 12 - maxMonth);
			if (snowCoverSettings.MaxWinterMonth > snowCoverSettings.MaxSummerMonth)
				summerToWinter = 1 - summerToWinter;
		} else if (snowCoverSettings.Month < minMonth) {
			summerToWinter = (12 - maxMonth + snowCoverSettings.Month) / (minMonth + 12 - maxMonth);
			if (snowCoverSettings.MaxSummerMonth > snowCoverSettings.MaxWinterMonth)
				summerToWinter = 1 - summerToWinter;
		} else {
			summerToWinter = (snowCoverSettings.Month - minMonth) / (maxMonth - minMonth);
			if (snowCoverSettings.MaxSummerMonth > snowCoverSettings.MaxWinterMonth)
				summerToWinter = 1 - summerToWinter;
		}

		return (GetHeightMult(p) - lerp(snowCoverSettings.SummerHeightOffset, snowCoverSettings.WinterHeightOffset, summerToWinter)) / 10000;
	}

	void ApplyFoliageColor(inout float3 color, float env_mult)
	{
		float gmult = saturate(env_mult - snowCoverSettings.FoliageHeightOffset / 1000);
		float3 hsv = RGBtoHSV(color);
		if (hsv.x > 0.5625)
			hsv.x = frac(lerp(hsv.x, 1.125, gmult));
		else
			hsv.x = lerp(hsv.x, 0.125, gmult);
		//hsv.z = pow(hsv.z, 1+gmult*0.5);
		color = HSVtoRGB(hsv);
	}

	void ApplySnowFoliage(inout float3 color, inout float3 worldNormal, float3 p, float skylight)
	{
		float env_mult = GetEnvironmentalMultiplier(p);
		float mult = saturate(pow(abs(worldNormal.z), 1)) * saturate(env_mult) * skylight;
		if (snowCoverSettings.AffectFoliageColor) {
			ApplyFoliageColor(color, env_mult);
		}
		float2 uv = snowCoverSettings.UVScale * p.xy / 100;
		float3 diffuse = SnowDiffuse.Sample(SampColorSampler, uv).rgb;
#	if !defined(TRUE_PBR)
		diffuse = pow(LinearToGamma(diffuse) / 3.141, 1 / 1.5);
#	endif
		color = lerp(color, diffuse, mult);
	}

#	if !defined(BASIC_SNOW_COVER)
	float ApplySnowBase(inout float3 worldNormal, inout float sh0, inout float2 uv, float underDispScale, float3 p, float skylight, float waterDist, float3 viewPos)
	{
		if (snowCoverSettings.Sky < 3)  // 3 = exterior
			return 0;
			//float viewDist = max(1, sqrt(viewPos.z) / 512);
#		if defined(TRUE_PBR) && defined(LANDSCAPE)
		float disp = sh0;
#		else
		float disp = sh0 - 0.5;
#		endif
		float raw_p = SnowParallax.Sample(SampColorSampler, snowCoverSettings.UVScale * p.xy / 1000).x;
		float parallax = 0.1 * snowCoverSettings.ParallaxScale * (raw_p - 0.5);
		float env_mult = GetEnvironmentalMultiplier(p) + parallax + disp * underDispScale * 0.1;
		waterDist = smoothstep(-64, 8, -waterDist);
		float disp_factor = 0;
#		if defined(TRUE_PBR)
		if (extendedMaterialSettings.ExtendShadows)
			disp_factor = -disp * underDispScale;
#		endif                                                                                                                                                                                              //
		float mult = skylight * (smoothstep(0.3, 0.5, (pow(max(0, worldNormal.z), 2) + disp_factor) * (max(0, env_mult - waterDist) * (0.5 + 0.5 * raw_p + disp_factor) + snowCoverSettings.SnowAmount)));  //-smoothstep(-32, 8, -waterDist)
		uv = snowCoverSettings.UVScale * p.xy / 100 + parallax * viewPos.xy;
		if (mult < 0.01)
			return 0;
		sh0 = saturate(sh0 + mult * parallax);
		return mult;
	}

#		if defined(TRUE_PBR)
	float ApplySnowPBR(inout float3 diffuse, inout float3 worldNormal, inout PBR::SurfaceProperties prop, inout float sh0, float underDispScale, float3 p, float skylight, float waterDist, float3 viewPos)
	{
		float2 uv;
		float mult = ApplySnowBase(worldNormal, sh0, uv, underDispScale, p, skylight, waterDist, viewPos);
		if (mult <= 0.0)
			return 0;
		diffuse = SnowDiffuse.Sample(SampColorSampler, uv).rgb;
		//diffuse = frac(float3(uv.x, uv.y, 0));
		float3 normal = TransformNormal(SnowNormal.Sample(SampNormalSampler, uv).rgb);
		//worldNormal = normalize(lerp(worldNormal, MyReorientNormal(worldNormal, normal), mult));
		worldNormal = normalize(lerp(worldNormal, normal, mult));
		float4 rmaos = SnowRMAOS.Sample(SampRMAOSSampler, uv);
		prop.Roughness = lerp(prop.Roughness, rmaos.x, mult);
		prop.Metallic = lerp(prop.Metallic, rmaos.y, mult);
		prop.AO = lerp(prop.AO, rmaos.z, mult);
		prop.F0 = lerp(prop.F0, rmaos.w * 0.08, mult);
		prop.GlintScreenSpaceScale = lerp(prop.GlintScreenSpaceScale, snowCoverSettings.Glint.x, mult);
		prop.GlintLogMicrofacetDensity = lerp(prop.GlintLogMicrofacetDensity, snowCoverSettings.Glint.y, mult);
		prop.GlintMicrofacetRoughness = lerp(prop.GlintMicrofacetRoughness, snowCoverSettings.Glint.z, mult);
		prop.GlintDensityRandomization = lerp(prop.GlintDensityRandomization, snowCoverSettings.Glint.w, mult);
		return mult;
	}
#		else

	float ApplySnow(inout float3 diffuse, inout float3 worldNormal, inout float glossiness, inout float shininess, inout float sh0, float underDispScale, float3 p, float skylight, float waterDist, float3 viewPos)
	{
		float2 uv;
		float mult = ApplySnowBase(worldNormal, sh0, uv, underDispScale, p, skylight, waterDist, viewPos);
		if (mult <= 0.0)
			return 0;
		// apparently LOD landscape color sampler clamps uvs
		diffuse = SnowDiffuse.Sample(SampColorSampler, frac(uv)).rgb;
		//diffuse = frac(float3(uv.x, uv.y, 0));
		diffuse = pow(LinearToGamma(diffuse) / PI, 1 / 1.5);
		float4 rmaos = SnowRMAOS.Sample(SampColorSampler, uv);
		glossiness = lerp(glossiness, 1 - rmaos.x, mult);  // yes these are named wrong not my fault bye
		shininess = lerp(shininess, 25 * 500 * rmaos.w, mult);
		diffuse *= rmaos.z;
		float3 normal = TransformNormal(SnowNormal.Sample(SampNormalSampler, uv).rgb);
		worldNormal = normalize(lerp(worldNormal, normal, mult));
		//glossiness = lerp(glossiness, 0.5 * pow(v * s, 3.0), mult);
		//shininess = lerp(shininess, max(1, pow(1 - v, 3.0) * 100), mult);
		return mult;
	}
#		endif
#	endif

}
#endif
