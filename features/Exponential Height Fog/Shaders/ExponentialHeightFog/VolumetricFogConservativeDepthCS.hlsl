#include "ExponentialHeightFog/VolumetricFogCSCommon.hlsli"

RWTexture2D<float> ConservativeDepthTexture : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= VolumetricFogGridSize.xy))
		return;

	float2 volumeUVMin = (float2(dispatchID.xy) - 0.5f.xx) * VolumetricFogInvGridSize.xy;
	float2 volumeUVMax = (float2(dispatchID.xy + 1u) + 0.5f.xx) * VolumetricFogInvGridSize.xy;

	float2 eyeUVMin = saturate(volumeUVMin);
	float2 eyeUVMax = saturate(volumeUVMax);

	// The volume spans the whole view, while the depth buffer occupies the active
	// dynamic-resolution region. Include every depth pixel touched by this froxel.
	float2 renderSize = SharedData::BufferDim.xy * FrameBuffer::DynamicResolutionParams1.xy;
	float2 sampleCoordMin = min(eyeUVMin, eyeUVMax) * renderSize;
	float2 sampleCoordMax = max(eyeUVMin, eyeUVMax) * renderSize;

	int2 minCoord = int2(floor(sampleCoordMin));
	int2 maxCoord = int2(ceil(sampleCoordMax)) - 1;
	maxCoord = max(maxCoord, minCoord);

	int2 bufferMax = max(int2(ceil(renderSize)) - 1, int2(0, 0));
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
