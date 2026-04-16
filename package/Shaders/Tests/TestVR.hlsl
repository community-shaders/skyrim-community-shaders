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

/// @tags vr, stereo, depth, edge-detection
/// MaxDepthDiff: identical neighbors -> 0
[numthreads(1, 1, 1)] void TestMaxDepthDiffAllSame() {
	float result = Stereo::MaxDepthDiff(0.5, float4(0.5, 0.5, 0.5, 0.5));
	ASSERT(IsTrue, abs(result) < kEps);
}

	/// @tags vr, stereo, depth, edge-detection
	/// MaxDepthDiff: returns |center - neighbor| when one neighbor differs
	[numthreads(1, 1, 1)] void TestMaxDepthDiffOneDiffers()
{
	// Only .z differs
	float result = Stereo::MaxDepthDiff(0.5, float4(0.5, 0.5, 0.8, 0.5));
	ASSERT(IsTrue, abs(result - 0.3) < kEps);
}

/// @tags vr, stereo, depth, edge-detection
/// MaxDepthDiff: returns the largest difference across all four neighbors
[numthreads(1, 1, 1)] void TestMaxDepthDiffPicksLargest() {
	float result = Stereo::MaxDepthDiff(0.5, float4(0.55, 0.45, 0.9, 0.48));
	ASSERT(IsTrue, abs(result - 0.4) < kEps);  // abs(0.5 - 0.9) = 0.4
}

	/// @tags vr, stereo, depth, edge-detection
	/// MaxDepthDiff: arm/world case returns exact diff (arm=0.75, world=1.0 -> 0.25)
	[numthreads(1, 1, 1)] void TestMaxDepthDiffArmWorldCase()
{
	float armDepth = 0.75;
	float worldDepth = 1.0;
	float result = Stereo::MaxDepthDiff(armDepth, float4(worldDepth, armDepth, armDepth, armDepth));
	ASSERT(IsTrue, abs(result - abs(worldDepth - armDepth)) < kEps);
}

/// @tags vr, stereo, depth, edge-detection
/// MaxDepthDiff: symmetric - diff(a,b) == diff(b,a)
[numthreads(1, 1, 1)] void TestMaxDepthDiffSymmetry() {
	float a = 0.3, b = 0.7;
	float fwd = Stereo::MaxDepthDiff(a, float4(b, a, a, a));
	float rev = Stereo::MaxDepthDiff(b, float4(a, b, b, b));
	ASSERT(IsTrue, abs(fwd - rev) < kEps);
}

	/// @tags vr, stereo, depth, edge-detection
	/// MaxDepthDiff: center == 0 (mask pixel) against world neighbor
	[numthreads(1, 1, 1)] void TestMaxDepthDiffMaskCenter()
{
	float result = Stereo::MaxDepthDiff(0.0, float4(0.8, 0.0, 0.0, 0.0));
	ASSERT(IsTrue, abs(result - 0.8) < kEps);
}

/// @tags vr, stereo, edge-detection
/// ClampToEyeBounds: interior pixel is returned unchanged for both eyes
[numthreads(1, 1, 1)] void TestClampToEyeBoundsInterior() {
	float2 frameDim = float2(2048, 1024);
	int2 left = Stereo::ClampToEyeBounds(int2(512, 512), 0, frameDim);
	ASSERT(AreEqual, left.x, 512);
	ASSERT(AreEqual, left.y, 512);

	int2 right = Stereo::ClampToEyeBounds(int2(1536, 512), 1, frameDim);
	ASSERT(AreEqual, right.x, 1536);
	ASSERT(AreEqual, right.y, 512);
}

	/// @tags vr, stereo, edge-detection
	/// ClampToEyeBounds: left eye x cannot cross the half-width seam
	[numthreads(1, 1, 1)] void TestClampToEyeBoundsLeftEyeSeam()
{
	float2 frameDim = float2(2048, 1024);
	// x past the seam clamps to halfWidth - 1 = 1023
	int2 result = Stereo::ClampToEyeBounds(int2(1025, 512), 0, frameDim);
	ASSERT(AreEqual, result.x, 1023);
}

/// @tags vr, stereo, edge-detection
/// ClampToEyeBounds: right eye x cannot cross the half-width seam
[numthreads(1, 1, 1)] void TestClampToEyeBoundsRightEyeSeam() {
	float2 frameDim = float2(2048, 1024);
	// x before the seam clamps to halfWidth = 1024
	int2 result = Stereo::ClampToEyeBounds(int2(1022, 512), 1, frameDim);
	ASSERT(AreEqual, result.x, 1024);
}

	/// @tags vr, stereo, edge-detection
	/// ClampToEyeBounds: x clamped at outer borders (left eye left edge, right eye right edge)
	[numthreads(1, 1, 1)] void TestClampToEyeBoundsOuterBorders()
{
	float2 frameDim = float2(2048, 1024);
	int2 leftBorder = Stereo::ClampToEyeBounds(int2(-1, 512), 0, frameDim);
	ASSERT(AreEqual, leftBorder.x, 0);

	int2 rightBorder = Stereo::ClampToEyeBounds(int2(2049, 512), 1, frameDim);
	ASSERT(AreEqual, rightBorder.x, 2047);
}

/// @tags vr, stereo, edge-detection
/// ClampToEyeBounds: y is clamped to [0, frameDim.y - 1] independently of eye
[numthreads(1, 1, 1)] void TestClampToEyeBoundsY() {
	float2 frameDim = float2(2048, 1024);
	int2 top = Stereo::ClampToEyeBounds(int2(512, -1), 0, frameDim);
	ASSERT(AreEqual, top.y, 0);

	int2 bottom = Stereo::ClampToEyeBounds(int2(512, 1025), 0, frameDim);
	ASSERT(AreEqual, bottom.y, 1023);
}

	/// @tags vr, stereo, edge-detection
	/// ClampToEyeUV: interior UV is returned unchanged for both eyes
	[numthreads(1, 1, 1)] void TestClampToEyeUVInterior()
{
	float2 left = Stereo::ClampToEyeUV(float2(0.25, 0.5), 0);
	ASSERT(IsTrue, abs(left.x - 0.25) < kEps);
	ASSERT(IsTrue, abs(left.y - 0.5) < kEps);

	float2 right = Stereo::ClampToEyeUV(float2(0.75, 0.5), 1);
	ASSERT(IsTrue, abs(right.x - 0.75) < kEps);
	ASSERT(IsTrue, abs(right.y - 0.5) < kEps);
}

/// @tags vr, stereo, edge-detection
/// ClampToEyeUV: left eye x cannot cross the x=0.5 seam
[numthreads(1, 1, 1)] void TestClampToEyeUVLeftEyeSeam() {
	// x past the seam clamps to 0.5
	float2 result = Stereo::ClampToEyeUV(float2(0.6, 0.5), 0);
	ASSERT(IsTrue, abs(result.x - 0.5) < kEps);
}

	/// @tags vr, stereo, edge-detection
	/// ClampToEyeUV: right eye x cannot cross the x=0.5 seam
	[numthreads(1, 1, 1)] void TestClampToEyeUVRightEyeSeam()
{
	// x before the seam clamps to 0.5
	float2 result = Stereo::ClampToEyeUV(float2(0.4, 0.5), 1);
	ASSERT(IsTrue, abs(result.x - 0.5) < kEps);
}

/// @tags vr, stereo, edge-detection
/// ClampToEyeUV: x clamped at outer borders (left eye at 0.0, right eye at 1.0)
[numthreads(1, 1, 1)] void TestClampToEyeUVOuterBorders() {
	float2 leftBorder = Stereo::ClampToEyeUV(float2(-0.1, 0.5), 0);
	ASSERT(IsTrue, abs(leftBorder.x - 0.0) < kEps);

	float2 rightBorder = Stereo::ClampToEyeUV(float2(1.1, 0.5), 1);
	ASSERT(IsTrue, abs(rightBorder.x - 1.0) < kEps);
}

	/// @tags vr, stereo, edge-detection
	/// ClampToEyeUV: y coordinate is not modified
	[numthreads(1, 1, 1)] void TestClampToEyeUVYUnchanged()
{
	float2 result = Stereo::ClampToEyeUV(float2(0.25, 1.5), 0);
	ASSERT(IsTrue, abs(result.y - 1.5) < kEps);

	result = Stereo::ClampToEyeUV(float2(0.75, -0.5), 1);
	ASSERT(IsTrue, abs(result.y - (-0.5)) < kEps);
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

/// @tags vr, stereo, uv
/// ApplyVelocityToUV: correctly translates UV keeping stereoscopic boundaries intact and reports bounds
[numthreads(1, 1, 1)] void TestApplyVelocityToUV() {
	float2 velocity = float2(0.1, 0.0);
	bool oob;

	// Left eye bounds [0, 0.5], mapping left eye UV of 0.25 + 0.1 mono velocity -> 0.3 stereo
	float2 resultLeft = Stereo::ApplyVelocityToUV(float2(0.25, 0.5), velocity, oob);
	ASSERT(IsTrue, abs(resultLeft.x - 0.3) < kEps);
	ASSERT(IsTrue, abs(resultLeft.y - 0.5) < kEps);
	ASSERT(IsFalse, oob);

	// Right eye bounds [0.5, 1.0], mapping right eye UV of 0.75 + 0.1 mono velocity -> 0.8 stereo
	float2 resultRight = Stereo::ApplyVelocityToUV(float2(0.75, 0.5), velocity, oob);
	ASSERT(IsTrue, abs(resultRight.x - 0.8) < kEps);
	ASSERT(IsTrue, abs(resultRight.y - 0.5) < kEps);
	ASSERT(IsFalse, oob);

	// OOB condition: mono velocity pushes past 1.0
	float2 resultOob = Stereo::ApplyVelocityToUV(float2(0.25, 0.5), float2(1.5, 0.0), oob);
	ASSERT(IsTrue, oob);
	// In VR, out of bounds is clamped (mono x < 0 maps to 0 -> stereo 0, mono x > 1 saturates to 1 -> stereo 0.5 for left)
	ASSERT(IsTrue, abs(resultOob.x - 0.5) < kEps);
}
