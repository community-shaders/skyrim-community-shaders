// HLSL Unit Tests for Common/VR.hlsli
// Tests the pure-math UV conversion functions that form the foundation of VR stereo rendering.
// These run with VR defined so the stereo code paths are exercised.
// COMPUTESHADER prevents FrameBuffer.hlsli inclusion (we only need the UV math).
#define VR
#define COMPUTESHADER
#include "/Shaders/Common/VR.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

static const float kEps = 0.0001f;

/// @tags vr, stereo, uv
/// ConvertToStereoUV: left eye maps [0,1] -> [0,0.5]
[numthreads(1, 1, 1)] void TestConvertToStereoUVLeftEye() {
	float2 uv = float2(0.0, 0.5);
	float2 result = Stereo::ConvertToStereoUV(uv, 0);
	ASSERT(IsTrue, abs(result.x - 0.0) < kEps);
	ASSERT(IsTrue, abs(result.y - 0.5) < kEps);

	uv = float2(1.0, 0.5);
	result = Stereo::ConvertToStereoUV(uv, 0);
	ASSERT(IsTrue, abs(result.x - 0.5) < kEps);
	ASSERT(IsTrue, abs(result.y - 0.5) < kEps);

	uv = float2(0.5, 0.25);
	result = Stereo::ConvertToStereoUV(uv, 0);
	ASSERT(IsTrue, abs(result.x - 0.25) < kEps);
	ASSERT(IsTrue, abs(result.y - 0.25) < kEps);
}

	/// @tags vr, stereo, uv
	/// ConvertToStereoUV: right eye maps [0,1] -> [0.5,1]
	[numthreads(1, 1, 1)] void TestConvertToStereoUVRightEye()
{
	float2 uv = float2(0.0, 0.5);
	float2 result = Stereo::ConvertToStereoUV(uv, 1);
	ASSERT(IsTrue, abs(result.x - 0.5) < kEps);
	ASSERT(IsTrue, abs(result.y - 0.5) < kEps);

	uv = float2(1.0, 0.5);
	result = Stereo::ConvertToStereoUV(uv, 1);
	ASSERT(IsTrue, abs(result.x - 1.0) < kEps);

	uv = float2(0.5, 0.25);
	result = Stereo::ConvertToStereoUV(uv, 1);
	ASSERT(IsTrue, abs(result.x - 0.75) < kEps);
}

/// @tags vr, stereo, uv
/// ConvertToStereoUV with Y inversion
[numthreads(1, 1, 1)] void TestConvertToStereoUVInvertY() {
	float2 uv = float2(0.5, 0.25);
	float2 result = Stereo::ConvertToStereoUV(uv, 0, 1);
	ASSERT(IsTrue, abs(result.x - 0.25) < kEps);
	ASSERT(IsTrue, abs(result.y - 0.75) < kEps);
}

	/// @tags vr, stereo, uv
	/// ConvertFromStereoUV: left eye maps [0,0.5] -> [0,1]
	[numthreads(1, 1, 1)] void TestConvertFromStereoUVLeftEye()
{
	float2 stereoUV = float2(0.0, 0.5);
	float2 result = Stereo::ConvertFromStereoUV(stereoUV, 0);
	ASSERT(IsTrue, abs(result.x - 0.0) < kEps);
	ASSERT(IsTrue, abs(result.y - 0.5) < kEps);

	stereoUV = float2(0.5, 0.5);
	result = Stereo::ConvertFromStereoUV(stereoUV, 0);
	ASSERT(IsTrue, abs(result.x - 1.0) < kEps);

	stereoUV = float2(0.25, 0.25);
	result = Stereo::ConvertFromStereoUV(stereoUV, 0);
	ASSERT(IsTrue, abs(result.x - 0.5) < kEps);
}

/// @tags vr, stereo, uv
/// ConvertFromStereoUV: right eye maps [0.5,1] -> [0,1]
[numthreads(1, 1, 1)] void TestConvertFromStereoUVRightEye() {
	float2 stereoUV = float2(0.5, 0.5);
	float2 result = Stereo::ConvertFromStereoUV(stereoUV, 1);
	ASSERT(IsTrue, abs(result.x - 0.0) < kEps);

	stereoUV = float2(1.0, 0.5);
	result = Stereo::ConvertFromStereoUV(stereoUV, 1);
	ASSERT(IsTrue, abs(result.x - 1.0) < kEps);

	stereoUV = float2(0.75, 0.25);
	result = Stereo::ConvertFromStereoUV(stereoUV, 1);
	ASSERT(IsTrue, abs(result.x - 0.5) < kEps);
}

	/// @tags vr, stereo, uv
	/// ConvertToStereoUV and ConvertFromStereoUV are inverses of each other
	[numthreads(1, 1, 1)] void TestStereoUVRoundTrip()
{
	float2 original = float2(0.3, 0.7);

	// Left eye round-trip
	float2 stereo = Stereo::ConvertToStereoUV(original, 0);
	float2 recovered = Stereo::ConvertFromStereoUV(stereo, 0);
	ASSERT(IsTrue, abs(recovered.x - original.x) < kEps);
	ASSERT(IsTrue, abs(recovered.y - original.y) < kEps);

	// Right eye round-trip
	stereo = Stereo::ConvertToStereoUV(original, 1);
	recovered = Stereo::ConvertFromStereoUV(stereo, 1);
	ASSERT(IsTrue, abs(recovered.x - original.x) < kEps);
	ASSERT(IsTrue, abs(recovered.y - original.y) < kEps);
}

/// @tags vr, stereo, uv
/// GetEyeIndexFromTexCoord: left half -> 0, right half -> 1
[numthreads(1, 1, 1)] void TestGetEyeIndexFromTexCoord() {
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.0, 0.5)), 0u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.25, 0.5)), 0u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.49, 0.5)), 0u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.5, 0.5)), 1u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(0.75, 0.5)), 1u);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(float2(1.0, 0.5)), 1u);
}

	/// @tags vr, stereo, uv
	/// GetEyeIndexFromTexCoord is consistent with ConvertToStereoUV output
	[numthreads(1, 1, 1)] void TestEyeIndexConsistentWithStereoUV()
{
	float2 monoUV = float2(0.6, 0.4);

	// Convert to stereo for left eye, then detect eye index
	float2 stereoLeft = Stereo::ConvertToStereoUV(monoUV, 0);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(stereoLeft), 0u);

	// Convert to stereo for right eye, then detect eye index
	float2 stereoRight = Stereo::ConvertToStereoUV(monoUV, 1);
	ASSERT(AreEqual, Stereo::GetEyeIndexFromTexCoord(stereoRight), 1u);
}

/// @tags vr, stereo, uv
/// ConvertToStereoUV clamps input x to [0,1] via saturate
[numthreads(1, 1, 1)] void TestConvertToStereoUVClamping() {
	// x > 1 should be clamped to 1 before conversion
	float2 uv = float2(1.5, 0.5);
	float2 resultLeft = Stereo::ConvertToStereoUV(uv, 0);
	ASSERT(IsTrue, abs(resultLeft.x - 0.5) < kEps);  // saturate(1.5)=1.0, (1.0+0)/2=0.5

	float2 resultRight = Stereo::ConvertToStereoUV(uv, 1);
	ASSERT(IsTrue, abs(resultRight.x - 1.0) < kEps);  // saturate(1.5)=1.0, (1.0+1)/2=1.0

	// x < 0 should be clamped to 0
	uv = float2(-0.5, 0.5);
	resultLeft = Stereo::ConvertToStereoUV(uv, 0);
	ASSERT(IsTrue, abs(resultLeft.x - 0.0) < kEps);  // saturate(-0.5)=0.0, (0+0)/2=0
}

	/// @tags vr, stereo, uv
	/// ConvertUVToNormalizedScreenSpace maps to [-1,1] range
	[numthreads(1, 1, 1)] void TestConvertUVToNormalizedScreenSpace()
{
	// Center of left eye (stereo UV 0.25) -> x should be near 0 (center of that eye)
	float2 result = Stereo::ConvertUVToNormalizedScreenSpace(float2(0.25, 0.5));
	ASSERT(IsTrue, abs(result.x - 0.0) < kEps);
	ASSERT(IsTrue, abs(result.y - 0.0) < kEps);

	// Center of right eye (stereo UV 0.75) -> x should also be near 0
	result = Stereo::ConvertUVToNormalizedScreenSpace(float2(0.75, 0.5));
	ASSERT(IsTrue, abs(result.x - 0.0) < kEps);

	// Outer edges (stereo UV 0.0 and 1.0) -> x = +1
	result = Stereo::ConvertUVToNormalizedScreenSpace(float2(0.0, 0.5));
	ASSERT(IsTrue, abs(result.x - 1.0) < kEps);

	// Inner edge / midpoint (stereo UV 0.5) -> x = -1
	result = Stereo::ConvertUVToNormalizedScreenSpace(float2(0.5, 0.5));
	ASSERT(IsTrue, abs(result.x - (-1.0)) < kEps);

	// Top -> y = -1, bottom -> y = 1
	result = Stereo::ConvertUVToNormalizedScreenSpace(float2(0.25, 0.0));
	ASSERT(IsTrue, abs(result.y - (-1.0)) < kEps);

	result = Stereo::ConvertUVToNormalizedScreenSpace(float2(0.25, 1.0));
	ASSERT(IsTrue, abs(result.y - 1.0) < kEps);
}
