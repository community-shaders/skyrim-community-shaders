#include "Common/GrassInstance.hlsli"
#include "Common/Random.hlsli"

cbuffer CullParams : register(b0)
{
	float4 FrustumPlanes[6];

	// Retain the main projection when shadow passes replace PerFrame.
	float4 CameraViewProj[4];
	float4 CameraPos;

	float MinPixelSize;
	float FullDetailPixelSize;
	float LODMinKeep;
	float LODFadeBand;

	float MeshCostBias;
	float ProjScale;
	float MaxDistSq;
	float EdgeFadeStart;

	float AlphaParam1;
	float AlphaParam2;
	float FadeNow;
	float FadeInTimeRcp;

	float InvisibleFadeCull;
	float SimpleShadingPixelSize;
	float CollisionDistSq;
	float MidLODPixelSize;

	float MeshLODBandPx;
	float HiZEnabled;
	float2 HiZSize;

	float ProjectionWLength;
	float HiZMipCount;
	// Separation behind an occluder as a fraction of its view depth.
	float OcclusionDepthBias;
	float CostBiasStartDist;

	float FarLODPixelSize;
	// Precomputed sqrt(MaxDistSq) for edge fading.
	float MaxDist;
	// Values below 1 shrink the occlusion sphere for more aggressive culling.
	float OcclusionTightness;
	// Perspective depth is NDCDepthAtInfinity + NDCDepthNumerator / w.
	float NDCDepthAtInfinity;

	float NDCDepthNumerator;
	float3 _pad0;
};

// CullBucketCB fields followed by the dispatch range.
struct CullBucket
{
	uint GroupCount;
	uint SliceTableOffset;
	uint SliceCount;
	uint GroupBase;
	uint TierStride;
	uint OutputOffset;
	uint ArgsOffset;
	float OcclusionRadius;
	float BoundExtent;
	float ModelRadius;
	float DistScale;
	float MinPixelScale;
	float MidLODEnabled;
	float FarLODEnabled;
	float ComplexThreshold;
	float MidLODMeshCost;
	float WavePeriod;
	float TimeBase;
	float PrevTimeBase;
	float FarLODMeshCost;
	float3 CanopyHeights;
	// Lowest alpha threshold among depth-writing draws.
	float AlphaTestRef;
	uint2 _entryPad0;
	// Buckets are sorted by DispatchStart for lookup.
	uint DispatchStart;
	uint _entryPad1;
};
StructuredBuffer<CullBucket> Buckets : register(t4);

cbuffer DispatchParams : register(b1)
{
	uint BucketCount;
	uint DispatchGroupCount;
	uint DispatchRowWidth;
	uint _dispatchPad;
};

// All threads in a group share one bucket.
static CullBucket Bucket;

ByteAddressBuffer Instances : register(t0);
StructuredBuffer<GrassInstance::CullGroup> Groups : register(t1);
Texture2D<float> HiZ : register(t2);
// x and y are the run's first source and dispatch group indices.
StructuredBuffer<uint2> SliceTable : register(t3);

// RunGrass resolves each output record against its source instance and group.
RWByteAddressBuffer Compacted : register(u0);
// Each tier's DrawIndexedInstanced arguments store the instance count at byte offset 4.
RWByteAddressBuffer Args : register(u1);

static const uint TierCount = 3;
static const uint ArgsStride = 20;
static const uint TierCulled = TierCount;

// Reserve output once per occupied tier to reduce global atomics.
groupshared uint GroupTierCount[TierCount];
groupshared uint GroupTierBase[TierCount];

float RandFloat(uint bits)
{
	const uint mantissaMask = 0x007FFFFFu;
	const uint one = 0x3F800000u;

	bits &= mantissaMask;
	bits |= one;

	return asfloat(bits) - 1.0;
}

float4 ProjectDirection(float3 v)
{
	return float4(dot(CameraViewProj[0].xyz, v), dot(CameraViewProj[1].xyz, v), dot(CameraViewProj[2].xyz, v), dot(CameraViewProj[3].xyz, v));
}

// Invalid clip W must not occlude grass.
float HiZViewDepth(float ndcDepth)
{
	const float w = NDCDepthNumerator / (ndcDepth - NDCDepthAtInfinity);
	return w > 0.0 ? w : 3.402823466e+38;
}

bool OccludedSphere(float3 centerDV, float radius)
{
	const float4 clipC = ProjectDirection(centerDV) + float4(CameraViewProj[0].w, CameraViewProj[1].w, CameraViewProj[2].w, CameraViewProj[3].w);
	const float minW = clipC.w - radius * ProjectionWLength;

	// Keep camera-plane intersections visible. [branch] causes fxc X4000 here.
	if (minW <= 1e-4)
		return false;

	const float3 ndc = clipC.xyz / clipC.w;
	// Bound projected extents using the projection gradient and nearest positive W.
	const float gradientX = length(CameraViewProj[0].xyz - ndc.x * CameraViewProj[3].xyz);
	const float gradientY = length(CameraViewProj[1].xyz - ndc.y * CameraViewProj[3].xyz);
	const float gradientZ = length(CameraViewProj[2].xyz - ndc.z * CameraViewProj[3].xyz);

	const float3 extent = (radius / minW) * float3(gradientX, gradientY, gradientZ);
	const float3 ndcLo = ndc - extent - 1e-5;
	const float3 ndcHi = ndc + extent + 1e-5;

	[branch] if (ndcLo.z <= 0.0) return false;

	const float2 tcLo = saturate(float2(ndcLo.x, -ndcHi.y) * 0.5 + 0.5) * HiZSize;
	const float2 tcHi = saturate(float2(ndcHi.x, -ndcLo.y) * 0.5 + 0.5) * HiZSize;

	// This mip covers the footprint with at most 2x2 texels.
	const float wantLevel = ceil(log2(max(max(tcHi.x - tcLo.x, tcHi.y - tcLo.y), 1.0)));

	[branch] if (wantLevel > HiZMipCount - 1.0) return false;

	const float scale = exp2(wantLevel);
	const int level = (int)wantLevel;
	const int2 dimL = max(int2(ceil(HiZSize / scale)), int2(1, 1));
	const int2 lo = clamp(int2(floor(tcLo / scale)), int2(0, 0), dimL - 1);
	const int2 hi = clamp(int2(floor(tcHi / scale)), int2(0, 0), dimL - 1);
	const float tileMax = max(max(HiZ.Load(int3(lo, level)), HiZ.Load(int3(hi.x, lo.y, level))),
		max(HiZ.Load(int3(lo.x, hi.y, level)), HiZ.Load(int3(hi, level))));

	// View-space bias stays proportional to occluder depth.
	if (minW > HiZViewDepth(tileMax) * (1.0 + OcclusionDepthBias))
		return true;

	// Refining by two mips covers at most 5x5 texels and requires a negative inverse-depth coefficient.
	[branch] if (wantLevel < 2.0 || NDCDepthNumerator >= 0.0) return false;

	// Compare in NDC to avoid a division for each texel.
	const float ndcLimit = NDCDepthAtInfinity + NDCDepthNumerator * (1.0 + OcclusionDepthBias) / minW;
	const int fineLevel = level - 2;
	const float fineScale = scale * 0.25;

	const int2 dimF = max(int2(ceil(HiZSize / fineScale)), int2(1, 1));
	const int2 fineLo = clamp(int2(floor(tcLo / fineScale)), int2(0, 0), dimF - 1);
	const int2 fineHi = clamp(int2(floor(tcHi / fineScale)), int2(0, 0), dimF - 1);

	[loop] for (int y = fineLo.y; y <= fineHi.y; ++y)
	{
		[loop] for (int x = fineLo.x; x <= fineHi.x; ++x)
		{
			if (HiZ.Load(int3(x, y, fineLevel)) >= ndcLimit)
				return false;
		}
	}
	return true;
}

uint CullInstance(uint idx, GrassInstance::CullGroup group, out float2 wind, out float fade, out uint flags)
{
	wind = 0.0;
	fade = 0.0;
	flags = 0;

	float4 data1, data2, data3, data4;
	GrassInstance::Decode(Instances.Load4(idx * GrassInstance::Stride), data1, data2, data3, data4);

	const float3 world = data1.xyz + group.Origin;

	const float3 dv = world - CameraPos.xyz;
	const float distSq = dot(dv, dv);
	if (distSq > MaxDistSq)
		return TierCulled;

	// Frustum bounds include the mesh offset, scaling and deformation.
	const float sizeVariance = data4.y;
	const float boundsRadius = Bucket.BoundExtent * max(1.0, abs(1.0 + sizeVariance)) + 128.0;

	[unroll] for (uint p = 0; p < 6; ++p)
	{
		if (dot(FrustumPlanes[p].xyz, world) - FrustumPlanes[p].w < -boundsRadius)
			return TierCulled;
	}

	const float dist = sqrt(distSq);

	// Conservatively bound the vertex shader's size variation.
	const float instanceRadius = Bucket.ModelRadius * (1.0 + max(sizeVariance, 0.0));

	// Use view depth to keep projected size consistent across the image.
	const float clipW = dot(CameraViewProj[3].xyz, dv) + CameraViewProj[3].w;
	const float projPx = (instanceRadius * ProjectionWLength / max(clipW, 1e-4)) * ProjScale;

	// Seed from world position so compaction cannot change density or LOD choices.
	const uint2 rand = Random::pcg2d(asuint(world.xy) ^ asuint(world.z));

	const float h = RandFloat(rand.y);
	const float halfBand = MeshLODBandPx * 0.5;
	const float bandRcp = 1.0 / max(MeshLODBandPx, 1e-4);

	uint tier = 0;
	if (Bucket.MidLODEnabled > 0.5 && h < saturate((MidLODPixelSize + halfBand - projPx) * bandRcp))
		tier = 1;
	if (Bucket.FarLODEnabled > 0.5 && h < saturate((FarLODPixelSize + halfBand - projPx) * bandRcp))
		tier = 2;

	const float meshCost = tier == 1 ? Bucket.MidLODMeshCost : (tier == 2 ? Bucket.FarLODMeshCost : Bucket.MinPixelScale);
	const float costRamp = saturate((dist - CostBiasStartDist) / max(CostBiasStartDist, 1e-4));

	const float effCostBias = MeshCostBias * costRamp;
	const float dScale = lerp(1.0, tier == 0 ? Bucket.DistScale : rcp(meshCost), effCostBias);
	const float effMaxDistSq = MaxDistSq * dScale * dScale;

	if (distSq > effMaxDistSq)
		return TierCulled;

	const float pxScale = lerp(1.0, meshCost, effCostBias);
	const float effMinPx = MinPixelSize * pxScale;

	if (projPx < effMinPx)
		return TierCulled;

	float lodFade = 1.0;
	const float effFullPx = FullDetailPixelSize * pxScale;

	if (projPx < effFullPx) {
		const float ratio = saturate((effFullPx - projPx) / max(effFullPx - effMinPx, 1e-4));
		const float keep = lerp(1.0, LODMinKeep, ratio);
		const float densityRandom = RandFloat(rand.x);

		// Scale the fade band with density to avoid retaining too many fading instances.
		const float cutoff = keep * (1.0 + LODFadeBand);
		if (densityRandom > cutoff)
			return TierCulled;

		lodFade = saturate((cutoff - densityRandom) / max(keep * LODFadeBand, 1e-4));
	}

	const float maxDist = MaxDist * abs(dScale);
	const float edgeStart = maxDist * EdgeFadeStart;
	const float edgeFade = saturate((maxDist - dist) / max(maxDist - edgeStart, 1e-4));

	const float4 clipOffset = float4(CameraViewProj[0].w, CameraViewProj[1].w, CameraViewProj[2].w, CameraViewProj[3].w);
	const float4 clip = ProjectDirection(dv) + clipOffset;
	const float distFade = 1.0 - saturate((length(clip.xyz) - AlphaParam1) / AlphaParam2);
	const float spawnFade = saturate((FadeNow - group.FadeStart) * FadeInTimeRcp);

	fade = distFade * spawnFade * lodFade * edgeFade;
	if (fade <= InvisibleFadeCull)
		return TierCulled;

	// Match the vertex shader's 9-bit fade and allow for interpolation rounding.
	const float storedFade = round(saturate(fade) * GrassInstance::RecordFadeMask) / GrassInstance::RecordFadeMask;
	if (storedFade < Bucket.AlphaTestRef - 1e-3)
		return TierCulled;

	// Occlusion uses the canopy sphere without frustum deformation padding.
	[branch] if (HiZEnabled > 0.5)
	{
		const float canopyMid = 0.5 * (tier == 0 ? Bucket.CanopyHeights.x : (tier == 1 ? Bucket.CanopyHeights.y : Bucket.CanopyHeights.z));
		const float3 canopyOffset = canopyMid * (1.0 + sizeVariance) * float3(data2.z, data3.z, data3.w);

		const float3 occCenter = dv + canopyOffset;
		const float occRadius = Bucket.OcclusionRadius * max(1.0, abs(1.0 + sizeVariance));

		if (OccludedSphere(occCenter, occRadius * OcclusionTightness))
			return TierCulled;
	}

	flags = tier << GrassInstance::FlagTierShift;
	if (distSq < CollisionDistSq)
		flags |= GrassInstance::FlagCollision;

	if (SimpleShadingPixelSize > 0.0 && projPx < SimpleShadingPixelSize)
		flags |= GrassInstance::FlagSimpleShading;

	wind = float2(GrassInstance::WindScalar(data1.xy, Bucket.TimeBase * Bucket.WavePeriod), GrassInstance::WindScalar(data1.xy, Bucket.PrevTimeBase * Bucket.WavePeriod));

	return tier;
}

[numthreads(64, 1, 1)] void main(uint3 groupId : SV_GroupID, uint threadIdx : SV_GroupIndex) {
	if (threadIdx < TierCount)
		GroupTierCount[threadIdx] = 0;

	GroupMemoryBarrierWithGroupSync();

	uint tier = TierCulled;
	uint groupIndex = 0;
	float2 wind = 0.0;
	float fade = 0.0;
	uint flags = 0;

	GrassInstance::CullGroup group = (GrassInstance::CullGroup)0;
	const uint dispatchIdx = groupId.y * DispatchRowWidth + groupId.x;
	bool activeGroup = dispatchIdx < DispatchGroupCount;
	uint groupIdx = 0;

	if (activeGroup) {
		uint first = 0;
		uint last = BucketCount - 1;
		[loop] while (first < last)
		{
			const uint mid = (first + last + 1) >> 1;
			if (Buckets[mid].DispatchStart <= dispatchIdx)
				first = mid;
			else
				last = mid - 1;
		}

		Bucket = Buckets[first];
		groupIdx = dispatchIdx - Bucket.DispatchStart;
		activeGroup = groupIdx < Bucket.GroupCount;
	}

	if (activeGroup) {
		uint lo = 0;
		uint hi = Bucket.SliceCount - 1;
		[loop] while (lo < hi)
		{
			const uint mid = (lo + hi + 1) >> 1;
			if (SliceTable[Bucket.SliceTableOffset + mid].y <= groupIdx)
				lo = mid;
			else
				hi = mid - 1;
		}

		const uint2 run = SliceTable[Bucket.SliceTableOffset + lo];
		groupIndex = run.x + (groupIdx - run.y);
		group = Groups[Bucket.GroupBase + groupIndex];
	}

	if (activeGroup && threadIdx < group.Count)
		tier = CullInstance(group.SourceStart + threadIdx, group, wind, fade, flags);

	uint localSlot = 0;
	if (tier != TierCulled)
		InterlockedAdd(GroupTierCount[tier], 1, localSlot);

	GroupMemoryBarrierWithGroupSync();

	if (threadIdx < TierCount && GroupTierCount[threadIdx] != 0) {
		uint base;
		Args.InterlockedAdd(Bucket.ArgsOffset + threadIdx * ArgsStride + 4, GroupTierCount[threadIdx], base);
		GroupTierBase[threadIdx] = base;
	}

	GroupMemoryBarrierWithGroupSync();

	if (tier == TierCulled)
		return;

	const uint slot = Bucket.OutputOffset + tier * Bucket.TierStride + GroupTierBase[tier] + localSlot;
	// Instance indices are absolute, while group indices are relative to the bucket.
	const uint sourceIndex = group.SourceStart + threadIdx;
	const uint4 record = uint4(sourceIndex, GrassInstance::PackRecord(groupIndex, fade, flags), asuint(wind));
	Compacted.Store4(slot * GrassInstance::RecordStride, record);
}
