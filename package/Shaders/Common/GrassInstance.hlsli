#ifndef __GRASS_INSTANCE_DEPENDENCY_HLSL__
#define __GRASS_INSTANCE_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"

namespace GrassInstance
{
	static const uint Stride = 16;
	static const uint RecordStride = 16;

	// The packed word contains a 19-bit group index, 9-bit fade and 4-bit flags.
	static const uint RecordGroupBits = 19;
	static const uint RecordFadeBits = 9;
	static const uint RecordGroupMask = (1u << RecordGroupBits) - 1;
	static const uint RecordFadeMask = (1u << RecordFadeBits) - 1;
	static const uint RecordFlagsShift = RecordGroupBits + RecordFadeBits;

	// The flags contain collision range, simple shading and a two-bit LOD tier (0 = full, 1 = middle, 2 = far).
	static const uint FlagCollision = 1;
	static const uint FlagSimpleShading = 2;
	static const uint FlagTierShift = 2;

	// Each group contains up to 64 contiguous instances from one slice
	struct CullGroup
	{
		float3 Origin;
		float FadeStart;
		uint SourceStart;
		uint Count;
	};

	// This group count selects the engine's instance stream.
	static const uint VanillaDrawMarker = 0xFFFFFFFF;

	uint PackRecord(uint groupIndex, float fade, uint flags)
	{
		return groupIndex | ((uint)round(saturate(fade) * RecordFadeMask) << RecordGroupBits) | (flags << RecordFlagsShift);
	}

	void UnpackRecord(uint packed, out uint groupIndex, out float fade, out uint flags)
	{
		groupIndex = packed & RecordGroupMask;
		fade = (float)((packed >> RecordGroupBits) & RecordFadeMask) / RecordFadeMask;
		flags = packed >> RecordFlagsShift;
	}

	float WindScalar(float2 localXY, float timer)
	{
		const float phase = 0.4 * ((localXY.x + localXY.y) * -0.0078125 + timer);
		float sine, cosine;
		sincos(phase, sine, cosine);
		const float cosineTerm = 0.2 * cos(Math::PI * cosine);
		const float sineTerm = sin(Math::PI * sine);
		const float harmonicTerm = sin(Math::TAU * sine);
		return (sineTerm + harmonicTerm) * 0.3 + cosineTerm;
	}

	float DecodeQuaternionComponent(uint bits)
	{
		return ((float)(bits & 0x7FFF) - 16384.0) * (0.70710678 / 16383.0);
	}

	// Restore the vanilla layout from packed data
	void Decode(uint4 packed, out float4 data1, out float4 data2, out float4 data3, out float4 data4)
	{
		const float a = DecodeQuaternionComponent(packed.z);
		const float b = DecodeQuaternionComponent(packed.z >> 15);
		const float c = DecodeQuaternionComponent(packed.w);

		// The largest quaternion component was omitted and is reconstructed as the positive remainder.
		const float big = sqrt(saturate(1.0 - a * a - b * b - c * c));
		const uint omitted = packed.z >> 30;
		const float4 q = omitted == 0 ? float4(big, a, b, c) : omitted == 1 ? float4(a, big, b, c) : omitted == 2 ? float4(a, b, big, c) : float4(a, b, c, big);
		const float x = q.x, y = q.y, z = q.z, w = q.w;

		const float3x3 rotation = float3x3(
			1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w),
			2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w),
			2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y));

		data1 = float4(f16tof32(packed.x), f16tof32(packed.x >> 16), f16tof32(packed.y), (float)(packed.y >> 16) / 65535.0);
		data2 = float4(rotation[0], rotation[2][1]);
		data3 = float4(rotation[1], rotation[2][2]);
		data4 = float4(rotation[2][0], f16tof32(packed.w >> 16), 0.0, 0.0);
	}
}

#endif  // __GRASS_INSTANCE_DEPENDENCY_HLSL__
