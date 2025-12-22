#include "Common/SharedData.hlsli"
#include "Common/FrameBuffer.hlsli"
#ifdef VR
#include "Common/VR.hlsli"
#endif

// =============================================================================
// HiZ Occlusion Test Compute Shader
// =============================================================================
// VERSION: 2.1
//
// PURPOSE:
//   Tests geometry bounding spheres against a Hierarchical-Z depth pyramid to
//   determine visibility. Objects fully occluded by closer geometry are culled
//   to save rendering work.
//
// ALGORITHM OVERVIEW:
//   1. Each thread tests one geometry's bounding sphere
//   2. Transform sphere center from world space -> camera-relative -> view space
//   3. Early-out if camera is inside sphere (always visible)
//   4. Calculate appropriate mip level based on screen coverage
//   5. Sample Hi-Z depth at multiple points on the sphere surface
//   6. If ANY point passes depth test -> object is visible
//   7. If ALL points fail -> object is occluded
//
// ASYNC READBACK ARCHITECTURE:
//   Results are written to VisibilityResults buffer which is copied to a staging
//   buffer for CPU readback. Due to GPU latency, results are typically read back
//   2-3 frames later. This means:
//   - Objects are always rendered at least once before being culled
//   - Culled objects may take 2-3 frames to reappear when camera reveals them
//   - This latency is the tradeoff for non-blocking GPU queries
//
// RESULT CODES (written to VisibilityResults):
//   -3: Visible - depth test passed (at least one sample passed)
//   -2: Visible - camera is inside bounding sphere
//   -1: Visible - invalid radius (skip culling for safety)
//    0: Default/unprocessed (should not remain after shader runs)
//    1: Culled - frustum culled (all sample points behind camera)
//    2: Culled - occluded (all depth tests failed)
//
// VR SUPPORT:
//   - Uses average eye position (center between eyes) for camera position
//   - Uses left eye (eye 0) matrices for view/projection transforms
//   - Applies stereo UV conversion when sampling Hi-Z buffer
//   - Conservative approach: slight over-rendering at periphery is acceptable
//
// REFERENCES:
//   - https://www.nickdarnell.com/hierarchical-z-buffer-occlusion-culling/
//   - Skyrim uses left-handed coordinate system (Direct3D convention)
//   - https://learn.microsoft.com/en-us/windows/win32/direct3d9/viewports-and-clipping
// =============================================================================

Texture2D<float> HiZBuffer : register(t0);  // Hierarchical depth pyramid (mip 0 = full res)
SamplerState HiZSampler : register(s0);

// Input: Bounding spheres for all geometry to test this frame
// Format: xyz = world-space center, w = radius
StructuredBuffer<float4> GeometryBounds : register(t1);

// Output: Visibility result codes (see header for code meanings)
// One uint per geometry object, indexed by SV_DispatchThreadID.x
RWStructuredBuffer<uint> VisibilityResults : register(u0);

// =============================================================================
// Constant Buffer - Updated each frame from CPU
// =============================================================================
cbuffer HiZParams : register(b0)
{
    // HiZSettings components:
    //   x = mipCount (number of mip levels in Hi-Z pyramid)
    //   y = conservativeBias (depth bias to reduce false occlusion, e.g. 0.01 = 1%)
    //   z = geometryCount (number of objects to test this dispatch)
    //   w = debugMode (1 = enable debug output)
    float4 HiZSettings;
    
    // overlaySettings components:
    //   x = overlayEnabled (0 or 1)
    //   y = maxObjectsToDraw (limit overlay rendering for performance)
    //   z,w = unused
    float4 overlaySettings;
    
    // overlayColorToggles: bit flags for filtering which result types to draw
    //   bit 0: show visible (test passed)
    //   bit 1: show visible (inside bounds)
    //   bit 2: show visible (invalid radius)
    //   bit 3: show culled (frustum)
    //   bit 4: show culled (occluded)
    float4 overlayColorToggles;
    
    // Camera world position - used to convert world coords to camera-relative
    // In VR: This is the average position between both eyes
    float3 CameraWorldPos;
    float pad0;
    
    // Screen dimensions for debug overlay rendering
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

/// Converts a view-space position to Hi-Z buffer UV coordinates.
/// In VR mode, applies stereo UV conversion to sample from the left eye region.
/// @param viewPos Position in view/camera space
/// @return UV coordinates suitable for sampling HiZBuffer
float2 ViewToHiZUV(float3 viewPos) {
    float2 uv = FrameBuffer::ViewToUV(viewPos, true, 0);
#ifdef VR
    // VR renders both eyes side-by-side: left eye = [0, 0.5], right eye = [0.5, 1]
    // We test against left eye only for conservative culling
    uv = Stereo::ConvertToStereoUV(uv, 0);
#endif
    return uv;
}

// =============================================================================
// Debug Visualization (optional, controlled by overlaySettings)
// =============================================================================

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

// =============================================================================
// Mip Level Selection
// =============================================================================

/// Calculates the appropriate Hi-Z mip level to sample for a given object.
/// 
/// The mip level is chosen based on the object's screen-space size:
/// - Larger objects (more pixels) -> higher mip level (coarser depth)
/// - Smaller objects (fewer pixels) -> lower mip level (finer depth)
/// 
/// Using a mip level that roughly matches the object's screen coverage ensures
/// we get a representative depth value without over-sampling.
/// 
/// @param centerVS Object center in view space
/// @param radius Object bounding sphere radius
/// @return Mip level to use for Hi-Z sampling (0 = full resolution)
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
    
    // Subtract MIP_LEVEL_BIAS to use finer depth resolution (reduces false occlusion)
    float mipLevel = max(0.0, log2(max(1.0, screenSizePixels)) - MIP_LEVEL_BIAS);
    
    return clamp(mipLevel, 0.0, HiZSettings.x - 1.0);  // Clamp to valid mip range
}

// =============================================================================
// Visibility Reporting
// =============================================================================

/// Reports a geometry as visible and optionally draws debug overlay.
/// Called when depth test passes or early-out determines visibility.
/// @param geometryIndex Index into GeometryBounds/VisibilityResults
/// @param centerVS Object center in view space (for debug rendering)
/// @param radius Object radius (for debug rendering)
void ReportVisibleGeometry(int geometryIndex, float3 centerVS, float radius) {
    if (overlaySettings.x != 0 && geometryIndex < overlaySettings.y) {
        DrawBounds(geometryIndex, centerVS, radius);
    }
}

// =============================================================================
// Main Entry Point
// =============================================================================
// Each thread tests one geometry object's bounding sphere against the Hi-Z pyramid.
// Thread count = geometryCount, dispatched as (ceil(geometryCount/256), 1, 1)
// =============================================================================

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint geometryIndex = dispatchThreadID.x;
    uint geometryCount = (uint)HiZSettings.z;
    if (geometryIndex >= geometryCount)
        return;

    // Initialize to "unprocessed" - this should be overwritten before shader exits
    VisibilityResults[geometryIndex] = 0;

    // Load bounding sphere data
    float4 Bounds = GeometryBounds[geometryIndex];
    float3 centerWS = Bounds.xyz;
    float radius = Bounds.w;

    // =========================================================================
    // COORDINATE TRANSFORM: World Space -> View Space
    // =========================================================================
    // Skyrim stores positions in camera-relative world coordinates.
    // Subtract camera position to get true world-relative coords before view transform.
    // NOTE: Some interior cell bounds appear to follow the camera inverted - needs investigation.
    float3 centerWSCameraRelative = centerWS - CameraWorldPos;
    float3 centerVS = mul(FrameBuffer::CameraView[0], float4(centerWSCameraRelative, 1)).xyz;

    // =========================================================================
    // EARLY OUT 1: Invalid radius -> mark visible (safety)
    // =========================================================================
    if (radius <= 0.0) {
        VisibilityResults[geometryIndex] = -1;  // Visible: invalid radius
        if (overlaySettings.x != 0 && geometryIndex < (uint)overlaySettings.y) {
            DrawBounds(geometryIndex, centerVS, radius);
        }
        return;
    }

    // =========================================================================
    // EARLY OUT 2: Camera inside bounding sphere -> always visible
    // =========================================================================
    float centerDist = length(centerVS);
    if (centerDist <= radius) {
        VisibilityResults[geometryIndex] = -2;  // Visible: camera inside bounds
        if (overlaySettings.x != 0 && geometryIndex < (uint)overlaySettings.y) {
            DrawBounds(geometryIndex, centerVS, radius);
        }
        return;
    }

    // =========================================================================
    // MIP LEVEL SELECTION
    // =========================================================================
    // Choose Hi-Z mip level based on object's screen coverage.
    // Larger screen coverage -> coarser mip (faster, acceptable accuracy)
    float mipLevel = GetMipLevel(centerVS, radius);
    float conservativeBias = HiZSettings.y;

    // =========================================================================
    // HIERARCHICAL DEPTH TESTING
    // =========================================================================
    // Test multiple points on the bounding sphere surface against the Hi-Z depth.
    // If ANY point passes the depth test, the object is considered visible.
    // 
    // Points are tested in hierarchical order (center first, then sphere surface)
    // to maximize early-out opportunities for visible objects.
    // 
    // The 26 offset directions sample a cube's corners, edges, and face centers,
    // providing good coverage of the sphere surface.
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

    // -------------------------------------------------------------------------
    // Test 1: Center point and nearest sphere point (highest visibility probability)
    // -------------------------------------------------------------------------
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
                VisibilityResults[geometryIndex] = -3;  // Visible: depth test passed
                ReportVisibleGeometry(geometryIndex, centerVS, radius);
                return;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Test 2: Hierarchical sphere surface sampling
    // -------------------------------------------------------------------------
    // Test 26 points on the sphere surface at 3 different radius scales.
    // Starting with larger radius (1.5x) provides conservative culling that
    // helps reduce pop-in for objects that are just barely hidden.
    // 
    // The unrolled loop tests all 78 points (26 * 3) with early-out on first
    // passing depth test - visible objects exit quickly.
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
                VisibilityResults[geometryIndex] = -3;  // Visible: depth test passed
                ReportVisibleGeometry(geometryIndex, centerVS, radius);
                return;
            }
        }
    }

    // =========================================================================
    // FRUSTUM CULLING: No points in front of camera
    // =========================================================================
    // If ALL tested points were behind the camera (z <= 0), the object is
    // completely outside the view frustum and can be culled.
    if (!anyPointOnScreen) {
        VisibilityResults[geometryIndex] = 1;  // Culled: frustum (behind camera)
        if (overlaySettings.x != 0 && geometryIndex < (uint)overlaySettings.y) {
            DrawBounds(geometryIndex, centerVS, radius);
        }
        return;
    }

    // =========================================================================
    // OCCLUSION CULLING: All depth tests failed
    // =========================================================================
    // If we reach here, all tested points were on-screen but failed the depth test.
    // The object is occluded by closer geometry and can be culled.
    // 
    // NOTE: This result will be read back by the CPU 2-3 frames later due to
    // async GPU readback. The object will be re-tested each frame while culled.
    VisibilityResults[geometryIndex] = 2;  // Culled: occluded (all depth tests failed)
    if (overlaySettings.x != 0 && geometryIndex < (uint)overlaySettings.y) {
        DrawBounds(geometryIndex, centerVS, radius);
    }
}