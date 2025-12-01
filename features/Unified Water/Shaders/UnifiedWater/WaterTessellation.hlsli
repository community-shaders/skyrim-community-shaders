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

// Tessellation parameters passed from CPU (register b9)
cbuffer TessellationParams : register(b9)
{
	float TessellationMinDistance;   // Distance where max tessellation applies
	float TessellationMaxDistance;   // Distance where min tessellation applies  
	float TessellationMinFactor;     // Minimum tessellation factor (1 = no tessellation)
	float TessellationMaxFactor;     // Maximum tessellation factor (up to 64)
	float TessCameraWorldPosX;
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

// Calculate edge tessellation factor based on distance and edge properties
// This ensures adjacent patches share consistent edge factors to prevent cracks
float CalculateEdgeTessellation(float3 p0, float3 p1)
{
	// Use edge midpoint for distance calculation
	float3 edgeMid = (p0 + p1) * 0.5f;
	float dist = length(edgeMid);
	
	// Get base factor from distance
	float baseFactor = CalculateDistanceTessellation(dist);
	
	// Optionally scale by edge length for screen-space consistency
	// Longer edges in world space may need more subdivision
	float edgeLength = length(p1 - p0);
	
	// Screen-space edge length approximation
	// Larger edges relative to distance need more tessellation
	float screenEdgeLength = edgeLength / max(dist, 1.0f);
	
	// Scale factor: edges that appear larger on screen get more tessellation
	// The 50.0 multiplier maps typical water triangle sizes to useful factor range
	float edgeScale = saturate(screenEdgeLength * 50.0f);
	
	// Blend edge scale with base factor - edge scale can boost but not reduce
	float finalFactor = baseFactor * lerp(0.5f, 1.0f, edgeScale);
	
	return clamp(finalFactor, TessellationMinFactor, TessellationMaxFactor);
}

#endif // __WATER_TESSELLATION_HLSLI__
