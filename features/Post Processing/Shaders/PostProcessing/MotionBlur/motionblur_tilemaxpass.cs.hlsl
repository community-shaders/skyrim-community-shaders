/**
 * Motion Blur - Tile Max Pass (Pass 1 of 3)
 * 
 * First pass of the motion blur implementation that operates on 8x8 thread groups:
 * - Calculates maximum velocity for each NxN tile
 * - Outputs a downsampled texture with one pixel per tile
 * - Encodes velocity direction and magnitude in RGB channels
 */

#include "PostProcessing/common.hlsli"

// Input texture resources
Texture2D<float4> TexVelocity : register(t0);  // Motion vector buffer 
RWTexture2D<float4> RWTexTileMax : register(u0); // Output texture for tile max velocities (downsampled)

// Constants shared with C++ code
cbuffer MotionBlurCB : register(b0)
{
    uint g_TileSize;         // Size of each tile (typically 16 or 32)
    float g_VelocityScale;   // Scale factor for velocity vectors
    float2 g_Padding;        // Padding for 16-byte alignment
}

// First pass: calculate max velocity in each tile
// Using fixed 8x8 thread groups for 8x8 tiles (typical case)
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
    // Get texture dimensions
    uint2 dimensions;
    TexVelocity.GetDimensions(dimensions.x, dimensions.y);
    
    // Calculate the tile bounds
    uint2 tileStart = Gid.xy * g_TileSize;
    uint2 tileEnd = min(tileStart + g_TileSize, dimensions);
    
    // Initialize max velocity magnitude
    float maxVelocityMagnitude = 0.0f;
    float2 maxVelocity = float2(0.0f, 0.0f);
    
    // Scan entire tile to find max velocity
    for (uint y = tileStart.y; y < tileEnd.y; y++) {
        for (uint x = tileStart.x; x < tileEnd.x; x++) {
            // Read velocity from g-buffer
            float2 velocity = TexVelocity[uint2(x, y)].xy;
            
            // We want to keep the original direction from the g-buffer, but scale it
            // For consistency: When camera moves right, objects appear to move left
            velocity *= g_VelocityScale; // No sign flip here - use original sign
            
            // Compare magnitude and keep the largest
            float velocityMagnitude = length(velocity);
            if (velocityMagnitude > maxVelocityMagnitude) {
                maxVelocityMagnitude = velocityMagnitude;
                maxVelocity = velocity;
            }
        }
    }
    
    // Only a single thread per group should write the tile's result
    // All other threads in the group will exit
    if (GTid.x > 0 || GTid.y > 0)
        return;
    
    // Store velocity information
    // R: normalized x direction (-1..1 mapped to 0..1)
    // G: normalized y direction (-1..1 mapped to 0..1)
    // B: velocity magnitude encoded
    float2 normalizedDir = maxVelocityMagnitude > 0.001f ? normalize(maxVelocity) : float2(0.0f, 0.0f);
    float encodedMagnitude = saturate(maxVelocityMagnitude * 0.05f); // Scale down to fit in 0..1 range
    
    // Create output color with velocity data
    // Store direction in R,G channels and magnitude in B channel
    float4 outputColor = float4(
        normalizedDir.x * 0.5f + 0.5f,  // Map -1..1 to 0..1
        normalizedDir.y * 0.5f + 0.5f,  // Map -1..1 to 0..1
        encodedMagnitude,
        1.0f
    );
    
    // Write to output texture at tile coordinates (one pixel per tile)
    RWTexTileMax[Gid.xy] = outputColor;
} 