// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

/// RCAS - Robust Contrast Adaptive Sharpening
/// Based on AMD FidelityFX FSR1 RCAS algorithm.
/// https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
///
/// Applies adaptive sharpening using a 3x3 cross pattern neighborhood.
/// Includes noise detection to avoid sharpening noise/grain.

cbuffer RCASConfig : register(b0)
{
	float sharpness;  ///< Sharpening strength (0 = none, higher = sharper)
	float3 pad;
};

Texture2D<float4> Source : register(t0);   ///< Input texture to sharpen
RWTexture2D<float4> Dest : register(u0);   ///< Output sharpened texture

/// Compute perceptual luma using RCAS weighting (emphasizes green channel)
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

	int2 bCoord = clamp(center + int2(0, -1), minCoord, maxCoord);
	int2 dCoord = clamp(center + int2(-1, 0), minCoord, maxCoord);
	int2 fCoord = clamp(center + int2(1, 0), minCoord, maxCoord);
	int2 hCoord = clamp(center + int2(0, 1), minCoord, maxCoord);

	float3 e = Source.Load(int3(center, 0)).rgb;

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
