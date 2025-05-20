/**
 * Motion Blur - Vertical Reduction Pass (Pass 1b of 3)
 * 
 * Performs vertical reduction from [grid × height] to [grid × grid]
 * Each column of threads processes a vertical strip of the input texture
 * Preserves the maximum velocity vectors for each grid cell
 */

#include "PostProcessing/common.hlsli"

// Textures
Texture2D<float4> TexHorizontal : register(t0);    // [20 x height] input
RWTexture2D<float4> RWTexVertical : register(u0);  // [20 x 20] output

// Constants
cbuffer MotionBlurCB : register(b0)
{
	float g_VelocityScale;  // Velocity multiplier
}

// Fixed parameters
static const uint GRID_SIZE = 20;  // Fixed grid size

// Extract velocity from encoded color
float2 ExtractVelocity(float4 colorSample)
{
	// Extract direction from R,G channels
	float2 dir;
	dir.x = (colorSample.r * 2.0f) - 1.0f;
	dir.y = (colorSample.g * 2.0f) - 1.0f;

	// Normalize if needed
	float dirLength = length(dir);
	if (dirLength > 0.001f)
		dir /= dirLength;

	// Get magnitude from B channel
	float magnitude = colorSample.b * 20.0f;

	return dir * magnitude;
}

// Vertical reduction pass
[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID, uint3 GTid
								: SV_GroupThreadID, uint3 Gid
								: SV_GroupID) {
	// Get dimensions and check bounds
	uint2 horizontalDimensions;
	TexHorizontal.GetDimensions(horizontalDimensions.x, horizontalDimensions.y);

	if (DTid.x >= GRID_SIZE || DTid.y >= GRID_SIZE)
		return;

	// Calculate vertical range to process
	uint yTileSize = (horizontalDimensions.y + GRID_SIZE - 1) / GRID_SIZE;
	uint yStart = DTid.y * yTileSize;
	uint yEnd = min(yStart + yTileSize, horizontalDimensions.y);

	// Track maximum velocity
	float maxVelocityMagnitude = 0.0f;
	float2 maxVelocity = float2(0.0f, 0.0f);

	// Process vertical strip
	for (uint y = yStart; y < yEnd; y++) {
		// Sample horizontal pass texture
		float4 sampleColor = TexHorizontal[uint2(DTid.x, y)];

		// Extract velocity
		float2 velocity = ExtractVelocity(sampleColor);

		// Keep largest velocity
		float velocityMagnitude = length(velocity);
		if (velocityMagnitude > maxVelocityMagnitude) {
			maxVelocityMagnitude = velocityMagnitude;
			maxVelocity = velocity;
		}
	}

	// Encode velocity for output
	// R: normalized x direction mapped to 0..1
	// G: normalized y direction mapped to 0..1
	// B: velocity magnitude scaled to 0..1
	float2 normalizedDir = maxVelocityMagnitude > 0.001f ? normalize(maxVelocity) : float2(0.0f, 0.0f);
	float encodedMagnitude = saturate(maxVelocityMagnitude * 0.05f);

	// Create output color
	float4 outputColor = float4(
		normalizedDir.x * 0.5f + 0.5f,
		normalizedDir.y * 0.5f + 0.5f,
		encodedMagnitude,
		1.0f);

	// Write to grid coordinates
	RWTexVertical[DTid.xy] = outputColor;
}