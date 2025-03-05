/**centerVelocity = originalVelocity + (cameraMotionVector * g_CameraMotio
 * Motion Blur - Blur Pass (Pass 3 of 3)
 * 
 * Final pass of the motion blur implementation:
 * - Uses the neighborhood max velocities from previous passes
 * - Applies blur based on velocity directions and magnitudes
 * - Provides camera motion reduction to separate object motion from camera movement
 * - Includes multiple visualization modes for debugging
 * - Applies depth-aware edge handling to prevent foreground/background bleeding
 */

#include "PostProcessing/common.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/MotionBlur.hlsli"

// Input texture resources
Texture2D<float4> TexColor : register(t0);     // Input color buffer
Texture2D<float4> TexVelocity : register(t1);  // Motion vector buffer
Texture2D<float4> TexNeighborMax : register(t2); // Neighbor max velocity texture (downsampled)
Texture2D<float> TexDepth : register(t3);      // Depth buffer for edge detection
RWTexture2D<float4> RWTexOut : register(u0);  // Output texture

// Samplers
SamplerState LinearSampler : register(s0);  // Linear for smooth color sampling
SamplerState PointSampler : register(s1);   // Point for depth and velocity sampling

// Constants shared with C++ code
cbuffer MotionBlurCB : register(b0)
{
    uint g_TileSize;         // Size of each tile (typically 16 or 32)
    float g_VelocityScale;   // Scale factor for velocity vectors
    float g_BlurScale;       // Overall blur strength
    int g_SampleCount;       // Number of samples to take
    int g_VisualizationMode; // Visualization mode for debugging
    float g_CameraMotionReduction; // 0=full camera motion, 1=only object motion
    float g_PaddingX;        // Padding for 16-byte alignment
    float g_PaddingY;        // Padding for 16-byte alignment
}

// Fixed values (formerly in the constant buffer)
static const float g_MaxBlurRadius = 40.0f;      // Maximum blur distance in pixels
static const float g_DepthBiasFactor = 1.5f;     // How strictly to enforce depth boundaries
static const int g_UseDepthBounds = 1;           // Whether to use depth boundaries (always enabled)

// Constants for the shader
#define PI 3.14159265359f
#define MB_SOFTZ_INCHES 5.0f    // Depth threshold for foreground/background classification
#define MB_SINGLE_PIXEL_RADIUS 0.5f
#define MB_MIRROR_FILTER 1      // Enable mirrored background reconstruction

// Calculate camera motion vector for the current pixel
float2 CalculateCameraMotionVector(float2 uv, float depth)
{
    // Convert to clip space position
    float4 positionCS = float4(2.0f * float2(uv.x, -uv.y + 1.0f) - 1.0f, depth, 1.0f);
    
    // Convert to world space using current frame's inverse view-projection
    uint eyeIndex = 0;
    float4 positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionCS);
    
    // Protect against division by zero
    float w = max(positionWS.w, 0.0001);
    positionWS.xyz /= w;
    
    // For camera motion, we want to see how this same world position moves on screen between frames
    float4 currentPositionSS = mul(FrameBuffer::CameraViewProjUnjittered[eyeIndex], float4(positionWS.xyz, 1.0f));
    float4 previousPositionSS = mul(FrameBuffer::CameraPreviousViewProjUnjittered[eyeIndex], float4(positionWS.xyz, 1.0f));
    
    // Protect against division by zero
    float w1 = max(currentPositionSS.w, 0.0001);
    float w2 = max(previousPositionSS.w, 0.0001);
    
    // Safely convert to NDC space
    float2 currentPos = currentPositionSS.xy / w1;
    float2 previousPos = previousPositionSS.xy / w2;
    
    // Calculate motion vector with bounds checking
    float2 motionVector = currentPos - previousPos;
    
    // Clamp to reasonable values to prevent extreme vectors
    motionVector = clamp(motionVector, float2(-100, -100), float2(100, 100));
    
    // Apply Skyrim's scale factor to match engine's motion vectors
    return float2(-0.5, 0.5) * motionVector;
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
    
    // Estimate magnitude from blue channel and apply blur scale
    float magnitude = colorSample.b * 20.0f * g_BlurScale; // Scaling back from encoding in previous passes
    
    return dir * magnitude;
}

// Depth comparison function returning two weights (foreground and background) that sum to 1
float2 DepthCmp(float centerDepth, float sampleDepth, float depthScale)
{
    return saturate(0.5f + float2(depthScale, -depthScale) * (sampleDepth - centerDepth));
}

// Spread comparison function to determine if a sample contributes
float2 SpreadCmp(float offsetLen, float2 spreadLen, float pixelToSampleUnitsScale)
{
    return saturate(pixelToSampleUnitsScale * spreadLen - max(offsetLen - 1.0f, 0.0f));
}

// Toe function to smooth the contribution near the center
float SpreadToe(float offsetLen, float spreadCmp)
{
    return offsetLen <= 1.0f ? pow(spreadCmp, 2.0f) : spreadCmp;
}

// Calculate the weight for a sample
float SampleWeight(
    float centerDepth, float sampleDepth, float offsetLen, float centerSpreadLen, 
    float sampleSpreadLen, float pixelToSampleUnitsScale, float depthScale)
{
    float2 depthCmp = DepthCmp(centerDepth, sampleDepth, depthScale);
    float2 spreadCmp = SpreadCmp(offsetLen, float2(centerSpreadLen, sampleSpreadLen), pixelToSampleUnitsScale);
    return dot(depthCmp, spreadCmp);
}

// Generate a dithered offset for sampling
float GetDitheredOffset(uint2 position, float sampleIndex)
{
    float scale = 0.25f;
    uint2 positionMod = position & 1; // Simple 2x2 checkerboard pattern
    return (-scale + 2.0f * scale * positionMod.x) * (-1.0f + 2.0f * positionMod.y);
}

// Map a motion vector to a color using a color wheel
float3 MotionVectorToColor(float2 velocity)
{
    // Get normalized direction
    float2 dir = normalize(velocity);
    if (length(velocity) < 0.001f)
        dir = float2(0.0f, 0.0f);
    
    // Get angle and create color wheel
    float angle = atan2(dir.y, dir.x);
    float3 hueColor = 0.5f + 0.5f * float3(
        cos(angle),
        cos(angle - 2.0f * PI / 3.0f),
        cos(angle - 4.0f * PI / 3.0f)
    );
    
    // Scale by length
    float normalizedLen = saturate(length(velocity) / g_MaxBlurRadius);
    return hueColor * normalizedLen;
}

// Main compute shader function
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Get texture dimensions
    uint2 dimensions;
    TexColor.GetDimensions(dimensions.x, dimensions.y);
    
    // Early out if outside texture bounds
    if (DTid.x >= dimensions.x || DTid.y >= dimensions.y)
        return;
    
    // Get pixel coordinates
    uint2 pixelPos = DTid.xy;
    
    // Sample center pixel color, depth, and velocity
    float2 texCoord = (pixelPos + 0.5f) / float2(dimensions);
    float4 centerColor = TexColor.SampleLevel(LinearSampler, texCoord, 0);
    float centerDepth = TexDepth.SampleLevel(PointSampler, texCoord, 0);
    float2 centerVelocity = TexVelocity.SampleLevel(PointSampler, texCoord, 0).xy;
    
    // Apply velocity scaling (consistent with first pass - no sign flip)
    centerVelocity *= g_VelocityScale;
    
    // Calculate camera motion vectors and apply camera motion reduction
    float2 cameraMotionVector = float2(0, 0);
    if (g_CameraMotionReduction > 0.002f)
    {
        // // Get camera motion for this pixel
        cameraMotionVector = CalculateCameraMotionVector(texCoord, centerDepth);
        
        // // Apply velocity scale to match the rest of our motion vectors
        // cameraMotionVector *= g_VelocityScale;
        
        // // Store original velocity for visualization
        // float2 originalVelocity = centerVelocity;
        
        // // Apply camera motion reduction - fully subtract camera motion when set to 1.0
        // // This isolates object motion by removing camera contribution
        // centerVelocity = originalVelocity - 0;

        if (cameraMotionVector.x == 0)
        {
            centerColor.rgb = float3(1.0, 0.0, 0.0);
            RWTexOut[pixelPos] = centerColor;
            return;
        }
        if (cameraMotionVector.x < 0)
        {
            centerColor.rgb = float3(0.0, 1.0, 0.0);
            RWTexOut[pixelPos] = centerColor;
            return;
        }
        if (cameraMotionVector.x > 0)
        {
            centerColor.rgb = float3(0.0, 0.0, 1.0);
            RWTexOut[pixelPos] = centerColor;
            return;
        }
        if (cameraMotionVector.y == 0)
        {
            centerColor.rgb = float3(0.0, 1.0, 0.0);
            RWTexOut[pixelPos] = centerColor;
            return;
        }
        if (cameraMotionVector.y < 0)
        {
            centerColor.rgb = float3(0.0, 0.0, 1.0);
            RWTexOut[pixelPos] = centerColor;
            return;
        }
        if (cameraMotionVector.y > 0)
        {
            centerColor.rgb = float3(0.0, 0.0, 1.0);
            RWTexOut[pixelPos] = centerColor;
            return;
        }
        if (isnan(cameraMotionVector.x) || isnan(cameraMotionVector.y) || 
            isinf(cameraMotionVector.x) || isinf(cameraMotionVector.y))
        {
            centerColor.rgb = float3(1.0, 0.0, 1.0); // Magenta for NaN/infinity
            RWTexOut[pixelPos] = centerColor;
        return;
}
        else {
            centerColor.rgb = float3(1.0, 1.0, 1.0);
            RWTexOut[pixelPos] = centerColor;
            return;
        }
    }
    
    // Calculate tile coordinates for this pixel (to sample from downsampled neighbor max texture)
    uint2 tileCoord = pixelPos / g_TileSize;
    
    // Get NeighborMax texture dimensions
    uint2 neighborMaxDimensions;
    TexNeighborMax.GetDimensions(neighborMaxDimensions.x, neighborMaxDimensions.y);
    
    // Ensure tile coordinates are within bounds
    tileCoord = min(tileCoord, neighborMaxDimensions - 1);
    
    // Sample the maximum velocity from the downsampled neighbor max texture
    float4 neighborMaxSample = TexNeighborMax[tileCoord];
    float2 neighborMaxVelocity = ExtractVelocity(neighborMaxSample);
    
    // Apply camera motion reduction to tile max velocity as well
    if (g_CameraMotionReduction > 0.001f)
    {
        // Store original velocity
        float2 originalNeighborVelocity = neighborMaxVelocity;
        
        // Simply apply the same reduction approach used for centerVelocity
        neighborMaxVelocity = originalNeighborVelocity - abs((cameraMotionVector * g_CameraMotionReduction))*0.5;
    }
    
    // Determine the blur direction and length
    float2 blurDir = length(neighborMaxVelocity) > 0.001f ? normalize(neighborMaxVelocity) : float2(0.0f, 0.0f);
    float blurLength = min(length(neighborMaxVelocity), g_MaxBlurRadius);
    
    // Skip the blur if no motion
    if (blurLength < 0.5f)
    {
        RWTexOut[pixelPos] = centerColor;
        return;
    }
    
    // Calculate the center velocity length
    float centerVelocityLen = length(centerVelocity);
    
    // Clamp the sample count to a reasonable range
    int sampleCount = min(g_SampleCount, 32);
    
    // Initialize accumulation values
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float pixelToSampleUnitsScale = float(sampleCount) / blurLength;
    
    // Sample in pairs (mirrored)
    for (int i = 0; i < sampleCount / 2; i++)
    {
        // Calculate base sample offset in pixels
        float offset = (float(i) + 0.5f) / float(sampleCount / 2) * blurLength;
        
        // Add dithered offset for better quality
        offset += GetDitheredOffset(pixelPos, i);
        
        // Calculate the pixel offsets for the sample pair (positive and negative)
        float2 pixelOffset1 = blurDir * offset;
        float2 pixelOffset2 = -pixelOffset1;
        
        // Convert pixel offsets to texture coordinates
        float2 sampleTexCoords1 = (pixelPos + pixelOffset1 + 0.5f) / float2(dimensions);
        float2 sampleTexCoords2 = (pixelPos + pixelOffset2 + 0.5f) / float2(dimensions);
        
        // Sample depth and velocity for both offsets
        float sampleDepth1 = TexDepth.SampleLevel(PointSampler, sampleTexCoords1, 0);
        float sampleDepth2 = TexDepth.SampleLevel(PointSampler, sampleTexCoords2, 0);
        
        float4 rawVelocityDepth1 = TexVelocity.SampleLevel(PointSampler, sampleTexCoords1, 0);
        float4 rawVelocityDepth2 = TexVelocity.SampleLevel(PointSampler, sampleTexCoords2, 0);
        
        // Get velocity vectors for both samples (consistent with first pass - no sign flip)
        float2 sampleVelocity1 = rawVelocityDepth1.xy * g_VelocityScale;
        float2 sampleVelocity2 = rawVelocityDepth2.xy * g_VelocityScale;
        
        // Apply camera motion reduction to sample velocities
        if (g_CameraMotionReduction > 0.001f)
        {
            // Calculate camera motion for these positions - use the improved function
            float2 cameraMotion1 = CalculateCameraMotionVector(sampleTexCoords1, sampleDepth1) * g_VelocityScale;
            float2 cameraMotion2 = CalculateCameraMotionVector(sampleTexCoords2, sampleDepth2) * g_VelocityScale;
            
            // Apply camera motion reduction - fully subtract camera motion when set to 1.0
            sampleVelocity1 = sampleVelocity1 - (cameraMotion1 * g_CameraMotionReduction);
            sampleVelocity2 = sampleVelocity2 - (cameraMotion2 * g_CameraMotionReduction);
        }
        
        // Calculate velocity lengths
        float sampleVelocityLen1 = length(sampleVelocity1);
        float sampleVelocityLen2 = length(sampleVelocity2);
        
        // Get the length of the sample offset
        float offsetLen = offset;
        
        // Calculate the contribution weights
        float weight1 = SampleWeight(
            centerDepth, sampleDepth1, offsetLen, centerVelocityLen, sampleVelocityLen1,
            pixelToSampleUnitsScale, MB_SOFTZ_INCHES);
            
        float weight2 = SampleWeight(
            centerDepth, sampleDepth2, offsetLen, centerVelocityLen, sampleVelocityLen2,
            pixelToSampleUnitsScale, MB_SOFTZ_INCHES);
        
        // Mirrored background reconstruction
        #if MB_MIRROR_FILTER
        bool2 mirror = bool2(rawVelocityDepth1.b > rawVelocityDepth2.b, sampleVelocityLen2 > sampleVelocityLen1);
        weight1 = all(mirror) ? weight2 : weight1;
        weight2 = any(mirror) ? weight2 : weight1;
        #endif
        
        // Sample colors
        float4 sampleColor1 = TexColor.SampleLevel(LinearSampler, sampleTexCoords1, 0);
        float4 sampleColor2 = TexColor.SampleLevel(LinearSampler, sampleTexCoords2, 0);
        
        // Accumulate weighted samples
        sum += weight1 * float4(sampleColor1.rgb, 1.0f);
        sum += weight2 * float4(sampleColor2.rgb, 1.0f);
    }
    
    // Normalize the result
    sum.rgb *= 1.0f / float(sampleCount);
    sum.w *= 1.0f / float(sampleCount);
    
    // Final color blend with the background
    float4 outputColor = float4(
        sum.rgb + (1.0f - sum.w) * centerColor.rgb, // Blend foreground over background
        1.0f
    );
    
    // Visualization modes
    if (g_VisualizationMode == 1) // Show tile max motion vectors
    {
        // Visualize motion vectors using a color wheel
        float angle = atan2(blurDir.y, blurDir.x);
        float3 hueColor = 0.5f + 0.5f * float3(
            cos(angle),
            cos(angle - 2.0f * PI / 3.0f),
            cos(angle - 4.0f * PI / 3.0f)
        );
        
        // Scale by length
        float maxVelocity = g_MaxBlurRadius;
        float normalizedVel = saturate(blurLength / maxVelocity);
        
        // Blend with grayscale based on velocity magnitude
        float gray = dot(centerColor.rgb, float3(0.3f, 0.59f, 0.11f));
        outputColor.rgb = lerp(float3(gray, gray, gray), hueColor, normalizedVel);
    }
    else if (g_VisualizationMode == 2) // Show raw motion vectors from velocity texture
    {
        // Use our helper function to map raw velocity to color
        outputColor.rgb = MotionVectorToColor(centerVelocity);
    }
    else if (g_VisualizationMode == 3) // Show sample weights (moved from mode 2)
    {
        // Visualize sample weights
        outputColor.rgb = float3(sum.w, sum.w, sum.w);
    }
    else if (g_VisualizationMode == 4) // Show camera motion vectors
    {
        // Visualize camera motion vectors
        // Even if camera reduction is off, we calculate and show camera motion
        float2 cameraMotion = CalculateCameraMotionVector(texCoord, centerDepth) * g_VelocityScale;
        outputColor.rgb = MotionVectorToColor(cameraMotion);
    }
    else if (g_VisualizationMode == 5) // Show motion after camera subtraction
    {
        // For visualization mode 5, we calculate object-only motion
        // Get original velocity without camera reduction
        float2 rawVelocity = TexVelocity.SampleLevel(PointSampler, texCoord, 0).xy * g_VelocityScale;
        
        // Calculate camera motion regardless of reduction setting
        float2 cameraMotion = CalculateCameraMotionVector(texCoord, centerDepth) * g_VelocityScale;
        
        // Object-only motion is the raw velocity minus camera motion
        float2 objectOnlyMotion = rawVelocity - cameraMotion;
        
        // Visualize the object-only motion vectors
        outputColor.rgb = MotionVectorToColor(objectOnlyMotion);
    }
    
    // Write output
    RWTexOut[pixelPos] = outputColor;
}
