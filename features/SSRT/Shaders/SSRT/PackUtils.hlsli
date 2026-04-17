// AMD Capsaicin GI-1.1 — Half-precision packing utilities
// Ported from Capsaicin math/pack.hlsl

#ifndef PACK_UTILS_HLSLI
#define PACK_UTILS_HLSLI

uint2 packHalf3(float3 value)
{
	return uint2(
		f32tof16(value.x) | (f32tof16(value.y) << 16),
		f32tof16(value.z));
}

uint2 packHalf4(float4 value)
{
	return uint2(
		f32tof16(value.x) | (f32tof16(value.y) << 16),
		f32tof16(value.z) | (f32tof16(value.w) << 16));
}

float3 unpackHalf3(uint2 packedValue)
{
	return float3(
		f16tof32(packedValue.x & 0xFFFFu),
		f16tof32(packedValue.x >> 16),
		f16tof32(packedValue.y & 0xFFFFu));
}

float4 unpackHalf4(uint2 packedValue)
{
	return float4(
		f16tof32(packedValue.x & 0xFFFFu),
		f16tof32(packedValue.x >> 16),
		f16tof32(packedValue.y & 0xFFFFu),
		f16tof32(packedValue.y >> 16));
}

#endif  // PACK_UTILS_HLSLI
