// VR Stereo Optimizations - Stencil Write Pixel Shader
//
// Reads from the per-pixel mode classification texture.
// Only MODE_MAIN pixels write stencil ref=1 — these are reprojected by ReprojectionCS
// and must be skipped by the geometry pass (NOT_EQUAL stencil test, ref=1).
//
// All other modes (DISOCCLUDED, EDGE, EDGE_NEIGHBOUR, FULL_BLEND) discard so
// geometry renders those pixels normally. ReprojectionCS only fills MODE_MAIN, so
// stencil must not be written for any other mode.
//
// Mode texture is full SBS resolution (same as render target).
// The DSS is configured with StencilFunc=ALWAYS, StencilPassOp=REPLACE, ref=1.
// Pixels that survive (not discarded) get stencil=1 written.

#include "VRStereoOptimizations/cbuffers.hlsli"

Texture2D<uint> ModeTexture : register(t0);

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

	// Only MODE_MAIN pixels are filled by ReprojectionCS and should be stencil-culled.
	// EDGE/EDGE_NEIGHBOUR/FULL_BLEND must render normally; DISOCCLUDED is also fully shaded.
	if (mode != MODE_MAIN)
		discard;

	// Pixel survives: DSS writes stencil ref=1
	// No color output (no RTV bound)
}
