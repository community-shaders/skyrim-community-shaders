#include "SnowDeformation/SnowNoise.hlsli"

#define MAX_STAMPS 256

#define STAMP_MODE_CONE 10.0
#define STAMP_MODE_BOWL 20.0
#define STAMP_MODE_BODY 40.0
#define STAMP_IMPACT_SCALE 0.45
#define CARVE_BOWL_FLOOR 0.0
#define CONE_FRONT_LEAD 0.1

#define DRIFT_FETCH_TEXELS 3.0
#define MELT_NOISE_COARSE 80.0
#define MELT_NOISE_FINE 24.0
#define PIT_LOBES 5
#define PIT_LOBE_MIN 0.45
#define PIT_LOBE_MAX 1.15
#define PIT_LOBE_WIDTH 0.16
#define PIT_POCK_CELLS_ACROSS 4.5
#define PIT_POCK_CELL_MIN 6.0
#define DRIFT_GAIN 2.0

#define SLUMP_RADII 3
#define SLUMP_GATE_RADII 1
static const float kSlumpRadius[SLUMP_RADII] = { 16.0, 32.0, 64.0 };
static const float kSlumpReach[SLUMP_RADII] = { 1.0, 0.85, 0.7 };
#define SLUMP_SETTLE 0.5
#define SLUMP_MIN_SUPPORT 0.5
#define SLUMP_AXES 8
static const float2 kSlumpAxis[SLUMP_AXES] = {
	float2(1.0, 0.0), float2(0.9239, 0.3827), float2(0.7071, 0.7071), float2(0.3827, 0.9239),
	float2(0.0, 1.0), float2(-0.3827, 0.9239), float2(-0.7071, 0.7071), float2(-0.9239, 0.3827)
};
#define SLUMP_SPEED 0.5
#define SLUMP_FLOOR_NOISE 0.45

cbuffer PerFrame : register(b0)
{
#include "SnowDeformation/DeformationFrame.hlsli"

	int4 RingRects[2];
	uint RingRectCount;
	uint RingTotalTexels;
	uint ForceAllDirty;
	float TrackRebase;

	float4 Stamps[MAX_STAMPS];
	float4 StampEnds[MAX_STAMPS];
	float4 StampCones[MAX_STAMPS];

	float2 ContactCenter;
	float ContactHalfExtent;
	float ContactDim;
	float2 TerrainWindowOrigin;
	float TerrainTexelSize;
	float TerrainDim;
	float2 ViewCenter;
	float ViewHalf;
	float BodyPrint;
	float4 RoadRasterPad;
	uint PassClock;
	uint PassGap;
	uint2 PassPad;
	float4 ShoutShape;
	float4 ShoutChurn;
	float UndulationAmp;
	float UndulationScale;
	float AccumulationScale;
	float DrawnPad;
}

Texture2D<float4> PreviousDeformation : register(t0);
RWTexture2D<float4> CurrentDeformation : register(u0);
RWTexture2D<float2> EvolveSnapshot : register(u0);
#define TRACK_AGE_HORIZON 128.0
RWTexture2D<float> TrackClock : register(u4);
static const float kTrackCutMin = 0.02;
static const float kTrackFloorMargin = 0.1;
StructuredBuffer<uint> StampTiles : register(t2);

RWTexture2D<uint> Occupancy : register(u3);
Texture2D<uint> OccupancyIn : register(t4);
RWStructuredBuffer<uint> EvolveTiles : register(u4);
StructuredBuffer<uint> EvolveTilesIn : register(t3);
Texture2D<float2> ContactHeight : register(t7);
Texture2D<float4> TerrainWindow : register(t8);
RWTexture2D<uint> PassState : register(u7);
RWTexture2D<unorm float> BodyReachMax : register(u5);
#define CONTACT_NONE 1.0e30
RWByteAddressBuffer TileArgs : register(u5);

RWTexture2D<uint> BermDirty : register(u6);
Texture2D<uint> BermDirtyIn : register(t6);
RWStructuredBuffer<uint> BermTiles : register(u7);

int2 TorusPhys(int2 logical, int2 dims)
{
	return (logical + MapOrigin) & (dims - 1);
}

RWByteAddressBuffer ActivityFlag : register(u1);
groupshared uint gActivity;
groupshared uint gCountR;
groupshared uint gCountG;
groupshared uint gCountB;
groupshared uint gCMinX;
groupshared uint gCMinY;
groupshared uint gMaxX;
groupshared uint gMaxY;
groupshared uint gDeltaSum;
groupshared uint gNonZero;
groupshared uint gClockMoved;

float4 StoredDelta(float4 a, float4 b)
{
	float4 d = abs(a - b);
	float4 tol = max(abs(a), abs(b)) * exp2(-11.0) + 1e-6;
	return max(d - tol, 0.0);
}

void ActivityReset(uint GIdx)
{
	if (GIdx == 0) {
		gActivity = 0;
		gCountR = 0;
		gCountG = 0;
		gCountB = 0;
		gCMinX = 0;
		gCMinY = 0;
		gMaxX = 0;
		gMaxY = 0;
		gDeltaSum = 0;
		gNonZero = 0;
		gClockMoved = 0;
	}
	GroupMemoryBarrierWithGroupSync();
}

void ActivityAccumulate(float4 delta, uint2 pixel)
{
	if (any(delta > 0.0)) {
		InterlockedAdd(gActivity, 1u);
		if (delta.x > 0.0)
			InterlockedAdd(gCountR, 1u);
		if (delta.y > 0.0)
			InterlockedAdd(gCountG, 1u);
		if (max(delta.z, delta.w) > 0.0)
			InterlockedAdd(gCountB, 1u);
		InterlockedMax(gCMinX, 65535u - pixel.x);
		InterlockedMax(gCMinY, 65535u - pixel.y);
		InterlockedMax(gMaxX, pixel.x);
		InterlockedMax(gMaxY, pixel.y);
		float maxDelta = max(max(delta.x, delta.y), max(delta.z, delta.w));
		InterlockedAdd(gDeltaSum, min((uint)(maxDelta * 1e6 + 0.5), 1000000u));
	}
}

void ActivityPublish(uint GIdx)
{
	GroupMemoryBarrierWithGroupSync();
	if (GIdx == 0 && gActivity != 0) {
		ActivityFlag.InterlockedOr(0, 1u);
		ActivityFlag.InterlockedAdd(4, gActivity);
		uint unused;
		ActivityFlag.InterlockedMax(8, gCMinX, unused);
		ActivityFlag.InterlockedMax(12, gCMinY, unused);
		ActivityFlag.InterlockedMax(16, gMaxX, unused);
		ActivityFlag.InterlockedMax(20, gMaxY, unused);
		ActivityFlag.InterlockedAdd(24, gCountR);
		ActivityFlag.InterlockedAdd(28, gCountG);
		ActivityFlag.InterlockedAdd(32, gCountB);
		ActivityFlag.InterlockedAdd(36, gDeltaSum);
	}
}

float StampNoiseHash(float2 cell)
{
	cell -= 512.0 * floor(cell / 512.0);
	float3 p3 = frac(float3(cell.x, cell.y, cell.x) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.x + p3.y) * p3.z);
}

float StampNoise(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(StampNoiseHash(i), StampNoiseHash(i + float2(1, 0)), f.x),
		lerp(StampNoiseHash(i + float2(0, 1)), StampNoiseHash(i + float2(1, 1)), f.x), f.y);
}

float SlumpTap(int2 p, int2 dims)
{
	if (any(p < 0) || any(p >= dims))
		return 0.0;
	float4 t = PreviousDeformation[uint2(TorusPhys(p, dims))];
	return saturate(t.x - abs(t.y));
}

[numthreads(64, 1, 1)] void RingCS(uint3 DTid
								   : SV_DispatchThreadID) {
	uint id = DTid.x;
	if (id >= RingTotalTexels || RingRectCount == 0)
		return;

	uint2 dims;
	CurrentDeformation.GetDimensions(dims.x, dims.y);

	int4 rect = RingRects[0];
	const uint rect0Texels = (uint)(rect.z * rect.w);
	if (id >= rect0Texels) {
		id -= rect0Texels;
		rect = RingRects[1];
	}
	const int2 logical = int2(rect.x + (int)(id % (uint)rect.z), rect.y + (int)(id / (uint)rect.z));

	const uint2 phys = uint2(TorusPhys(logical, int2(dims)));
	CurrentDeformation[phys] = 0.0;
	TrackClock[phys] = 0.0;
	BodyReachMax[phys] = 0.0;
	BermDirty[phys >> 3] = 1u;
}

RWTexture2D<float4> BloodMapRing : register(u0);
RWTexture2D<float2> BloodClockRing : register(u1);

[numthreads(64, 1, 1)] void BloodRingCS(uint3 DTid
										: SV_DispatchThreadID) {
	uint id = DTid.x;
	if (id >= RingTotalTexels || RingRectCount == 0)
		return;
	uint2 dims;
	BloodMapRing.GetDimensions(dims.x, dims.y);
	int4 rect = RingRects[0];
	const uint rect0Texels = (uint)(rect.z * rect.w);
	if (id >= rect0Texels) {
		id -= rect0Texels;
		rect = RingRects[1];
	}
	const int2 logical = int2(rect.x + (int)(id % (uint)rect.z), rect.y + (int)(id / (uint)rect.z));
	const uint2 phys = uint2(TorusPhys(logical, int2(dims)));
	BloodMapRing[phys] = float4(0.0, 0.0, 0.0, 0.0);
	BloodClockRing[phys] = float2(0.0, 0.0);
}

RWTexture2D<float> TrackClockRebase : register(u0);
RWTexture2D<float4> BermFieldRebase : register(u1);

[numthreads(8, 8, 1)] void TrackRebaseCS(uint3 DTid
										 : SV_DispatchThreadID) {
	uint2 dims;
	TrackClockRebase.GetDimensions(dims.x, dims.y);
	if (any(DTid.xy >= dims))
		return;
	const float cut = TrackClockRebase[DTid.xy];
	[branch] if (cut > 0.0)
		TrackClockRebase[DTid.xy] = max(cut - TrackRebase, 1.0);
	float4 berm = BermFieldRebase[DTid.xy];
	[branch] if (max(berm.z, berm.w) > 0.0)
	{
		berm.zw = berm.zw > 0.0 ? max(berm.zw - TrackRebase * berm.yx, berm.yx) : 0.0;
		BermFieldRebase[DTid.xy] = berm;
	}
}

bool EvolveTexel(uint2 phys)
{
	uint2 dims;
	PreviousDeformation.GetDimensions(dims.x, dims.y);
	const uint2 pixel = uint2((int2(phys) - MapOrigin) & (int2(dims) - 1));

	float2 worldPos = WindowOrigin + (float2(pixel) + 0.5) * TexelSize;

	const float4 carried = CurrentDeformation[phys];
	float deformation = carried.x;
	float melted = carried.y;
	float crust = carried.z;
	float impact = carried.w;

	{
		float refill = RefillAmount;
		float windStrength = length(WindBias);
		[branch] if (refill > 0.0 && windStrength > 0.001)
		{
			int2 upwindPixel = int2(pixel) - int2(round(WindBias / windStrength * DRIFT_FETCH_TEXELS));
			float upwindDeformation = deformation;
			[branch] if (all(upwindPixel >= 0) && all(upwindPixel < int2(dims)))
			{
				upwindDeformation = PreviousDeformation[uint2(TorusPhys(upwindPixel, int2(dims)))].x;
			}
			refill *= lerp(1.0, (1.0 - upwindDeformation) * DRIFT_GAIN, windStrength);
		}
		refill *= lerp(1.0, 1.0 - saturate(MeltPersistence), saturate(max(melted, 0.0) / max(deformation, 1e-4)));

		deformation = max(deformation - refill, 0.0);
		melted = melted >= 0.0 ? min(max(melted - refill, 0.0), deformation) :
		                         min(melted + refill, 0.0);
		crust = max(crust - refill - CrustThaw * GameDeltaTime, 0.0);
		impact = max(impact - refill, 0.0);

		[branch] if (SlumpRate > 0.001 && deformation < 0.999 && abs(melted) < 0.005)
		{
			float slumpTarget = 0.0;
			[unroll] for (uint axis = 0; axis < SLUMP_AXES; axis++)
			{
				float gate = 0.0;
				float axisTarget = 0.0;
				[unroll] for (uint r = 0; r < SLUMP_RADII; r++)
				{
					int2 off = int2(round(kSlumpAxis[axis] * (kSlumpRadius[r] / max(TexelSize, 1e-4))));
					float support = min(SlumpTap(int2(pixel) + off, int2(dims)),
						SlumpTap(int2(pixel) - off, int2(dims)));
					if (r < SLUMP_GATE_RADII)
						gate = max(gate, support);
					axisTarget = max(axisTarget, support * kSlumpReach[r]);
				}
				if (gate > SLUMP_MIN_SUPPORT)
					slumpTarget = max(slumpTarget, axisTarget);
			}
			slumpTarget *= SLUMP_SETTLE * (1.0 - SLUMP_FLOOR_NOISE * StampNoise(worldPos / MELT_NOISE_COARSE));
			[branch] if (slumpTarget - deformation > max(slumpTarget, deformation) * exp2(-10.0) + 1e-6)
			{
				deformation = min(deformation + SlumpRate * SLUMP_SPEED * (1.0 - crust) * GameDeltaTime,
					slumpTarget);
			}
		}
	}

	float4 result = float4(deformation, melted, crust, impact);
	CurrentDeformation[phys] = result;

	ActivityAccumulate(StoredDelta(result, carried), pixel);
	return any(result != 0.0);
}

[numthreads(8, 8, 1)] void EvolveCS(uint3 GTid
									: SV_GroupThreadID, uint3 Gid
									: SV_GroupID, uint GIdx
									: SV_GroupIndex) {
	const uint listIndex = Gid.y * 1024u + Gid.x;
	if (listIndex >= EvolveTilesIn[0])
		return;

	ActivityReset(GIdx);
	const uint packed = EvolveTilesIn[1 + listIndex];
	const uint2 tile = uint2(packed & 0xFFFFu, packed >> 16u);
	if (EvolveTexel(tile * 8u + GTid.xy))
		InterlockedOr(gNonZero, 1u);
	ActivityPublish(GIdx);
	if (GIdx == 0) {
		Occupancy[tile] = gNonZero;
		if (gActivity != 0) {
			BermDirty[tile] = 1u;
			ActivityFlag.InterlockedOr(40, 1u);
		}
	}
}

bool ScanFinish(uint GIdx, uint2 dims, out uint count)
{
	count = 0;
	bool last = false;
	DeviceMemoryBarrierWithGroupSync();
	[branch] if (GIdx == 0)
	{
		uint done;
		TileArgs.InterlockedAdd(16, 1u, done);
		[branch] if (done + 1u >= ((dims.x + 7u) / 8u) * ((dims.y + 7u) / 8u))
		{
			TileArgs.InterlockedExchange(12, 0u, count);
			TileArgs.InterlockedExchange(16, 0u, done);
			TileArgs.Store3(0, uint3(min(count, 1024u), (count + 1023u) / 1024u, 1u));
			last = true;
		}
	}
	return last;
}

[numthreads(8, 8, 1)] void ScanEvolveCS(uint3 DTid
										: SV_DispatchThreadID, uint GIdx
										: SV_GroupIndex) {
	uint2 dims;
	OccupancyIn.GetDimensions(dims.x, dims.y);

	bool need = false;
	[branch] if (all(DTid.xy < dims))
	{
		need = ForceAllDirty != 0;
		if (!need) {
			const int halo = (int)ceil((64.0 / max(TexelSize, 1e-3) + 1.0) / 8.0);
			for (int dy = -halo; dy <= halo && !need; dy++)
				for (int dx = -halo; dx <= halo && !need; dx++) {
					const uint2 t = (DTid.xy + uint2(int2(dx, dy) + int2(dims))) & (dims - 1);
					need = OccupancyIn[t] != 0;
				}
		}
	}
	[branch] if (need)
	{
		uint idx;
		TileArgs.InterlockedAdd(12, 1u, idx);
		EvolveTiles[1 + idx] = DTid.x | (DTid.y << 16);
		ActivityFlag.InterlockedAdd(44, 1u);
	}
	uint count;
	if (ScanFinish(GIdx, dims, count))
		EvolveTiles[0] = count;
}

#define SNAPSHOT_MAX_APRON 16
#define SNAPSHOT_SIDE (8 + 2 * SNAPSHOT_MAX_APRON)
groupshared uint gSnapOcc[SNAPSHOT_SIDE * SNAPSHOT_SIDE];
groupshared uint gSnapRows[SNAPSHOT_SIDE * 8];

[numthreads(8, 8, 1)] void ScanSnapshotCS(uint3 DTid
										  : SV_DispatchThreadID, uint3 Gid
										  : SV_GroupID, uint3 GTid
										  : SV_GroupThreadID, uint GIdx
										  : SV_GroupIndex) {
	uint2 dims;
	OccupancyIn.GetDimensions(dims.x, dims.y);
	const uint apron = 2u * (uint)ceil((64.0 / max(TexelSize, 1e-3) + 1.0) / 8.0);
	const bool everything = ForceAllDirty != 0 || apron > SNAPSHOT_MAX_APRON;
	const uint side = 8u + 2u * apron;
	[branch] if (!everything)
	{
		const int2 base = int2(Gid.xy * 8u) - (int)apron;
		for (uint i = GIdx; i < side * side; i += 64u)
			gSnapOcc[i] = OccupancyIn[uint2(base + int2(i % side, i / side) + int2(dims)) & (dims - 1u)];
	}
	GroupMemoryBarrierWithGroupSync();
	[branch] if (!everything)
	{
		for (uint i = GIdx; i < side * 8u; i += 64u) {
			uint occupied = 0;
			for (uint dx = 0; dx <= 2u * apron; dx++)
				occupied |= gSnapOcc[(i >> 3) * side + (i & 7u) + dx];
			gSnapRows[i] = occupied;
		}
	}
	GroupMemoryBarrierWithGroupSync();
	bool need = everything;
	[branch] if (!everything)
	{
		for (uint dy = 0; dy <= 2u * apron && !need; dy++)
			need = gSnapRows[(GTid.y + dy) * 8u + GTid.x] != 0;
	}
	[branch] if (need && all(DTid.xy < dims))
	{
		uint idx;
		TileArgs.InterlockedAdd(12, 1u, idx);
		EvolveTiles[1 + idx] = DTid.x | (DTid.y << 16);
	}
	uint count;
	if (ScanFinish(GIdx, dims, count))
		EvolveTiles[0] = count;
}

[numthreads(8, 8, 1)] void SnapshotCS(uint3 GTid
									  : SV_GroupThreadID, uint3 Gid
									  : SV_GroupID) {
	const uint listIndex = Gid.y * 1024u + Gid.x;
	if (listIndex >= EvolveTilesIn[0])
		return;
	const uint packed = EvolveTilesIn[1 + listIndex];
	const uint2 texel = uint2(packed & 0xFFFFu, packed >> 16u) * 8u + GTid.xy;
	EvolveSnapshot[texel] = PreviousDeformation[texel].xy;
}

[numthreads(8, 8, 1)] void SnapshotFullCS(uint3 DTid
										  : SV_DispatchThreadID) {
	EvolveSnapshot[DTid.xy] = PreviousDeformation[DTid.xy].xy;
}

[numthreads(8, 8, 1)] void ScanBermCS(uint3 DTid
									  : SV_DispatchThreadID, uint GIdx
									  : SV_GroupIndex) {
	uint2 dims;
	BermDirtyIn.GetDimensions(dims.x, dims.y);

	bool need = false;
	[branch] if (all(DTid.xy < dims))
	{
		need = ForceAllDirty != 0;
		if (!need) {
			const int halo = (int)ceil((40.0 * BermReach / max(TexelSize, 1e-3) + 2.0) / 8.0);
			for (int dy = -halo; dy <= halo && !need; dy++)
				for (int dx = -halo; dx <= halo && !need; dx++) {
					const uint2 t = (DTid.xy + uint2(int2(dx, dy) + int2(dims))) & (dims - 1);
					need = BermDirtyIn[t] != 0;
				}
		}
	}
	[branch] if (need)
	{
		uint idx;
		TileArgs.InterlockedAdd(12, 1u, idx);
		BermTiles[1 + idx] = DTid.x | (DTid.y << 16);
		ActivityFlag.InterlockedAdd(48, 1u);
	}
	uint count;
	if (ScanFinish(GIdx, dims, count))
		BermTiles[0] = count;
}

float2 TerrainGroundLayer(float2 worldPos)
{
	float2 t = (worldPos - TerrainWindowOrigin) / TerrainTexelSize;
	int tmax = (int)TerrainDim - 1;
	int2 t0 = clamp(int2(floor(t)), 0, tmax);
	int2 t1 = min(t0 + 1, tmax);
	float2 f = saturate(t - float2(t0));
	float4 s00 = TerrainWindow.Load(int3(t0.x, t0.y, 0));
	float4 s10 = TerrainWindow.Load(int3(t1.x, t0.y, 0));
	float4 s01 = TerrainWindow.Load(int3(t0.x, t1.y, 0));
	float4 s11 = TerrainWindow.Load(int3(t1.x, t1.y, 0));
	float2 terrain = lerp(lerp(s00.xy, s10.xy, f.x), lerp(s01.xy, s11.xy, f.x), f.y);
	return float2(terrain.x, max(terrain.y, 1.0));
}

float ContactAbove(float2 worldPos, float contact);

float ContactAboveDrawn(float2 worldPos, float contact, float dune)
{
	const float2 groundLayer = TerrainGroundLayer(worldPos);
	const float h = contact - groundLayer.x;
	const float left = max(h - dune, h * 8.0 / (8.0 + dune));
	return min(ContactAbove(worldPos, contact), left / (groundLayer.y * AccumulationScale));
}

float ContactAbove(float2 worldPos, float contact)
{
	float2 groundLayer = TerrainGroundLayer(worldPos);
	return (contact - groundLayer.x) / groundLayer.y;
}

float4 ShoutMark(float2 worldPos, float2 apex, float2 seg, float segLenSq, float3 cone)
{
	float axisLen = sqrt(segLenSq);
	float2 axis = axisLen > 1e-3 ? seg / axisLen : float2(1.0, 0.0);
	float2 rel = worldPos - apex;
	float along = dot(rel, axis);
	float perp = rel.x * axis.y - rel.y * axis.x;
	float reach = max(cone.x, axisLen);
	float u = along / reach;
	float waist = clamp(ShoutShape.x, 0.05, 0.95);
	float t = u < waist ? (waist - u) / waist : (u - waist) / (1.0 - waist);
	float m = max(ShoutShape.y, 1.0);
	float env = pow(max(1.0 - pow(saturate(t), m), 0.0), 1.0 / m);
	float halfWidth = max(cone.y * env, 1e-3);
	float churn = saturate(ShoutChurn.x);
	float2 np = float2(along / max(ShoutChurn.y * ShoutChurn.z, 1.0), perp / max(ShoutChurn.y, 1.0)) + cone.z;
	float n = 0.7 * StampNoise(np) + 0.3 * StampNoise(np * 2.9 + 17.3);
	float edgeCoord = abs(perp) / halfWidth / (1.0 + (n - 0.5) * 0.6 * churn);
	edgeCoord = (along < 0.0 || u > 1.0) ? 1e6 : edgeCoord;
	float fade = lerp(1.0, saturate(ShoutShape.w), smoothstep(saturate(ShoutShape.z), 1.0, u));
	float lead = reach * CONE_FRONT_LEAD;
	float frontAlong = axisLen * (1.0 + CONE_FRONT_LEAD);
	float reveal = 1.0 - smoothstep(frontAlong - lead, frontAlong, along);
	float coneSoft = fade * reveal;
	float band = max(0.2 * churn, 1e-3);
	float thr = 0.9 * churn * u;
	float cover = smoothstep(thr - band, thr + band, n);
	return float4(edgeCoord, coneSoft, coneSoft * cover * lerp(1.0, n, 0.35 * churn), along);
}

bool StampTexel(uint2 phys)
{
	uint2 dims;
	CurrentDeformation.GetDimensions(dims.x, dims.y);
	const uint2 pixel = uint2((int2(phys) - MapOrigin) & (int2(dims) - 1));

	const float4 carried = CurrentDeformation[phys];
	float deformation = carried.x;
	float melted = carried.y;
	float crust = carried.z;

	float2 worldPos = WindowOrigin + (float2(pixel) + 0.5) * TexelSize;
	float bodyReach = 0.0;
	float cutNow = 0.0;

	float carve = deformation;
	float meltTarget = 0.0;
	float meltRate = 0.0;
	float scorch = max(-melted, 0.0);
	float impact = carried.w;
	float crustTarget = 0.0;
	float crustRate = 0.0;
	float crustBreak = 0.0;
	const float standingCrust = crust;

	const float gateScale = max(PIT_LOBE_MAX + PIT_LOBE_WIDTH, 1.0 + MeltEdgeNoise);
	const float gateScaleSq = gateScale * gateScale;

	for (uint i = 0; i < StampCount; i++) {
		float2 p0 = StampEnds[i].xy;
		float2 p1 = Stamps[i].xy;
		float2 seg = p1 - p0;
		float segLenSq = dot(seg, seg);
		float t = segLenSq > 1e-4 ? saturate(dot(worldPos - p0, seg) / segLenSq) : 0.0;
		float2 delta = worldPos - (p0 + seg * t);
		float distSq = dot(delta, delta);
		float radius = Stamps[i].w;

		float modeRaw = StampEnds[i].z;
		bool isBody = modeRaw > STAMP_MODE_BODY - 0.5;
		modeRaw = isBody ? modeRaw - STAMP_MODE_BODY : modeRaw;
		bool isBowl = modeRaw > STAMP_MODE_BOWL - 0.5;
		float modeNoBowl = isBowl ? modeRaw - STAMP_MODE_BOWL : modeRaw;
		bool isCone = modeNoBowl > STAMP_MODE_CONE - 0.5;
		float mode = isCone ? modeNoBowl - STAMP_MODE_CONE : modeNoBowl;

		[branch] if (distSq < radius * radius * gateScaleSq)
		{
			float dist = sqrt(distSq);

			float edgeCoord = dist / max(radius, 1e-3);
			float coneSoft = 1.0;
			float coneCarve = 1.0;
			[branch] if (isCone)
			{
				const float4 shout = ShoutMark(worldPos, p0, seg, segLenSq, StampCones[i].xyz);
				edgeCoord = shout.x;
				coneSoft = shout.y;
				coneCarve = shout.z;
			}

			[branch] if (mode < 0.5)
			{
				float edgeDist = edgeCoord;
				float falloff = (1.0 - smoothstep(isBowl ? CARVE_BOWL_FLOOR : StampFalloffStart, 1.0, edgeDist)) * coneCarve;

		const float impactE = saturate((mode - floor(mode)) / STAMP_IMPACT_SCALE);
				float force = saturate(StampEnds[i].w);
				float resist = standingCrust * (1.0 - force);
				float reach = Stamps[i].z * falloff * lerp(1.0, CrustPrintDepth, resist);
				float printed = reach * (isBody ? BodyPrint : 1.0);
				[flatten] if (isBody)
				{
					bodyReach = max(bodyReach, reach);
				}
				else
				{
					carve = max(carve, printed);
					cutNow = max(cutNow, printed);
				}

			impact = max(impact, impactE * coneSoft * (1.0 - smoothstep(0.6, 1.3, edgeCoord)));
				crustBreak = max(crustBreak, saturate(max(force, printed * CrustBreakOnCarve) * falloff));
			}
			else if (mode > 2.5)
			{
				float glazeWobble = 1.0;
				[branch] if (MeltEdgeNoise > 0.001)
				{
					float wobble = 0.5 * StampNoise(worldPos / MELT_NOISE_FINE) +
					               0.5 * StampNoise(worldPos / MELT_NOISE_COARSE);
					glazeWobble = 1.0 + (wobble - 0.5) * 2.0 * MeltEdgeNoise;
				}
				float falloff = (1.0 - smoothstep(MeltFloorStart, 1.0, edgeCoord / max(glazeWobble, 1e-3))) * coneSoft;
				crustTarget = max(crustTarget, Stamps[i].z * falloff);
				crustRate += Stamps[i].z * StampEnds[i].w * falloff;
			}
			else if (mode > 1.5)
			{
				float2 centre = Stamps[i].xy;
				float shape = 1.0 - smoothstep(0.15, 1.0, edgeCoord);

				[branch] if (!isCone)
				[unroll] for (int lobe = 0; lobe < PIT_LOBES; lobe++) {
					float2 seed = centre * 0.05 + float2(lobe * 7.3, lobe * 3.1);
					float angle = StampNoiseHash(floor(seed)) * 6.2831853;
					float legLength = radius * lerp(PIT_LOBE_MIN, PIT_LOBE_MAX,
						StampNoiseHash(floor(seed + 31.7)));
					float2 tip = centre + float2(cos(angle), sin(angle)) * legLength;

					float2 legVec = tip - centre;
					float legLenSq = max(dot(legVec, legVec), 1e-4);
					float legT = saturate(dot(worldPos - centre, legVec) / legLenSq);
					float2 legDelta = worldPos - (centre + legVec * legT);
					float legWidth = radius * PIT_LOBE_WIDTH * (1.0 - 0.75 * legT);
					shape = max(shape, 1.0 - smoothstep(0.2, 1.0, length(legDelta) / max(legWidth, 1e-3)));
				}

				float pockCell = max(radius / PIT_POCK_CELLS_ACROSS, PIT_POCK_CELL_MIN);
				float pock = StampNoise(worldPos / pockCell);
				shape *= max(smoothstep(0.30, 0.62, pock), 0.45);

				float pitDepth = Stamps[i].z * shape * coneSoft;
				carve = max(carve, pitDepth);
				cutNow = max(cutNow, pitDepth);
				scorch = max(scorch, pitDepth);
			}
			else
			{
				float meltWobble = 1.0;
				[branch] if (MeltEdgeNoise > 0.001)
				{
					float wobble = 0.5 * StampNoise(worldPos / MELT_NOISE_FINE) +
					               0.5 * StampNoise(worldPos / MELT_NOISE_COARSE);
					meltWobble = 1.0 + (wobble - 0.5) * 2.0 * MeltEdgeNoise;
				}
				float falloff = (1.0 - smoothstep(MeltFloorStart, 1.0, edgeCoord / max(meltWobble, 1e-3))) * coneSoft;
				meltTarget = max(meltTarget, Stamps[i].z * falloff);
				meltRate += Stamps[i].z * StampEnds[i].w * falloff;
			}
		}
	}

	[branch] if (ContactDim > 0.5)
	{
		float2 rel = (worldPos - ContactCenter) / ContactHalfExtent;
		[branch] if (all(abs(rel) < 1.0))
		{
			int dim = (int)ContactDim;
			const float2 cf = float2(rel.x * 0.5 + 0.5, 0.5 - rel.y * 0.5) * ContactDim;
			const float span = TexelSize * ContactDim / (4.0 * ContactHalfExtent);
			const int2 k0 = int2(floor(cf - span));
			const int2 k1 = int2(ceil(cf + span)) - 1;
			float2 contact = float2(CONTACT_NONE, CONTACT_NONE);
			float2 cover = 0.0;
			[loop] for (int ky = k0.y; ky <= k1.y; ky++)
			{
				const float wy = saturate(min(ky + 1.0, cf.y + span) - max((float)ky, cf.y - span));
				[loop] for (int kx = k0.x; kx <= k1.x; kx++)
				{
					const float2 c = ContactHeight.Load(int3(clamp(int2(kx, ky), 0, dim - 1), 0));
					contact = min(contact, c);
					cover += wy * saturate(min(kx + 1.0, cf.x + span) - max((float)kx, cf.x - span)) * float2(c < CONTACT_NONE * 0.5);
				}
			}
			cover = saturate(cover / (4.0 * span * span));
			const float crustDepth = lerp(1.0, CrustPrintDepth, standingCrust);
			float dune = 0.0;
			[branch] if (min(contact.x, contact.y) < CONTACT_NONE * 0.5)
				dune = max(UndulationNorm(worldPos, UndulationScale) * UndulationAmp, 0.0);
			float bodyContact = 0.0;
			float propPrinted = 0.0;
			[branch] if (contact.x < CONTACT_NONE * 0.5)
				bodyContact = saturate(1.0 - ContactAboveDrawn(worldPos, contact.x, dune)) * crustDepth * cover.x;
			[branch] if (contact.y < CONTACT_NONE * 0.5)
				propPrinted = saturate(1.0 - ContactAboveDrawn(worldPos, contact.y, dune)) * crustDepth * cover.y;
			bodyReach = max(bodyReach, bodyContact);
			carve = max(carve, propPrinted);
			cutNow = max(cutNow, propPrinted);
			crustBreak = max(crustBreak, saturate(max(bodyContact * BodyPrint, propPrinted) * CrustBreakOnCarve));
		}
	}

	[branch] if (bodyReach > 0.0)
	{
		const uint state = PassState[phys];
		float passStart = float(state & 63u) / 63.0;
		[flatten] if (((PassClock - (state >> 6)) & 1023u) > PassGap || passStart > deformation + 1.0 / 63.0)
			passStart = floor(saturate(deformation) * 63.0) / 63.0;
		carve = max(carve, passStart + BodyPrint * max(bodyReach - passStart, 0.0));
		PassState[phys] = ((PassClock & 1023u) << 6) | uint(passStart * 63.0 + 0.5);
		[branch] if (bodyReach > BodyReachMax[phys] + 0.5 / 255.0)
			BodyReachMax[phys] = bodyReach;
	}
	cutNow = max(cutNow, max(bodyReach, meltTarget));

	float total = carve;
	[flatten] if (meltTarget > total)
		total = min(total + meltRate * DeltaTime, meltTarget);
	total = min(total, 1.0);

	float meltedNow = min(melted + max(total - carve, 0.0), total);

	float crustNow = saturate(crust * (1.0 - crustBreak));
	[flatten] if (crustTarget > crustNow)
		crustNow = min(crustNow + crustRate * DeltaTime, crustTarget);
	crustNow = saturate(crustNow);

	float4 result = float4(total,
		meltedNow > 0.0 ? meltedNow : -min(scorch, 1.0), crustNow, saturate(impact));
	CurrentDeformation[phys] = result;

	[branch] if (cutNow > kTrackCutMin && cutNow >= deformation - kTrackFloorMargin)
	{
		[branch] if (abs(TrackClock[phys] - AgeingClock) > 0.5)
		{
			TrackClock[phys] = AgeingClock;
			InterlockedOr(gClockMoved, 1u);
		}
	}

	ActivityAccumulate(StoredDelta(result, carried), pixel);
	return any(result != 0.0);
}

[numthreads(8, 8, 1)] void StampCS(uint3 GTid
								   : SV_GroupThreadID, uint3 Gid
								   : SV_GroupID, uint GIdx
								   : SV_GroupIndex) {
	ActivityReset(GIdx);
	const uint packed = StampTiles[Gid.x];
	const uint2 tile = uint2(packed & 0xFFFFu, packed >> 16u);
	if (StampTexel(tile * 8u + GTid.xy))
		InterlockedOr(gNonZero, 1u);
	ActivityPublish(GIdx);
	if (GIdx == 0) {
		Occupancy[tile] = gNonZero;
		if (gActivity != 0 || gClockMoved != 0)
			BermDirty[tile] = 1u;
	}
}

[numthreads(8, 8, 1)] void StampAllCS(uint3 DTid
									  : SV_DispatchThreadID, uint3 Gid
									  : SV_GroupID, uint GIdx
									  : SV_GroupIndex) {
	ActivityReset(GIdx);
	if (StampTexel(DTid.xy))
		InterlockedOr(gNonZero, 1u);
	ActivityPublish(GIdx);
	if (GIdx == 0) {
		Occupancy[Gid.xy] = gNonZero;
		if (gActivity != 0 || gClockMoved != 0)
			BermDirty[Gid.xy] = 1u;
	}
}
