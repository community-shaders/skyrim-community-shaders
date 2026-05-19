/**
 * Motion Blur - Neighbor Max Pass (Pass 2 of 3)
 *
 * Examines 3x3 grid neighborhoods to find maximum velocities
 * Handles diagonal neighbors by checking if velocity points toward current cell
 * Outputs final neighborhood-aware velocity data for blur pass
 */

#include "PostProcessing/common.hlsli"

// Textures
Texture2D<float4> TexVertical : register(t0);         // [20×20] input from vertical pass
RWTexture2D<float4> RWTexNeighborMax : register(u0);  // [20×20] output

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

// Process one cell in the grid
[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	// Check bounds
	if (DTid.x >= GRID_SIZE || DTid.y >= GRID_SIZE)
		return;

	uint2 cellIndex = DTid.xy;

	// Track maximum velocity
	float2 maxVelocity = float2(0.0f, 0.0f);
	float maxVelocityMagnitude = 0.0f;

	// Check 3x3 neighborhood
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			// Get neighbor coordinates
			int2 neighborCellIndex = int2(cellIndex) + int2(x, y);

			// Skip out-of-bounds neighbors
			if (neighborCellIndex.x < 0 || neighborCellIndex.y < 0 ||
				neighborCellIndex.x >= GRID_SIZE ||
				neighborCellIndex.y >= GRID_SIZE)
				continue;

			// Get neighbor velocity
			float4 neighborColor = TexVertical[neighborCellIndex];
			float2 neighborVelocity = ExtractVelocity(neighborColor);

			// For diagonal neighbors, only include if velocity points toward current cell
			bool isDiagonal = (x != 0 && y != 0);
			if (isDiagonal) {
				// Direction from neighbor to current cell
				float2 dirToCurrentCell = normalize(float2(-x, -y));

				// Neighbor velocity direction
				float2 neighborDir = normalize(neighborVelocity);

				// Skip if velocity doesn't point toward current cell
				if (dot(neighborDir, dirToCurrentCell) <= 0.0f)
					continue;
			}

			// Keep largest velocity
			float neighborVelocityMagnitude = length(neighborVelocity);
			if (neighborVelocityMagnitude > maxVelocityMagnitude) {
				maxVelocityMagnitude = neighborVelocityMagnitude;
				maxVelocity = neighborVelocity;
			}
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
	RWTexNeighborMax[DTid.xy] = outputColor;
}