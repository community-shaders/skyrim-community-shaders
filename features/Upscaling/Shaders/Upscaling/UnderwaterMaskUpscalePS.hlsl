#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "Common/FrameBuffer.hlsli"
#	include "Common/Math.hlsli"
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

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);

	// Clamp within bounds
	uv = clamp(uv, 0.0, FrameBuffer::DynamicResolutionParams1.xy);

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

	// Tile sentinel: try TESWaterSystem fallback. WaterSystemHeight is valid only when
	// playerUnderwater == true (fully submerged); it is stored eye-0 camera-relative so
	// the same per-eye correction as GetWaterData applies.
	if (waterHeight <= WATER_HEIGHT_NO_TILE_SENTINEL) {
		float sysHeight = SharedData::WaterSystemHeight;
		if (sysHeight > WATER_HEIGHT_NO_TILE_SENTINEL)
			waterHeight = sysHeight + FrameBuffer::CameraPosAdjust[0].z - FrameBuffer::CameraPosAdjust[eyeIndex].z;
	}

	// GetWaterData returns INT_MIN (~-2.147e9) when the tile is outside the 5x5 grid.
	if (waterHeight > WATER_HEIGHT_NO_TILE_SENTINEL) {
		// Unpack from side-by-side stereo layout to per-eye UV [0, 1]
		float2 eyeUV = float2(input.TexCoord.x * 2.0 - (float)eyeIndex, input.TexCoord.y);

		// Convert to NDC [-1, 1].  UV y=0 is the top of the screen; NDC y=+1 is the top.
		float2 ndc = float2(eyeUV.x * 2.0 - 1.0, 1.0 - eyeUV.y * 2.0);

		// Sample depth using the shared de-jittered stereo UV (already DR-adjusted above).
		// uv is in stereo space so no ConvertUVToSampleCoord round-trip is needed.
		float depth = SceneDepth.Load(int3(uv * SharedData::BufferDim.xy, 0)).x;

		if (depth > EPSILON_DEPTH_SKY) {
			// Geometry pixel: reconstruct world position from depth.
			// CameraViewProjInverse[eyeIndex] maps clip-space back to the per-eye
			// camera-relative world space.  waterHeight has been adjusted to the same
			// frame, so the comparison is correct for both eyes.
			float4 worldPos = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], float4(ndc, depth, 1.0));
			worldPos /= worldPos.w;
			// kSurfaceBias (Skyrim world units, ~1 unit ≈ 1.4 cm) anchors the mask
			// threshold relative to the flat waterHeight plane to absorb wave-vertex
			// displacement (measured max trough ≈ 2.92 units; 3.5 gives margin).
			//
			// The threshold direction depends on view orientation:
			//   Looking UP   (worldPos.z > 0, pixel above camera in world space):
			//     Camera is below the surface viewing it from underneath.
			//     Expand threshold upward by +kSurfaceBias so the entire wave surface
			//     (crests and troughs alike) is included in the masked region.
			//   Looking DOWN (worldPos.z <= 0, pixel below or level with camera):
			//     The surface is seen from above or the camera is above water.
			//     Shrink threshold downward by -kSurfaceBias so the surface itself
			//     is excluded from the mask (no fog on the surface seen from above).
			static const float kSurfaceBias = 3.5;
			bool lookingUp = worldPos.z > 0.0;
			bool cameraUnderwater = waterHeight > 0.0;
			float threshold = (cameraUnderwater && lookingUp) ? waterHeight + kSurfaceBias : waterHeight - kSurfaceBias;
			psout.UnderwaterMask = (worldPos.z < threshold) ? 1.0 : 0.0;
		} else {
			// depth <= EPSILON_DEPTH_SKY: sky / unrendered pixels (reversed-Z depth clear value).
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
	// No water tile or system height available: fall through to the standard sampler path.
	// The left-eye result from the vanilla mask is still accurate here; the right-eye
	// will be approximate, but both sources failing implies no nearby water so the
	// visual impact is nil.
#	endif

	// Upscale using linear sampling with jitter-corrected coordinates
	psout.UnderwaterMask = UnderwaterMask.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif
