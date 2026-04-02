// VR Stereo Optimizations - Stencil Write Pixel Shader
//
// Reads from the per-pixel mode classification texture and depth texture.
// Discards pixels that should NOT be stencil-culled:
//   - MODE_DISOCCLUDED (0) = fully shaded in Eye 1, no reprojection needed
//   - MODE_FULL_BLEND (4) = near-camera pixels fully shaded in both eyes for supersampling
//   - Sky/HMD-mask pixels (depth >= 1.0 or depth < 1e-5) = need normal rendering
//     in the sky pass; they keep their MODE_EDGE tag in
//     the mode texture for VRPostProcess but must not be stencil-culled.
//
// Only geometry MODE_MAIN/MODE_EDGE pixels survive and get stencil ref=1 written.
//
// Mode texture is full SBS resolution (same as render target).
// The DSS is configured with StencilFunc=ALWAYS, StencilPassOp=REPLACE, ref=1.
// Pixels that survive (not discarded) get stencil=1 written.

#include "VRStereoOptimizations/cbuffers.hlsli"

Texture2D<uint> ModeTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);

struct PS_INPUT
{
	float4 Position: SV_Position;
	float2 TexCoord: TEXCOORD0;
};

void main(PS_INPUT input)
{
	// Mode texture is full SBS resolution — SV_Position maps directly
	// (viewport is Eye 1 half, so SV_Position.x starts at eyeWidth)
	int2 modeCoord = int2(input.Position.xy);

	uint mode = ModeTexture[modeCoord];

	// MODE_MAIN and MODE_EDGE in Eye 1 write stencil ref=1 (reprojectable).
	// These are reprojected from Eye 0; MODE_DISOCCLUDED and MODE_FULL_BLEND are fully shaded in Eye 1.
	if (mode == MODE_DISOCCLUDED)
		discard;

	// Sky/HMD-mask pixels must not be stencil-culled regardless of edge classification.
	// They keep their MODE_EDGE tag in the mode texture for VRPostProcess,
	// but must render normally in the sky pass (which runs after stencil culling).
	float depth = DepthTexture[modeCoord];
	if (depth >= 1.0 || depth < 1e-5)
		discard;

	// MODE_FULL_BLEND: near-camera pixels fully shaded in both eyes for supersampling
	if (mode == MODE_FULL_BLEND)
		discard;

	// Pixel survives: DSS writes stencil ref=1
	// No color output (no RTV bound)
}
