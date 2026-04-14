#include "Common/Random.hlsli"
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
	float GlareIntensity;
	float GlareDivider;

	// Glare
	float2 GlareDirection;
	uint FFTResolution;
	float _pad0;

	float3 GlareScale_packed;
	int GLocalMask;

	float3 Tint;
	float _pad1;

	// Ghost data
	float4 GhostColors[NUM_GHOSTS];
	float4 GhostScalesPacked[2];  // 8 scales packed into 2 float4s
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

	// --- Ghosts (MIT licensed section from PotatoFX) ---
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
// Pass 4: CSGlareStreak — directional blur streak
// Dispatched 3x at different angles (0, 60, 120 deg) for 6-point star
// Input: threshold buffer (t0), Output: glare buffer (u0, additive)
// GlareDirection is set per-dispatch from CPU
// ============================================================
[numthreads(8, 8, 1)] void CSGlareStreak(uint3 DTid : SV_DispatchThreadID) {
	if (DTid.x >= (uint)OutputWidth || DTid.y >= (uint)OutputHeight || GlareIntensity < EPSILON)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(OutputWidth, OutputHeight);
	float2 pixelSize = 1.0f / float2(OutputWidth, OutputHeight);

	// Smooth 2D value noise — breaks up uniform streaks without looking screen-fixed.
	// Project pixel into streak-local coordinate system (perpendicular & parallel to streak),
	// then use smooth bilinear-interpolated hash noise. Center brightness offsets the coordinate
	// so the pattern drifts naturally as the camera pans across bright sources.
	float2 toCenter = uv - 0.5f;
	float2 streakDir = normalize(GlareDirection);
	float2 streakPerp = float2(-streakDir.y, streakDir.x);

	// Sample center brightness as a camera-dependent offset
	float centerLum = dot(InputTexture.SampleLevel(ColorSampler, float2(0.5f, 0.5f), 0).rgb, float3(0.299f, 0.587f, 0.114f));
	float driftOffset = centerLum * 50.0f;

	// Streak-space coordinates: perpendicular (tight period → distinct rays), parallel (loose period → subtle length variation)
	float perpCoord = dot(toCenter, streakPerp) * OutputWidth / 12.0f + driftOffset;
	float paraCoord = dot(toCenter, streakDir) * OutputWidth / 80.0f + driftOffset * 0.3f;

	// Bilinear value noise — smooth blobs instead of sharp bands
	int2 cell = int2(floor(perpCoord), floor(paraCoord));
	float2 f = float2(perpCoord, paraCoord) - float2(cell);
	f = f * f * (3.0f - 2.0f * f);  // smoothstep

	float n00 = (float)(Random::iqint3(uint2((uint)abs(cell.x), (uint)abs(cell.y))) & 0xFFFFu) / 65535.0f;
	float n10 = (float)(Random::iqint3(uint2((uint)abs(cell.x + 1), (uint)abs(cell.y))) & 0xFFFFu) / 65535.0f;
	float n01 = (float)(Random::iqint3(uint2((uint)abs(cell.x), (uint)abs(cell.y + 1))) & 0xFFFFu) / 65535.0f;
	float n11 = (float)(Random::iqint3(uint2((uint)abs(cell.x + 1), (uint)abs(cell.y + 1))) & 0xFFFFu) / 65535.0f;

	float noiseVal = lerp(lerp(n00, n10, f.x), lerp(n01, n11, f.x), f.y);
	// 0.6 base + 0.4 noise → visible but not harsh brightness variation
	float noiseModulation = 0.6f + 0.4f * noiseVal;

	// Directional streak: sample along direction with exponential falloff
	static const int NUM_SAMPLES = 16;
	static const float ATTENUATION = 0.92f;

	float3 color = InputTexture.SampleLevel(ColorSampler, uv, 0).rgb;
	float falloff = 1.0f;
	float totalWeight = 1.0f;

	float2 stepDir = GlareDirection * pixelSize * 4.0f;

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

	color *= luminanceScale * edgeMask * GlareIntensity * noiseModulation;

	// Read existing glare and add (additive accumulation across 3 dispatches)
	float3 existing = OutputTexture[DTid.xy].rgb;
	OutputTexture[DTid.xy] = float4(existing + color, 1.0f);
}

	// ============================================================
	// Pass 5: CSMix — combine ghost+halo + glare, apply tint & gradient
	// Input: ghost+halo (t0), glare (t1), Output: full-res final flare (u0)
	// Uses InputWidth/InputHeight for sampling half-res inputs
	// ============================================================
	[numthreads(8, 8, 1)] void CSMix(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= (uint)OutputWidth || DTid.y >= (uint)OutputHeight)
		return;

	float2 uv = (DTid.xy + 0.5f) / float2(OutputWidth, OutputHeight);
	float2 pixelSize = 1.0f / float2(InputWidth, InputHeight);

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
