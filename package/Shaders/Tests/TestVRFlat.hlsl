// HLSL Unit Tests for Common/VR.hlsli - Flat (non-VR) mode
// Verifies that all Stereo:: functions are correct no-ops / identity when VR is not defined.
// This is the code path most developers exercise.
#define COMPUTESHADER
#include "/Shaders/Common/VR.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

static const float kEps = 0.0001f;

/// @tags vr, flat, uv
/// ConvertToStereoUV is identity in flat mode
[numthreads(1, 1, 1)]
void TestFlatConvertToStereoUVIsIdentity()
{
	float2 uv = float2(0.3, 0.7);
	float2 result = Stereo::ConvertToStereoUV(uv, 0);
	ASSERT(IsTrue, abs(result.x - uv.x) < kEps);
	ASSERT(IsTrue, abs(result.y - uv.y) < kEps);

	// Eye index should not matter in flat
	result = Stereo::ConvertToStereoUV(uv, 1);
	ASSERT(IsTrue, abs(result.x - uv.x) < kEps);
	ASSERT(IsTrue, abs(result.y - uv.y) < kEps);

	// invertY param should also be ignored
	result = Stereo::ConvertToStereoUV(uv, 0, 1);
	ASSERT(IsTrue, abs(result.x - uv.x) < kEps);
	ASSERT(IsTrue, abs(result.y - uv.y) < kEps);
}

/// @tags vr, flat, uv
/// ConvertFromStereoUV is identity in flat mode
[numthreads(1, 1, 1)]
void TestFlatConvertFromStereoUVIsIdentity()
{
	float2 uv = float2(0.6, 0.4);
	float2 result = Stereo::ConvertFromStereoUV(uv, 0);
	ASSERT(IsTrue, abs(result.x - uv.x) < kEps);
	ASSERT(IsTrue, abs(result.y - uv.y) < kEps);

	result = Stereo::ConvertFromStereoUV(uv, 1);
	ASSERT(IsTrue, abs(result.x - uv.x) < kEps);
	ASSERT(IsTrue, abs(result.y - uv.y) < kEps);
}

/// @tags vr, flat, uv
/// float3/float4 overloads are also identity in flat mode
[numthreads(1, 1, 1)]
void TestFlatStereoUVOverloadsAreIdentity()
{
	float3 uv3 = float3(0.3, 0.7, 0.5);
	float3 result3 = Stereo::ConvertToStereoUV(uv3, 0);
	ASSERT(IsTrue, abs(result3.x - uv3.x) < kEps);
	ASSERT(IsTrue, abs(result3.y - uv3.y) < kEps);
	ASSERT(IsTrue, abs(result3.z - uv3.z) < kEps);

	result3 = Stereo::ConvertFromStereoUV(uv3, 1);
	ASSERT(IsTrue, abs(result3.x - uv3.x) < kEps);
	ASSERT(IsTrue, abs(result3.y - uv3.y) < kEps);
	ASSERT(IsTrue, abs(result3.z - uv3.z) < kEps);

	float4 uv4 = float4(0.3, 0.7, 0.5, 1.0);
	float4 result4 = Stereo::ConvertToStereoUV(uv4, 0);
	ASSERT(IsTrue, abs(result4.x - uv4.x) < kEps);
	ASSERT(IsTrue, abs(result4.y - uv4.y) < kEps);
	ASSERT(IsTrue, abs(result4.z - uv4.z) < kEps);
	ASSERT(IsTrue, abs(result4.w - uv4.w) < kEps);

	float4 result4_from = Stereo::ConvertFromStereoUV(uv4, 1);
	ASSERT(IsTrue, abs(result4_from.x - uv4.x) < kEps);
	ASSERT(IsTrue, abs(result4_from.y - uv4.y) < kEps);
	ASSERT(IsTrue, abs(result4_from.z - uv4.z) < kEps);
	ASSERT(IsTrue, abs(result4_from.w - uv4.w) < kEps);
}

/// @tags vr, flat, uv
/// Round-trip through To/From is identity in flat mode
[numthreads(1, 1, 1)]
void TestFlatStereoUVRoundTrip()
{
	float2 uv = float2(0.8, 0.2);
	float2 stereo = Stereo::ConvertToStereoUV(uv, 0);
	float2 recovered = Stereo::ConvertFromStereoUV(stereo, 0);
	ASSERT(IsTrue, abs(recovered.x - uv.x) < kEps);
	ASSERT(IsTrue, abs(recovered.y - uv.y) < kEps);
}

/// @tags vr, flat, uv
/// GetEyeIndexFromTexCoord always returns 0 in flat mode
[numthreads(1, 1, 1)]
void TestFlatGetEyeIndexAlwaysZero()
{
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.0, 0.5)), 0u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.25, 0.5)), 0u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.5, 0.5)), 0u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.75, 0.5)), 0u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(1.0, 0.5)), 0u);
}
