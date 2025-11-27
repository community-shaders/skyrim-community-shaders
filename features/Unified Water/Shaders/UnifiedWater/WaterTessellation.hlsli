#ifndef __WATER_TESSELLATION_HLSLI__
#define __WATER_TESSELLATION_HLSLI__

// Water Tessellation System for Dynamic LOD
//
// This system implements hardware tessellation for water surfaces, allowing
// dynamic vertex density based on distance from camera. This solves the
// spatial aliasing problem where Gerstner wavelengths are smaller than
// triangle spacing.
//
// Pipeline: VS -> HS -> Tessellator -> DS -> PS
// - Vertex Shader: Outputs control points with basic transforms (no wave displacement)
// - Hull Shader: Calculates tessellation factors per patch based on distance
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
	float DetailHeightScale;         // Scale for heightmap-based detail displacement
}

// Patch constant data (output from hull shader patch constant function)
struct HS_CONSTANT_OUTPUT
{
	float EdgeTess[3] : SV_TessFactor;
	float InsideTess : SV_InsideTessFactor;
};

// Calculate tessellation factor based on distance from camera
// In camera-relative space, camera is at origin (0,0,0), so distance is just length(worldPos)
float CalculateTessellationFactor(float3 worldPos)
{
	float dist = length(worldPos);
	
	// Early out - no tessellation beyond max distance
	if (dist > TessellationMaxDistance)
		return TessellationMinFactor;
	
	// Inverse distance falloff - more aggressive reduction with distance
	float t = saturate((dist - TessellationMinDistance) / (TessellationMaxDistance - TessellationMinDistance));
	t = t * t;  // Quadratic falloff - faster reduction at distance
	
	return lerp(TessellationMaxFactor, TessellationMinFactor, t);
}

// Calculate edge tessellation factor based on screen-space edge length
float CalculateEdgeTessellation(float3 p0, float3 p1)
{
	float3 edgeMid = (p0 + p1) * 0.5;
	float dist = length(edgeMid);
	
	// Skip tessellation for distant edges
	if (dist > TessellationMaxDistance)
		return TessellationMinFactor;
	
	// Factor in edge length - longer edges need more tessellation
	float edgeLength = length(p1 - p0);
	float screenFactor = edgeLength / max(dist, 1.0);  // Approximate screen-space size
	
	float baseFactor = CalculateTessellationFactor(edgeMid);
	
	// Scale by screen factor but clamp to reasonable range
	return clamp(baseFactor * saturate(screenFactor * 0.1), TessellationMinFactor, TessellationMaxFactor);
}

#endif // __WATER_TESSELLATION_HLSLI__
