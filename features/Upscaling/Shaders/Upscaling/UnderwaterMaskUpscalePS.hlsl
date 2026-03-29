#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "Common/FrameBuffer.hlsli"
#	include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float UnderwaterMask: SV_TARGET;
};

SamplerState LinearSampler : register(s0);

Texture2D<float> UnderwaterMask : register(t0);
#	if defined(VR)
Texture2D<float> SceneDepth : register(t1);
#	endif

cbuffer JitterCB : register(b0)
{
	float2 jitter;
	float useWideKernel;
	float pad0;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(VR)
	// In VR the vanilla waterline draw (DrawIndexedInstanced, 2 instances) emits
	// identical left-eye clip positions for both instances.  The internal-res mask
	// therefore only represents the left eye: the right-eye half of the buffer
	// contains the tapered apex of the left-eye polygon, which is nearly all black.
	// GetDynamicResolutionAdjustedScreenPosition then samples that black region for
	// the right eye, making the entire right-eye underwater fog incorrect.
	//
	// Fix: reconstruct the mask analytically per-eye.  For a horizontal water plane
	// at height waterHeight, a pixel is "underwater" (mask = 1) when:
	//   - the camera itself is below the water surface, OR
	//   - the ray from the per-eye camera through this pixel points downward
	//     (rayDir.z < 0), meaning it looks below the water plane.
	// This exactly reproduces what the vanilla waterline polygon approximates,
	// but correctly per-eye.

	uint eyeIndex = (input.TexCoord.x >= 0.5) ? 1 : 0;

	// WaterData is a 5×5 grid centered on the camera; tile 12 (row 2, col 2) is
	// always the camera's own tile.  Pass eyeIndex so GetWaterData corrects the .w
	// (water surface height) from eye-0 camera-relative Z into the current eye's frame.
	// GetWaterData expects a camera-relative XY position; float3(0,0,0) is the camera
	// itself, which always maps to the center tile (12).
	float waterHeight = SharedData::GetWaterData(float3(0, 0, 0), eyeIndex).w;

	// Sentinel: -FLT_MAX means no water body is present in this tile.
	if (waterHeight > -1e9) {
		// Unpack from side-by-side stereo layout to per-eye UV [0, 1]
		float2 eyeUV = float2(input.TexCoord.x * 2.0 - (float)eyeIndex, input.TexCoord.y);

		// Convert to NDC [-1, 1].  UV y=0 is the top of the screen; NDC y=+1 is the top.
		float2 ndc = float2(eyeUV.x * 2.0 - 1.0, 1.0 - eyeUV.y * 2.0);

		// Sample the scene depth.  SceneDepth is depthCopy (t1), explicitly bound
		// by the C++ pass.  ConvertUVToSampleCoord handles stereo layout and dynamic
		// resolution.
		float depth = SceneDepth.Load(SharedData::ConvertUVToSampleCoord(eyeUV, eyeIndex)).x;

		if (depth > 0.0) {
			// Geometry pixel: reconstruct world position from depth.
			// CameraViewProjInverse[eyeIndex] maps clip-space back to the per-eye
			// camera-relative world space.  waterHeight has been adjusted to the same
			// frame, so the comparison is exact for both eyes.
			float4 worldPos = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4(ndc, depth, 1.0));
			worldPos /= worldPos.w;
			psout.UnderwaterMask = (worldPos.z < waterHeight) ? 1.0 : 0.0;
		} else {
			// depth == 0: sky / unrendered pixels (reversed-Z depth clear value).
			// Unproject to obtain the per-pixel ray direction and decide based on that.
			float4 worldFarPos = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4(ndc, 0.0, 1.0));
			worldFarPos /= worldFarPos.w;
			float3 rayDir = normalize(worldFarPos.xyz);
			// Per-eye waterHeight > 0 means the water surface is above THIS eye's camera
			// (eye is below water); <= 0 means the eye camera is above the water surface.
			psout.UnderwaterMask = (waterHeight > 0.0 || rayDir.z < 0.0) ? 1.0 : 0.0;
		}
		return psout;
	}
	// No water tile in range: fall through to the standard sampler path.
	// The left-eye result from the vanilla mask is still accurate here; the right-eye
	// will be approximate, but in the absence of nearby water the visual impact is nil.
#	endif

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);

	// Clamp within bounds
	uv = clamp(uv, 0.0, FrameBuffer::DynamicResolutionParams1.xy);

	// Upscale using linear sampling with jitter-corrected coordinates
	psout.UnderwaterMask = UnderwaterMask.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif
