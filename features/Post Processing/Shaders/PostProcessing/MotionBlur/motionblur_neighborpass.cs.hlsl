/**
 * Motion Blur - Neighbor Max Pass (Pass 2 of 3)
 * 
 * Second pass of the motion blur implementation:
 * - Examines the 3x3 neighborhood of tiles around each tile
 * - Finds the maximum velocity affecting each tile
 * - Handles diagonal neighbors specially (only considered if velocity points toward current tile)
 * - Outputs a texture with the neighborhood max velocity for each tile
 */

#include "PostProcessing/common.hlsli"

// Input texture resources
Texture2D<float4> TexTileMax : register(t0);  // Input tile max velocities (downsampled)
RWTexture2D<float4> RWTexNeighborMax : register(u0); // Output texture for neighbor max velocities (same size as TileMax)

// Constants shared with C++ code
cbuffer MotionBlurCB : register(b0)
{
    uint g_TileSize;         // Size of each tile (typically 16 or 32)
    float g_VelocityScale;   // Scale factor for velocity vectors
    float2 g_Padding;        // Padding for 16-byte alignment
}

// Extract velocity vector from a color sample
float2 ExtractVelocity(float4 colorSample)
{
    // Extract direction (convert back from 0..1 to -1..1)
    float2 dir;
    dir.x = (colorSample.r * 2.0f) - 1.0f;
    dir.y = (colorSample.g * 2.0f) - 1.0f;
    
    // Normalize if needed
    float dirLength = length(dir);
    if (dirLength > 0.001f)
        dir /= dirLength;
    
    // Estimate magnitude from blue channel - maintain consistent sign with other passes
    float magnitude = colorSample.b * 20.0f; // Reverse the 0.05f scaling
    
    return dir * magnitude;
}

// Second pass: calculate max velocity from neighboring tiles
// Each thread processes one tile
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Get texture dimensions of the tile max texture (already downsampled)
    uint2 dimensions;
    TexTileMax.GetDimensions(dimensions.x, dimensions.y);
    
    // Early out if outside texture bounds
    if (DTid.x >= dimensions.x || DTid.y >= dimensions.y)
        return;
        
    // Current tile position is directly DTid.xy since we're working on downsampled texture
    uint2 tileIndex = DTid.xy;
    
    // Initialize max velocity
    float2 maxVelocity = float2(0.0f, 0.0f);
    float maxVelocityMagnitude = 0.0f;
    
    // Examine the 3x3 neighborhood of tiles
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            // Calculate neighboring tile index
            int2 neighborTileIndex = int2(tileIndex) + int2(x, y);
            
            // Skip if out of bounds
            if (neighborTileIndex.x < 0 || neighborTileIndex.y < 0 || 
                neighborTileIndex.x >= dimensions.x || 
                neighborTileIndex.y >= dimensions.y)
                continue;
            
            // Read the velocity directly from the neighboring tile (one pixel per tile)
            float4 neighborTileColor = TexTileMax[neighborTileIndex];
            float2 neighborVelocity = ExtractVelocity(neighborTileColor);
            
            // As per the paper, for diagonal neighbors (x != 0 && y != 0), only include them
            // if their velocity points toward the current tile
            bool isDiagonal = (x != 0 && y != 0);
            if (isDiagonal) {
                // Compute the direction from neighbor to current tile
                float2 dirToCurrentTile = normalize(float2(-x, -y));
                
                // Compute the normalized neighbor velocity direction
                float2 neighborDir = normalize(neighborVelocity);
                
                // Dot product to determine if velocity points toward current tile
                // Only include if the dot product is positive (velocity points toward current)
                if (dot(neighborDir, dirToCurrentTile) <= 0.0f)
                    continue;
            }
            
            // Check if this neighbor's velocity is larger
            float neighborVelocityMagnitude = length(neighborVelocity);
            if (neighborVelocityMagnitude > maxVelocityMagnitude) {
                maxVelocityMagnitude = neighborVelocityMagnitude;
                maxVelocity = neighborVelocity;
            }
        }
    }
    
    // Store velocity information in the same format as the first pass
    // R: normalized x direction (-1..1 mapped to 0..1)
    // G: normalized y direction (-1..1 mapped to 0..1)
    // B: velocity magnitude encoded
    float2 normalizedDir = maxVelocityMagnitude > 0.001f ? normalize(maxVelocity) : float2(0.0f, 0.0f);
    float encodedMagnitude = saturate(maxVelocityMagnitude * 0.05f); // Scale down to fit in 0..1 range
    
    // Create output color with velocity data
    float4 outputColor = float4(
        normalizedDir.x * 0.5f + 0.5f,  // Map -1..1 to 0..1
        normalizedDir.y * 0.5f + 0.5f,  // Map -1..1 to 0..1
        encodedMagnitude,
        1.0f
    );
    
    // Write to downsampled output texture
    RWTexNeighborMax[DTid.xy] = outputColor;
} 