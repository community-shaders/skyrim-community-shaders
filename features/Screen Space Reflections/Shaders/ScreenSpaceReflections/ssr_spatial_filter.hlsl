#include "ScreenSpaceReflections/ssr_common.hlsli"

Texture2D<float4> SSRColorTexture : register(t0);
Texture2D<float4> HitPDFTexture : register(t1);
Texture2D<float> DepthTextureMips : register(t5);
Texture2DArray<float> NoiseTexture : register(t6);

RWTexture2D<float4> SpatialOutput : register(u0);

cbuffer SSRCB : register(b1)
{
    uint MaxSteps;
    uint MaxMips;
    float Thickness;
    float SpatialRadius;
    float RoughnessMask;
    float TemporalScale;
    float TemporalWeight;
    float BilateralRadius;
    float ColorWeight;
    float DepthWeight;
    float NormalWeight;
    float BRDFBias;
};

float GetEdgeStoppNormalWeight(float3 normal_p, float3 normal_q, float sigma)
{
    return pow(max(dot(normal_p, normal_q), 0.0), sigma);
}

float GetEdgeStopDepthWeight(float x, float m, float sigma)
{
    float a = length(x - m) / sigma;
    a *= a;
    return exp(-0.5 * a);
}

float SSR_D(float Roughness, float NdotH)
{
	float m = Roughness * Roughness;
	float m2 = m * m;
	float D = m2 / (3.14 * Pow2(Pow2(NdotH) * (m2 - 1) + 1));
	return D;
}

float SSR_G(float Roughness, float NdotL, float NdotV)
{
	float m = Roughness * Roughness;
	float m2 = m * m;

	float G_L = 1.0f / (NdotL + sqrt(m2 + (1 - m2) * NdotL * NdotL));
	float G_V = 1.0f / (NdotV + sqrt(m2 + (1 - m2) * NdotV * NdotV));
	float G = G_L * G_V;
	return G;
}

float BRDF_DG(float3 V, float3 L, float3 N, float Roughness)
{
    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N,H));
    float NdotL = saturate(dot(N,L));
    float NdotV = saturate(dot(N,V));
    float D = SSR_D(Roughness, NdotH);
    float G = SSR_G(Roughness, NdotL, NdotV);
    return D * G;
}

[numthreads(16, 16, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw;
    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // Adjust for dynamic res
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

    uint2 pixelCoord = SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy;
    float sceneDepth = DepthTextureMips.SampleLevel(LinearSampler, uv, 0);

    if (sceneDepth <= 0.0f)
    {
        SpatialOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, sceneDepth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;
    float3 positionVS = FrameBuffer::WorldToView(positionWS.xyz, true, eyeIndex);

    float3 normalVS;
    float roughness;
    GetNormalRoughness(DTid.xy, normalVS, roughness);
    roughness = clamp(roughness, 0.05f, 1.0f);

    float3 worldNormal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

    float numWeight = 0.0f;
    // float3 momentA, momentB = float3(0, 0, 0);
    float4 spatialColor = float4(0, 0, 0, 0);

    uint2 uintRandom = Rand3DPCG16(uint3(DTid.xy + 0.5, SharedData::FrameCount & 8)).xy;

    int2 sampleCoords[4] = {
        int2(1, 0),
        int2(-1, 0),
        int2(0, 1),
        int2(0, -1)
    };

    [loop]
    for (int i = 0; i < 15; ++i)
    {
        float2 offsetRotation = Hammersley16(i, 15, uintRandom) * 2 - 1;
        float2x2 rotationMatrix = float2x2(offsetRotation.x, offsetRotation.y,
                                           -offsetRotation.y, offsetRotation.x);
        float2 offsetUV = kStackowiakSampleSet4[i] * SharedData::BufferDim.zw * SpatialRadius;
        offsetUV = mul(rotationMatrix, offsetUV) + uv;
        // float2 offsetUV = uv + (kStackowiakSampleSet4[i] * SharedData::BufferDim.zw * SpatialRadius);

        float offsetDepth = DepthTextureMips.SampleLevel(LinearSampler, offsetUV, 0);
        float3 offsetNormalVS;
        float offsetRoughness;
        GetNormalRoughnessUV(offsetUV, offsetNormalVS, offsetRoughness);
        float3 offsetWorldNormal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(offsetNormalVS, 0)).xyz);
        float4 hitColor = SSRColorTexture.SampleLevel(LinearSampler, offsetUV, 0);
        float4 hitPDF = HitPDFTexture.SampleLevel(LinearSampler, offsetUV, 0);

        // momentA += hitColor.xyz;
        // momentB += hitColor.xyz * hitColor.xyz;

        float4 hitPositionWS = float4(2 * float2(offsetUV.x, -offsetUV.y + 1) - 1, offsetDepth, 1);
        hitPositionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], hitPositionWS);
        hitPositionWS.xyz = hitPositionWS.xyz / hitPositionWS.w;
        float3 hitPositionVS = FrameBuffer::WorldToView(hitPositionWS.xyz, true, eyeIndex);

        float depthWeight = GetEdgeStopDepthWeight(sceneDepth, offsetDepth, 0.001f);
        float normalWeight = GetEdgeStoppNormalWeight(worldNormal, offsetWorldNormal, 64);
        float brdfWeight = BRDF_DG(normalize(-positionVS), normalize(hitPositionVS - positionVS), normalVS, offsetRoughness) / max(hitPDF.w, 1e-6f);
        float weight = depthWeight * normalWeight;
        numWeight += weight;
        spatialColor += hitColor * weight;
    }

    spatialColor /= max(numWeight, 1e-6f);
    // momentA /= 15.0f;
    // momentB /= 15.0f;
    // float3 varianceColor = (momentB - momentA * momentA);
    // float variance = max(varianceColor.x, max(varianceColor.y, varianceColor.z));
    
    SpatialOutput[DTid.xy] = spatialColor;
}