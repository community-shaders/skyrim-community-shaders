// Caches the rendered terrain surface for Low and Far roots in a world-aligned map.
// Export both the lift above LAND and the surface height that bounds it at detailed crests.
// Approach distance controls the generator's blend back to LAND.

#include "Common/FrameBuffer.hlsli"

#include "ProceduralGrass/PGrassCommon.hlsli"

Texture2D<float> TerrainHeightTexture : register(t0);
Texture2D<float> SceneDepth : register(t1);  // Full-resolution scene depth, including the High and Mid prepass.
RWTexture2D<float> MeasuredHeight : register(u0);
RWTexture2D<float> AppliedLift : register(u1);
RWTexture2D<float> AppliedHeight : register(u2);
SamplerState LinearSampler : register(s0);

static const float InvalidSurfaceHeight = 1.0e30f;
// Simplified LOD triangles can span a LAND cell across the loaded boundary.
static const float TerrainLodOverlapMargin = 4096.0f;

float SampleLandHeight(float2 world2D)
{
	return SampleTerrainHeight(TerrainHeightTexture, LinearSampler, world2D);
}

// LOD terrain vertices are LAND samples, so its surface cannot rise above the highest LAND around it.
static const float LodTerrainLiftMargin = 8.0f;
static const float2 LodTerrainVertexReach = float2(256.0f, 512.0f);

/**
 * @brief Returns the highest a LOD terrain surface can be at a position: the highest LAND within a LOD triangle's reach.
 * Flat-topped rocks and ledges pass the steep-face test, but their tops stand above all nearby LAND.
 */
float GetLodTerrainHeightBound(float2 world2D, float landHeight)
{
	static const float2 Directions[8] = { float2(1.0f, 0.0f), float2(0.70710678f, 0.70710678f), float2(0.0f, 1.0f), float2(-0.70710678f, 0.70710678f),
		float2(-1.0f, 0.0f), float2(-0.70710678f, -0.70710678f), float2(0.0f, -1.0f), float2(0.70710678f, -0.70710678f) };
	float highest = landHeight;
	[unroll] for (uint ring = 0u; ring < 2u; ++ring)
	{
		[unroll] for (uint direction = 0u; direction < 8u; ++direction)
			highest = max(highest, SampleLandHeight(world2D + Directions[direction] * LodTerrainVertexReach[ring]));
	}
	return highest + LodTerrainLiftMargin;
}

/** @brief Projects a camera-relative position; xy is its scene-depth pixel, z its depth, and w is positive when it is on screen. */
float4 ProjectToSceneDepth(float3 position)
{
	float4 clip = mul(FrameBuffer::CameraViewProj, float4(position, 1.0f));
	float2 uv = clip.xy / max(clip.w, 1.0f) * float2(0.5f, -0.5f) + 0.5f;
	return float4(uv / dynamicResolutionInverted, clip.z / max(clip.w, 1.0f), clip.w > 1.0f && all(uv >= 0.0f) && all(uv < 1.0f) ? 1.0f : -1.0f);
}

/** @brief Returns how far a position lies outside the loaded cells; negative inside. */
float DistanceOutsideLoadedLand(float2 world2D)
{
	float2 outside = max(loadedLandBounds.xy - world2D, world2D - loadedLandBounds.zw);
	return max(outside.x, outside.y);
}

/** @brief Reconstructs the world position at a scene-depth sample. */
float3 GetSurfacePosition(float2 pixel, float depth)
{
	float2 ndc = pixel * dynamicResolutionInverted * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float4 position = mul(FrameBuffer::CameraViewProjInverse, float4(ndc, depth, 1.0f));
	return position.xyz / position.w + FrameBuffer::CameraPosAdjust.xyz;
}

/** @brief Samples depth at the projected point without snapping the probe to a screen pixel. */
bool SampleSurfaceDepth(float3 position, out float4 probe, out float depth)
{
	probe = ProjectToSceneDepth(position);
	depth = 1.0f;
	if (probe.w < 0.0f || any(probe.xy < 2.0f) || any(probe.xy + 2.0f >= rcp(dynamicResolutionInverted)))
		return false;

	depth = SceneDepth.SampleLevel(LinearSampler, probe.xy * dynamicResolutionInverted, 0);
	return depth < 1.0f;
}

/**
 * @brief Finds the visible terrain height on the world-space vertical through a cache sample.
 * Returns the cached reference when foreground geometry or a silhouette prevents a measurement.
 */
float MeasureHeight(float2 world2D, float previous)
{
	const bool forwardPerspective = FrameBuffer::CameraProj._m32 == 1.0f && FrameBuffer::CameraProj._m33 == 0.0f && FrameBuffer::CameraProj._m23 < 0.0f;
	const float landHeight = SampleLandHeight(world2D);
	const float3 root = float3(world2D, landHeight) - FrameBuffer::CameraPosAdjust.xyz;
	float4 probe;
	float depth;
	if (!forwardPerspective || !SampleSurfaceDepth(root, probe, depth))
		return previous;

	float lower = 0.0f;
	float upper = 0.0f;

	// A root already in front of the scene needs no lift, but must still belong to the sampled terrain surface.
	// Otherwise bracket the surface within the existing lift bound.
	[branch] if (probe.z > depth)
	{
		upper = TerrainLiftMax;
		if (!SampleSurfaceDepth(root + float3(0.0f, 0.0f, upper), probe, depth) || probe.z > depth)
			return previous;

		[unroll] for (uint iteration = 0u; iteration < 8u; ++iteration)
		{
			float middle = (lower + upper) * 0.5f;
			if (!SampleSurfaceDepth(root + float3(0.0f, 0.0f, middle), probe, depth))
				return previous;
			if (probe.z > depth)
				lower = middle;
			else
				upper = middle;
		}
	}

	// Validate the surface at the intersection and retain the reference at silhouettes or object faces.
	if (!SampleSurfaceDepth(root + float3(0.0f, 0.0f, upper), probe, depth))
		return previous;
	float3 scenePosition = GetSurfacePosition(probe.xy, depth);
	const float sceneLift = scenePosition.z - SampleLandHeight(scenePosition.xy);
	if (DistanceOutsideLoadedLand(scenePosition.xy) <= -TerrainLodOverlapMargin || sceneLift < -8.0f || sceneLift > TerrainLiftMax)
		return previous;
	if (dot(scenePosition.xy - world2D, scenePosition.xy - world2D) > 128.0f * 128.0f)
		return previous;

	float4 depths = float4(
		SceneDepth.SampleLevel(LinearSampler, (probe.xy + float2(-1.0f, -1.0f)) * dynamicResolutionInverted, 0),
		SceneDepth.SampleLevel(LinearSampler, (probe.xy + float2(1.0f, -1.0f)) * dynamicResolutionInverted, 0),
		SceneDepth.SampleLevel(LinearSampler, (probe.xy + float2(-1.0f, 1.0f)) * dynamicResolutionInverted, 0),
		SceneDepth.SampleLevel(LinearSampler, (probe.xy + float2(1.0f, 1.0f)) * dynamicResolutionInverted, 0));
	if (any(depths >= 1.0f))
		return previous;

	float3 corner = GetSurfacePosition(probe.xy + float2(-1.0f, -1.0f), depths.x);
	float3 right = GetSurfacePosition(probe.xy + float2(1.0f, -1.0f), depths.y);
	float3 down = GetSurfacePosition(probe.xy + float2(-1.0f, 1.0f), depths.z);
	float3 diagonal = GetSurfacePosition(probe.xy + float2(1.0f, 1.0f), depths.w);
	float3 normal = cross(right - corner, down - corner);
	float normalLengthSquared = dot(normal, normal);
	if (normalLengthSquared < 1.0e-8f || normal.z * normal.z < 0.5f * normalLengthSquared ||
		abs(dot(normal, diagonal - corner)) > LodTerrainLiftMargin * abs(normal.z))
		return previous;

	return landHeight + upper + 4.0f;
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	const int2 texel = dispatchID.xy;
	// Each texel holds the one cell of the camera-centred window that wraps onto it.
	const int2 cell = terrainLiftOrigin.xy + ((texel - terrainLiftOrigin.xy) & (TerrainLiftDim - 1));
	const int2 previousCell = terrainLiftOrigin.zw + ((texel - terrainLiftOrigin.zw) & (TerrainLiftDim - 1));
	const float2 world2D = (float2(cell) + 0.5f) * TerrainLiftCellSize;
	const float outsideDistance = DistanceOutsideLoadedLand(world2D);

	// A wrapped texel knows nothing about its new cell. Retain the LOD reference as LAND loads.
	const bool wrapped = any(cell != previousCell);
	float measured = wrapped ? InvalidSurfaceHeight : MeasuredHeight[texel];
	const float2 referenceOffset = abs(world2D - grassLodOrigin);
	const bool needsReference = GetTerrainLiftBlend(max(referenceOffset.x, referenceOffset.y)) > 0.0f;
	if (needsReference && outsideDistance > -TerrainLodOverlapMargin && (outsideDistance > 0.0f || measured >= InvalidSurfaceHeight) && (uint(texel.x & 1) | uint(texel.y & 1) << 1) == terrainLiftPhase) {
		// Keep a known reference after LAND loads, so only approach distance lowers its roots.
		measured = MeasureHeight(world2D, measured);
	}
	MeasuredHeight[texel] = measured;

	// Unknown cells stay on LAND. The generator caps interpolated lift at the cached surface height.
	const bool valid = measured < InvalidSurfaceHeight;
	const float landHeight = SampleLandHeight(world2D);
	float appliedHeight = valid ? measured : landHeight;
	[branch] if (appliedHeight > landHeight + LodTerrainLiftMargin)
	{
		appliedHeight = min(appliedHeight, GetLodTerrainHeightBound(world2D, landHeight));
	}
	AppliedLift[texel] = max(appliedHeight - landHeight, 0.0f);
	AppliedHeight[texel] = appliedHeight;
}
