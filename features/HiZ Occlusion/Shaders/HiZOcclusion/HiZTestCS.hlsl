#include "Common/SharedData.hlsli"
#include "Common/FrameBuffer.hlsli"
#ifdef VR
#include "Common/VR.hlsli"
#endif

// VERSION: 2.1 - Fixed camera-relative coordinate system, VR stereo support
/* Notes: 
- Skyrim uses left-handed coordinate system, because it uses Direct3D 
- https://learn.microsoft.com/en-us/windows/win32/direct3d9/viewports-and-clipping
- VR: Uses average eye position and eye 0 matrices for conservative culling
*/

// https://www.nickdarnell.com/hierarchical-z-buffer-occlusion-culling/

Texture2D<float> HiZBuffer : register(t0);  // This is just the depth buffer
SamplerState HiZSampler : register(s0);

// Input buffer of geometry bounds to test
StructuredBuffer<float4> GeometryBounds : register(t1); // xyz=center, w=radius

// Output buffer of visibility results: x = fSphereDepth, y = fMaxSampledDepth
RWStructuredBuffer<uint> VisibilityResults : register(u0);

// Hi-Z specific parameters
cbuffer HiZParams : register(b0)
{
    // x = mipCount, y = conservativeBias, z = geometryCount, w = debugMode
    float4 HiZSettings;
    // x=overlayEnabled(0/1), y=maxObjectsToDraw, z=unused, w=unused
    float4 overlaySettings;
    // x contains packed bits for 8 toggles
    float4 overlayColorToggles;
    // Camera world position for proper distance calculations
    float3 CameraWorldPos;
    float pad0;
    float2 BufferDim;      // screenWidth, screenHeight
    float2 BufferDimInv;   // 1/screenWidth, 1/screenHeight
};

// =============================================================================
// Constants for mip level calculation
// =============================================================================

// Minimum depth threshold to prevent division by zero in mip calculations
static const float MIN_DEPTH_THRESHOLD = 0.01;

// NDC range is [-1, 1], so multiply by 0.5 to convert to [0, 1] range for pixel calculation
static const float NDC_TO_PIXEL_SCALE = 0.5;

// Mip level bias: subtract from log2(screenSize) to use finer depth resolution.
// Using a lower mip level (finer resolution) reduces false occlusion but costs more samples.
// A value of 1.5 means we use approximately 2-3x finer resolution than the object's screen coverage.
static const float MIP_LEVEL_BIAS = 1.5;

// =============================================================================
// VR Helper Functions
// =============================================================================

// Convert view-space position to Hi-Z buffer UV coordinates
// In VR, this applies stereo UV conversion for the left eye (eye 0)
float2 ViewToHiZUV(float3 viewPos) {
    float2 uv = FrameBuffer::ViewToUV(viewPos, true, 0);
#ifdef VR
    // Convert to stereo UV for left eye region [0, 0.5]
    uv = Stereo::ConvertToStereoUV(uv, 0);
#endif
    return uv;
}

// Debug output buffer - structured for comprehensive debugging
struct DebugData {
    float4 centerWS_radius;       // xyz=centerWS, w=radius (16 bytes, offset 0)
    float4 centerRel_objDepth;    // xyz=centerWSCameraRelative, w=objDepth (16 bytes, offset 16)
    float sceneDepth;             // 4 bytes (offset 32)
    uint earlyOutReason;          // 4 bytes (offset 36)
    float2 padding;               // 8 bytes padding (offset 40) -> total 48 bytes
};

RWStructuredBuffer<DebugData> DebugOutput : register(u1);

RWTexture2D<unorm float4> DebugOverlay : register(u2);

void DrawPixel(int2 p, uint baseW, uint baseH, float4 color) {
    if ((p.x >= 0) && (p.x < (int)baseW) && (p.y >= 0) && (p.y < (int)baseH)) {
        DebugOverlay[p] = color;
    }
}

void DrawCross(int2 p, uint baseW, uint baseH, float4 color, int thickness) {
    // Variable size cross based on thickness
    // Min: 3x3, Max: 15x15
    int halfSize = max(1, thickness * 3);
    [unroll] for (int i = -halfSize; i <= halfSize; ++i) {
        DrawPixel(int2(p.x + i, p.y), baseW, baseH, color);
        DrawPixel(int2(p.x, p.y + i), baseW, baseH, color);
    }
}

void DrawRectOutline(int2 minTex0, int2 maxTex0, uint baseW, uint baseH, float4 color, int thickness) {
    // Optimized rectangle drawing with pixel budget to prevent GPU stalls
    // For debug visualization, we don't need perfect rectangles at extreme sizes
    int halfThickness = max(0, thickness / 2);
    
    int width = maxTex0.x - minTex0.x;
    int height = maxTex0.y - minTex0.y;
    
    // Maximum pixels to draw per rectangle (prevents excessive work for large bounding boxes)
    const int MAX_PIXELS_PER_RECT = 512;
    
    // Calculate stride to skip pixels if rectangle is too large
    int horizPixels = width * (1 + 2 * halfThickness);
    int vertPixels = height * (1 + 2 * halfThickness);
    int totalPixels = 2 * horizPixels + 2 * vertPixels;
    
    int stride = max(1, totalPixels / MAX_PIXELS_PER_RECT);
    
    for (int t = -halfThickness; t <= halfThickness; ++t) {
        // Top/bottom edges
        for (int x = minTex0.x; x <= maxTex0.x; x += stride) {
            DrawPixel(int2(x, minTex0.y + t), baseW, baseH, color);
            DrawPixel(int2(x, maxTex0.y + t), baseW, baseH, color);
        }
        // Left/right edges
        for (int y = minTex0.y; y <= maxTex0.y; y += stride) {
            DrawPixel(int2(minTex0.x + t, y), baseW, baseH, color);
            DrawPixel(int2(maxTex0.x + t, y), baseW, baseH, color);
        }
    }
    
    // Always draw corners for clarity, even with large stride
    if (stride > 1) {
        for (int t = -halfThickness; t <= halfThickness; ++t) {
            DrawPixel(int2(maxTex0.x, minTex0.y + t), baseW, baseH, color);
            DrawPixel(int2(maxTex0.x, maxTex0.y + t), baseW, baseH, color);
            DrawPixel(int2(minTex0.x + t, maxTex0.y), baseW, baseH, color);
            DrawPixel(int2(maxTex0.x + t, maxTex0.y), baseW, baseH, color);
        }
    }
}

void DrawBounds(uint geometryIndex, float3 centerVS, float radius) {
    // Check if this color is enabled
    uint toggleBits = uint(overlayColorToggles.x);
    bool shouldDraw = false;
    
    if (
        (VisibilityResults[geometryIndex] == -3 && toggleBits & 1) || // Not culled: Test passed
        (VisibilityResults[geometryIndex] == -2 && toggleBits & 2) || // Not culled: Inside bounds
        (VisibilityResults[geometryIndex] == -1 && toggleBits & 4) || // Not culled: Invalid Radius
        (VisibilityResults[geometryIndex] == 1 && toggleBits & 8) || // Culled: Frustum
        (VisibilityResults[geometryIndex] == 2 && toggleBits & 16)) // Culled: No early out
    {
        shouldDraw = true;
    }
    
    // Early exit if this color is filtered out, or frustum culled.
    if (!shouldDraw || VisibilityResults[geometryIndex] == 1) return;  
    
    // Compute base dimensions
    uint baseW, baseH, mipCount;
    HiZBuffer.GetDimensions(0, baseW, baseH, mipCount);

    // Center point UV (use stereo-aware conversion for VR overlay)
    float2 centerUV = ViewToHiZUV(centerVS);
    int2 centerPix = int2(centerUV * float2(baseW, baseH));
    
    // Calculate distance-based thickness
    // Close objects (depth 0-100) = thick (3-5 pixels)
    // Medium objects (depth 100-500) = medium (2-3 pixels)
    // Far objects (depth 500+) = thin (1 pixel)

    // Thickness = depth / 500
    float distance = length(centerVS);
    int thickness = int(500 / distance);
    if (thickness < 1) thickness = 1;
    if (thickness > 5) thickness = 5;

    // Color based on VisibilityResults
    float4 color;
    if (VisibilityResults[geometryIndex] == -3) { // Not culled: Test passed
        color = float4(0, 1, 0, 1);  // Green
    } else if (VisibilityResults[geometryIndex] == -2) { // Not culled: Inside bounds
        color = float4(0, 1, 0.5, 1);  // Teal
    } else if (VisibilityResults[geometryIndex] == -1) { // Not culled: Invalid Radius
        color = float4(0, 1, 1, 1);  // Cyan
    } else if (VisibilityResults[geometryIndex] == 0) { // Default value
        color = float4(1, 1, 1, 1);  // white
    } else if (VisibilityResults[geometryIndex] == 1) { // Culled: Frustum
        color = float4(1, 0, 1, 1);  // Magenta
    } else if (VisibilityResults[geometryIndex] == 2) { // Culled: No early out
        color = float4(1, 0, 0, 1);  // Red
    } else {
        return;
    }

    // Draw center point cross with distance-based thickness
    DrawCross(centerPix, baseW, baseH, color, thickness);

    // Calculate approximate screen-space bounding box for the sphere
    // Project sphere bounds to get conservative rectangle
    float3 viewDir = centerVS * rsqrt(max(dot(centerVS, centerVS), 1e-12));
    
    // Calculate approximate corner positions in view space
    float3 right = float3(1, 0, 0);
    float3 up = float3(0, 1, 0);
    
    // Estimate screen bounds by projecting offset points
    float2 minUV = float2(1, 1);
    float2 maxUV = float2(0, 0);

    float3 offsets[4] = {
        float3(1, 0, 0),
        float3(0, 1, 0),
        float3(-1, 0, 0),
        float3(0, -1, 0)
    };

    [unroll] for (int i = 0; i < 4; i++) {
        float3 dir3 = offsets[i];
        float lenSq = dot(dir3, dir3);
        float3 unitDir = dir3 * rsqrt(max(lenSq, 1e-12));
        float3 pointVS = centerVS + unitDir * radius;
        float2 pointUV = ViewToHiZUV(pointVS);
        minUV = min(minUV, pointUV);
        maxUV = max(maxUV, pointUV);
    }
    
    // Draw outline of bounding rectangle
    int2 minTex0 = int2(
        clamp((int)floor(minUV.x * baseW), 0, (int)baseW - 1),
        clamp((int)floor(minUV.y * baseH), 0, (int)baseH - 1)
    );
    int2 maxTex0 = int2(
        clamp((int)floor(maxUV.x * baseW), 0, (int)baseW - 1),
        clamp((int)floor(maxUV.y * baseH), 0, (int)baseH - 1)
    );
    DrawRectOutline(minTex0, maxTex0, baseW, baseH, color, thickness);
}

// Determines the appropriate mip level for a given object based on its screen coverage
float GetMipLevel(float3 centerVS, float radius) {
    float depth = abs(centerVS.z);
    
    // Prevent division by zero
    if (depth < MIN_DEPTH_THRESHOLD) return 0.0;
    
    // Get projection scale from first element of projection matrix
    // CameraProj[0][0][0] = horizontal FOV scale = 1/tan(fovX/2)
    float projScaleX = FrameBuffer::CameraProj[0][0][0];
    
    // Screen-space radius in NDC: (radius / depth) * projectionScale  
    float screenRadiusNDC = (radius / depth) * projScaleX;
    
    // Convert to pixels (NDC is [-1,1], so multiply by half width)
    uint hiZWidth, hiZHeight, hiZMipCount;
    HiZBuffer.GetDimensions(0, hiZWidth, hiZHeight, hiZMipCount);
    float screenRadiusPixels = abs(screenRadiusNDC) * hiZWidth * NDC_TO_PIXEL_SCALE;
    
    // Diameter in pixels
    float screenSizePixels = screenRadiusPixels * 2.0;
    
    // Subtract MIP_LEVEL_BIAS to use finer depth resolution
    float mipLevel = max(0.0, log2(max(1.0, screenSizePixels)) - MIP_LEVEL_BIAS);
    
    return clamp(mipLevel, 0.0, HiZSettings.x - 1.0);  // Also avoid highest mip
}

/* 
 * Reports a geometry that has been determined visible as a result of a valid depth test
 * and draws it in the bounds overlay if enabled.
*/
void ReportVisibleGeometry(int geometryIndex, float3 centerVS, float radius) {
    if (overlaySettings.x != 0 && geometryIndex < overlaySettings.y) {
        DrawBounds(geometryIndex, centerVS, radius);
    }
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint geometryIndex = dispatchThreadID.x;
    uint geometryCount = (uint)HiZSettings.z;
    if (geometryIndex >= geometryCount)
        return;

    // Set default return value to unculled
    VisibilityResults[geometryIndex] = 0;

    float4 Bounds = GeometryBounds[geometryIndex];
    float3 centerWS = Bounds.xyz;
    float radius = Bounds.w;

    // Skyrim uses camera-relative coordinates - subtract camera position before view transform
    // Note: SOMETIMES, seemingingly only interior cells, some bounds follow the camera inverted.. needs investigation.
    float3 centerWSCameraRelative = centerWS - CameraWorldPos;
    float3 centerVS = mul(FrameBuffer::CameraView[0], float4(centerWSCameraRelative, 1)).xyz;

    // Check for invalid radius
    if (radius <= 0.0) {
        VisibilityResults[geometryIndex] = -1;
        if (overlaySettings.x != 0 && geometryIndex < (uint)overlaySettings.y) {
            DrawBounds(geometryIndex, centerVS, radius);
        }
        return;
    }

    /*
     * Increase radius by percentage based on the conservative bias value
     * This is to help reduce pop-in by conserving geometry which is just barely hidden.
     * E.g. 0.001 = 1% increase, 0.01 = 10% increase, etc.
    */
    // float conservativeRadius = radius * (1.0 + HiZSettings.y * 100.0);
    // this is problematic because increasing the radius can move some points off screen.
    
    // Check if the camera is inside the bounds
    float centerDist = length(centerVS);
    if (centerDist <= radius) {
        VisibilityResults[geometryIndex] = -2;
        if (overlaySettings.x != 0 && geometryIndex < (uint)overlaySettings.y) {
            DrawBounds(geometryIndex, centerVS, radius);
        }
        return;
    }

    float mipLevel = 0.0;
    {
        // Choose appropriate mip based on screen coverage of object bounds
        mipLevel = GetMipLevel(centerVS, radius);
    }

    float conservativeBias = HiZSettings.y;

    // Bounding sphere test points in a hierarchical order:
    // - 26 cube corners at 3 different scales (center, vertex, face, edge points)
    // - Test coarse first (larger radius), then fine for early-out optimization
    static const float3 offsets[26] = {
        float3(-1, -1, -1), float3(-1, -1,  0), float3(-1, -1,  1),
        float3(-1,  0, -1), float3(-1,  0,  0), float3(-1,  0,  1),
        float3(-1,  1, -1), float3(-1,  1,  0), float3(-1,  1,  1),
        float3( 0, -1, -1), float3( 0, -1,  0), float3( 0, -1,  1),
        float3( 0,  0, -1), float3( 0,  0,  1),
        float3( 0,  1, -1), float3( 0,  1,  0), float3( 0,  1,  1),
        float3( 1, -1, -1), float3( 1, -1,  0), float3( 1, -1,  1),
        float3( 1,  0, -1), float3( 1,  0,  0), float3( 1,  0,  1),
        float3( 1,  1, -1), float3( 1,  1,  0), float3( 1,  1,  1)
    };

    bool anyPointOnScreen = false;

    // Test center and nearest sphere point first (most likely to be visible)
    if (centerVS.z > 0.0) {
        float2 centerUV = clamp(ViewToHiZUV(centerVS), float2(0.0, 0.0), float2(1.0, 1.0));
        float4 centerClip = mul(FrameBuffer::CameraProj[0], float4(centerVS, 1));
        float centerDepth = centerClip.z / centerClip.w;
        
        float hiZDepth = HiZBuffer.SampleLevel(HiZSampler, centerUV, mipLevel).r;
        if (centerDepth <= hiZDepth + conservativeBias) {
            VisibilityResults[geometryIndex] = -3;
            ReportVisibleGeometry(geometryIndex, centerVS, radius);
            return;
        }
        anyPointOnScreen = true;
        
        // Test nearest sphere point
        float3 normalDir = -normalize(centerVS);
        float3 nearestSpherePoint = centerVS + normalDir * radius;
        if (nearestSpherePoint.z > 0.0) {
            float2 nearestUV = clamp(ViewToHiZUV(nearestSpherePoint), float2(0.0, 0.0), float2(1.0, 1.0));
            float4 nearestClip = mul(FrameBuffer::CameraProj[0], float4(nearestSpherePoint, 1));
            float nearestDepth = nearestClip.z / nearestClip.w;
            
            hiZDepth = HiZBuffer.SampleLevel(HiZSampler, nearestUV, mipLevel).r;
            if (nearestDepth <= hiZDepth + conservativeBias) {
                VisibilityResults[geometryIndex] = -3;
                ReportVisibleGeometry(geometryIndex, centerVS, radius);
                return;
            }
        }
    }

    // Hierarchical testing: Test larger radius first (1.5x) for conservative culling
    // This catches objects that are just barely hidden and reduces pop-in
    // Scales: [1.5x, 1.0x, 0.5x] - coarse to fine with early-out
    static const float radiusScales[3] = { 1.5, 1.0, 0.5 };
    
    [unroll]
    for (int scaleIdx = 0; scaleIdx < 3; ++scaleIdx) {
        float testRadius = radius * radiusScales[scaleIdx];
        
        // Test 26 cube corner points at this radius scale
        for (int i = 0; i < 26; ++i) {
            float3 dir3 = offsets[i];
            float lenSq = dot(dir3, dir3);
            float3 unitDir = dir3 * rsqrt(max(lenSq, 1e-12));
            float3 pointVS = centerVS + unitDir * testRadius;

            if (pointVS.z <= 0.0) {
                continue;
            }

            anyPointOnScreen = true;
            
            float4 pointClip = mul(FrameBuffer::CameraProj[0], float4(pointVS, 1));
            float pointDepth = pointClip.z / pointClip.w;
            float2 pointUV = clamp(ViewToHiZUV(pointVS), float2(0.0, 0.0), float2(1.0, 1.0));

            float hiZDepth = HiZBuffer.SampleLevel(HiZSampler, pointUV, mipLevel).r;
            if (pointDepth <= hiZDepth + conservativeBias) {
                VisibilityResults[geometryIndex] = -3;
                ReportVisibleGeometry(geometryIndex, centerVS, radius);
                return;
            }
        }
    }

    // If not a single point was on screen, frustum cull
    if (!anyPointOnScreen) {
        VisibilityResults[geometryIndex] = 1;
        if (overlaySettings.x != 0 && geometryIndex < (uint)overlaySettings.y) {
            DrawBounds(geometryIndex, centerVS, radius);
        }
        return;
    }

    // If we get to this point, this object has not been deemed visible, and thus shall be culled.
    VisibilityResults[geometryIndex] = 2;
    if (overlaySettings.x != 0 && geometryIndex < (uint)overlaySettings.y) {
        DrawBounds(geometryIndex, centerVS, radius);
    }
}