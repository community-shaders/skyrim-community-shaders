SamplerState LinearSampler : register(s0);
Texture3D<float4> VBufferA : register(t0);
Texture2DArray<float> DirectionalShadowMap : register(t1);
RWTexture3D<float4> LightScattering : register(u0);

#include "ExponentialHeightFog/VolumetricFogCSCommon.hlsli"

struct DirectionalShadowLightData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};

StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t98);

float SampleDirectionalShadow(float3 positionWS, uint eyeIndex)
{
	if (InInterior || HideSky || InMapMenu)
		return 1.0f;
	if (VolumetricFogHasDirectionalShadowMap == 0)
		return 1.0f;

	DirectionalShadowLightData directionalShadowLightData = DirectionalShadowLights[0];
	float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(positionWS, eyeIndex));
	if (shadowMapDepth >= directionalShadowLightData.EndSplitDistances.y)
		return 1.0f;

	float splitDenom = max(directionalShadowLightData.EndSplitDistances.x - directionalShadowLightData.StartSplitDistances.y, 1e-4f);
	float cascadeSelect = saturate((shadowMapDepth - directionalShadowLightData.StartSplitDistances.y) / splitDenom);
	uint primaryCascade = (uint)cascadeSelect;

	float3 absolutePositionWS = positionWS + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
	float3 positionLS = mul(directionalShadowLightData.ShadowProj[primaryCascade], float4(absolutePositionWS, 1.0f)).xyz;
	if (any(positionLS.xy < 0.0f) || any(positionLS.xy > 1.0f))
		return 1.0f;

	float shadowMapValue = DirectionalShadowMap.SampleLevel(LinearSampler, float3(positionLS.xy, primaryCascade), 0);
	float shadow = shadowMapValue >= positionLS.z - exponentialHeightFogSettings.volumetricShadowBias ? 1.0f : 0.0f;

	[branch] if (cascadeSelect > 0.0f && cascadeSelect < 1.0f)
	{
		uint secondaryCascade = 1u - primaryCascade;
		float3 secondaryLS = mul(directionalShadowLightData.ShadowProj[secondaryCascade], float4(absolutePositionWS, 1.0f)).xyz;
		if (!any(secondaryLS.xy < 0.0f) && !any(secondaryLS.xy > 1.0f)) {
			float secondaryShadowMapValue = DirectionalShadowMap.SampleLevel(LinearSampler, float3(secondaryLS.xy, secondaryCascade), 0);
			float secondaryShadow = secondaryShadowMapValue >= secondaryLS.z - exponentialHeightFogSettings.volumetricShadowBias ? 1.0f : 0.0f;
			shadow = lerp(shadow, secondaryShadow, cascadeSelect);
		}
	}

	float fade = saturate(shadowMapDepth / max(directionalShadowLightData.EndSplitDistances.y, 1.0f));
	float fadeFactor = 1.0f - pow(fade * fade, 8.0f);
	return lerp(1.0f, shadow, fadeFactor);
}

[numthreads(8, 8, 4)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (!ExponentialHeightFog::IsInsideVolumetricGrid(dispatchID))
		return;

	float4 materialScatteringAndExtinction = VBufferA[dispatchID];
	float extinction = materialScatteringAndExtinction.w;

	uint eyeIndex;
	float viewDepth;
	float3 positionWS = ExponentialHeightFog::ComputeCellWorldPosition(dispatchID, 0.5f.xxx, eyeIndex, viewDepth);

	float3 viewDirection = normalize(positionWS);
	float phase = ExponentialHeightFog::HenyeyGreenstein(
		dot(normalize(DirLightDirection.xyz), -viewDirection),
		exponentialHeightFogSettings.volumetricFogScatteringDistribution);

	float directionalShadow = SampleDirectionalShadow(positionWS, eyeIndex);
	float3 directionalScattering =
		DirLightColor.xyz *
		exponentialHeightFogSettings.volumetricDirectionalScatteringIntensity *
		directionalShadow *
		phase *
		materialScatteringAndExtinction.rgb;

	float3 emissive = exponentialHeightFogSettings.volumetricFogEmissive.rgb *
	                  exponentialHeightFogSettings.volumetricFogEmissive.a *
	                  extinction;

	LightScattering[dispatchID] = float4(max(directionalScattering + emissive, 0.0f.xxx), extinction);
}
