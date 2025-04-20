#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#if defined(PSHADER)

namespace SnowCover
{

	Texture2D<float4> SnowAlbedo : register(t73);
	Texture2D<float3> SnowNormal : register(t74);
	Texture2D<float4> SnowRmaos : register(t75);
	Texture2D<float4> IceAlbedo : register(t76);
	Texture2D<float3> IceNormal : register(t77);
	Texture2D<float4> IceRmaos : register(t78);

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
		float height_tresh = p.z - SharedData::snowCoverSettings.SnowHeightOffset - (p.x * SharedData::snowCoverSettings.equation.x - p.y * SharedData::snowCoverSettings.equation.y - p.x * p.x * SharedData::snowCoverSettings.equation.z - p.x * p.y * SharedData::snowCoverSettings.equation.w - p.y * p.y * SharedData::snowCoverSettings.equation2.x - p.x * p.x * p.x * SharedData::snowCoverSettings.equation2.y + p.x * p.x * p.y * SharedData::snowCoverSettings.equation2.z + p.x * p.y * p.y * SharedData::snowCoverSettings.equation2.w + p.y * p.y * p.y * SharedData::snowCoverSettings.equation3);
		return height_tresh;
	}

	float GetEnvironmentalMultiplier(float3 p)
	{
		float maxMonth = max(SharedData::snowCoverSettings.MaxSummerMonth, SharedData::snowCoverSettings.MaxWinterMonth);
		float minMonth = min(SharedData::snowCoverSettings.MaxSummerMonth, SharedData::snowCoverSettings.MaxWinterMonth);
		float summerToWinter;
		if (SharedData::snowCoverSettings.Month > maxMonth) {
			summerToWinter = (SharedData::snowCoverSettings.Month - maxMonth) / (minMonth + 12 - maxMonth);
			if (SharedData::snowCoverSettings.MaxWinterMonth > SharedData::snowCoverSettings.MaxSummerMonth)
				summerToWinter = 1 - summerToWinter;
		} else if (SharedData::snowCoverSettings.Month < minMonth) {
			summerToWinter = (12 - maxMonth + SharedData::snowCoverSettings.Month) / (minMonth + 12 - maxMonth);
			if (SharedData::snowCoverSettings.MaxSummerMonth > SharedData::snowCoverSettings.MaxWinterMonth)
				summerToWinter = 1 - summerToWinter;
		} else {
			summerToWinter = (SharedData::snowCoverSettings.Month - minMonth) / (maxMonth - minMonth);
			if (SharedData::snowCoverSettings.MaxSummerMonth > SharedData::snowCoverSettings.MaxWinterMonth)
				summerToWinter = 1 - summerToWinter;
		}

		return (GetHeightMult(p) - lerp(SharedData::snowCoverSettings.SummerHeightOffset, SharedData::snowCoverSettings.WinterHeightOffset, summerToWinter)) / 10000;
	}

	void ApplyFoliageColor(inout float3 color, float env_mult)
	{
		float gmult = pow(saturate(env_mult - SharedData::snowCoverSettings.FoliageHeightOffset / 10000), 0.25);
		float3 hsv = RGBtoHSV(color);
		if (hsv.x > 0.55)
			hsv.x = frac(lerp(hsv.x, 1.1, gmult * (1.1 - hsv.x)) * 4);
		else
			hsv.x = lerp(hsv.x, 0.1, gmult * (hsv.x + 0.1) * 4);
		//hsv.z = pow(hsv.z, 1+gmult*0.5);
		color = HSVtoRGB(hsv);
	}

	void ApplySnowFoliage(inout float3 color, inout float3 worldNormal, float3 p, float skylight, float viewDist)
	{
		float env_mult = GetEnvironmentalMultiplier(p);
		float distMult = 1 - smoothstep(10000, 30000, viewDist);
		float weatherMult = distMult * SharedData::snowCoverSettings.TimeSnowing * max(500, SharedData::snowCoverSettings.SnowingDensity) / 100;
		float mult = SharedData::snowCoverSettings.MainTint.a * pow(saturate(env_mult), 0.25);
		mult = skylight * saturate(mult + weatherMult) * smoothstep(SharedData::snowCoverSettings.minAngle, SharedData::snowCoverSettings.maxAngle, worldNormal.z * 0.5);
		if (SharedData::snowCoverSettings.AffectFoliageColor) {
			ApplyFoliageColor(color, env_mult);
		}
		float2 uv = frac(SharedData::snowCoverSettings.UVScale * (p.xy + worldNormal.xy * 2) / 100);
		float3 diffuse = Color::TrueLinearToGamma(SnowAlbedo.Sample(SampColorSampler, uv).rgb) * SharedData::snowCoverSettings.MainTint.rgb;

		color = lerp(color, diffuse, mult);
	}

#	if !defined(BASIC_SNOW_COVER)
	float ApplySnowBase(inout float3 worldNormal, inout float2 uv, out bool alt, float disp, float3 p, float skylight, float waterDist, float viewDist)
	{
		waterDist = smoothstep(-64, 8, -waterDist - disp);
		float distMult = 1 - smoothstep(10000, 30000 + 1000 * sin(p.z * 0.001 + cos(p.x * p.y * 0.001)), viewDist);
		float weatherMult = distMult * SharedData::snowCoverSettings.TimeSnowing * max(500, SharedData::snowCoverSettings.SnowingDensity) / 100;
		float env_mult = saturate(max(0, pow(saturate(GetEnvironmentalMultiplier(p) + disp), 0.25)) + clamp(-1, 1, weatherMult) - waterDist);
		float main_mult = (1 - abs(worldNormal.z - SharedData::snowCoverSettings.peakMainAngle));
		float alt_mult = (1 - abs(worldNormal.z - SharedData::snowCoverSettings.peakAltAngle)) + sin((p.x + p.z) * 0.025) * 0.05;
		alt = alt_mult > main_mult;
		float mult = skylight * env_mult * smoothstep(SharedData::snowCoverSettings.minAngle, SharedData::snowCoverSettings.maxAngle, worldNormal.z);
		if (mult < 0.01)
			return 0;
		//mult = step(0.5, mult);
		uv = frac(SharedData::snowCoverSettings.UVScale * (uv + p.xy / 100));

		//sh0 = saturate(sh0 + mult * parallax);
		return mult;
	}

#		if defined(TRUE_PBR)
	PBR::SurfaceProperties ApplySnowPBR(inout float3 diffuse, inout float3 worldNormal, out float mult, float disp, float3 p, float skylight, float waterDist, float viewDist, PBR::SurfaceProperties prop, float2 uv)
	{
		bool alt;
		mult = max(0.0, ApplySnowBase(worldNormal, uv, alt, disp, p, skylight, waterDist, viewDist));
		if (mult <= 0.0)
			return prop;
		//diffuse = Color::Diffuse(alt ? IceAlbedo.Sample(SampColorSampler, uv).rgb : SnowAlbedo.Sample(SampColorSampler, uv).rgb)*(alt ? SharedData::snowCoverSettings.AltTint.rgb : SharedData::snowCoverSettings.MainTint.rgb); return prop;
		float3 albedo = alt ? IceAlbedo.Sample(SampColorSampler, uv).rgb : SnowAlbedo.Sample(SampColorSampler, uv).rgb;
		albedo = Color::Diffuse(albedo) * (alt ? SharedData::snowCoverSettings.AltTint.rgb : SharedData::snowCoverSettings.MainTint.rgb);
		diffuse = lerp(diffuse, albedo, mult * (alt ? SharedData::snowCoverSettings.AltTint.w : SharedData::snowCoverSettings.MainTint.w));
		float4 rmaos = alt ? IceRmaos.Sample(SampColorSampler, uv) : SnowRmaos.Sample(SampColorSampler, uv);
		//diffuse = frac(float3(uv.x, uv.y, 0));
		worldNormal = TransformNormal(alt ? IceNormal.Sample(SampNormalSampler, uv).rgb : SnowNormal.Sample(SampNormalSampler, uv).rgb);
		//worldNormal = normalize(lerp(worldNormal, normal, mult));
		prop.Roughness = lerp(prop.Roughness, rmaos.x, mult);
		prop.Metallic = lerp(prop.Metallic, rmaos.y, mult);
		prop.AO = lerp(prop.AO, rmaos.z, mult);
		prop.F0 = lerp(prop.F0, rmaos.w * 0.04, mult);
		prop.GlintScreenSpaceScale = lerp(prop.GlintScreenSpaceScale, SharedData::snowCoverSettings.Glint.x, mult);
		prop.GlintLogMicrofacetDensity = lerp(prop.GlintLogMicrofacetDensity, SharedData::snowCoverSettings.Glint.y, mult);
		prop.GlintMicrofacetRoughness = lerp(prop.GlintMicrofacetRoughness, SharedData::snowCoverSettings.Glint.z, mult);
		prop.GlintDensityRandomization = lerp(prop.GlintDensityRandomization, SharedData::snowCoverSettings.Glint.w, mult);
		return prop;
	}
#		else

	float ApplySnow(inout float3 diffuse, inout float3 worldNormal, inout float glossiness, inout float shininess, float disp, float3 p, float skylight, float waterDist, float viewDist, float2 uv)
	{
		bool alt;
		float mult = ApplySnowBase(worldNormal, uv, alt, disp, p, skylight, waterDist, viewDist);
		if (mult <= 0.0)
			return 0;
		// apparently LOD landscape color sampler clamps uvs
		float3 albedo = alt ? IceAlbedo.Sample(SampColorSampler, uv).rgb : SnowAlbedo.Sample(SampColorSampler, uv).rgb;
		albedo = Color::TrueLinearToGamma(albedo) * (alt ? SharedData::snowCoverSettings.AltTint.rgb : SharedData::snowCoverSettings.MainTint.rgb);
		float4 rmaos = alt ? IceRmaos.Sample(SampColorSampler, uv) : SnowRmaos.Sample(SampColorSampler, uv);
		diffuse = lerp(diffuse, rmaos.z * albedo, mult * (alt ? SharedData::snowCoverSettings.AltTint.w : SharedData::snowCoverSettings.MainTint.w));
		//diffuse = frac(float3(uv.x, uv.y, 0));
		glossiness = lerp(glossiness, 1 - rmaos.x, mult);
		shininess = lerp(shininess, 25 * 500 * 0.04 * rmaos.w, mult);
		worldNormal = TransformNormal(alt ? IceNormal.Sample(SampNormalSampler, uv).rgb : SnowNormal.Sample(SampNormalSampler, uv).rgb);
		//glossiness = lerp(glossiness, 0.5 * pow(v * s, 3.0), mult);
		//shininess = lerp(shininess, max(1, pow(1 - v, 3.0) * 100), mult);
		mult *= alt ? SharedData::snowCoverSettings.AltTint.w : SharedData::snowCoverSettings.MainTint.w;
		return mult;
	}
#		endif
#	endif

}
#endif
