#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Random.hlsli"
#if defined(PSHADER)

namespace SnowCover
{

	Texture2D<float4> SnowAlbedo : register(t38);
	Texture2D<float3> SnowNormal : register(t39);
	Texture2D<float4> SnowRmaos : register(t40);
	Texture2D<float4> IceAlbedo : register(t41);
	Texture2D<float3> IceNormal : register(t42);
	Texture2D<float4> IceRmaos : register(t43);
	Texture2D<float> SnowMap : register(t44);

	// https://blog.selfshadow.com/publications/blending-in-detail/
	// for when s = (0,0,1)
	float3 MyReorientNormal(float3 n1, float3 n2)
	{
		n1 += float3(0, 0, 1);
		n2 *= float3(-1, -1, 1);

		return n1 * dot(n1, n2) / n1.z - n2;
	}

	

	

	float GetHeightMult(float3 p)
	{
		float2 scale = SharedData::snowCoverSettings.mapScale;
		float2 offset = SharedData::snowCoverSettings.mapOffset;
		float2 uv = p.xy*scale + offset;
		float height_tresh = p.z - SharedData::snowCoverSettings.SnowHeightOffset + (SnowMap.SampleLevel(SampColorSampler, uv, 0) - 0.5)*SharedData::snowCoverSettings.mapZscale;
		return height_tresh;
	}

	float GetEnvironmentalMultiplier(float3 p)
	{
		return (GetHeightMult(p) + SharedData::snowCoverSettings.SeasonalAltitude) / SharedData::snowCoverSettings.BlendSmoothness;
	}

	void ApplyFoliageColor(inout float3 color, float env_mult)
	{
		float gmult = saturate(env_mult - SharedData::snowCoverSettings.FoliageHeightOffset / 5000);
		float3 hsv = Color::RGBtoHSV(color);
		if (hsv.x > 0.55)
			hsv.x = frac(lerp(hsv.x, 1.1, gmult) * 2);
		else
			hsv.x = lerp(hsv.x, 0.1, gmult);
		hsv.y *= lerp(1, 0.25, 4.0 * gmult * (1.0 - gmult));
		color = Color::HSVtoRGB(hsv);
	}

	void ApplySnowFoliage(inout float3 color, float3 worldNormal, float3 p, float skylight, float viewDist)
	{
		float env_mult = GetEnvironmentalMultiplier(p);
		float distMult = 1 - smoothstep(10000, 30000, viewDist);
		float weatherMult = distMult * SharedData::snowCoverSettings.TimeSnowing * max(500, SharedData::snowCoverSettings.SnowingDensity) / 500;
		float mult = SharedData::snowCoverSettings.MainTint.a * saturate(env_mult);
		mult = skylight * saturate(mult + weatherMult) * smoothstep(SharedData::snowCoverSettings.minAngle, SharedData::snowCoverSettings.maxAngle, worldNormal.z);
#	if defined(GRASS)
		if (SharedData::snowCoverSettings.AffectGrassTint) {
#	else
		if (SharedData::snowCoverSettings.AffectTreeTint) {
#	endif
			ApplyFoliageColor(color, env_mult);
		}
		if (mult < 0.01)
			return;
		float2 uv = frac(SharedData::snowCoverSettings.UVScale * (p.xy + worldNormal.xy) / 100);
		float3 diffuse = Color::TrueLinearToGamma(SnowAlbedo.Sample(SampColorSampler, uv).rgb) * SharedData::snowCoverSettings.MainTint.rgb * Color::PBRLightingScale;

		color = lerp(color, diffuse, mult);
	}

#	if !defined(BASIC_SNOW_COVER)
	float ApplySnowBase(float3 worldNormal, inout float2 uv, out bool alt, float disp, float3 p, float skylight, float waterDist, float viewDist)
	{
		// the range in which water level affects snow
		waterDist = smoothstep(-64, 8, -waterDist - disp);
		// distance from the camera in which weather has effect, this extends far beyond where lod starts
		float weatherRange = 1 - smoothstep(20000, 40000 + 1000 * sin(p.z * 0.001 + cos(p.x * p.y * 0.001)), viewDist);
		// the amount of snow based on weather, TimeSnowing transitions smoothly between -1 in rain and 1 when snowing
		float weatherMult = weatherRange * pow(SharedData::snowCoverSettings.TimeSnowing, 3) * max(500, SharedData::snowCoverSettings.SnowingDensity) / 500;
		weatherMult = clamp((weatherMult + disp * 0.1) * max(SharedData::snowCoverSettings.minAngle, worldNormal.z), -1, 1);
		// the amount of snow based on season and weather
		float env_mult = saturate(max(saturate(GetEnvironmentalMultiplier(p) + disp), weatherMult)) - waterDist;
#		if !defined(LANDSCAPE) && !defined(LOD)
		// removes pure white lod object billboard trees (ultra billboards) that have no special flags and are not marked as lod
		float distMult = 1 - smoothstep(4096+2048,9192, viewDist)*0.5;
#		else
		float distMult = 1;
#		endif
		float mult = distMult * skylight * env_mult * smoothstep(SharedData::snowCoverSettings.minAngle, SharedData::snowCoverSettings.maxAngle, worldNormal.z);
		if (mult < 0.001){
			alt = false;
			return 0;
		}
		float main_mult = (1 - abs(worldNormal.z - SharedData::snowCoverSettings.peakMainAngle)) + min(0, weatherMult) * SharedData::snowCoverSettings.minAngle;
		float alt_mult = (1 - abs(worldNormal.z - SharedData::snowCoverSettings.peakAltAngle)) + sin(p.z * 0.01 + cos(p.x * p.y * 0.01) * 0.025) * 0.05;
		alt = alt_mult > main_mult;
		// apparently LOD landscape color sampler clamps uvs
		uv = frac(SharedData::snowCoverSettings.UVScale * (p.xy / 100 + worldNormal.xy * disp));
		return mult;
	}

#		if defined(TRUE_PBR)
	PBR::SurfaceProperties ApplySnowPBR(inout float3 diffuse, inout float3 worldNormal, out float mult, float disp, float3 p, float skylight, float waterDist, float viewDist, PBR::SurfaceProperties prop, float2 uv)
	{
		bool alt;
		mult = ApplySnowBase(worldNormal, uv, alt, disp, p, skylight, waterDist, viewDist);
		if (mult <= 0.0)
			return prop;
		float4 rmaos;
		if (alt){
			rmaos = IceRmaos.Sample(SampColorSampler, uv);
			float3 albedo = IceAlbedo.Sample(SampColorSampler, uv).rgb;
			albedo = Color::Diffuse(albedo) * SharedData::snowCoverSettings.AltTint.rgb;
			diffuse = lerp(diffuse, albedo, mult * SharedData::snowCoverSettings.AltTint.w);
			worldNormal = TransformNormal(IceNormal.Sample(SampNormalSampler, uv).rgb);
			prop.F0 = lerp(prop.F0, rmaos.w *SharedData::snowCoverSettings.altSpec, mult);

		}
		else{
			rmaos = SnowRmaos.Sample(SampColorSampler, uv);
			float3 albedo = SnowAlbedo.Sample(SampColorSampler, uv).rgb;
			albedo = Color::Diffuse(albedo) * SharedData::snowCoverSettings.MainTint.rgb;
			diffuse = lerp(diffuse, albedo, mult * SharedData::snowCoverSettings.MainTint.w);
			worldNormal = TransformNormal(SnowNormal.Sample(SampNormalSampler, uv).rgb);
			prop.F0 = lerp(prop.F0, rmaos.w * SharedData::snowCoverSettings.mainSpec, mult);

		}
		prop.Roughness = lerp(prop.Roughness, rmaos.x, mult);
		prop.Metallic = lerp(prop.Metallic, rmaos.y, mult);
		prop.AO = lerp(prop.AO, rmaos.z, mult * 0.5); //always leave a part of the original ao to make it more interesting
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
		float4 rmaos;
		if(alt){
			float3 albedo = IceAlbedo.Sample(SampColorSampler, uv).rgb;
			albedo = Color::TrueLinearToGamma(albedo) * SharedData::snowCoverSettings.AltTint.rgb * Color::PBRLightingScale;
			rmaos = IceRmaos.Sample(SampColorSampler, uv);
			shininess = lerp(shininess, 25 * 500 * SharedData::snowCoverSettings.altSpec * rmaos.w, mult);
			worldNormal = TransformNormal(IceNormal.Sample(SampNormalSampler, uv).rgb);
			diffuse = lerp(diffuse, rmaos.z * albedo, mult * SharedData::snowCoverSettings.AltTint.w);

		}
		else{
			float3 albedo = SnowAlbedo.Sample(SampColorSampler, uv).rgb;
			albedo = Color::TrueLinearToGamma(albedo) * SharedData::snowCoverSettings.MainTint.rgb * Color::PBRLightingScale;
			rmaos = SnowRmaos.Sample(SampColorSampler, uv);
			shininess = lerp(shininess, 25 * 500 *  SharedData::snowCoverSettings.mainSpec * rmaos.w, mult);
			worldNormal = TransformNormal(SnowNormal.Sample(SampNormalSampler, uv).rgb);
			diffuse = lerp(diffuse, rmaos.z * albedo, mult * SharedData::snowCoverSettings.MainTint.w);

		}
		glossiness = lerp(glossiness, 1 - rmaos.x, mult);
		return mult;
	}
#		endif
#	endif

}
#endif
