#ifndef __WATER_TESSELLATION_HLSLI__
#define __WATER_TESSELLATION_HLSLI__

// Water Tessellation System for Dynamic LOD
//
// This system implements hardware tessellation for water surfaces, allowing
// dynamic vertex density based on distance from camera. This solves the
// spatial aliasing problem where Gerstner wavelengths are smaller than
// triangle spacing.
//
// The tessellation is now wave-aware: areas with higher wave amplitude and
// steepness receive more tessellation to properly represent the curved surface.
//
// Pipeline: VS -> HS -> Tessellator -> DS -> PS
// - Vertex Shader: Outputs control points with basic transforms (no wave displacement)
// - Hull Shader: Calculates tessellation factors per patch based on distance + wave properties
// - Tessellator (fixed function): Generates new vertices
// - Domain Shader: Interpolates attributes and applies Gerstner wave displacement
// - Pixel Shader: Unchanged from original
//
// CELL BOUNDARY FIX:
// To prevent cracks at water cell boundaries, edge tessellation factors are computed
// using ABSOLUTE world positions (via FrameBuffer::CameraPosAdjust) and quantized to
// ensure adjacent cells compute identical factors for shared edges.

// Tessellation parameters passed from CPU (register b9)
cbuffer TessellationParams : register(b9)
{
	float TessellationMinDistance;   // Distance where max tessellation applies
	float TessellationMaxDistance;   // Distance where min tessellation applies  
	float TessellationMinFactor;     // Minimum tessellation factor (1 = no tessellation)
	float TessellationMaxFactor;     // Maximum tessellation factor (up to 64)
	float TessCameraWorldPosX;       // Backup camera pos (prefer FrameBuffer::CameraPosAdjust)
	float TessCameraWorldPosY;
	float TessCameraWorldPosZ;
	float DetailHeightScale;         // Unused - kept for cbuffer compatibility
}

// Patch constant data (output from hull shader patch constant function)
struct HS_CONSTANT_OUTPUT
{
	float EdgeTess[3] : SV_TessFactor;
	float InsideTess : SV_InsideTessFactor;
};

// Calculate base tessellation factor using proper distance interpolation
// Uses smooth logarithmic falloff for more natural LOD transitions
float CalculateDistanceTessellation(float dist)
{
	// Beyond max distance: minimum tessellation
	if (dist >= TessellationMaxDistance)
		return TessellationMinFactor;
	
	// Within min distance: maximum tessellation
	if (dist <= TessellationMinDistance)
		return TessellationMaxFactor;
	
	// Smooth interpolation between min and max distance
	// Use a curve that provides more detail at medium distances
	float range = TessellationMaxDistance - TessellationMinDistance;
	float normalizedDist = (dist - TessellationMinDistance) / range;
	
	// Smoothstep for gradual transition without harsh cutoffs
	float t = normalizedDist * normalizedDist * (3.0f - 2.0f * normalizedDist);
	
	return lerp(TessellationMaxFactor, TessellationMinFactor, t);
}

// Calculate edge tessellation factor based on ABSOLUTE world position
// This ensures adjacent patches share EXACTLY the same edge factors to prevent cracks
// 
// CRITICAL: To prevent cracks at cell boundaries, the tessellation factor for a shared
// edge must be computed identically by both triangles that share it. We achieve this by:
// 1. Using FrameBuffer::CameraPosAdjust (same value used throughout the shader pipeline)
// 2. Computing absolute world position for edge calculation
// 3. Quantizing edge midpoint to avoid floating-point precision issues
// 4. Using only the edge endpoints (which are shared) not any per-triangle data
//
// The quantization grid (0.5 units) ensures that even with tiny FP precision differences,
// adjacent cells will compute identical tessellation factors for shared edges.
float CalculateEdgeTessellation(float3 p0, float3 p1)
{
	// Use FrameBuffer::CameraPosAdjust for consistency with rest of shader pipeline
	// This is the same value used in VS, DS, and PS for coordinate transforms
	float3 cameraPos = FrameBuffer::CameraPosAdjust[0].xyz;
	
	// Convert from camera-relative to absolute world position
	float3 absP0 = p0 + cameraPos;
	float3 absP1 = p1 + cameraPos;
	
	// Calculate edge midpoint in absolute world space
	float3 absEdgeMid = (absP0 + absP1) * 0.5f;
	
	// CRITICAL: Quantize to a world-space grid to ensure adjacent cells
	// compute identical values for shared edges despite FP precision
	// Using 0.5 unit grid (coarser than before to handle larger FP errors)
	absEdgeMid = floor(absEdgeMid * 2.0f + 0.5f) * 0.5f;
	
	// Calculate distance from camera to the quantized midpoint
	float dist = length(absEdgeMid - cameraPos);
	
	// Get base factor from distance using smooth interpolation
	float baseFactor = CalculateDistanceTessellation(dist);
	
	// Scale by edge length for screen-space consistency
	// Quantize edge endpoints too for consistent length calculation
	float3 quantP0 = floor(absP0 * 2.0f + 0.5f) * 0.5f;
	float3 quantP1 = floor(absP1 * 2.0f + 0.5f) * 0.5f;
	float edgeLength = length(quantP1 - quantP0);
	
	// Screen-space edge length approximation
	float screenEdgeLength = edgeLength / max(dist, 1.0f);
	
	// Scale factor: edges that appear larger on screen get more tessellation
	float edgeScale = saturate(screenEdgeLength * 50.0f);
	
	// Blend edge scale with base factor
	float finalFactor = baseFactor * lerp(0.5f, 1.0f, edgeScale);
	
	return clamp(finalFactor, TessellationMinFactor, TessellationMaxFactor);
}

#endif // __WATER_TESSELLATION_HLSLI__
