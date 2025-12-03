// RCAS - Robust Contrast Adaptive Sharpening
// Based on AMD FidelityFX FSR1
// https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK

cbuffer RCASConfig : register(b0)
{
	float sharpness;
	float3 pad;
};

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Dest : register(u0);

float getRCASLuma(float3 rgb)
{
	return dot(rgb, float3(0.5, 1.0, 0.5));
}

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 texDim;
	Dest.GetDimensions(texDim.x, texDim.y);

	if (DTid.x >= texDim.x || DTid.y >= texDim.y)
		return;

	int2 center = int2(DTid.xy);
	int2 minCoord = int2(0, 0);
	int2 maxCoord = int2(texDim - 1);

	int2 eCoord = clamp(center, minCoord, maxCoord);
	int2 bCoord = clamp(eCoord + int2(0, -1), minCoord, maxCoord);
	int2 dCoord = clamp(eCoord + int2(-1, 0), minCoord, maxCoord);
	int2 fCoord = clamp(eCoord + int2(1, 0), minCoord, maxCoord);
	int2 hCoord = clamp(eCoord + int2(0, 1), minCoord, maxCoord);

	float3 e = Source.Load(int3(eCoord, 0)).rgb;

	float3 b = Source.Load(int3(bCoord, 0)).rgb;
	float3 d = Source.Load(int3(dCoord, 0)).rgb;
	float3 f = Source.Load(int3(fCoord, 0)).rgb;
	float3 h = Source.Load(int3(hCoord, 0)).rgb;

	float bL = getRCASLuma(b);
	float dL = getRCASLuma(d);
	float eL = getRCASLuma(e);
	float fL = getRCASLuma(f);
	float hL = getRCASLuma(h);

	// Noise detection
	float nz = (bL + dL + fL + hL) * 0.25 - eL;
	float range = max(max(max(bL, dL), max(hL, fL)), eL) - min(min(min(bL, dL), min(eL, fL)), hL);
	nz = saturate(abs(nz) * rcp(range));
	nz = -0.5 * nz + 1.0;

	// Min and max of ring
	float3 minRGB = min(min(b, d), min(f, h));
	float3 maxRGB = max(max(b, d), max(f, h));

	float2 peakC = float2(1.0, -4.0);

	float3 hitMin = minRGB * rcp(4.0 * maxRGB);
	float3 hitMax = (peakC.xxx - maxRGB) * rcp(4.0 * minRGB + peakC.yyy);
	float3 lobeRGB = max(-hitMin, hitMax);
	float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * sharpness;

	lobe *= nz;

	float rcpL = rcp(4.0 * lobe + 1.0);
	float3 output = ((b + d + f + h) * lobe + e) * rcpL;

	Dest[DTid.xy] = float4(output, 1.0);
}
