// BC6H real-time GPU encoder compute shader.
// Adapted from GPURealTimeBC6H by Krzysztof Narkowicz.
// Source: https://github.com/knarkowicz/GPURealTimeBC6H
//
// MIT License
//
// Copyright (c) 2015 Krzysztof Narkowicz
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Modifications for Community Shaders:
//   - Input changed from Texture2D to Texture2DArray to process all 6 cubemap faces in one dispatch
//   - Output changed from RWTexture2D to RWTexture2DArray accordingly
//   - cbuffer simplified to only fields needed for compression
//   - QUALITY forced to 0 (fast P1-only mode)

#pragma warning(disable: 3078)

// Fast mode: P1 only (single endpoint pair, 4-bit indices). No P2.
#define QUALITY 0
#define ENCODE_P2 0

#define INSET_COLOR_BBOX 1
#define OPTIMIZE_ENDPOINTS 1
#define LUMINANCE_WEIGHTS 1

static const float HALF_MAX = 65504.0f;
static const uint PATTERN_NUM = 32;

Texture2DArray<float4> SrcTexture : register(t0);
RWTexture2DArray<uint4> OutputTexture : register(u0);

cbuffer BC6HEncodeCB : register(b0)
{
	uint2 TextureSizeInBlocks;
	uint MipLevel;
	uint pad;
};

float CalcMSLE(float3 a, float3 b)
{
	float3 delta = log2((b + 1.0f) / (a + 1.0f));
	float3 deltaSq = delta * delta;

#if LUMINANCE_WEIGHTS
	float3 luminanceWeights = float3(0.299f, 0.587f, 0.114f);
	deltaSq *= luminanceWeights;
#endif

	return deltaSq.x + deltaSq.y + deltaSq.z;
}

uint PatternFixupID(uint i)
{
	uint ret = 15;
	ret = ((3441033216 >> i) & 0x1) ? 2 : ret;
	ret = ((845414400 >> i) & 0x1) ? 8 : ret;
	return ret;
}

uint Pattern(uint p, uint i)
{
	uint p2 = p / 2;
	uint p3 = p - p2 * 2;

	uint enc = 0;
	enc = p2 == 0 ? 2290666700 : enc;
	enc = p2 == 1 ? 3972591342 : enc;
	enc = p2 == 2 ? 4276930688 : enc;
	enc = p2 == 3 ? 3967876808 : enc;
	enc = p2 == 4 ? 4293707776 : enc;
	enc = p2 == 5 ? 3892379264 : enc;
	enc = p2 == 6 ? 4278255592 : enc;
	enc = p2 == 7 ? 4026597360 : enc;
	enc = p2 == 8 ? 9369360 : enc;
	enc = p2 == 9 ? 147747072 : enc;
	enc = p2 == 10 ? 1930428556 : enc;
	enc = p2 == 11 ? 2362323200 : enc;
	enc = p2 == 12 ? 823134348 : enc;
	enc = p2 == 13 ? 913073766 : enc;
	enc = p2 == 14 ? 267393000 : enc;
	enc = p2 == 15 ? 966553998 : enc;

	enc = p3 ? enc >> 16 : enc;
	uint ret = (enc >> i) & 0x1;
	return ret;
}

float3 Quantize7(float3 x)
{
	return (f32tof16(x) * 128.0f) / (0x7bff + 1.0f);
}

float3 Quantize9(float3 x)
{
	return (f32tof16(x) * 512.0f) / (0x7bff + 1.0f);
}

float3 Quantize10(float3 x)
{
	return (f32tof16(x) * 1024.0f) / (0x7bff + 1.0f);
}

float3 Unquantize7(float3 x)
{
	return (x * 65536.0f + 0x8000) / 128.0f;
}

float3 Unquantize9(float3 x)
{
	return (x * 65536.0f + 0x8000) / 512.0f;
}

float3 Unquantize10(float3 x)
{
	return (x * 65536.0f + 0x8000) / 1024.0f;
}

float3 FinishUnquantize(float3 endpoint0Unq, float3 endpoint1Unq, float weight)
{
	float3 comp = (endpoint0Unq * (64.0f - weight) + endpoint1Unq * weight + 32.0f) * (31.0f / 4096.0f);
	return f16tof32(uint3(comp));
}

void Swap(inout float3 a, inout float3 b)
{
	float3 tmp = a;
	a = b;
	b = tmp;
}

void Swap(inout float a, inout float b)
{
	float tmp = a;
	a = b;
	b = tmp;
}

uint ComputeIndex3(float texelPos, float endPoint0Pos, float endPoint1Pos)
{
	float r = (texelPos - endPoint0Pos) / (endPoint1Pos - endPoint0Pos);
	return (uint)clamp(r * 6.98182f + 0.00909f + 0.5f, 0.0f, 7.0f);
}

uint ComputeIndex4(float texelPos, float endPoint0Pos, float endPoint1Pos)
{
	float r = (texelPos - endPoint0Pos) / (endPoint1Pos - endPoint0Pos);
	return (uint)clamp(r * 14.93333f + 0.03333f + 0.5f, 0.0f, 15.0f);
}

void SignExtend(inout float3 v1, uint mask, uint signFlag)
{
	int3 v = (int3)v1;
	v.x = (v.x & mask) | (v.x < 0 ? signFlag : 0);
	v.y = (v.y & mask) | (v.y < 0 ? signFlag : 0);
	v.z = (v.z & mask) | (v.z < 0 ? signFlag : 0);
	v1 = v;
}

void InsetColorBBoxP1(float3 texels[16], inout float3 blockMin, inout float3 blockMax)
{
	float3 refinedBlockMin = blockMax;
	float3 refinedBlockMax = blockMin;

	for (uint i = 0; i < 16; ++i) {
		refinedBlockMin = min(refinedBlockMin, texels[i] == blockMin ? refinedBlockMin : texels[i]);
		refinedBlockMax = max(refinedBlockMax, texels[i] == blockMax ? refinedBlockMax : texels[i]);
	}

	float3 logRefinedBlockMax = log2(refinedBlockMax + 1.0f);
	float3 logRefinedBlockMin = log2(refinedBlockMin + 1.0f);

	float3 logBlockMax = log2(blockMax + 1.0f);
	float3 logBlockMin = log2(blockMin + 1.0f);
	float3 logBlockMaxExt = (logBlockMax - logBlockMin) * (1.0f / 32.0f);

	logBlockMin += min(logRefinedBlockMin - logBlockMin, logBlockMaxExt);
	logBlockMax -= min(logBlockMax - logRefinedBlockMax, logBlockMaxExt);

	blockMin = exp2(logBlockMin) - 1.0f;
	blockMax = exp2(logBlockMax) - 1.0f;
}

void OptimizeEndpointsP1(float3 texels[16], inout float3 blockMin, inout float3 blockMax, in float3 blockMinNonInset, in float3 blockMaxNonInset)
{
	float3 blockDir = blockMax - blockMin;
	blockDir = blockDir / (blockDir.x + blockDir.y + blockDir.z);

	float endPoint0Pos = f32tof16(dot(blockMin, blockDir));
	float endPoint1Pos = f32tof16(dot(blockMax, blockDir));

	float3 alphaTexelSum = 0.0f;
	float3 betaTexelSum = 0.0f;
	float alphaBetaSum = 0.0f;
	float alphaSqSum = 0.0f;
	float betaSqSum = 0.0f;

	for (int i = 0; i < 16; i++) {
		float texelPos = f32tof16(dot(texels[i], blockDir));
		uint texelIndex = ComputeIndex4(texelPos, endPoint0Pos, endPoint1Pos);

		float beta = saturate(texelIndex / 15.0f);
		float alpha = 1.0f - beta;

		float3 texelF16 = f32tof16(texels[i].xyz);
		alphaTexelSum += alpha * texelF16;
		betaTexelSum += beta * texelF16;

		alphaBetaSum += alpha * beta;
		alphaSqSum += alpha * alpha;
		betaSqSum += beta * beta;
	}

	float det = alphaSqSum * betaSqSum - alphaBetaSum * alphaBetaSum;

	if (abs(det) > 0.00001f) {
		float detRcp = rcp(det);
		blockMin = clamp(f16tof32(clamp(detRcp * (alphaTexelSum * betaSqSum - betaTexelSum * alphaBetaSum), 0.0f, HALF_MAX)), blockMinNonInset, blockMaxNonInset);
		blockMax = clamp(f16tof32(clamp(detRcp * (betaTexelSum * alphaSqSum - alphaTexelSum * alphaBetaSum), 0.0f, HALF_MAX)), blockMinNonInset, blockMaxNonInset);
	}
}

void EncodeP1(inout uint4 block, inout float blockMSLE, float3 texels[16])
{
	float3 blockMin = texels[0];
	float3 blockMax = texels[0];
	for (uint i = 1; i < 16; ++i) {
		blockMin = min(blockMin, texels[i]);
		blockMax = max(blockMax, texels[i]);
	}

	float3 blockMinNonInset = blockMin;
	float3 blockMaxNonInset = blockMax;
#if INSET_COLOR_BBOX
	InsetColorBBoxP1(texels, blockMin, blockMax);
#endif

#if OPTIMIZE_ENDPOINTS
	OptimizeEndpointsP1(texels, blockMin, blockMax, blockMinNonInset, blockMaxNonInset);
#endif

	float3 blockDir = blockMax - blockMin;
	blockDir = blockDir / (blockDir.x + blockDir.y + blockDir.z);

	float3 endpoint0 = Quantize10(blockMin);
	float3 endpoint1 = Quantize10(blockMax);
	float endPoint0Pos = f32tof16(dot(blockMin, blockDir));
	float endPoint1Pos = f32tof16(dot(blockMax, blockDir));

	float fixupTexelPos = f32tof16(dot(texels[0], blockDir));
	uint fixupIndex = ComputeIndex4(fixupTexelPos, endPoint0Pos, endPoint1Pos);
	if (fixupIndex > 7) {
		Swap(endPoint0Pos, endPoint1Pos);
		Swap(endpoint0, endpoint1);
	}

	uint indices[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	for (uint i = 0; i < 16; ++i) {
		float texelPos = f32tof16(dot(texels[i], blockDir));
		indices[i] = ComputeIndex4(texelPos, endPoint0Pos, endPoint1Pos);
	}

	float3 endpoint0Unq = Unquantize10(endpoint0);
	float3 endpoint1Unq = Unquantize10(endpoint1);
	float msle = 0.0f;
	for (uint i = 0; i < 16; ++i) {
		float weight = floor((indices[i] * 64.0f) / 15.0f + 0.5f);
		float3 texelUnc = FinishUnquantize(endpoint0Unq, endpoint1Unq, weight);
		msle += CalcMSLE(texels[i], texelUnc);
	}

	blockMSLE = msle;
	block.x = 0x03;

	block.x |= (uint)endpoint0.x << 5;
	block.x |= (uint)endpoint0.y << 15;
	block.x |= (uint)endpoint0.z << 25;
	block.y |= (uint)endpoint0.z >> 7;
	block.y |= (uint)endpoint1.x << 3;
	block.y |= (uint)endpoint1.y << 13;
	block.y |= (uint)endpoint1.z << 23;
	block.z |= (uint)endpoint1.z >> 9;

	block.z |= indices[0] << 1;
	block.z |= indices[1] << 4;
	block.z |= indices[2] << 8;
	block.z |= indices[3] << 12;
	block.z |= indices[4] << 16;
	block.z |= indices[5] << 20;
	block.z |= indices[6] << 24;
	block.z |= indices[7] << 28;
	block.w |= indices[8] << 0;
	block.w |= indices[9] << 4;
	block.w |= indices[10] << 8;
	block.w |= indices[11] << 12;
	block.w |= indices[12] << 16;
	block.w |= indices[13] << 20;
	block.w |= indices[14] << 24;
	block.w |= indices[15] << 28;
}

[numthreads(8, 8, 1)] void main(
	uint3 dispatchThreadID : SV_DispatchThreadID) {
	uint2 blockCoord = dispatchThreadID.xy;
	uint faceIndex = dispatchThreadID.z;

	if (all(blockCoord < TextureSizeInBlocks)) {
		int2 texelBase = int2(blockCoord) * 4;

		float3 texels[16];
		[unroll] for (int i = 0; i < 16; i++)
		{
			int tx = i % 4;
			int ty = i / 4;
			texels[i] = SrcTexture.Load(int4(texelBase.x + tx, texelBase.y + ty, int(faceIndex), int(MipLevel))).rgb;
		}

		uint4 block = uint4(0, 0, 0, 0);
		float blockMSLE = 0.0f;

		EncodeP1(block, blockMSLE, texels);

		OutputTexture[uint3(blockCoord, faceIndex)] = block;
	}
}
