#include "ExponentialHeightFog/VolumetricFogCSCommon.hlsli"

RWTexture3D<float4> VBufferA : register(u0);

[numthreads(8, 8, 4)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (!ExponentialHeightFog::IsInsideVolumetricGrid(dispatchID))
		return;

	float viewDepth;
	float3 positionWS = ExponentialHeightFog::ComputeCellWorldPosition(dispatchID, 0.5f.xxx, viewDepth);

	float extinction = ExponentialHeightFog::EvaluateHeightFogExtinction(positionWS, FrameBuffer::CameraPosAdjust.xyz);
	float3 albedo = saturate(SharedData::exponentialHeightFogSettings.volumetricFogAlbedo.rgb);
	float3 scattering = extinction * albedo * SharedData::exponentialHeightFogSettings.volumetricFogAlbedo.a;

	VBufferA[dispatchID] = float4(scattering, extinction);
}
