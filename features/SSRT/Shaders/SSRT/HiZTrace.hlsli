// Hi-Z Screen-Space Ray Tracing
// Cell-boundary aligned hierarchical depth buffer ray march
// Based on AMD FidelityFX SSSR adapted for Capsaicin GI-1.1 probe population
// Uses MIN pyramid (reversed-Z: min = farthest surface = conservative skip bound)

#ifndef HIZ_TRACE_HLSLI
#define HIZ_TRACE_HLSLI

#include "SSRT/GI1Common.hlsli"

struct HiZTraceResult
{
	float3 hitRadiance;
	float hitDistance;
	bool hit;
};

// Project world position to screen space: xy = UV [0,1], z = NDC depth
// Must use jittered CameraViewProj to match the depth buffer written with the jittered camera
float3 HiZ_ProjectToScreen(float3 worldPos)
{
	float4 clip = mul(FrameBuffer::CameraViewProj[0], float4(worldPos, 1.0f));
	clip.xyz /= clip.w;
	return float3(clip.x * 0.5f + 0.5f, 0.5f - clip.y * 0.5f, clip.z);
}

// Screen-space direction from projecting (origin + dir) and subtracting
float3 HiZ_ProjectDirection(float3 worldOrigin, float3 worldDir, float3 ssOrigin)
{
	return HiZ_ProjectToScreen(worldOrigin + worldDir) - ssOrigin;
}

// Get mip resolution
float2 HiZ_GetMipResolution(int mip)
{
	return g_BufferDimensions * exp2(-mip);
}

// Load depth from pyramid at integer coords + mip
float HiZ_LoadDepth(Texture2D<float> depthPyramid, int2 coord, int mip)
{
	return depthPyramid.Load(int3(coord, mip)).x;
}

// Initial advance: move ray to the first cell boundary to avoid self-intersection
void HiZ_InitialAdvanceRay(
	float3 origin, float3 direction, float3 invDirection,
	float2 mipResolution, float2 mipResolutionInv,
	float2 floorOffset, float2 uvOffset,
	out float3 position, out float currentT)
{
	float2 currentMipPos = mipResolution * origin.xy;

	// Intersect with the half-box pointing away from the ray origin
	float2 xyPlane = floor(currentMipPos) + floorOffset;
	xyPlane = xyPlane * mipResolutionInv + uvOffset;

	// t = (plane - origin) / direction
	float2 t = xyPlane * invDirection.xy - origin.xy * invDirection.xy;
	currentT = min(t.x, t.y);
	position = origin + currentT * direction;
}

// Advance ray to the next cell boundary (XY or depth surface)
// Returns true if the tile was skipped (ray passed through without hitting depth)
bool HiZ_AdvanceRay(
	float3 origin, float3 direction, float3 invDirection,
	float2 currentMipPos, float2 mipResolutionInv,
	float2 floorOffset, float2 uvOffset,
	float surfaceZ,
	inout float3 position, inout float currentT)
{
	// Cell boundary planes in UV space + depth surface plane
	float2 xyPlane = floor(currentMipPos) + floorOffset;
	xyPlane = xyPlane * mipResolutionInv + uvOffset;
	float3 boundaryPlanes = float3(xyPlane, surfaceZ);

	// Ray-plane intersection: t = (plane - origin) / direction
	float3 t = boundaryPlanes * invDirection - origin * invDirection;

	// Reversed-Z: ray going toward far plane has direction.z < 0
	// Only use Z plane if the ray is heading into the depth buffer
	t.z = direction.z < 0.0f ? t.z : FLT_MAX;

	float tMin = min(min(t.x, t.y), t.z);

	// Reversed-Z: larger z = closer to camera
	// Ray is "above" (in front of) surface if position.z > surfaceZ
	bool aboveSurface = position.z > surfaceZ;

	// Tile was skipped if we hit an XY boundary (not Z) and we're above the surface
	bool skippedTile = (asuint(tMin) != asuint(t.z)) && aboveSurface;

	// Only advance if above surface (don't push ray behind geometry)
	currentT = aboveSurface ? tMin : currentT;
	position = origin + currentT * direction;

	return skippedTile;
}

// Validate hit: thickness check, back-face rejection, edge fade
float HiZ_ValidateHit(
	Texture2D<float> depthPyramid,
	float3 hit, float2 originUV, float3 worldOrigin, float3 worldDir,
	float thickness, float maxDistance,
	out float3 hitWorld, out float hitDist)
{
	hitWorld = 0.0f;
	hitDist = FLT_MAX;

	// Reject off-screen
	if (any(hit.xy < 0.0f) || any(hit.xy > 1.0f))
		return 0.0f;

	// Don't hit sky pixels (reversed-Z: sky = 0)
	float surfaceZ = HiZ_LoadDepth(depthPyramid, int2(hit.xy * g_BufferDimensions), 0);
	if (surfaceZ == 0.0f)
		return 0.0f;

	// Thickness test in linear depth (SharedData::GetScreenDepth matches DeferredComposite)
	float linearSurface = SharedData::GetScreenDepth(surfaceZ);
	float linearHit = SharedData::GetScreenDepth(hit.z);
	float depthDiff = abs(linearHit - linearSurface);

	float confidence = 1.0f - smoothstep(0.0f, thickness * linearSurface, depthDiff);
	confidence *= confidence;

	if (confidence < 0.01f)
		return 0.0f;

	// Self-intersection rejection (< 2 pixels away)
	float2 manhattanDist = abs(hit.xy - originUV);
	if (manhattanDist.x < 2.0f / g_BufferDimensions.x && manhattanDist.y < 2.0f / g_BufferDimensions.y)
		return 0.0f;

	// Reconstruct world hit and check distance
	hitWorld = reconstructWorldPosition(hit.xy, surfaceZ);
	hitDist = distance(worldOrigin, hitWorld);

	if (hitDist > maxDistance)
		return 0.0f;

	// Screen edge fade
	float2 fov = 0.05f * float2(g_BufferDimensions.y / g_BufferDimensions.x, 1.0f);
	float2 border = smoothstep(0.0f, fov, hit.xy) * (1.0f - smoothstep(1.0f - fov, 1.0f, hit.xy));
	float vignette = border.x * border.y;

	return vignette * confidence;
}

// Hi-Z screen-space ray march (cell-boundary aligned)
HiZTraceResult HiZTrace(
	in Texture2D<float> depthPyramid,
	in Texture2D radiancePyramid,
	in float3 worldOrigin,
	in float3 worldDir,
	in float maxDistance)
{
	HiZTraceResult result;
	result.hitRadiance = 0.0f;
	result.hitDistance = FLT_MAX;
	result.hit = false;

	// Project to screen space (UV + NDC depth)
	float3 ssOrigin = HiZ_ProjectToScreen(worldOrigin);
	float3 ssDir = HiZ_ProjectDirection(worldOrigin, worldDir, ssOrigin);

	// Skip degenerate rays
	if (length(ssDir.xy * g_BufferDimensions) < 1e-5f)
		return result;

	// Inverse direction for cell boundary intersection
	float3 invDir = abs(ssDir) > float(1e-12f) ? float(1.0f) / ssDir : FLT_MAX;

	// Start at finest mip
	int currentMip = 0;
	int maxMip = int(g_DepthPyramidMipCount) - 1;
	float2 mipResolution = HiZ_GetMipResolution(currentMip);
	float2 mipResolutionInv = rcp(mipResolution);

	// Offset to nudge past cell boundaries (avoid exact boundary ambiguity)
	float2 uvOffset = 0.005f / g_BufferDimensions;
	uvOffset = ssDir.xy < 0.0f ? -uvOffset : uvOffset;

	// Floor offset: advance to far edge of cell in ray direction
	float2 floorOffset = ssDir.xy < 0.0f ? float2(0.0f, 0.0f) : float2(1.0f, 1.0f);

	// Initial advance past the origin cell
	float currentT;
	float3 position;
	HiZ_InitialAdvanceRay(ssOrigin, ssDir, invDir, mipResolution, mipResolutionInv,
		floorOffset, uvOffset, position, currentT);

	uint numIters = 0;
	while (numIters < g_MaxHiZSteps && currentMip >= 0)
	{
		// Bounds check
		if (any(position.xy < 0.0f) || any(position.xy > 1.0f))
			break;

		// Reversed-Z: sky at 0, reject if we've gone past everything
		if (position.z < 1e-6f)
			break;

		float2 currentMipPos = mipResolution * position.xy;
		float surfaceZ = HiZ_LoadDepth(depthPyramid, int2(currentMipPos), currentMip);

		bool skippedTile = HiZ_AdvanceRay(
			ssOrigin, ssDir, invDir,
			currentMipPos, mipResolutionInv,
			floorOffset, uvOffset,
			surfaceZ,
			position, currentT);

		// Mip transition: skip -> increase (bigger steps), hit -> decrease (refine)
		bool nextMipOutOfRange = skippedTile && (currentMip >= maxMip);
		if (!nextMipOutOfRange)
		{
			currentMip += skippedTile ? 1 : -1;
			mipResolution *= skippedTile ? 0.5f : 2.0f;
			mipResolutionInv *= skippedTile ? 2.0f : 0.5f;
		}

		++numIters;
	}

	// Validate hit
	if (numIters < g_MaxHiZSteps && currentMip < 0)
	{
		float3 hitWorld;
		float hitDist;
		float confidence = HiZ_ValidateHit(
			depthPyramid, position, ssOrigin.xy, worldOrigin, worldDir,
			g_HiZThickness, maxDistance, hitWorld, hitDist);

		if (confidence > 0.01f)
		{
			result.hit = true;
			result.hitDistance = hitDist;

			// Sample radiance with distance-based mip for filtering
			float radianceMip = clamp(log2(max(hitDist * 0.1f, 1.0f)), 0.0f, 4.0f);
			result.hitRadiance = radiancePyramid.SampleLevel(g_LinearClampSampler, position.xy, radianceMip).rgb;
			result.hitRadiance *= confidence;
		}
	}

	return result;
}

#endif  // HIZ_TRACE_HLSLI
