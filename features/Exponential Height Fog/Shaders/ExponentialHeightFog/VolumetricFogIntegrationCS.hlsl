Texture3D<float4> LightScattering : register(t0);
RWTexture3D<float4> IntegratedLightScattering : register(u0);

#include "ExponentialHeightFog/VolumetricFogCSCommon.hlsli"

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= VolumetricFogGridSize.xy))
		return;

	float3 accumulatedLighting = 0.0f.xxx;
	float accumulatedTransmittance = 1.0f;
	float accumulatedDepth = 0.0f;

	uint eyeIndex;
	float previousDepth;
	float3 previousPositionWS = ExponentialHeightFog::ComputeCellWorldPosition(uint3(dispatchID.xy, 0), float3(0.5f, 0.5f, 0.0f), eyeIndex, previousDepth);

	[loop] for (uint layerIndex = 0; layerIndex < VolumetricFogGridSize.z; layerIndex++)
	{
		uint3 layerCoordinate = uint3(dispatchID.xy, layerIndex);
		float4 scatteringAndExtinction = LightScattering[layerCoordinate];

		uint layerEyeIndex;
		float layerDepth;
		float3 layerPositionWS = ExponentialHeightFog::ComputeCellWorldPosition(layerCoordinate, 0.5f.xxx, layerEyeIndex, layerDepth);
		float stepLength = length(layerPositionWS - previousPositionWS);
		previousPositionWS = layerPositionWS;

		float extinction = max(scatteringAndExtinction.w, 0.0f);
		float transmittance = exp(-extinction * stepLength);

		accumulatedDepth += stepLength;
		float fadeIn = saturate(accumulatedDepth * VolumetricFogNearFadeInDistanceInv);

		float3 scatteringIntegratedOverSlice =
			fadeIn * (scatteringAndExtinction.rgb - scatteringAndExtinction.rgb * transmittance) / max(extinction, 1e-5f);
		accumulatedLighting += scatteringIntegratedOverSlice * accumulatedTransmittance;
		accumulatedTransmittance *= lerp(1.0f, transmittance, fadeIn);

		IntegratedLightScattering[layerCoordinate] = float4(max(accumulatedLighting, 0.0f.xxx), saturate(accumulatedTransmittance));
	}
}
