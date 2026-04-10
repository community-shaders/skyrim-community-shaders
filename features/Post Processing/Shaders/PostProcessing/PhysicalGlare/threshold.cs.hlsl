// PhysicalGlare - Threshold extraction & downsample to FFT resolution
//
// Reference:
//   Delavennat, J. (2021). Physically-based Real-time Glare.
//   Master's thesis (LIU-ITN-TEK-A--21/068-SE), Linköping University.
//   https://www.diva-portal.org/smash/record.jsf?pid=diva2:1629565
//
// Per-channel thresholding (paper section 3.2).
// Zero-padding to eliminate FFT circular convolution wrap-around (paper section 2.5):
//   Scene is placed in center quarter [N/4, 3N/4) of the N×N FFT texture;
//   the surrounding border remains zero, absorbing convolution overflow.

RWTexture2D<float2> RWTexFFT_R : register(u0);
RWTexture2D<float2> RWTexFFT_G : register(u1);
RWTexture2D<float2> RWTexFFT_B : register(u2);

Texture2D<float4> TexColor : register(t0);

cbuffer GlareCB : register(b1)
{
	float Threshold;
	float Intensity;
	float ScatterStrength;
	uint ApertureMode;

	int ApertureBlades;
	float ApertureRotation;
	float AdaptSpeed;
	float DeltaTime;

	uint FFTResolution;
	float RcpFFTResolution;
	float ScreenWidth;
	float ScreenHeight;

	uint ChannelIndex;
	uint EnableEyelashes;
	uint EyelashCount;
	float EyelashLength;

	float EyelashCurvature;
	float FresnelExponent;
	float ChromaticSpread;
	float ApertureSize;

	uint ParticleCount;
	float ParticleSize;
	uint GratingCount;
	float GratingStrength;
};

[numthreads(8, 8, 1)] void CS_Threshold(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	// Zero-padding (paper section 2.5):
	// Place the scene in the center quarter [N/4, 3N/4) of the N×N FFT texture.
	// The surrounding border stays zero, absorbing convolution overflow and
	// eliminating circular wrap-around artifacts.
	uint padding = FFTResolution / 4;
	uint sceneSize = FFTResolution / 2;

	if (tid.x >= padding && tid.x < padding + sceneSize &&
		tid.y >= padding && tid.y < padding + sceneSize) {
		// Map center region to screen UV [0,1]
		float2 localPos = float2(tid.x - padding, tid.y - padding);
		float2 uv = (localPos + 0.5) / float(sceneSize);

		// Sample scene at corresponding screen position
		uint2 screenPos = uint2(uv * float2(ScreenWidth, ScreenHeight));
		screenPos = min(screenPos, uint2(uint(ScreenWidth) - 1, uint(ScreenHeight) - 1));

		float3 color = TexColor[screenPos].rgb;

		// Per-channel threshold (paper section 3.2, adapted for HDR pipeline):
		// Subtract threshold per channel; values above threshold pass in original
		// HDR magnitude.  The DC-normalised convolution preserves this energy,
		// so no empirical ×10000 scaling is needed.
		float3 extracted = max(0, color.rgb - Threshold);

		RWTexFFT_R[tid] = float2(extracted.r, 0);
		RWTexFFT_G[tid] = float2(extracted.g, 0);
		RWTexFFT_B[tid] = float2(extracted.b, 0);
	} else {
		// Zero-padded border — absorbs convolution overflow
		RWTexFFT_R[tid] = float2(0, 0);
		RWTexFFT_G[tid] = float2(0, 0);
		RWTexFFT_B[tid] = float2(0, 0);
	}
}
