#ifndef PHYSICAL_SKY_UPPER_CLOUD_TILES_HLSLI
#define PHYSICAL_SKY_UPPER_CLOUD_TILES_HLSLI

Texture2D<uint> TexUpperTiles : register(t87);
RWTexture2D<uint> RWUpperTiles : register(u4);
groupshared uint UpperTileActive;

[numthreads(8, 8, 1)] void classifyUpperTiles(uint2 tid : SV_DispatchThreadID, uint2 group : SV_GroupID, uint lane : SV_GroupIndex) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (lane == 0u)
		UpperTileActive = 0u;
	GroupMemoryBarrierWithGroupSync();
	const bool valid = all(tid < uint2(info.lowFrameDim));
	if (valid) {
		const uint2 pixel = tid * 4u + CloudPhaseOffset();
		const float3 direction = CloudScreenRay(pixel);
		const float deviceDepth = CloudSceneDeviceDepth(pixel, 1u);
		const float sceneDistance = deviceDepth >= 1.0 - 1e-6 ? 0.0 : SharedData::GetScreenDepth(deviceDepth) / max(abs(mul(FrameBuffer::CameraView, float4(direction, 0)).z), 1e-6);
		const float3 eye = FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ);
		const float first = UpperCloudFirstDistance(eye, direction, sceneDistance);
		if (first < 1e30 && (TexVolLowTr[tid] > 0.0 || !UpperCloudOccluded(first, TexVolLowAux[tid].x)))
			InterlockedOr(UpperTileActive, 1u);
	}
	GroupMemoryBarrierWithGroupSync();
	if (lane == 0u)
		RWUpperTiles[group] = UpperTileActive;
	if (valid && UpperTileActive == 0u) {
		VolumetricCloudResult empty = (VolumetricCloudResult)0;
		empty.transmittance = 1.0;
		empty.cloud_depth = info.rayMarchRange;
		StoreUpperCloudPixel(empty, tid);
	}
}

	[numthreads(8, 8, 1)] void renderUpperTiles(uint2 tid : SV_DispatchThreadID, uint2 group : SV_GroupID)
{
	// Uniform group branch keeps visibility bookkeeping out of the long march.
	if (TexUpperTiles[group] != 0u)
		RenderUpperViewPixel(tid);
}

#endif
