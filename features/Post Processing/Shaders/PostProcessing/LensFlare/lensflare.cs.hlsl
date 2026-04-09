// Lens Flare compute shader pipeline
// Ghost/Halo core based on PotatoFX by Gimle Larpes (MIT License, see below).
// Threshold, glare, fisheye halo, mix passes inspired by Froyok's UE4 lens flare article.
// Adapted for Community Shaders by Jiaye.
/*
MIT License - applies to SampleCA, ghost loop, and halo sampling patterns derived from PotatoFX

Copyright (c) 2023 Gimle Larpes

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "PostProcessing/common.hlsli"

static const float PI = 3.14159265359;
static const float EPSILON = 1e-6;
static const int NUM_GHOSTS = 8;

// Resources
Texture2D<float4> InputTexture : register(t0);
Texture2D<float4> FlareTexture : register(t1);
SamplerState ColorSampler : register(s0);
SamplerState BorderSampler : register(s1);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer LensFlareConstants : register(b1)
{
	// Threshold
	float ThresholdLevel;
	float ThresholdRange;
	float ScreenWidth;
	float ScreenHeight;

	// Ghost
	float GhostStrength;
	float GhostChromaShift;
	float HaloStrength;
	float HaloRadius;

	float HaloWidth;
	float HaloCompression;
	float HaloChromaShift;
	float Intensity;

	// Glare
	float GlareIntensity;
	float GlareDivider;
	float2 GlareDirection;

	float3 GlareScale_packed;
	int DownsizeScale;

	float3 Tint;
	int GLocalMask;

	float4 GhostColors[NUM_GHOSTS];
	float GhostScales[NUM_GHOSTS];
	// padding to 16-byte boundary handled by CB struct
}

// ============================================================
// Utilities
// ============================================================

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
// Input: scene color (t0), Output: thresholded buffer (u0)
// ============================================================
[numthreads(8, 8, 1)] void CSThreshold(uint3 DTid : SV_DispatchThreadID) {
	if (DTid.x >= (uint)ScreenWidth || DTid.y >= (uint)ScreenHeight)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(ScreenWidth, ScreenHeight);
	float2 pixelSize = 1.0f / float2(ScreenWidth, ScreenHeight);

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
	// Input: threshold buffer (t0), Output: ghost+halo buffer (u0)
	// ============================================================
	[numthreads(8, 8, 1)] void CSGhostHalo(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= (uint)ScreenWidth || DTid.y >= (uint)ScreenHeight)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(ScreenWidth, ScreenHeight);
	float3 color = 0;
	float2 radiantVector = uv - 0.5f;

	// --- Ghosts (MIT licensed section from PotatoFX) ---
	[branch] if (GhostStrength > EPSILON)
	{
		// Chromatic aberration on input for ghosts
		for (int i = 0; i < NUM_GHOSTS; i++) {
			float4 ghostColor = GhostColors[i];
			float ghostScale = GhostScales[i];

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
// Pass 3a/3b: Kawase blur down/up — smooth ghost+halo artifacts
// ============================================================
[numthreads(8, 8, 1)] void CSFlareDown(uint3 DTid : SV_DispatchThreadID) {
	OutputTexture[DTid.xy] = KawaseBlurDownSample(FlareTexture, ColorSampler, DTid.xy, DownsizeScale, ScreenWidth, ScreenHeight);
}

	[numthreads(8, 8, 1)] void CSFlareUp(uint3 DTid : SV_DispatchThreadID)
{
	OutputTexture[DTid.xy] = KawaseBlurUpSample(FlareTexture, ColorSampler, DTid.xy, DownsizeScale, ScreenWidth, ScreenHeight);
}

// ============================================================
// Pass 4: CSGlareStreak — directional blur streak
// Dispatched 3x at different angles (0, 60, 120 deg) for 6-point star
// Input: threshold buffer (t0), Output: glare buffer (u0, additive)
// GlareDirection is set per-dispatch from CPU
// ============================================================
[numthreads(8, 8, 1)] void CSGlareStreak(uint3 DTid : SV_DispatchThreadID) {
	if (DTid.x >= (uint)ScreenWidth || DTid.y >= (uint)ScreenHeight || GlareIntensity < EPSILON)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(ScreenWidth, ScreenHeight);
	float2 pixelSize = 1.0f / float2(ScreenWidth, ScreenHeight);

	// Directional streak: sample along direction with exponential falloff
	static const int NUM_SAMPLES = 8;
	static const float ATTENUATION = 0.95f;

	float3 color = InputTexture.SampleLevel(ColorSampler, uv, 0).rgb;
	float falloff = 1.0f;
	float totalWeight = 1.0f;

	float2 stepDir = GlareDirection * pixelSize * 2.0f;

	[unroll] for (int i = 1; i <= NUM_SAMPLES; i++)
	{
		falloff *= ATTENUATION;

		float2 offset = stepDir * (float)i;

		float3 s1 = InputTexture.SampleLevel(ColorSampler, uv + offset, 0).rgb;
		float3 s2 = InputTexture.SampleLevel(ColorSampler, uv - offset, 0).rgb;

		color += (s1 + s2) * falloff;
		totalWeight += 2.0f * falloff;
	}

	color /= totalWeight;

	// Scale by luminance-based intensity
	float luminance = dot(color, float3(0.333f, 0.333f, 0.333f));
	float luminanceScale = saturate(luminance / max(GlareDivider, 0.01f));

	// Screen-space fade at edges
	float edgeMask = 1.0f - saturate(distance(uv, 0.5f) * 2.0f);
	edgeMask = edgeMask * 0.6f + 0.4f;

	color *= luminanceScale * edgeMask * GlareIntensity;

	// Read existing glare and add (additive accumulation across 3 dispatches)
	float3 existing = OutputTexture[DTid.xy].rgb;
	OutputTexture[DTid.xy] = float4(existing + color, 1.0f);
}

	// ============================================================
	// Pass 5: CSMix — combine ghost+halo + glare, apply tint & gradient
	// Input: ghost+halo (t0), glare (t1), Output: final flare (u0)
	// ============================================================
	[numthreads(8, 8, 1)] void CSMix(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= (uint)ScreenWidth || DTid.y >= (uint)ScreenHeight)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(ScreenWidth, ScreenHeight);
	float2 pixelSize = 1.0f / float2(ScreenWidth, ScreenHeight);

	float3 flares = InputTexture.SampleLevel(ColorSampler, uv, 0).rgb;

	// Sample glare with 4-tap box filter to smooth artifacts
	float3 glare = 0;
	glare += FlareTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(-1.0f, 1.0f), 0).rgb;
	glare += FlareTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(1.0f, 1.0f), 0).rgb;
	glare += FlareTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(-1.0f, -1.0f), 0).rgb;
	glare += FlareTexture.SampleLevel(ColorSampler, uv + pixelSize * float2(1.0f, -1.0f), 0).rgb;
	glare *= 0.25f;

	flares += glare;

	// Procedural radial gradient based on distance from center
	float gradientT = saturate(distance(uv, 0.5f) * 2.0f);
	float3 gradient = lerp(1.0f, Tint, gradientT);

	float3 result = flares * gradient * Intensity;

	OutputTexture[DTid.xy] = float4(result, 1.0f);
}
