// Zeros color in the HMD hidden area per eye.
// Prevents DLSS/FSR from temporally accumulating the engine's sky/ambient clear color
// into visible pixels during head movement ("light blue border" ghosting).
// depth == 0.0 is the unrendered/hidden area value (Skyrim reversed-Z: far plane = 0).
// DepthIn is the combined stereo depth buffer; DepthOffsetX selects the eye's half.
// ColorInOut is the isolated per-eye buffer; ColorOffsetX is always 0.

cbuffer ClearHMDMaskCB : register(b0)
{
	uint DepthOffsetX;  // X offset into combined stereo depth (0 = left, eyeWidth = right)
	uint ColorOffsetX;  // X offset into color target (always 0 for per-eye buffers)
	uint pad0;
	uint pad1;
};

Texture2D<float> DepthIn : register(t0);
RWTexture2D<float4> ColorInOut : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Read from stereo depth, write to potentially stereo color
	if (DepthIn[dispatchID.xy + uint2(DepthOffsetX, 0)] == 0.0)
		ColorInOut[dispatchID.xy + uint2(ColorOffsetX, 0)] = float4(0.0, 0.0, 0.0, 0.0);
}
