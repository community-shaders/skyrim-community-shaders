#include "ScreenSpaceReflections/common.hlsli"

Texture2D<float> srcDepth : register(t0);

#if defined(FUSED_HIZ_MIPS)

RWTexture2D<float> outDepth0 : register(u0);
RWTexture2D<float> outDepth1 : register(u1);
RWTexture2D<float> outDepth2 : register(u2);
RWTexture2D<float> outDepth3 : register(u3);
RWTexture2D<float> outDepth4 : register(u4);

groupshared float sharedDepth[8][8];

float Min4(float a, float b, float c, float d)
{
	return min(min(a, b), min(c, d));
}

[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {
	const uint2 textureSize = (uint2)TexDim;
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixelCoord = baseCoord << 1;

	// Do not return before the group barriers. The C++ path only selects this
	// variant when both texture dimensions are 16-aligned, so the padded dynamic-
	// resolution edge remains in bounds. Clamp source reads to the dynres active
	// region so padding threads never feed stale depth outside it into the min-z
	// reduction (they fold the edge texel instead).
	const uint2 maxCoord = min(textureSize, (uint2)FrameDim) - 1;
	const float depth0 = srcDepth[min(pixelCoord + uint2(0, 0), maxCoord)];
	const float depth1 = srcDepth[min(pixelCoord + uint2(1, 0), maxCoord)];
	const float depth2 = srcDepth[min(pixelCoord + uint2(0, 1), maxCoord)];
	const float depth3 = srcDepth[min(pixelCoord + uint2(1, 1), maxCoord)];

	outDepth0[pixelCoord + uint2(0, 0)] = depth0;
	outDepth0[pixelCoord + uint2(1, 0)] = depth1;
	outDepth0[pixelCoord + uint2(0, 1)] = depth2;
	outDepth0[pixelCoord + uint2(1, 1)] = depth3;

	float reducedDepth = Min4(depth0, depth1, depth2, depth3);
	outDepth1[baseCoord] = reducedDepth;
	sharedDepth[groupThreadID.y][groupThreadID.x] = reducedDepth;

	GroupMemoryBarrierWithGroupSync();

	if (all((groupThreadID & 1) == 0)) {
		reducedDepth = Min4(
			sharedDepth[groupThreadID.y + 0][groupThreadID.x + 0],
			sharedDepth[groupThreadID.y + 0][groupThreadID.x + 1],
			sharedDepth[groupThreadID.y + 1][groupThreadID.x + 0],
			sharedDepth[groupThreadID.y + 1][groupThreadID.x + 1]);
		outDepth2[baseCoord >> 1] = reducedDepth;
		sharedDepth[groupThreadID.y][groupThreadID.x] = reducedDepth;
	}

	GroupMemoryBarrierWithGroupSync();

	if (all((groupThreadID & 3) == 0)) {
		reducedDepth = Min4(
			sharedDepth[groupThreadID.y + 0][groupThreadID.x + 0],
			sharedDepth[groupThreadID.y + 0][groupThreadID.x + 2],
			sharedDepth[groupThreadID.y + 2][groupThreadID.x + 0],
			sharedDepth[groupThreadID.y + 2][groupThreadID.x + 2]);
		outDepth3[baseCoord >> 2] = reducedDepth;
		sharedDepth[groupThreadID.y][groupThreadID.x] = reducedDepth;
	}

	GroupMemoryBarrierWithGroupSync();

	if (all((groupThreadID & 7) == 0)) {
		reducedDepth = Min4(
			sharedDepth[groupThreadID.y + 0][groupThreadID.x + 0],
			sharedDepth[groupThreadID.y + 0][groupThreadID.x + 4],
			sharedDepth[groupThreadID.y + 4][groupThreadID.x + 0],
			sharedDepth[groupThreadID.y + 4][groupThreadID.x + 4]);
		outDepth4[baseCoord >> 3] = reducedDepth;
	}
}

#else

RWTexture2D<float> outDepth : register(u0);

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	outDepth[dtid] = srcDepth[dtid];
}

#endif
