// AMD Contrast Adaptive Sharpening (CAS) - Sharpen-only for VR
// Based on AMD FidelityFX CAS (sharpen-only path)
// Reference: https://gpuopen.com/fidelityfx-cas/
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// CASParams[0] = sharpness (0.0 = no sharpening, 1.0 = maximum sharpening)
StructuredBuffer<float> CASParams : register(t1);

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Dest : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	uint2 texDim;
	Dest.GetDimensions(texDim.x, texDim.y);

	if (DTid.x >= texDim.x || DTid.y >= texDim.y)
		return;

	float sharpness = CASParams[0];

	// Fetch 3x3 neighborhood
	int2 sp = int2(DTid.xy);
	float3 a = Source.Load(int3(sp + int2(-1, -1), 0)).rgb;
	float3 b = Source.Load(int3(sp + int2(0, -1), 0)).rgb;
	float3 c = Source.Load(int3(sp + int2(1, -1), 0)).rgb;
	float3 d = Source.Load(int3(sp + int2(-1, 0), 0)).rgb;
	float3 e = Source.Load(int3(sp, 0)).rgb;
	float3 f = Source.Load(int3(sp + int2(1, 0), 0)).rgb;
	float3 g = Source.Load(int3(sp + int2(-1, 1), 0)).rgb;
	float3 h = Source.Load(int3(sp + int2(0, 1), 0)).rgb;
	float3 i = Source.Load(int3(sp + int2(1, 1), 0)).rgb;

	// Soft min/max of cross neighborhood
	float3 mnRGB = min(min(min(d, e), min(f, b)), h);
	float3 mxRGB = max(max(max(d, e), max(f, b)), h);

	// Expand with diagonal neighbors for soft min/max
	float3 mnRGB2 = min(min(a, c), min(g, i));
	float3 mxRGB2 = max(max(a, c), max(g, i));
	mnRGB += mnRGB2;
	mxRGB += mxRGB2;

	// Adaptive sharpening amount
	float3 ampRGB = saturate(min(mnRGB, 2.0 - mxRGB) * rcp(max(mxRGB, 1e-4)));
	ampRGB = rsqrt(ampRGB);

	// Peak controls sharpening strength:
	//   sharpness 0.0 -> peak 8.0 (no sharpening)
	//   sharpness 1.0 -> peak 5.0 (maximum sharpening)
	float peak = -3.0 * sharpness + 8.0;
	float3 wRGB = -rcp(ampRGB * peak);
	float3 rcpWeightRGB = rcp(4.0 * wRGB + 1.0);

	// Apply sharpening filter
	float3 outColor = saturate(((b + d) + (f + h)) * wRGB + e) * rcpWeightRGB;

	Dest[DTid.xy] = float4(outColor, 1.0);
}
