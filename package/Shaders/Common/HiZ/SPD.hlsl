// FidelityFX Single Pass Downsampler (SPD)
// Based on https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/upscalers/fsr3/include/gpu/spd/ffx_spd.h
//
// Copyright (C) 2026 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
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

// Hi-Z uses six output mips on D3D11. Extended reductions require more UAVs and a completion counter.
#ifndef SPD_MAX_MIPS
#	define SPD_MAX_MIPS 6
#endif

globallycoherent RWTexture2D<float> SpdMip1 : register(u0);
globallycoherent RWTexture2D<float> SpdMip2 : register(u1);
globallycoherent RWTexture2D<float> SpdMip3 : register(u2);
globallycoherent RWTexture2D<float> SpdMip4 : register(u3);
globallycoherent RWTexture2D<float> SpdMip5 : register(u4);
globallycoherent RWTexture2D<float> SpdMip6 : register(u5);
#if SPD_MAX_MIPS > 6
globallycoherent RWTexture2D<float> SpdMip7 : register(u6);
globallycoherent RWTexture2D<float> SpdMip8 : register(u7);
globallycoherent RWTexture2D<float> SpdMip9 : register(u8);
globallycoherent RWTexture2D<float> SpdMip10 : register(u9);
globallycoherent RWTexture2D<float> SpdMip11 : register(u10);
globallycoherent RWTexture2D<float> SpdMip12 : register(u11);
globallycoherent RWByteAddressBuffer SpdCounter : register(u12);
#endif

// Mip 0 has a dedicated SRV so reads never overlap the output UAVs.
Texture2D<float> SpdSource : register(t0);

cbuffer SpdParams : register(b0)
{
	uint2 SpdSourceSize;
	uint SpdMipCount;       // Output levels, from 1 to SPD_MAX_MIPS.
	uint SpdNumWorkGroups;  // Group count for tail completion.
};

groupshared float spdIntermediate[16][16];
#if SPD_MAX_MIPS > 6
groupshared uint spdCounterValue;

// The tail reads texture mip 6 after all groups have written it.
float SpdLoad(int2 tex)
{
	return SpdMip6[tex];
}
#endif

float SpdLoadSource(int2 tex)
{
	return SpdSource.Load(int3(clamp(tex, int2(0, 0), int2(SpdSourceSize) - 1), 0));
}

void SpdStore(int2 pix, float value, uint index)
{
	switch (index) {
	case 0:
		SpdMip1[pix] = value;
		break;
	case 1:
		SpdMip2[pix] = value;
		break;
	case 2:
		SpdMip3[pix] = value;
		break;
	case 3:
		SpdMip4[pix] = value;
		break;
	case 4:
		SpdMip5[pix] = value;
		break;
	case 5:
		SpdMip6[pix] = value;
		break;
#if SPD_MAX_MIPS > 6
	case 6:
		SpdMip7[pix] = value;
		break;
	case 7:
		SpdMip8[pix] = value;
		break;
	case 8:
		SpdMip9[pix] = value;
		break;
	case 9:
		SpdMip10[pix] = value;
		break;
	case 10:
		SpdMip11[pix] = value;
		break;
	default:
		SpdMip12[pix] = value;
		break;
#endif
	}
}

float SpdReduce4(float a, float b, float c, float d)
{
	return max(max(a, b), max(c, d));
}

void SpdStoreIntermediate(uint x, uint y, float value)
{
	spdIntermediate[x][y] = value;
}

float SpdLoadIntermediate(uint x, uint y)
{
	return spdIntermediate[x][y];
}
#if SPD_MAX_MIPS > 6
// Only the last completed group processes the tail.
bool SpdExitWorkgroup(uint numWorkGroups, uint localInvocationIndex)
{
	if (localInvocationIndex == 0)
		SpdCounter.InterlockedAdd(0, 1, spdCounterValue);
	GroupMemoryBarrierWithGroupSync();
	return spdCounterValue != (numWorkGroups - 1);
}
#endif

float SpdReduceIntermediate(uint2 i0, uint2 i1, uint2 i2, uint2 i3)
{
	float v0 = SpdLoadIntermediate(i0.x, i0.y);
	float v1 = SpdLoadIntermediate(i1.x, i1.y);
	float v2 = SpdLoadIntermediate(i2.x, i2.y);
	float v3 = SpdLoadIntermediate(i3.x, i3.y);
	return SpdReduce4(v0, v1, v2, v3);
}
#if SPD_MAX_MIPS > 6
float SpdReduceLoad4(uint2 i0, uint2 i1, uint2 i2, uint2 i3)
{
	float v0 = SpdLoad(int2(i0));
	float v1 = SpdLoad(int2(i1));
	float v2 = SpdLoad(int2(i2));
	float v3 = SpdLoad(int2(i3));
	return SpdReduce4(v0, v1, v2, v3);
}

float SpdReduceLoad4(uint2 base)
{
	return SpdReduceLoad4(uint2(base + uint2(0, 0)), uint2(base + uint2(0, 1)), uint2(base + uint2(1, 0)), uint2(base + uint2(1, 1)));
}
#endif

float SpdReduceLoadSource(int2 base)
{
	float v0 = SpdLoadSource(base + int2(0, 0));
	float v1 = SpdLoadSource(base + int2(0, 1));
	float v2 = SpdLoadSource(base + int2(1, 0));
	float v3 = SpdLoadSource(base + int2(1, 1));
	return SpdReduce4(v0, v1, v2, v3);
}

void SpdDownsampleMips_0_1(uint x, uint y, uint2 workGroupID, uint localInvocationIndex, uint mip)
{
	float v[4];

	int2 tex = int2(workGroupID.xy * 64) + int2(x * 2, y * 2);
	int2 pix = int2(workGroupID.xy * 32) + int2(x, y);
	v[0] = SpdReduceLoadSource(tex);
	SpdStore(pix, v[0], 0);

	tex = int2(workGroupID.xy * 64) + int2(x * 2 + 32, y * 2);
	pix = int2(workGroupID.xy * 32) + int2(x + 16, y);
	v[1] = SpdReduceLoadSource(tex);
	SpdStore(pix, v[1], 0);

	tex = int2(workGroupID.xy * 64) + int2(x * 2, y * 2 + 32);
	pix = int2(workGroupID.xy * 32) + int2(x, y + 16);
	v[2] = SpdReduceLoadSource(tex);
	SpdStore(pix, v[2], 0);

	tex = int2(workGroupID.xy * 64) + int2(x * 2 + 32, y * 2 + 32);
	pix = int2(workGroupID.xy * 32) + int2(x + 16, y + 16);
	v[3] = SpdReduceLoadSource(tex);
	SpdStore(pix, v[3], 0);

	if (mip <= 1)
		return;

	for (uint i = 0; i < 4; i++) {
		SpdStoreIntermediate(x, y, v[i]);
		GroupMemoryBarrierWithGroupSync();
		if (localInvocationIndex < 64) {
			v[i] = SpdReduceIntermediate(uint2(x * 2 + 0, y * 2 + 0), uint2(x * 2 + 1, y * 2 + 0), uint2(x * 2 + 0, y * 2 + 1), uint2(x * 2 + 1, y * 2 + 1));
			SpdStore(int2(workGroupID.xy * 16) + int2(x + (i % 2) * 8, y + (i / 2) * 8), v[i], 1);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (localInvocationIndex < 64) {
		SpdStoreIntermediate(x + 0, y + 0, v[0]);
		SpdStoreIntermediate(x + 8, y + 0, v[1]);
		SpdStoreIntermediate(x + 0, y + 8, v[2]);
		SpdStoreIntermediate(x + 8, y + 8, v[3]);
	}
}

void SpdDownsampleMip_2(uint x, uint y, uint2 workGroupID, uint localInvocationIndex, uint mip)
{
	if (localInvocationIndex < 64) {
		float v = SpdReduceIntermediate(uint2(x * 2 + 0, y * 2 + 0), uint2(x * 2 + 1, y * 2 + 0), uint2(x * 2 + 0, y * 2 + 1), uint2(x * 2 + 1, y * 2 + 1));
		SpdStore(int2(workGroupID.xy * 8) + int2(x, y), v, mip);
		// Scatter shared values to reduce bank conflicts in the next stage.
		SpdStoreIntermediate(x * 2 + y % 2, y * 2, v);
	}
}

void SpdDownsampleMip_3(uint x, uint y, uint2 workGroupID, uint localInvocationIndex, uint mip)
{
	if (localInvocationIndex < 16) {
		float v = SpdReduceIntermediate(uint2(x * 4 + 0 + 0, y * 4 + 0), uint2(x * 4 + 2 + 0, y * 4 + 0), uint2(x * 4 + 0 + 1, y * 4 + 2), uint2(x * 4 + 2 + 1, y * 4 + 2));
		SpdStore(int2(workGroupID.xy * 4) + int2(x, y), v, mip);
		SpdStoreIntermediate(x * 4 + y, y * 4, v);
	}
}

void SpdDownsampleMip_4(uint x, uint y, uint2 workGroupID, uint localInvocationIndex, uint mip)
{
	if (localInvocationIndex < 4) {
		float v = SpdReduceIntermediate(uint2(x * 8 + 0 + 0 + y * 2, y * 8 + 0),
			uint2(x * 8 + 4 + 0 + y * 2, y * 8 + 0),
			uint2(x * 8 + 0 + 1 + y * 2, y * 8 + 4),
			uint2(x * 8 + 4 + 1 + y * 2, y * 8 + 4));
		SpdStore(int2(workGroupID.xy * 2) + int2(x, y), v, mip);
		SpdStoreIntermediate(x + y * 2, 0, v);
	}
}

void SpdDownsampleMip_5(uint2 workGroupID, uint localInvocationIndex, uint mip)
{
	if (localInvocationIndex < 1) {
		float v = SpdReduceIntermediate(uint2(0, 0), uint2(1, 0), uint2(2, 0), uint2(3, 0));
		SpdStore(int2(workGroupID.xy), v, mip);
	}
}
#if SPD_MAX_MIPS > 6
void SpdDownsampleMips_6_7(uint x, uint y, uint mips)
{
	int2 tex = int2(x * 4 + 0, y * 4 + 0);
	int2 pix = int2(x * 2 + 0, y * 2 + 0);
	float v0 = SpdReduceLoad4(tex);
	SpdStore(pix, v0, 6);

	tex = int2(x * 4 + 2, y * 4 + 0);
	pix = int2(x * 2 + 1, y * 2 + 0);
	float v1 = SpdReduceLoad4(tex);
	SpdStore(pix, v1, 6);

	tex = int2(x * 4 + 0, y * 4 + 2);
	pix = int2(x * 2 + 0, y * 2 + 1);
	float v2 = SpdReduceLoad4(tex);
	SpdStore(pix, v2, 6);

	tex = int2(x * 4 + 2, y * 4 + 2);
	pix = int2(x * 2 + 1, y * 2 + 1);
	float v3 = SpdReduceLoad4(tex);
	SpdStore(pix, v3, 6);

	if (mips <= 7)
		return;
	// Values are local to this thread; no barrier is needed.

	float v = SpdReduce4(v0, v1, v2, v3);
	SpdStore(int2(x, y), v, 7);
	SpdStoreIntermediate(x, y, v);
}
#endif

void SpdDownsampleNextFour(uint x, uint y, uint2 workGroupID, uint localInvocationIndex, uint baseMip, uint mips)
{
	if (mips <= baseMip)
		return;
	GroupMemoryBarrierWithGroupSync();
	SpdDownsampleMip_2(x, y, workGroupID, localInvocationIndex, baseMip);

	if (mips <= baseMip + 1)
		return;
	GroupMemoryBarrierWithGroupSync();
	SpdDownsampleMip_3(x, y, workGroupID, localInvocationIndex, baseMip + 1);

	if (mips <= baseMip + 2)
		return;
	GroupMemoryBarrierWithGroupSync();
	SpdDownsampleMip_4(x, y, workGroupID, localInvocationIndex, baseMip + 2);

	if (mips <= baseMip + 3)
		return;
	GroupMemoryBarrierWithGroupSync();
	SpdDownsampleMip_5(workGroupID, localInvocationIndex, baseMip + 3);
}

uint ffxBitfieldExtract(uint src, uint off, uint bits)
{
	uint mask = (1u << bits) - 1;
	return (src >> off) & mask;
}

uint ffxBitfieldInsertMask(uint src, uint ins, uint bits)
{
	uint mask = (1u << bits) - 1;
	return (ins & mask) | (src & (~mask));
}

uint2 ffxRemapForWaveReduction(uint a)
{
	return uint2(ffxBitfieldInsertMask(ffxBitfieldExtract(a, 2u, 3u), a, 1u), ffxBitfieldInsertMask(ffxBitfieldExtract(a, 3u, 3u), ffxBitfieldExtract(a, 1u, 2u), 2u));
}

// Output index N writes texture mip N+1.
void SpdDownsample(uint2 workGroupID, uint localInvocationIndex, uint mips, uint numWorkGroups)
{
	// Map threads to 16x16 coordinates shared by the four tile quadrants.
	uint2 sub_xy = ffxRemapForWaveReduction(localInvocationIndex % 64);
	uint x = sub_xy.x + 8 * ((localInvocationIndex >> 6) % 2);
	uint y = sub_xy.y + 8 * ((localInvocationIndex >> 7));
	SpdDownsampleMips_0_1(x, y, workGroupID, localInvocationIndex, mips);

	SpdDownsampleNextFour(x, y, workGroupID, localInvocationIndex, 2, mips);
#if SPD_MAX_MIPS > 6
	if (mips <= 6)
		return;

	// Only the last group continues across tile boundaries.
	if (SpdExitWorkgroup(numWorkGroups, localInvocationIndex))
		return;

	// Reset the completion counter for the next dispatch.
	if (localInvocationIndex == 0)
		SpdCounter.Store(0, 0);

	// The final group reduces texture mip 6 into the remaining levels.
	SpdDownsampleMips_6_7(x, y, mips);

	SpdDownsampleNextFour(x, y, uint2(0, 0), localInvocationIndex, 8, mips);
#endif
}

[numthreads(256, 1, 1)] void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex) {
	SpdDownsample(WorkGroupId.xy, LocalThreadIndex, SpdMipCount, SpdNumWorkGroups);
}
