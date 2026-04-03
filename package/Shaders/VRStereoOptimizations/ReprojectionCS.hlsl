// VR Stereo Optimizations - Reprojection Compute Shader
//
// Fills Eye 1 pixels that were stencil-culled during rendering by reprojecting
// color data from Eye 0. Only operates on pixels classified as MODE_MAIN.
//
// Reads Eye 0 color directly from the OutputRW UAV (left half) and writes to
// Eye 1 (right half). No read-write conflict because reads and writes target
// strictly different halves of the texture.
//
// Input:
//   t0 = Depth buffer
//   t1 = Per-pixel mode classification texture
// Output:
//   u0 = Main render target UAV (reads Eye 0, writes Eye 1)

#include "Common/VR.hlsli"
#include "VRStereoOptimizations/cbuffers.hlsli"

Texture2D<float> DepthTexture : register(t0);
Texture2D<uint> ModeTexture : register(t1);

RWTexture2D<float4> OutputRW : register(u0);

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	uint eyeWidth = (uint)FrameDim.x / 2;
	uint eyeHeight = (uint)FrameDim.y;

	if (any(dtid >= uint2(eyeWidth, eyeHeight)))
		return;

	// dtid is in Eye 1 local coords; convert to stereo buffer coords
	uint2 stereoCoord = uint2(dtid.x + eyeWidth, dtid.y);

	// Only fill pixels that were marked for reprojection
	// Mode texture is full SBS resolution, so use stereoCoord for Eye 1
	uint mode = ModeTexture[stereoCoord];
	if (mode != MODE_MAIN)
		return;

	float depth = DepthTexture[stereoCoord];

	// Compute mono UV for this Eye 1 pixel
	float2 stereoUV = (float2(stereoCoord) + 0.5) * RcpFrameDim;
	float2 monoUV = Stereo::ConvertFromStereoUV(stereoUV, 1);

	// Reproject to Eye 0 and sample color
	float3 otherEyeUV = Stereo::ConvertMonoUVToOtherEye(float3(monoUV, depth), 1);
	float2 eye0StereoUV = Stereo::ConvertToStereoUV(otherEyeUV.xy, 0);
	int2 eye0Px = clamp(int2(eye0StereoUV * FrameDim), int2(0, 0), int2(FrameDim) - 1);

	float4 reprojectedColor = OutputRW[eye0Px];

	// Write to Eye 1 in the main render target
	OutputRW[stereoCoord] = reprojectedColor;
}
