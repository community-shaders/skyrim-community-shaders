// Zeros color in the HMD hidden area per eye.
// Prevents DLSS/FSR from temporally accumulating the engine's sky/ambient clear color
// into visible pixels during head movement ("light blue border" ghosting).
// depth == 0.0 is the unrendered/hidden area value (Skyrim reversed-Z: far plane = 0).
// DepthIn is the combined stereo depth buffer; DepthOffsetX selects the eye's half.
// ColorInOut is the isolated per-eye buffer; ColorOffsetX is always 0.
//
// When DepthWidth > 0, coordinate scaling is enabled: depth is at render-res while
// color is at display-res. The shader maps display-res color coordinates to render-res
// depth coordinates for the mask lookup.
//
// FallbackIn (t1): when bound, masked pixels read from this texture instead of writing
// black. When unbound, D3D11 returns (0,0,0,0) — same as clearing to black.
// FallbackOffsetX selects the eye's half in the stereo fallback texture.

cbuffer ClearHMDMaskCB : register(b0)
{
	uint DepthOffsetX;     // X offset into combined stereo depth (0 = left, eyeWidth = right)
	uint ColorOffsetX;     // X offset into color target (always 0 for per-eye buffers)
	uint DepthOffsetY;     // Y offset into combined stereo depth (non-zero when viewport scaling crops vertically)
	uint FallbackOffsetX;  // X offset into FallbackIn for stereo (0 when unused or left eye)
	// Optional coordinate scaling (zero = disabled, for backwards compat)
	uint DepthWidth;       // render-res eye width; if 0, no scaling (1:1 depth/color coords)
	uint DepthHeight;      // render-res eye height
	uint ColorWidth;       // display-res eye width
	uint ColorHeight;      // display-res eye height
};

Texture2D<float> DepthIn : register(t0);
Texture2D<float4> FallbackIn : register(t1);
RWTexture2D<float4> ColorInOut : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint2 colorPos = dispatchID.xy + uint2(ColorOffsetX, 0);
	uint2 depthPos;

	if (DepthWidth > 0) {
		// Scale from display-res color coordinates to render-res depth coordinates
		depthPos = uint2(
			(dispatchID.x * DepthWidth) / ColorWidth,
			(dispatchID.y * DepthHeight) / ColorHeight
		) + uint2(DepthOffsetX, DepthOffsetY);
	} else {
		depthPos = dispatchID.xy + uint2(DepthOffsetX, DepthOffsetY);
	}

	if (DepthIn[depthPos] == 0.0)
		ColorInOut[colorPos] = FallbackIn[dispatchID.xy + uint2(FallbackOffsetX, 0)];
		// When FallbackIn is unbound (existing callers): returns (0,0,0,0) → clears to black
		// When FallbackIn is bound (TAA mask restore): returns display RT content
}
