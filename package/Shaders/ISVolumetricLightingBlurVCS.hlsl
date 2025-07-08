Texture2D<float> InVLTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
RWTexture2D<float> OutVLTexture : register(u0);

#ifndef TG_DIM
#	define TG_DIM 256
#endif

#ifndef WINDOW
#	define WINDOW 12
#endif

cbuffer CBSize : register(b0)
{
	float4 invSize;
	float4 maxUV;
}

cbuffer VLData : register(b1)
{
	int2 screenSize;
	int2 screenSizeMin1;
}

groupshared float vl[TG_DIM];
groupshared float depth[TG_DIM];

[numthreads(1, TG_DIM, 1)] void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID) {
	int idx = groupThreadId.y;
	int base = idx - WINDOW;
	int x = groupId.x;
	int y = groupId.y * (TG_DIM - WINDOW * 2) + base;

	int2 pix = min(int2(x, y), screenSizeMin1.xy);
	float vlValue = InVLTexture[pix];
	vl[idx] = vlValue;
	float depthValue = DepthTexture[pix];
	depth[idx] = depthValue;

	GroupMemoryBarrierWithGroupSync();

	if (base >= 0 && base < TG_DIM - WINDOW * 2) {
		int min12 = idx - 12;
		int min6 = idx - 6;
		int plus6 = idx + 6;
		int plus12 = idx + 12;

		float diff = depthValue * 4 - depth[min12] - depth[min6] - depth[plus6] - depth[plus12];
		vlValue = abs(diff) <= 0.002f ? vl[min12] * 0.178400f + vl[min6] * 0.210431f + vlValue * 0.222338f + vl[plus6] * 0.210431f + vl[plus12] * 0.178400f : vlValue;

		OutVLTexture[int2(x, y)] = vlValue;
	}
}
