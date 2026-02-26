// Stereo Bilateral Blend - Post-composite stereo consistency pass for VR
//
// Full-image depth-aware bilateral blend with back-check validation that
// reprojects each pixel to the other eye and blends based on depth agreement.
// No spatial cutoff -- the blend weight comes entirely from depth confidence
// and round-trip reprojection validation, so there's no visible seam.
//
// Based on the stereo-aware bilateral filter from:
// Shi, Billeter, Eisemann 2022, "Stereo-consistent screen-space ambient occlusion"
// https://eprints.whiterose.ac.uk/id/eprint/187713/

#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);

RWTexture2D<float4> OutputRW : register(u0);

cbuffer StereoBlendCB : register(b1)
{
	float2 FrameDim;
	float2 RcpFrameDim;
	float DepthSigma;
	float MaxBlendFactor;
	float ColorDiffThreshold;
	float pad;
};

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	if (any(dtid >= uint2(FrameDim)))
		return;

	float2 uv = (dtid + 0.5) * RcpFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float4 centerColor = ColorTexture[dtid];
	float centerDepth = DepthTexture[dtid];

	// State 0 = HMD mask (depth==0) or sky (depth==1): no reprojection attempted
	// State 1 = reprojected, out of other eye's bounds
	// State 2 = reprojected, back-check passed
	// State 3 = reprojected, back-check failed (occlusion edge)
	uint debugState = 0;

	Stereo::StereoBilateralResult r = (Stereo::StereoBilateralResult)0;
	float4 blendedColor = centerColor;

	// depth == 0.0: VR HMD mask (pixels outside the lens area, never written by the engine)
	// depth == 1.0: sky/far plane (no real geometry, bilateral reprojection not meaningful)
	bool isSkipPixel = centerDepth < 1e-5 || centerDepth >= 1.0;
	if (!isSkipPixel) {
		r = Stereo::ReprojectToOtherEye(uv, centerDepth, eyeIndex, FrameDim);
		if (r.valid) {
			float4 otherColor = ColorTexture[r.otherPx];
			float otherDepth = DepthTexture[r.otherPx];
			Stereo::FinalizeStereoBlend(r, uv, centerDepth, otherDepth, eyeIndex, FrameDim, DepthSigma, MaxBlendFactor);

			// Only blend where the two eyes actually disagree (screen-space effect
			// inconsistency). Luminance difference below the threshold means both
			// eyes computed the same result and blending would only destroy parallax.
			float colorDiff = abs(dot(centerColor.rgb, float3(0.2126, 0.7152, 0.0722)) -
								  dot(otherColor.rgb, float3(0.2126, 0.7152, 0.0722)));
			float colorGate = smoothstep(ColorDiffThreshold * 0.5, ColorDiffThreshold * 2.0, colorDiff);
			r.blendWeight *= colorGate;

			blendedColor = lerp(centerColor, otherColor, r.blendWeight);
			debugState = r.backCheckPassed ? 2 : 3;
		} else {
			debugState = 1;
		}
	}

#ifdef DEBUG_BACKCHECK
	// Debug visualization:
	//   Blue  = HMD mask (depth==0) or sky (depth==1): skipped
	//   Grey  = reprojection out of bounds (other eye can't see this point)
	//   Green = back-check passed (surfaces match in both eyes)
	//   Red   = back-check failed (occlusion edge, surfaces disagree)
	float3 debugColors[4] = {
		float3(0.1, 0.1, 0.5),  // 0: mask/sky - blue
		float3(0.3, 0.3, 0.3),  // 1: out of bounds - grey
		float3(0.0, 0.5, 0.0),  // 2: back-check passed - green
		float3(0.5, 0.0, 0.0)   // 3: back-check failed - red
	};
	OutputRW[dtid] = float4(lerp(centerColor.rgb, debugColors[debugState], 0.7), centerColor.a);
#elif defined(DEBUG_BLEND_WEIGHT)
	// Blend weight heatmap: only pixels with actual blend activity are colorized.
	// Untouched pixels pass through unmodified.
	float w = saturate(r.blendWeight / max(MaxBlendFactor, 1e-5));
	if (w > 1e-3) {
		float3 heatmap = Color::TurboColormap(w);
		OutputRW[dtid] = float4(lerp(centerColor.rgb, saturate(heatmap), 0.8), centerColor.a);
	} else {
		OutputRW[dtid] = centerColor;
	}
#else
	OutputRW[dtid] = blendedColor;
#endif
}
