#ifndef __HAIR_DEPENDENCY_HLSL__
#define __HAIR_DEPENDENCY_HLSL__

namespace Hair
{
    Texture2D<float> TexTangentShift : register(t73);

    float3 D_KajiyaKay(float3 T, float3 H, float n)
    {
        float TH = dot(T, H);
        float sinTH = saturate(1 - TH * TH);
        float dirAtten = saturate(TH + 1);
        float norm = (n + 2) / (2 * Math::PI);
        return dirAtten * norm * pow(sinTH, 0.5 * n);
    }

    float3 F_Schlick(float cosTheta, float3 F0)
    {
        // R(θ) = R0 + (1 - R0) * (1 - cosθ)^5
        return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
    }

    float3 ShiftTangent(float3 T, float3 N, float shift)
    {
        return normalize(T + N * shift);
    }

    float2 GetEnvBRDFApproxLazarov(float roughness, float NdotV)
	{
		const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
		const float4 c1 = { 1, 0.0425, 1.04, -0.04 };
		float4 r = roughness * c0 + c1;
		float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
		float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
		return AB;
	}

    float3 GetHairIndirectSpecularLobeWeights(float3 N, float3 V, float3 VN, float shininess)
    {	
        const float roughness = 1 - 0.01 * shininess * 0.75;
        const float NdotV = saturate(dot(N, V));

        float3 specularLobeWeight = 0;

        const float2 specularBRDF = GetEnvBRDFApproxLazarov(roughness, NdotV);

        const float3 F0 = { 0.046, 0.046, 0.046 };
        specularLobeWeight = F0 * specularBRDF.x + specularBRDF.y;
        specularLobeWeight *= 1 + F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

        float3 R = reflect(-V, N);
        float horizon = min(1.0 + dot(R, VN), 1.0);
        horizon = horizon * horizon;
        specularLobeWeight *= horizon;
        return specularLobeWeight;
    }

    float3 Saturation(float3 color, float saturation)
    {
        float luminance = RGBToLuminance(color);
        return lerp(float3(luminance, luminance, luminance), color, saturation);
    }
}
#endif  //__HAIR_DEPENDENCY_HLSL__