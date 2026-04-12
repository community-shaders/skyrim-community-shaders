// Blends DLSS subrect output back onto the stretched background in kMAIN.
// Replaces the hard CopySubresourceRegion with a feathered transition at the
// subrect boundary.  Two blend modes:
//   0 = Feather  – smoothstep alpha ramp over FeatherWidth pixels
//   1 = Dither   – noise-perturbed gradient in feather band (DitherStrength controls noise)

cbuffer BlendCB : register(b0)
{
	uint DstOffsetX;    // SBS destination X for this eye (0 or eyeWidthOut)
	uint DstOffsetY;    // SBS destination Y (usually 0, non-zero if subrect offset)
	uint SubWidth;      // DLSS output width  (subrect)
	uint SubHeight;     // DLSS output height (subrect)
	uint BlendMode;     // 0 = Feather, 1 = Dither
	float FeatherWidth; // Feather band in pixels (default ~8)
	uint FrameIndex;    // For dither noise animation
	uint SrcOffsetX;    // Source X offset (0 for most modes, non-zero for Extreme strip)
	float DitherStrength; // 0 = pure smooth gradient, 1 = natural noise, 2 = aggressive dither
};

Texture2D<float4> SrcTex  : register(t0);  // DLSS subrect output
RWTexture2D<float4> DstTex : register(u0);  // kMAIN (already has stretched background)

// Simple hash-based blue noise (no texture needed, near zero cost)
float BlueNoise(uint2 pos, uint frame)
{
	// Interleaved gradient noise (Jimenez 2014) — good spatial distribution
	float x = float(pos.x) + 5.588238 * float(frame);
	float y = float(pos.y) + 5.588238 * float(frame);
	return frac(52.9829189 * frac(0.06711056 * x + 0.00583715 * y));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x >= SubWidth || tid.y >= SubHeight)
		return;

	uint2 srcPos = uint2(tid.x + SrcOffsetX, tid.y);
	uint2 dstPos = uint2(tid.x + DstOffsetX, tid.y + DstOffsetY);

	float4 dlss = SrcTex.Load(int3(srcPos, 0));

	// Distance from nearest edge of the subrect (in pixels)
	float distL = (float)srcPos.x;
	float distR = (float)(SubWidth  - 1 - srcPos.x);
	float distT = (float)srcPos.y;
	float distB = (float)(SubHeight - 1 - srcPos.y);
	float edgeDist = min(min(distL, distR), min(distT, distB));

	if (edgeDist >= FeatherWidth) {
		// Interior: pure DLSS (fast path, skips background read)
		DstTex[dstPos] = dlss;
		return;
	}

	// We're in the feather band — need background
	float4 bg = DstTex[dstPos];

	if (BlendMode == 1) {
		// Dither: noise-perturbed continuous gradient
		// Noise shifts the blend threshold per-pixel → natural irregular boundary
		float t = edgeDist / FeatherWidth;  // 0 at edge, 1 at band end
		float noise = BlueNoise(srcPos, FrameIndex);
		float alpha = saturate(t + (noise - 0.5) * DitherStrength);
		DstTex[dstPos] = lerp(bg, dlss, alpha);
	} else {
		// Feather (default): smooth alpha ramp
		float alpha = smoothstep(0.0, FeatherWidth, edgeDist);
		DstTex[dstPos] = lerp(bg, dlss, alpha);
	}
}
