#include "ExponentialHeightFog/VolumetricFogCSCommon.hlsli"

RWTexture2D<float> ConservativeDepthTexture : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= VolumetricFogGridSize.xy))
		return;

	float2 volumeUVMin = (float2(dispatchID.xy) - 0.5f.xx) * VolumetricFogInvGridSize.xy;
	float2 volumeUVMax = (float2(dispatchID.xy + 1u) + 0.5f.xx) * VolumetricFogInvGridSize.xy;

	float2 eyeUVMin = saturate(volumeUVMin);
	float2 eyeUVMax = saturate(volumeUVMax);

	int2 minCoord = SharedData::ConvertUVToSampleCoord(min(eyeUVMin, eyeUVMax)).xy;
	int2 maxCoord = SharedData::ConvertUVToSampleCoord(max(eyeUVMin, eyeUVMax)).xy - 1;
	maxCoord = max(maxCoord, minCoord);

	int2 bufferMax = int2(SharedData::BufferDim.xy) - 1;
	minCoord = clamp(minCoord, int2(0, 0), bufferMax);
	maxCoord = clamp(maxCoord, int2(0, 0), bufferMax);

	float conservativeDepth = 0.0f;
	for (int y = minCoord.y; y <= maxCoord.y; y++) {
		for (int x = minCoord.x; x <= maxCoord.x; x++) {
			float rawDepth = SharedData::DepthTexture.Load(int3(x, y, 0)).x;
			conservativeDepth = max(conservativeDepth, SharedData::GetScreenDepth(rawDepth));
		}
	}

	ConservativeDepthTexture[dispatchID.xy] = conservativeDepth;
}
