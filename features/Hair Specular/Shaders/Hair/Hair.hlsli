#ifndef __HAIR_DEPENDENCY_HLSL__
#define __HAIR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"

#define MARSCHNER false

namespace Hair
{
	Texture2D<float> TexTangentShift : register(t73);

	// [Kajiya et al. 1989, "Rendering fur with three dimensional textures."]
	// https://doi.org/10.1145/74334.74361
	float3 D_KajiyaKay(float3 T, float3 H, float n)
	{
		float TH = dot(T, H);
		float sinTH = saturate(1 - TH * TH);
		float dirAtten = saturate(TH + 1);
		float norm = (n + 2) / (2 * Math::PI);
		return dirAtten * norm * pow(sinTH, 0.5 * n);
	}

	float HairF0()
	{
		const float n = 1.55;
		const float F0 = pow((1 - n) / (1 + n), 2);
		return F0;
	}

	// [Schlick et al. 1998, "An inexpensive brdf model for physically-based rendering."]
	// https://doi.org/10.1111/1467-8659.1330233
	float Hair_F(float CosTheta)
	{
		const float F0 = HairF0();
		return F0 + (1 - F0) * pow(1 - CosTheta, 5);
	}

	float3 ShiftTangent(float3 T, float3 N, float shift)
	{
		return normalize(T + N * shift);
	}

	// [Scheuermann 2004, "Hair Rendering and Shading"]
	// https://web.engr.oregonstate.edu/~mjb/cs557/Projects/Papers/HairRendering.pdf
	void GetHairDirectLightScheuermann(out float3 dirDiffuse, out float3 dirSpecular, float3 T, float3 L, float3 V, float3 N, float3 lightColor, float shininess, float2 uv, float3 baseColor)
	{
		const float3 H = normalize(L + V);
		const float NdotL = saturate(dot(N, L));
		const float NdotV = saturate(dot(N, V));

		dirDiffuse = NdotL * lightColor / Math::PI;

		float3 TshiftPrimary = T;
		float3 TshiftSecondary = T;

		if (SharedData::hairSpecularSettings.EnableTangentShift) {
			const float shift = TexTangentShift.SampleLevel(SampColorSampler, uv, SharedData::MipBias).x - 0.5;
			TshiftPrimary = ShiftTangent(T, N, shift + SharedData::hairSpecularSettings.PrimaryShift);
			TshiftSecondary = ShiftTangent(T, N, shift + SharedData::hairSpecularSettings.SecondaryShift);
		}

		const float3 specPrimary = D_KajiyaKay(TshiftPrimary, H, shininess);
		const float3 specSecondary = D_KajiyaKay(TshiftSecondary, H, shininess * 0.5);
		const float F = Hair_F(saturate(dot(H, V)));
		float3 specR = 0.25 * F * (specPrimary + specSecondary) * NdotL * saturate(NdotV * (3.4e+38));
		specR = Color::LinearToGamma(specR);
		float scatterFresnel1 = pow(saturate(-dot(L, V)), 9) * pow(saturate(1 - NdotV * NdotV), 12);
		float scatterFresnel2 = saturate(pow((1 - NdotV), 20));
		float3 specT = scatterFresnel1 + scatterFresnel2;
		float3 specTerm = specR + specT * baseColor;
		dirSpecular = specTerm * lightColor;
	}

	float Hair_g(float B, float Theta)
	{
		const float DenominatorB = max(B, 0.01f);
		return exp(-0.5 * pow(Theta, 2) / (B * B)) / (sqrt(2 * Math::PI) * DenominatorB);
	}

	// [Marschner et al. 2003, "Light reflection from human hair fibers."]
	// https://graphics.stanford.edu/papers/hair/hair-sg03final.pdf
	float3 D_Marschner(float3 L, float3 V, float3 N, float roughness, float3 baseColor, float Area, float Backlit)
	{
		const float VoL = dot(V, L);
		const float SinThetaL = dot(N, L);
		const float SinThetaV = dot(N, V);
		float CosThetaD = cos(0.5 * abs(asin(SinThetaV) - asin(SinThetaL)));

		const float3 Lp = L - SinThetaL * N;
		const float3 Vp = V - SinThetaV * N;
		const float CosPhi = dot(Lp, Vp) * rsqrt(dot(Lp, Lp) * dot(Vp, Vp) + 1e-4);
		const float CosHalfPhi = sqrt(saturate(0.5 + 0.5 * CosPhi));

		float n = 1.55;
		float n_prime = 1.19 / CosThetaD + 0.36 * CosThetaD;

		float Shift = 0.035;
		float Alpha[] = {
			-Shift * 2,
			Shift,
			Shift * 4,
		};

		float B[] = {
			Area + pow(roughness, 2),
			Area + pow(roughness, 2) / 2,
			Area + pow(roughness, 2) * 2,
		};

		float3 R, TT, TRT = 0;

		{
			const float sa = sin(Alpha[0]);
			const float ca = cos(Alpha[0]);
			float Shift = 2 * sa * (ca * CosHalfPhi * sqrt(1 - SinThetaV * SinThetaV) + sa * SinThetaV);

			float Mp = Hair_g(B[0] * sqrt(2.0) * CosHalfPhi, SinThetaL + SinThetaV - Shift);
			float Np = 0.25 * CosHalfPhi;
			float Fp = Hair_F(sqrt(saturate(0.5 + 0.5 * VoL)));
			R = Mp * Np * Fp * 2 * lerp(1, Backlit, saturate(-VoL));
		}

		{
			float Mp = Hair_g(B[1], SinThetaL + SinThetaV - Alpha[1]);
			float a = 1 / n_prime;
			float h = CosHalfPhi * (1 + a * (0.6 - 0.8 * CosPhi));
			float f = Hair_F(CosThetaD * sqrt(saturate(1 - h * h)));

			float Fp = pow(1 - f, 2);
			float3 Tp = pow(baseColor, 0.5 * sqrt(1 - pow(h * a, 2)) / CosThetaD);
			float Np = exp(-3.65 * CosPhi - 3.98);

			TT = Mp * Np * Fp * Tp * Backlit;
		}

		{
			float Mp = Hair_g(B[2], SinThetaL + SinThetaV - Alpha[2]);

			float f = Hair_F(CosThetaD * 0.5);
			float Fp = pow(1 - f, 2) * f;
			float3 Tp = pow(baseColor, 0.8 / CosThetaD);

			float Np = exp(17 * CosPhi - 16.78);

			TRT = Mp * Np * Fp * Tp;
		}

		return R + TT + TRT;
	}

	void GetHairDirectLightMarschner(out float3 dirDiffuse, out float3 dirSpecular, float3 T, float3 L, float3 V, float3 N, float3 lightColor, float shininess, float2 uv, float3 baseColor)
	{
		lightColor *= Math::PI;
		dirDiffuse = 0;
		dirSpecular = 0;
		const float roughness = 1 - 0.01 * shininess;

		dirSpecular += D_Marschner(L, V, N, roughness, baseColor, 0, 1) * lightColor;
	}

	void GetHairDirectLight(out float3 dirDiffuse, out float3 dirSpecular, float3 T, float3 L, float3 V, float3 N, float3 lightColor, float shininess, float2 uv, float3 baseColor)
	{
		if (!MARSCHNER)
			GetHairDirectLightScheuermann(dirDiffuse, dirSpecular, T, L, V, N, lightColor, shininess, uv, baseColor);
		else
			GetHairDirectLightMarschner(dirDiffuse, dirSpecular, T, L, V, N, lightColor, shininess, uv, baseColor);
	}

	// [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
	// https://blog.selfshadow.com/publications/s2013-shading-course/lazarov/s2013_pbs_black_ops_2_slides_v2.pdf
	float2 GetEnvBRDFApproxLazarov(float roughness, float NdotV)
	{
		const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
		const float4 c1 = { 1, 0.0425, 1.04, -0.04 };
		float4 r = roughness * c0 + c1;
		float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
		float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
		return AB;
	}

	float3 ShiftNormal(float3 T, float3 N, float shift)
	{
		float3 T_shifted = ShiftTangent(T, N, shift);
		float3 N_shifted = normalize(cross(T_shifted, cross(N, T_shifted)));
		return N_shifted;
	}

	float3 ShiftWorldNormal(float3 T, float3 N, float n, float2 uv)
	{
		const float shift = TexTangentShift.SampleLevel(SampColorSampler, uv, SharedData::MipBias).x - 0.5;
		float3 T_shifted = ShiftTangent(T, N, shift + n);
		float3 N_shifted = normalize(cross(T_shifted, cross(N, T_shifted)));
		return N_shifted;
	}

	void GetHairIndirectSpecularLobeWeights(out float3 diffuseLobeWeight, out float3 specularLobeWeightPrimary, out float3 specularLobeWeightSecondary, float3 T, float3 N, float3 V, float3 VN, float shininess, float2 uv, float3 baseColor)
	{
		const float roughnessPrimary = 1 - 0.01 * shininess;
		const float roughnessSecondary = 1 - 0.005 * shininess;
		const float NdotV = saturate(dot(N, V));

		if (MARSCHNER) {
			specularLobeWeightPrimary = 0;
			float3 L = normalize(V - N * dot(V, N));
			float NdotL = dot(N, L);
			float VdotL = dot(V, L);

			diffuseLobeWeight = D_Marschner(L, V, N, roughnessPrimary, baseColor * Math::PI, 0.2, 0);
			return;
		}

		float NdotVshifted = NdotV;
		float NdotVshifted2 = NdotV;

		if (SharedData::hairSpecularSettings.EnableTangentShift) {
			const float shift = TexTangentShift.SampleBias(SampColorSampler, uv, SharedData::MipBias).x - 0.5;
			NdotVshifted = saturate(dot(ShiftNormal(T, N, shift + SharedData::hairSpecularSettings.PrimaryShift), V));
			NdotVshifted2 = saturate(dot(ShiftNormal(T, N, shift + SharedData::hairSpecularSettings.SecondaryShift), V));
		}

		diffuseLobeWeight = baseColor;
		specularLobeWeightPrimary = 0;
		specularLobeWeightSecondary = 0;

		const float2 specularBRDFPrimary = GetEnvBRDFApproxLazarov(roughnessPrimary, NdotVshifted);
		const float2 specularBRDFSecondary = GetEnvBRDFApproxLazarov(roughnessSecondary, NdotVshifted2);

		const float3 F0 = HairF0();
		specularLobeWeightPrimary = F0 * specularBRDFPrimary.x + specularBRDFPrimary.y;
		diffuseLobeWeight *= (1 - specularLobeWeightPrimary);
		diffuseLobeWeight = saturate(diffuseLobeWeight);
		specularLobeWeightPrimary *= 1 + F0 * (1 / (specularBRDFPrimary.x + specularBRDFPrimary.y) - 1);

		specularLobeWeightSecondary = F0 * specularBRDFSecondary.x + specularBRDFSecondary.y;
		specularLobeWeightSecondary *= 1 + F0 * (1 / (specularBRDFSecondary.x + specularBRDFSecondary.y) - 1);
		specularLobeWeightSecondary *= baseColor;

		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon = horizon * horizon;
		specularLobeWeightPrimary *= horizon;
		specularLobeWeightSecondary *= horizon;
	}

	float3 Saturation(float3 color, float saturation)
	{
		float luminance = Color::RGBToLuminance(color);
		return saturate(lerp(float3(luminance, luminance, luminance), color, saturation));
	}

#if defined(DYNAMIC_CUBEMAPS)
#	if defined(SKYLIGHTING)
	float3 GetHairDynamicCubemapSpecularIrradiance(float2 uv, float2 ScreenUV, float3 T, float3 N, float3 VN, float3 V, float glossiness, float3 specLobePrim, float3 specLobeSec, sh2 skylighting)
#	else
	float3 GetHairDynamicCubemapSpecularIrradiance(float2 uv, float2 ScreenUV, float3 T, float3 N, float3 VN, float3 V, float glossiness, float3 specLobePrim, float3 specLobeSec)
#	endif
	{
		float3 SpecularIrradiance = 0;
		float3 N1 = N;
		float3 N2 = N;

		const float roughnessPrimary = 1 - 0.01 * glossiness;
		const float roughnessSecondary = 1 - 0.005 * glossiness;

		if (SharedData::hairSpecularSettings.EnableTangentShift) {
			const float shift = TexTangentShift.SampleLevel(SampColorSampler, uv, SharedData::MipBias).x - 0.5;
			N1 = ShiftNormal(T, N, shift + SharedData::hairSpecularSettings.PrimaryShift);
			N2 = ShiftNormal(T, N, shift + SharedData::hairSpecularSettings.SecondaryShift);
		}

#	if defined(SKYLIGHTING)
		SpecularIrradiance += DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(ScreenUV, N1, VN, V, roughnessPrimary, skylighting) * specLobePrim;
		SpecularIrradiance += DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(ScreenUV, N2, VN, V, roughnessSecondary, skylighting) * specLobeSec;
#	else
		SpecularIrradiance += DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(ScreenUV, N1, VN, V, roughnessPrimary) * specLobePrim;
		SpecularIrradiance += DynamicCubemaps::GetDynamicCubemapSpecularIrradiance(ScreenUV, N2, VN, V, roughnessSecondary) * specLobeSec;
#	endif
		return SpecularIrradiance;
	}
#endif
}
#endif  //__HAIR_DEPENDENCY_HLSL__