// Based on this Reshade shader
// https://github.com/RdenBlaauwen/RCAS-for-ReShade

// Based on https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/sdk/include/FidelityFX/gpu/fsr1/ffx_fsr1.h#L684
//==============================================================================================================================
//
//                                      FSR - [RCAS] ROBUST CONTRAST ADAPTIVE SHARPENING
//
//------------------------------------------------------------------------------------------------------------------------------
// CAS uses a simplified mechanism to convert local contrast into a variable amount of sharpness.
// RCAS uses a more exact mechanism, solving for the maximum local sharpness possible before clipping.
// RCAS also has a built in process to limit sharpening of what it detects as possible noise.
// RCAS sharper does not support scaling, as it should be applied after EASU scaling.
// Pass EASU output straight into RCAS, no color conversions necessary.
//------------------------------------------------------------------------------------------------------------------------------
// RCAS is based on the following logic.
// RCAS uses a 5 tap filter in a cross pattern (same as CAS),
//    w                n
//  w 1 w  for taps  w m e
//    w                s
// Where 'w' is the negative lobe weight.
//  output = (w*(n+e+w+s)+m)/(4*w+1)
// RCAS solves for 'w' by seeing where the signal might clip out of the {0 to 1} input range,
//  0 == (w*(n+e+w+s)+m)/(4*w+1) -> w = -m/(n+e+w+s)
//  1 == (w*(n+e+w+s)+m)/(4*w+1) -> w = (1-m)/(n+e+w+s-4*1)
// Then chooses the 'w' which results in no clipping, limits 'w', and multiplies by the 'sharp' amount.
// This solution above has issues with MSAA input as the steps along the gradient cause edge detection issues.
// So RCAS uses 4x the maximum and 4x the minimum (depending on equation)in place of the individual taps.
// As well as switching from 'm' to either the minimum or maximum (depending on side), to help in energy conservation.
// This stabilizes RCAS.
// RCAS does a simple highpass which is normalized against the local contrast then shaped,
//       0.25
//  0.25  -1  0.25
//       0.25
// This is used as a noise detection filter, to reduce the effect of RCAS on grain, and focus on real edges.

Texture2D<float3> Source : register(t0);
RWTexture2D<float3> Dest : register(u0);

float getRCASLuma(float3 rgb)
{
	return dot(rgb, float3(0.5, 1.0, 0.5));
}

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	float3 e = Source.Load(int3(DTid.x, DTid.y, 0)).rgb;

	float3 b = Source.Load(int3(DTid.x, DTid.y - 1, 0)).rgb;
	float3 d = Source.Load(int3(DTid.x - 1, DTid.y, 0)).rgb;
	float3 f = Source.Load(int3(DTid.x + 1, DTid.y, 0)).rgb;
	float3 h = Source.Load(int3(DTid.x, DTid.y + 1, 0)).rgb;

	// Luma times 2.
	float bL = getRCASLuma(b);
	float dL = getRCASLuma(d);
	float eL = getRCASLuma(e);
	float fL = getRCASLuma(f);
	float hL = getRCASLuma(h);

	// Noise detection.
	float nz = (bL + dL + fL + hL) * 0.25 - eL;
	float range = max(max(max(bL, dL), max(hL, fL)), eL) - min(min(min(bL, dL), min(eL, fL)), hL);
	nz = saturate(abs(nz) * rcp(range));
	nz = -0.5 * nz + 1.0;

	// Min and max of ring.
	float3 minRGB = min(min(b, d), min(f, h));
	float3 maxRGB = max(max(b, d), max(f, h));

	// Immediate constants for peak range.
	float2 peakC = float2(1.0, -4.0);

	// Limiters, these need to use high precision reciprocal operations.
	// Decided to use standard rcp for now in hopes of optimizing it
	float3 hitMin = minRGB * rcp(4.0 * maxRGB);
	float3 hitMax = (peakC.xxx - maxRGB) * rcp(4.0 * minRGB + peakC.yyy);
	float3 lobeRGB = max(-hitMin, hitMax);
	float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * SHARPNESS;

	// Apply noise removal.
	lobe *= nz;

	// Resolve, which needs medium precision rcp approximation to avoid visible tonality changes.
	float rcpL = rcp(4.0 * lobe + 1.0);
	float3 output = ((b + d + f + h) * lobe + e) * rcpL;

	Dest[DTid.xy] = output;
}