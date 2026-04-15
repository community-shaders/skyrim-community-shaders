#include "PostProcessing/common.hlsli"

static const float PI = 3.14159265359;
static const float EPSILON = 1e-6;
static const int NUM_GHOSTS = 8;

// Resources
Texture2D<float4> InputTexture : register(t0);
SamplerState ColorSampler : register(s0);
SamplerState BorderSampler : register(s1);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer LensFlareConstants : register(b1)
{
	// Per-pass dimensions (updated before each dispatch)
	float OutputWidth;
	float OutputHeight;
	float InputWidth;
	float InputHeight;

	// Threshold
	float ThresholdLevel;
	float ThresholdRange;
	float GhostStrength;
	float GhostChromaShift;

	// Halo
	float HaloStrength;
	float HaloRadius;
	float HaloWidth;
	float HaloCompression;

	float HaloChromaShift;
	float Intensity;
	uint FFTResolution;
	int GLocalMask;

	float3 Tint;
	float KernelScale;

	float AspectRatio;
	int ApertureBlades;
	float ApertureRotation;
	float PadScale;

	uint ActiveGhostMask;
	float ApertureSize;
	float2 _pad0;

	// Ghost data
	float4 GhostColors[NUM_GHOSTS];
	float4 GhostScalesPacked[2];  // 8 scales packed into 2 float4s
	float4 GhostKernelScalesPacked[2];
}

// ============================================================
// Utilities
// ============================================================

float GetGhostScale(int i)
{
	return GhostScalesPacked[i / 4][i % 4];
}

// Fisheye UV distortion (based on Shadertoy by Crucifer)
float2 FisheyeUV(float2 uv, float compression, float zoom)
{
	float2 negPosUV = 2.0f * uv - 1.0f;
	float scale = compression * atan(rcp(compression));
	float radiusDist = length(negPosUV) * scale;
	float radiusDir = compression * tan(radiusDist / compression) * zoom;
	float phi = atan2(negPosUV.y, negPosUV.x);
	float2 newUV = float2(radiusDir * cos(phi) + 1.0f, radiusDir * sin(phi) + 1.0f) * 0.5f;
	return newUV;
}

// Screen-space disc mask (vignette)
float DiscMask(float2 screenPos)
{
	return saturate(1.0f - dot(screenPos, screenPos));
}

// ============================================================
// Pass 1: CSThreshold — 13-tap CoD-style downsample + threshold
// Input: full-res scene (t0), Output: half-res thresholded buffer (u0)
// ============================================================
[numthreads(8, 8, 1)] void CSThreshold(uint3 DTid : SV_DispatchThreadID) {
	if (DTid.x >= (uint)OutputWidth || DTid.y >= (uint)OutputHeight)
		return;

	// UV maps output pixel to normalized [0,1] — bilinear sampling downscales from full-res input
	float2 uv = (DTid.xy + 0.5f) / float2(OutputWidth, OutputHeight);
	// Pixel size in input (full-res) space for sampling offsets
	float2 pixelSize = 1.0f / float2(InputWidth, InputHeight);

	float3 color = 0;

	// 4 center samples (weight 0.5)
	color += InputTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(-1.0f, 1.0f), 0).rgb;
	color += InputTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(1.0f, 1.0f), 0).rgb;
	color += InputTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(-1.0f, -1.0f), 0).rgb;
	color += InputTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(1.0f, -1.0f), 0).rgb;
	float3 result = (color / 4.0f) * 0.5f;

	// 9 outer samples (weight 0.5)
	color = 0;
	[unroll] for (int x = -1; x <= 1; x++)
	{
		[unroll] for (int y = -1; y <= 1; y++)
		{
			color += InputTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(x, y) * 2.0f, 0).rgb;
		}
	}
	result += (color / 9.0f) * 0.5f;

	// Smooth threshold (level + range)
	float luminance = dot(result, float3(0.333f, 0.333f, 0.333f));
	float thresholdScale = saturate((luminance - ThresholdLevel) / max(ThresholdRange, 0.001f));
	result *= thresholdScale;

	OutputTexture[DTid.xy] = float4(result, 1.0f);
}

	// ============================================================
	// Pass 2: CSGhostHalo — ghosts + fisheye halo from threshold buffer
	// Input: half-res threshold (t0), Output: half-res ghost+halo (u0)
	// ============================================================
	[numthreads(8, 8, 1)] void CSGhostHalo(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= (uint)OutputWidth || DTid.y >= (uint)OutputHeight)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(OutputWidth, OutputHeight);
	float3 color = 0;
	float2 radiantVector = uv - 0.5f;

	// --- Ghosts ---
	[branch] if (GhostStrength > EPSILON)
	{
		// Chromatic aberration on input for ghosts
		for (int i = 0; i < NUM_GHOSTS; i++) {
			float4 ghostColor = GhostColors[i];
			float ghostScale = GetGhostScale(i);

			if (abs(ghostColor.a * ghostScale) < 0.00001f)
				continue;

			float2 ghostVector = radiantVector * ghostScale;

			// Local mask
			float distanceMask = 1.0f - length(ghostVector);
			float weight;
			if (GLocalMask) {
				float mask1 = smoothstep(0.5f, 0.9f, distanceMask);
				float mask2 = smoothstep(0.75f, 1.0f, distanceMask) * 0.95f + 0.05f;
				weight = mask1 * mask2;
			} else {
				weight = distanceMask;
			}

			float4 s = SampleCA(InputTexture, BorderSampler, ghostVector + 0.5f, 8.0f * GhostChromaShift, 0);
			color += s.rgb * ghostColor.rgb * ghostColor.a * weight;
		}

		// Screen border mask
		float2 screenPos = uv * 2.0f - 1.0f;
		float screenBorderMask = DiscMask(screenPos * 0.9f);
		color *= screenBorderMask * GhostStrength;
	}

	// --- Halo with fisheye distortion ---
	if (HaloStrength > EPSILON) {
		float2 fishUV = FisheyeUV(uv, HaloCompression, 1.0f);
		float2 haloVector = normalize(0.5f - uv) * HaloWidth;

		// Halo mask
		float haloMask = distance(uv, 0.5f);
		haloMask = saturate(haloMask * 2.0f);
		haloMask = smoothstep(HaloRadius, 1.0f, haloMask);

		// Screen border mask
		float2 screenPos = uv * 2.0f - 1.0f;
		float screenBorderMask = DiscMask(screenPos) * DiscMask(screenPos * 0.8f);
		screenBorderMask = screenBorderMask * 0.95f + 0.05f;

		// Chromatic aberration sampling on fisheye-distorted UVs
		float2 uvR = (fishUV - 0.5f) * (1.0f + HaloChromaShift) + 0.5f + haloVector;
		float2 uvG = fishUV + haloVector;
		float2 uvB = (fishUV - 0.5f) * (1.0f - HaloChromaShift) + 0.5f + haloVector;

		float3 haloColor;
		haloColor.r = InputTexture.SampleLevel(BorderSampler, uvR, 0).r;
		haloColor.g = InputTexture.SampleLevel(BorderSampler, uvG, 0).g;
		haloColor.b = InputTexture.SampleLevel(BorderSampler, uvB, 0).b;

		color += haloColor * screenBorderMask * haloMask * HaloStrength;
	}

	OutputTexture[DTid.xy] = float4(color, 1.0f);
}

// ============================================================
// Pass 3a: Kawase blur downsample — half res → quarter res
// Proper UV-based sampling between actual different-resolution textures
// ============================================================
[numthreads(8, 8, 1)] void CSFlareDown(uint3 DTid : SV_DispatchThreadID) {
	if (DTid.x >= (uint)OutputWidth || DTid.y >= (uint)OutputHeight)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(OutputWidth, OutputHeight);
	float2 halfPixel = 0.5f / float2(InputWidth, InputHeight);

	// 5-tap Kawase downsample: center (weight 4) + 4 diagonals
	float4 color = InputTexture.SampleLevel(ColorSampler, uv, 0) * 4.0f;
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(-halfPixel.x, halfPixel.y), 0);
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(halfPixel.x, halfPixel.y), 0);
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(halfPixel.x, -halfPixel.y), 0);
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(-halfPixel.x, -halfPixel.y), 0);

	OutputTexture[DTid.xy] = color * 0.125f;
}

	// ============================================================
	// Pass 3b: Kawase blur upsample — quarter res → half res
	// ============================================================
	[numthreads(8, 8, 1)] void CSFlareUp(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= (uint)OutputWidth || DTid.y >= (uint)OutputHeight)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(OutputWidth, OutputHeight);
	float2 halfPixel = 0.5f / float2(InputWidth, InputHeight);

	// 12-tap Kawase upsample: 4 diagonals (weight 1) + 4 axis (weight 2)
	float4 color = 0;
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(-halfPixel.x, halfPixel.y), 0);
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(halfPixel.x, halfPixel.y), 0);
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(halfPixel.x, -halfPixel.y), 0);
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(-halfPixel.x, -halfPixel.y), 0);

	color += InputTexture.SampleLevel(ColorSampler, uv + float2(-halfPixel.x, 0), 0) * 2.0f;
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(halfPixel.x, 0), 0) * 2.0f;
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(0, halfPixel.y), 0) * 2.0f;
	color += InputTexture.SampleLevel(ColorSampler, uv + float2(0, -halfPixel.y), 0) * 2.0f;

	OutputTexture[DTid.xy] = color / 12.0f;
}

// ============================================================
// ============================================================
// Pass 4: CSMix — combine ghost+halo, apply tint & gradient
// Input: ghost+halo (t0), Output: full-res final flare (u0)
// Uses InputWidth/InputHeight for sampling half-res inputs
// Bicubic Catmull-Rom upsampling for sharper ghost reproduction
// ============================================================

float3 SampleBicubicCatmullRom(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 texSize)
{
	float2 samplePos = uv * texSize;
	float2 tc = floor(samplePos - 0.5) + 0.5;
	float2 f = samplePos - tc;
	float2 f2 = f * f;
	float2 f3 = f2 * f;

	// Catmull-Rom weights
	float2 w0 = f2 - 0.5 * (f3 + f);
	float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
	float2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
	float2 w3 = 0.5 * (f3 - f2);

	// Collapse to 4 bilinear taps
	float2 w12 = w1 + w2;
	float2 tc0 = (tc - 1.0) / texSize;
	float2 tc12 = (tc + w2 / w12) / texSize;
	float2 tc3 = (tc + 2.0) / texSize;

	float3 result =
		tex.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y) +
		tex.SampleLevel(samp, float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y) +
		tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rgb * (w12.x * w12.y) +
		tex.SampleLevel(samp, float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y) +
		tex.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);

	// The 4 corner taps have very small weights, skip for performance
	return max(result, 0.0);
}

[numthreads(8, 8, 1)] void CSMix(uint3 DTid : SV_DispatchThreadID) {
	if (DTid.x >= (uint)OutputWidth || DTid.y >= (uint)OutputHeight)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(OutputWidth, OutputHeight);

	float3 flares = SampleBicubicCatmullRom(InputTexture, ColorSampler, uv, float2(InputWidth, InputHeight));

	// Procedural radial gradient based on distance from center
	float gradientT = saturate(distance(uv, 0.5f) * 2.0f);
	float3 gradient = lerp(1.0f, Tint, gradientT);

	float3 result = flares * gradient * Intensity;

	OutputTexture[DTid.xy] = float4(result, 1.0f);
}
