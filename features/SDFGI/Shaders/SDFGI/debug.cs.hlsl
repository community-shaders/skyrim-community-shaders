#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

#include "SDFGI/common.hlsli"

// ---- Resources ----

Texture2D<float> depthTexture : register(t0);
Texture2D<float4> normalRoughnessTexture : register(t1);

// SDF cascade distance field textures (one per cascade, finest to coarsest)
Texture3D<float> sdfCascade0 : register(t2);
Texture3D<float> sdfCascade1 : register(t3);
Texture3D<float> sdfCascade2 : register(t4);
Texture3D<float> sdfCascade3 : register(t5);
Texture3D<float> sdfCascade4 : register(t6);
Texture3D<float> sdfCascade5 : register(t7);
Texture3D<float> sdfCascade6 : register(t8);
Texture3D<float> sdfCascade7 : register(t9);

RWTexture2D<float4> giOutput : register(u0);

// Helper to sample the appropriate cascade texture by index.
// HLSL does not support texture array indexing by variable, so we use a switch.
float SampleSDF(uint cascadeIdx, float3 uvw)
{
	switch (cascadeIdx) {
	case 0: return sdfCascade0.SampleLevel(linearSampler, uvw, 0);
	case 1: return sdfCascade1.SampleLevel(linearSampler, uvw, 0);
	case 2: return sdfCascade2.SampleLevel(linearSampler, uvw, 0);
	case 3: return sdfCascade3.SampleLevel(linearSampler, uvw, 0);
	case 4: return sdfCascade4.SampleLevel(linearSampler, uvw, 0);
	case 5: return sdfCascade5.SampleLevel(linearSampler, uvw, 0);
	case 6: return sdfCascade6.SampleLevel(linearSampler, uvw, 0);
	case 7: return sdfCascade7.SampleLevel(linearSampler, uvw, 0);
	default: return 1.0;
	}
}

// Map a signed distance value to a debug color.
//   Negative (inside solid)  -> green
//   Near zero (on surface)   -> red
//   Positive (in open space) -> blue, fading with distance
float3 DistanceToDebugColor(float dist)
{
	if (dist < 0.0) {
		// Inside geometry: brighter green for closer to surface
		float t = saturate(-dist * 2.0);
		return float3(0, lerp(0.2, 1.0, t), 0);
	}

	// Surface region (within ~0.5 cell)
	float surfaceThreshold = 0.5;
	if (dist < surfaceThreshold) {
		float t = dist / surfaceThreshold;
		// Red at the surface, fading to blue
		return lerp(float3(1, 0, 0), float3(0, 0, 1), t);
	}

	// Far from surface: blue fading to dark with distance
	float t = saturate((dist - surfaceThreshold) / 4.0);
	return float3(0, 0, lerp(1.0, 0.1, t));
}

// ---- Main kernel ----

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	// Bounds check
	if (any(dispatchID.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	// Screen UV
	float2 uv = float2(dispatchID.xy + 0.5) * SharedData::BufferDim.zw;
	uv *= FrameBuffer::DynamicResolutionParams2.xy;

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	// Depth
	float depth = depthTexture[dispatchID.xy];
	if (depth >= 1.0) {
		giOutput[dispatchID.xy] = float4(0, 0, 0, 0);
		return;
	}

	// Reconstruct camera-relative world position
	float4 positionCS = float4(2.0 * float2(uv.x, -uv.y + 1.0) - 1.0, depth, 1.0);
	float4 positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
	positionWS.xyz /= positionWS.w;

	float3 debugColor = float3(0, 0, 0);
	bool found = false;

	for (uint c = 0; c < MaxCascades && !found; c++) {
		// Convert world position to cascade grid coordinates [0, CASCADE_SIZE]
		float3 gridPos = WorldToCascadePos(positionWS.xyz, c);

		// Normalize to [0,1] UVW for texture sampling
		float3 uvw = gridPos / float(CASCADE_SIZE);

		// Check if inside cascade bounds
		if (any(uvw < 0.0) || any(uvw > 1.0))
			continue;

		// Sample the SDF distance at this position
		float dist = SampleSDF(c, uvw);

		// Convert distance to debug visualization color
		debugColor = DistanceToDebugColor(dist);

		// Tint slightly by cascade index for identification
		// Cascade 0: no tint, 1: slight yellow, 2: slight cyan, 3: slight magenta, etc.
		float3 cascadeTint = float3(1, 1, 1);
		switch (c) {
		case 0: cascadeTint = float3(1.0, 1.0, 1.0); break;
		case 1: cascadeTint = float3(1.0, 1.0, 0.8); break;
		case 2: cascadeTint = float3(0.8, 1.0, 1.0); break;
		case 3: cascadeTint = float3(1.0, 0.8, 1.0); break;
		case 4: cascadeTint = float3(1.0, 0.9, 0.8); break;
		case 5: cascadeTint = float3(0.8, 1.0, 0.9); break;
		case 6: cascadeTint = float3(0.9, 0.8, 1.0); break;
		case 7: cascadeTint = float3(0.9, 1.0, 0.8); break;
		}
		debugColor *= cascadeTint;

		found = true;
	}

	giOutput[dispatchID.xy] = float4(debugColor, 1.0);
}
