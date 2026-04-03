// VR Post-Process - Bilateral blend for near-camera 2x supersampling
//
// Runs after all compositing and stereo blending is complete.
// Reads per-pixel classification from StencilCS and applies:
//   - MODE_FULL_BLEND: bilateral depth-weighted blend for 2x supersampling
//
// Only MODE_FULL_BLEND pixels are processed. All others pass through untouched.

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

Texture2D<float4> ColorTexture : register(t0);  // Copy of final composited image
Texture2D<uint> ModeTexture : register(t1);
Texture2D<float> DepthTexture : register(t2);

RWTexture2D<float4> OutputRW : register(u0);

cbuffer VRPostProcessCB : register(b1)
{
	float2 FrameDim;
	float2 RcpFrameDim;
	float DebugEdgeTint;      // 0 = off, >0 = debug visualization strength
	uint DebugMode;           // 0 = normal, 1 = depth map diagnostic, 2 = full blend depth visualizer
	float FullBlendDistance;  // Linearized depth threshold for full blend zone visualization
	float _pad;               // Pad to 16-byte alignment
};

#include "VRStereoOptimizations/modes.hlsli"

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	if (any(dtid >= uint2(FrameDim)))
		return;

	uint pixelMode = ModeTexture[dtid];

	// Depth map diagnostic: show mode texture contents as solid colors
	if (DebugMode == 1) {
		float4 c = ColorTexture[dtid];
		if (pixelMode == MODE_EDGE)
			OutputRW[dtid] = float4(lerp(c.rgb, float3(0, 1, 0), 0.5), c.a);
		else if (pixelMode == MODE_EDGE_NEIGHBOUR)
			OutputRW[dtid] = float4(lerp(c.rgb, float3(1, 0, 1), 0.5), c.a);
		else if (pixelMode == MODE_DISOCCLUDED)
			OutputRW[dtid] = float4(lerp(c.rgb, float3(0, 0.5, 1), 0.3), c.a);
		else if (pixelMode == MODE_FULL_BLEND)
			OutputRW[dtid] = float4(lerp(c.rgb, float3(1, 0.5, 0), 0.5), c.a);  // Orange = full blend zone
		return;
	}

	// Full blend depth visualizer: shows the depth boundary as a cyan tint
	if (DebugMode == 2) {
		float2 uvDb = (dtid + 0.5) * RcpFrameDim;
		float depthDb = DepthTexture[dtid];
		if (depthDb < 1e-5 || depthDb >= 1.0)
			return;
		float linDepth = SharedData::GetScreenDepth(depthDb);
		if (linDepth < FullBlendDistance) {
			float4 c = ColorTexture[dtid];
			float proximity = saturate(1.0 - linDepth / max(FullBlendDistance, 1.0));
			OutputRW[dtid] = float4(lerp(c.rgb, float3(0, 1, 1), proximity * 0.4), c.a);
		}
		return;
	}

	// Only process full blend pixels
	if (pixelMode != MODE_FULL_BLEND)
		return;

	float2 uv = (dtid + 0.5) * RcpFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float4 result = ColorTexture[dtid];

	// === MODE_FULL_BLEND: bilateral blend for 2x supersampling ===
	{
		float4 center = result;
		float centerDepth = DepthTexture[dtid];

		// Reproject to the other eye
		Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, centerDepth, eyeIndex, FrameDim);
		if (!r.valid) {
			// Debug tint for failed reprojection
			if (DebugEdgeTint > 0)
				OutputRW[dtid] = float4(lerp(center.rgb, float3(1, 0.5, 0), DebugEdgeTint), center.a);
			return;
		}

		// Only blend with pixels that have valid composited data in both eyes.
		uint otherMode = ModeTexture[r.otherPx];
		if (otherMode != MODE_FULL_BLEND && otherMode != MODE_DISOCCLUDED)
			return;

		float4 otherColor = ColorTexture[r.otherPx];
		float otherDepth = DepthTexture[r.otherPx];

		// Depth-weighted bilateral blend
		float maxDepth = max(max(centerDepth, otherDepth), 1e-5);
		float depthAgreement = 1.0 - saturate(abs(centerDepth - otherDepth) / maxDepth / 0.02);
		float blendWeight = 0.5 * depthAgreement;

		result = lerp(center, otherColor, blendWeight);

		if (DebugEdgeTint > 0)
			result.rgb = lerp(result.rgb, float3(0, 1, 1), DebugEdgeTint);
	}

	OutputRW[dtid] = result;
}
