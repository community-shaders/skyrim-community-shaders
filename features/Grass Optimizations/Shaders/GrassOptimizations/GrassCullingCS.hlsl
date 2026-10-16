#include "Common/GrassInstance.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"

cbuffer CullParams : register(b0)
{
	float4 FrustumPlanes[6];

	// PerFrame uses the light's camera during shadow passes, so retain the main camera's projection here.
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
	float OcclusionBias;
	float CostBiasStartDist;

	float FarLODPixelSize;
	float3 _pad0;
};

cbuffer CullBucket : register(b1)
{
	uint GroupCount;
	// SliceTable maps dispatched groups to visible source runs.
	uint SliceTableOffset;
	uint SliceCount;
	// Split large buckets across rows to stay within the 65,535-group dispatch limit.
	uint DispatchWidth;

	// Each LOD tier occupies TierStride records in Compacted.
	uint TierStride;
	uint OutputOffset;
	uint ArgsOffset;
	uint _bucketPad0;

	float BoundExtent;
	float ModelRadius;
	float DistScale;
	float MinPixelScale;

	float MidLODEnabled;
	float FarLODEnabled;
	float ComplexThreshold;
	float _bucketPad1;

	float WavePeriod;
	float TimeBase;
	float PrevTimeBase;
	float _bucketPad2;
};

ByteAddressBuffer Instances : register(t0);
// Source groups contain instance ranges and cell origins.
StructuredBuffer<GrassInstance::CullGroup> Groups : register(t1);
Texture2D<float> HiZ : register(t2);
// Each entry stores the run's first source group in .x and its first dispatch-group index in .y.
StructuredBuffer<uint2> SliceTable : register(t3);

// RunGrass resolves each output record against its source instance and group.
RWByteAddressBuffer Compacted : register(u0);
// Each tier has DrawIndexedInstanced arguments with its instance count in the second DWORD.
RWByteAddressBuffer Args : register(u1);

// Tiers select the full, middle and far meshes.
static const uint TierCount = 3;
static const uint ArgsStride = 20;
static const uint TierCulled = TierCount;

// Aggregate survivors within each group to issue one global atomic per occupied tier.
groupshared uint GroupTierCount[TierCount];
groupshared uint GroupTierBase[TierCount];

// Convert hashed bits to a uniform value in [0, 1).
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

// Return the LOD tier or TierCulled and populate the output record for surviving instances.
uint CullInstance(uint idx, GrassInstance::CullGroup group, out float2 wind, out float fade, out uint flags)
{
	wind = 0.0;
	fade = 0.0;
	flags = 0;

	float4 data1, data2, data3, data4;
	GrassInstance::Decode(Instances.Load4(idx * GrassInstance::Stride), data1, data2, data3, data4);

	const float2 localXY = data1.xy;
	const float localZ = data1.z;

	const float3 world = float3(localXY, localZ) + group.Origin;

	const float3 dv = world - CameraPos.xyz;
	const float distSq = dot(dv, dv);

	const float dist = sqrt(distSq);

	const float costRamp = saturate((dist - CostBiasStartDist) / max(CostBiasStartDist, 1e-4));
	const float effCostBias = MeshCostBias * costRamp;

	const float dScale = lerp(1.0, DistScale, effCostBias);
	const float effMaxDistSq = MaxDistSq * dScale * dScale;
	if (distSq > effMaxDistSq)
		return TierCulled;

	// Bounds include the mesh's offset from its root, scale variation and deformation padding.
	const float sizeVariance = data4.y;
	const float boundsRadius = BoundExtent * max(1.0, abs(1.0 + sizeVariance)) + 128.0;
	[unroll] for (uint p = 0; p < 6; ++p)
	{
		if (dot(FrustumPlanes[p].xyz, world) - FrustumPlanes[p].w < -boundsRadius)
			return TierCulled;
	}

	// Conservatively approximate the vertex shader's ScaleMask-based size variation.
	const float instanceRadius = ModelRadius * (1.0 + max(sizeVariance, 0.0));

	const float projPx = (instanceRadius / dist) * ProjScale;
	const float pxScale = lerp(1.0, MinPixelScale, effCostBias);
	const float effMinPx = MinPixelSize * pxScale;
	if (projPx < effMinPx)
		return TierCulled;

	// Hash world positions so compaction preserves density and LOD choices.
	const uint2 rand = Random::pcg2d(asuint(world.xy) ^ asuint(world.z));

	float lodFade = 1.0;
	const float effFullPx = FullDetailPixelSize * pxScale;
	if (projPx < effFullPx) {
		const float t = saturate((effFullPx - projPx) / max(effFullPx - effMinPx, 1e-4));
		const float keep = lerp(1.0, LODMinKeep, t);
		const float h = RandFloat(rand.x);
		if (h > keep + LODFadeBand)
			return TierCulled;
		lodFade = saturate((keep + LODFadeBand - h) / LODFadeBand);
	}

	const float maxDist = sqrt(effMaxDistSq);
	const float edgeStart = maxDist * EdgeFadeStart;
	const float edgeFade = saturate((maxDist - dist) / max(maxDist - edgeStart, 1e-4));

	const float4 clipOffset = float4(CameraViewProj[0].w, CameraViewProj[1].w, CameraViewProj[2].w, CameraViewProj[3].w);
	const float4 clip = ProjectDirection(dv) + clipOffset;
	const float distFade = 1.0 - saturate((length(clip.xyz) - AlphaParam1) / AlphaParam2);
	const float spawnFade = saturate((FadeNow - group.FadeStart) * FadeInTimeRcp);

	fade = distFade * spawnFade * lodFade * edgeFade;
	if (fade <= InvisibleFadeCull)
		return TierCulled;

	// Apply density and fade rejection before sampling depth.
	if (HiZEnabled > 0.5) {
		const float occRadius = boundsRadius;
		const float4 clipC = clip;
		const float minW = clipC.w - occRadius * ProjectionWLength;

		// A sphere crossing the camera plane has no finite projected bounds.
		[branch] if (minW > 1e-4)
		{
			const float3 ndc = clipC.xyz / clipC.w;
			// Bound the NDC change by the sphere radius times the projection gradient, divided by the minimum positive W.
			const float gradientX = length(CameraViewProj[0].xyz - ndc.x * CameraViewProj[3].xyz);
			const float gradientY = length(CameraViewProj[1].xyz - ndc.y * CameraViewProj[3].xyz);
			const float gradientZ = length(CameraViewProj[2].xyz - ndc.z * CameraViewProj[3].xyz);

			const float3 extent = (occRadius / minW) * float3(gradientX, gradientY, gradientZ);
			const float3 ndcLo = ndc - extent - 1e-5;
			const float3 ndcHi = ndc + extent + 1e-5;
			const float2 tcLo = saturate(float2(ndcLo.x, -ndcHi.y) * 0.5 + 0.5) * HiZSize;
			const float2 tcHi = saturate(float2(ndcHi.x, -ndcLo.y) * 0.5 + 0.5) * HiZSize;

			float wantLevel = ceil(log2(max(max(tcHi.x - tcLo.x, tcHi.y - tcLo.y), 1.0)));
			float scale = exp2(wantLevel);
			int2 t0 = int2(floor(tcLo / scale));
			int2 t1 = int2(floor(tcHi / scale));

			[branch] if (any(t1 - t0 > 1))
			{
				wantLevel += 1.0;
				scale *= 2.0;
				t0 = int2(floor(tcLo / scale));
				t1 = int2(floor(tcHi / scale));
			}

			// Near-plane intersections and footprints beyond the available pyramid remain visible.
			[branch] if (ndcLo.z > 0.0 && wantLevel <= HiZMipCount - 1.0)
			{
				const int level = (int)wantLevel;
				const int2 dimL = max(int2(ceil(HiZSize / scale)), int2(1, 1));
				const int2 lo = clamp(t0, int2(0, 0), dimL - 1);
				const int2 hi = clamp(t1, int2(0, 0), dimL - 1);
				const float tileMax = max(max(HiZ.Load(int3(lo, level)), HiZ.Load(int3(hi.x, lo.y, level))),
					max(HiZ.Load(int3(lo.x, hi.y, level)), HiZ.Load(int3(hi, level))));

				if (ndcLo.z > tileMax + OcclusionBias)
					return TierCulled;
			}
		}
	}

	// Dither both LOD transitions with the same value to keep tier changes gradual and ordered.
	const float h = RandFloat(rand.y);
	const float halfBand = MeshLODBandPx * 0.5;
	const float bandRcp = 1.0 / max(MeshLODBandPx, 1e-4);

	uint tier = 0;
	if (MidLODEnabled > 0.5 && h < saturate((MidLODPixelSize + halfBand - projPx) * bandRcp))
		tier = 1;
	if (FarLODEnabled > 0.5 && h < saturate((FarLODPixelSize + halfBand - projPx) * bandRcp))
		tier = 2;

	flags = tier << GrassInstance::FlagTierShift;
	if (distSq < CollisionDistSq)
		flags |= GrassInstance::FlagCollision;
	if (SimpleShadingPixelSize > 0.0 && projPx < SimpleShadingPixelSize)
		flags |= GrassInstance::FlagSimpleShading;

	wind = float2(GrassInstance::WindScalar(localXY, TimeBase * WavePeriod), GrassInstance::WindScalar(localXY, PrevTimeBase * WavePeriod));

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

	const uint groupIdx = groupId.y * DispatchWidth + groupId.x;
	if (groupIdx < GroupCount) {
		// Every thread in the group follows the same search for its visible source run.
		uint lo = 0;
		uint hi = SliceCount - 1;
		[loop] while (lo < hi)
		{
			const uint mid = (lo + hi + 1) >> 1;
			if (SliceTable[SliceTableOffset + mid].y <= groupIdx)
				lo = mid;
			else
				hi = mid - 1;
		}

		const uint2 run = SliceTable[SliceTableOffset + lo];
		groupIndex = run.x + (groupIdx - run.y);
		const GrassInstance::CullGroup group = Groups[groupIndex];

		if (threadIdx < group.Count)
			tier = CullInstance(group.SourceStart + threadIdx, group, wind, fade, flags);
	}

	uint localSlot = 0;
	if (tier != TierCulled)
		InterlockedAdd(GroupTierCount[tier], 1, localSlot);

	GroupMemoryBarrierWithGroupSync();

	if (threadIdx < TierCount && GroupTierCount[threadIdx] != 0) {
		uint base;
		Args.InterlockedAdd(ArgsOffset + threadIdx * ArgsStride + 4, GroupTierCount[threadIdx], base);
		GroupTierBase[threadIdx] = base;
	}

	GroupMemoryBarrierWithGroupSync();

	if (tier == TierCulled)
		return;

	const uint slot = OutputOffset + tier * TierStride + GroupTierBase[tier] + localSlot;
	const uint sourceIndex = Groups[groupIndex].SourceStart + threadIdx;
	const uint4 record = uint4(sourceIndex, GrassInstance::PackRecord(groupIndex, fade, flags), asuint(wind));
	Compacted.Store4(slot * GrassInstance::RecordStride, record);
}
