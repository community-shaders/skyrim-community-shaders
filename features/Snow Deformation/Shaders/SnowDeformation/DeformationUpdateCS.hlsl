// Persistent snow deformation map update.
//
// The map is a toroidal window following the camera in whole-texel steps: a
// texel's physical address is its world texel index masked by dim - 1, so a
// scroll moves no data, it only reassigns the rows and columns that enter the
// window. Texel value = normalized depression depth (0 = untouched, 1 = ground).
// Updated in place, and only where something changed:
//   RingCS   - clears the texels a scroll reassigned (the whole map on a clear)
//   RefillCS - refills one band of rows per frame while a refill sweep runs
//   StampCS  - max-blends stamps into the 8x8 tiles they touch

#define MAX_STAMPS 128

cbuffer PerFrame : register(b0)
{
	float2 WindowOrigin;  // world xy of logical texel (0,0)
	int2 MapOrigin;       // physical texel of logical texel (0,0)

	float TexelSize;
	uint StampCount;
	float RefillAmount;
	uint RefillRowStart;

	int4 RingRects[2];  // logical x0, y0, w, h
	uint RingTotalTexels;
	uint RefillRowCount;
	uint2 PerFramePad;

	float4 Stamps[MAX_STAMPS];     // xy: world pos, z: depth, w: 1 / radius^2
	float4 StampEnds[MAX_STAMPS];  // xy: previous world pos (capsule segment start), z: radius
}

RWTexture2D<float> Deformation : register(u0);
StructuredBuffer<uint> StampTiles : register(t0);  // physical tile x | y << 16

uint2 MapDims()
{
	uint2 dims;
	Deformation.GetDimensions(dims.x, dims.y);
	return dims;
}

uint2 ToPhysical(uint2 logical, uint2 dims)
{
	return uint2((int2(logical) + MapOrigin) & (int2(dims) - 1));
}

uint2 ToLogical(uint2 phys, uint2 dims)
{
	return uint2((int2(phys) - MapOrigin) & (int2(dims) - 1));
}

[numthreads(256, 1, 1)] void RingCS(uint3 DTid
									: SV_DispatchThreadID) {
	uint index = DTid.x;
	if (index >= RingTotalTexels)
		return;

	int4 rect = RingRects[0];
	const uint area0 = uint(rect.z * rect.w);
	if (index >= area0) {
		index -= area0;
		rect = RingRects[1];
	}
	const uint2 logical = uint2(rect.x + index % uint(rect.z), rect.y + index / uint(rect.z));
	Deformation[ToPhysical(logical, MapDims())] = 0.0;
}

[numthreads(8, 8, 1)] void RefillCS(uint3 DTid
									: SV_DispatchThreadID) {
	const uint2 phys = uint2(DTid.x, RefillRowStart + DTid.y);
	const float deformation = Deformation[phys];
	[branch] if (deformation > 0.0)
		Deformation[phys] = max(deformation - RefillAmount, 0.0);
}

groupshared uint gTileStamps[MAX_STAMPS];
groupshared uint gTileStampCount;

void StampTile(uint2 tile, uint groupIndex, uint2 groupThread)
{
	const uint2 dims = MapDims();

	if (groupIndex == 0)
		gTileStampCount = 0;
	GroupMemoryBarrierWithGroupSync();

	// Each group keeps only the stamps whose bounds touch its tile. A tile on
	// the torus seam spans both window edges, so it keeps them all.
	const uint2 logicalCorner = ToLogical(tile * 8, dims);
	const bool contiguous = all(logicalCorner + 8 <= dims);
	const float2 tileMin = WindowOrigin + float2(logicalCorner) * TexelSize;
	const float2 tileMax = tileMin + 8.0 * TexelSize;
	for (uint i = groupIndex; i < StampCount; i += 64) {
		const float radius = StampEnds[i].z;
		const float2 stampMin = min(Stamps[i].xy, StampEnds[i].xy) - radius;
		const float2 stampMax = max(Stamps[i].xy, StampEnds[i].xy) + radius;
		if (!contiguous || all(stampMax >= tileMin && stampMin <= tileMax)) {
			uint slot;
			InterlockedAdd(gTileStampCount, 1, slot);
			gTileStamps[slot] = i;
		}
	}
	GroupMemoryBarrierWithGroupSync();

	const uint count = gTileStampCount;
	[branch] if (count == 0) return;

	const uint2 phys = tile * 8 + groupThread;
	const float2 worldPos = WindowOrigin + (float2(ToLogical(phys, dims)) + 0.5) * TexelSize;
	const float deformation = Deformation[phys];
	float stamped = deformation;

	for (uint k = 0; k < count; k++) {
		const uint i = gTileStamps[k];
		// Capsule stamp: distance to the segment from the actor's previous
		// position, so trails are continuous regardless of movement speed.
		const float2 p0 = StampEnds[i].xy;
		const float2 seg = Stamps[i].xy - p0;
		const float segLenSq = dot(seg, seg);
		const float t = segLenSq > 1e-4 ? saturate(dot(worldPos - p0, seg) / segLenSq) : 0.0;
		const float2 delta = worldPos - (p0 + seg * t);
		// (distance / radius)^2
		const float distNormSq = dot(delta, delta) * Stamps[i].w;

		[branch] if (distNormSq < 1.0)
		{
			// Falloff from 0.2 of the radius keeps a wide edge band, so
			// coarser consumers of the map can still represent trench walls.
			const float falloff = 1.0 - smoothstep(0.2, 1.0, sqrt(distNormSq));
			stamped = max(stamped, Stamps[i].z * falloff);
		}
	}

	[branch] if (stamped > deformation)
		Deformation[phys] = stamped;
}

[numthreads(8, 8, 1)] void StampCS(uint3 Gid
								   : SV_GroupID, uint3 GTid
								   : SV_GroupThreadID, uint GI
								   : SV_GroupIndex) {
	const uint packed = StampTiles[Gid.x];
	StampTile(uint2(packed & 0xFFFF, packed >> 16), GI, GTid.xy);
}

// Full-map fallback for a tile list past its capacity.
[numthreads(8, 8, 1)] void StampAllCS(uint3 Gid
									  : SV_GroupID, uint3 GTid
									  : SV_GroupThreadID, uint GI
									  : SV_GroupIndex) {
	StampTile(Gid.xy, GI, GTid.xy);
}
