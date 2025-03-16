#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#if defined(PSHADER)

namespace SnowCover
{

	Texture2D<float3> SnowRDAO : register(t73);    //snow_rdao
	Texture2D<float3> SnowNormal : register(t74);  //snow_n
	Texture2D<float3> IceRDAO : register(t75);     //ice_rdao
	Texture2D<float3> IceNormal : register(t76);   //ice_n

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
		float gmult = saturate(env_mult - SharedData::snowCoverSettings.FoliageHeightOffset / 1000);
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
		if (SharedData::snowCoverSettings.AffectFoliageColor) {
			ApplyFoliageColor(color, env_mult);
		}
		float2 uv = SharedData::snowCoverSettings.UVScale * p.xy / 100;
		float3 rdao = SnowRDAO.Sample(SampColorSampler, uv).rgb;
		float3 diffuse = rdao.yyy;
#	if !defined(TRUE_PBR)
		diffuse *= Color::PBRLightingScale * rdao.z;
#	endif
		color = lerp(color, diffuse, mult);
	}

#	if !defined(BASIC_SNOW_COVER)
	float ApplySnowBase(inout float3 worldNormal, inout float2 uv, out bool alt, float disp, float3 p, float skylight, float waterDist, float3 viewPos)
	{
		waterDist = smoothstep(-64, 8, -waterDist - disp);
		float weatherMult = SharedData::snowCoverSettings.TimeSnowing * SharedData::snowCoverSettings.SnowAmount / 3000;
		float env_mult = max(0, saturate(GetEnvironmentalMultiplier(p) + disp) - waterDist);
		float main_mult = skylight * saturate(smoothstep(0.1, 0.9, (max(0, worldNormal.z)) * (env_mult + max(0, saturate(weatherMult) - waterDist))));
		float alt_env_mult = max(0, saturate(GetEnvironmentalMultiplier(p) - disp) - waterDist * 0.65);
		float alt_mult = skylight * saturate(smoothstep(0.5, 1.0, worldNormal.z) * alt_env_mult);
		alt = alt_mult > main_mult;
		float mult = max(main_mult, alt_mult);
		//mult = step(0.5, mult);
		uv = SharedData::snowCoverSettings.UVScale * (uv + p.xy / 100);
		if (mult < 0.01)
			return 0;
		//sh0 = saturate(sh0 + mult * parallax);
		return mult;
	}

#		if defined(TRUE_PBR)
	PBR::SurfaceProperties ApplySnowPBR(inout float3 diffuse, inout float3 worldNormal, out float mult, float disp, float3 p, float skylight, float waterDist, float3 viewPos, PBR::SurfaceProperties prop, float2 uv, float3x3 tbn)
	{
		bool alt;
		mult = max(0.0, ApplySnowBase(worldNormal, uv, alt, disp, p, skylight, waterDist, viewPos));
		if (mult <= 0.0)
			return prop;
		float3 rdao = alt ? IceRDAO.Sample(SampColorSampler, uv).rgb : SnowRDAO.Sample(SampColorSampler, uv).rgb;
		diffuse = rdao.yyy * (alt ? SharedData::snowCoverSettings.AltTint.rgb : SharedData::snowCoverSettings.MainTint.rgb);
		//diffuse = frac(float3(uv.x, uv.y, 0));
		float3 normal = normalize(mul(tbn, TransformNormal(alt ? IceNormal.Sample(SampNormalSampler, uv).rgb : SnowNormal.Sample(SampNormalSampler, uv).rgb)));
		worldNormal = normalize(lerp(worldNormal, MyReorientNormal(worldNormal, normal), mult));
		//worldNormal = normalize(lerp(worldNormal, normal, mult));
		prop.Roughness = lerp(prop.Roughness, rdao.x, mult);
		prop.Metallic = lerp(prop.Metallic, 0, mult);
		prop.AO = lerp(prop.AO, rdao.z, mult);
		prop.F0 = lerp(prop.F0, 0.02, mult);
		prop.GlintScreenSpaceScale = lerp(prop.GlintScreenSpaceScale, SharedData::snowCoverSettings.Glint.x, mult);
		prop.GlintLogMicrofacetDensity = lerp(prop.GlintLogMicrofacetDensity, SharedData::snowCoverSettings.Glint.y, mult);
		prop.GlintMicrofacetRoughness = lerp(prop.GlintMicrofacetRoughness, SharedData::snowCoverSettings.Glint.z, mult);
		prop.GlintDensityRandomization = lerp(prop.GlintDensityRandomization, SharedData::snowCoverSettings.Glint.w, mult);
		mult *= alt ? SharedData::snowCoverSettings.AltTint.w : SharedData::snowCoverSettings.MainTint.w;
		return prop;
	}
#		else

	float ApplySnow(inout float3 diffuse, inout float3 worldNormal, inout float glossiness, inout float shininess, float disp, float3 p, float skylight, float waterDist, float3 viewPos, float2 uv, float3x3 tbn)
	{
		bool alt;
		float mult = ApplySnowBase(worldNormal, uv, alt, disp, p, skylight, waterDist, viewPos);
		if (mult <= 0.0)
			return 0;
		// apparently LOD landscape color sampler clamps uvs
		float3 rdao = alt ? IceRDAO.Sample(SampColorSampler, uv).rgb : SnowRDAO.Sample(SampColorSampler, uv).rgb;
		diffuse = rdao.yyy * (alt ? SharedData::snowCoverSettings.AltTint.rgb : SharedData::snowCoverSettings.MainTint.rgb);
		//diffuse = frac(float3(uv.x, uv.y, 0));
		diffuse *= Color::PBRLightingScale;
		glossiness = lerp(glossiness, 1 - rdao.x, mult);  // yes these are named wrong not my fault bye
		shininess = lerp(shininess, 25 * 500 * 0.02, mult);
		diffuse *= rdao.z;
		float3 normal = normalize(mul(tbn, TransformNormal(alt ? IceNormal.Sample(SampNormalSampler, uv).rgb : SnowNormal.Sample(SampNormalSampler, uv).rgb)));
		worldNormal = normalize(lerp(worldNormal, MyReorientNormal(worldNormal, normal), mult));
		//glossiness = lerp(glossiness, 0.5 * pow(v * s, 3.0), mult);
		//shininess = lerp(shininess, max(1, pow(1 - v, 3.0) * 100), mult);
		mult *= alt ? SharedData::snowCoverSettings.AltTint.w : SharedData::snowCoverSettings.MainTint.w;
		return mult;
	}
#		endif
#	endif

}
#endif
