// Temporal accumulation filter for the periphery background.
// Runs on render-resolution SBS layout. Blends current frame with reprojected
// history using motion vectors. EMA with motion-adaptive alpha and luma
// rejection to suppress ghosting during movement while keeping strong
// flicker reduction in the non-focal periphery when stationary.

cbuffer TemporalSmoothCB : register(b0)
{
	uint TexWidth;     // SBS width (render-res)
	uint TexHeight;    // SBS height (render-res)
	float BlendAlpha;  // Current-frame weight (0.05 = very smooth, 0.5 = less smooth)
	uint _pad;
};

Texture2D<float4> CurrentTex : register(t0);   // vrRenderSBS snapshot (current frame)
Texture2D<float4> HistoryTex : register(t1);   // Previous frame smoothed (ping-pong read)
Texture2D<float4> MvecTex : register(t2);      // Motion vectors (per-eye UV delta, current→previous)
SamplerState BilinearSampler : register(s0);   // For history reprojection
RWTexture2D<float4> OutputTex : register(u0);  // New history (ping-pong write)

[numthreads(8, 8, 1)] void main(uint3 tid : SV_DispatchThreadID) {
	if (tid.x >= TexWidth || tid.y >= TexHeight)
		return;

	uint2 pos = tid.xy;
	float4 current = CurrentTex.Load(int3(pos, 0));

	// Motion vector is per-eye UV delta (current → previous), range ≈ ±small.
	// In SBS layout the x axis spans two eyes, so x-component must be halved.
	float2 mv = MvecTex.Load(int3(pos, 0)).xy;
	float2 currentUV = (float2(pos) + 0.5) / float2(TexWidth, TexHeight);
	float2 reprojUV = currentUV + mv * float2(0.5, 1.0);

	// ── SBS eye-boundary clamp ──
	// Prevent reprojection from crossing into the other eye's half.
	// Left eye: x ∈ [0, 0.5), right eye: x ∈ [0.5, 1.0).
	float halfW = 0.5;
	float eyeMinX = (currentUV.x < halfW) ? 0.0 : halfW;
	float eyeMaxX = eyeMinX + halfW;
	float texelHalfX = 0.5 / (float)TexWidth;
	reprojUV.x = clamp(reprojUV.x, eyeMinX + texelHalfX, eyeMaxX - texelHalfX);
	reprojUV.y = clamp(reprojUV.y, 0.0, 1.0);

	// Bilinear sample history at reprojected position
	float4 history = HistoryTex.SampleLevel(BilinearSampler, reprojUV, 0);

	// ── Anti-ghosting: motion-adaptive alpha (squared for soft ramp) ──
	// Increased sensitivity (*10): VR head sway still low enough to keep smoothing,
	// but moderate movement now ramps up faster to reduce ghosting.
	float motionRaw = saturate(length(mv) * 10.0);
	float finalAlpha = max(BlendAlpha, motionRaw * motionRaw);

	// Exponential moving average with adaptive weight
	OutputTex[pos] = lerp(history, current, finalAlpha);
}
