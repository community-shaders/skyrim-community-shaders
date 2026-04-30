#include "SDFGI/common.hlsli"

static const int HALF_CASCADE_SIZE = CASCADE_SIZE / 2;

// ============================================================================
// MODE_JFA_INIT_HALF
// Initializes the half-resolution JFA seed buffer from full-res voxel data.
// Checks 2x2x2 blocks of full-res voxels; if any are solid (LSB set),
// stores the half-res position as seed.
// ============================================================================
#ifdef MODE_JFA_INIT_HALF

RWTexture3D<uint> renderAlbedo : register(u0);
RWTexture3D<uint4> renderSdfHalf : register(u1);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int3 pos = int3(dispatchThreadID);

	if (any(pos >= int3(HALF_CASCADE_SIZE, HALF_CASCADE_SIZE, HALF_CASCADE_SIZE)))
		return;

	int3 basePos = pos * 2;
	bool found = false;

	[unroll]
	for (int z = 0; z < 2 && !found; z++) {
		[unroll]
		for (int y = 0; y < 2 && !found; y++) {
			[unroll]
			for (int x = 0; x < 2 && !found; x++) {
				uint val = renderAlbedo[basePos + int3(x, y, z)];
				if (val & 1u) {
					found = true;
				}
			}
		}
	}

	if (found) {
		renderSdfHalf[pos] = uint4(pos.x, pos.y, pos.z, 255);
	} else {
		renderSdfHalf[pos] = uint4(0, 0, 0, 0);
	}
}

#endif  // MODE_JFA_INIT_HALF

// ============================================================================
// MODE_JFA_PASS
// Standard Jump Flood Algorithm pass. For each of 27 neighbors at the
// current step distance, finds the closest seed by Euclidean distance.
// ============================================================================
#ifdef MODE_JFA_PASS

Texture3D<uint4> srcPositions : register(t0);
RWTexture3D<uint4> dstPositions : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int3 pos = int3(dispatchThreadID);

	if (any(pos >= int3(HALF_CASCADE_SIZE, HALF_CASCADE_SIZE, HALF_CASCADE_SIZE)))
		return;

	float bestDist = 1e20;
	int3 bestPos = int3(0, 0, 0);
	bool bestFound = false;

	int step = StepSize;

	[unroll]
	for (int z = -1; z <= 1; z++) {
		[unroll]
		for (int y = -1; y <= 1; y++) {
			[unroll]
			for (int x = -1; x <= 1; x++) {
				int3 samplePos = pos + int3(x, y, z) * step;

				if (any(samplePos < 0) || any(samplePos >= int3(HALF_CASCADE_SIZE, HALF_CASCADE_SIZE, HALF_CASCADE_SIZE)))
					continue;

				uint4 data = srcPositions[samplePos];

				if (data.w == 0)
					continue;

				int3 seedPos = int3(data.xyz);
				float3 diff = float3(pos - seedPos);
				float dist = dot(diff, diff);

				if (dist < bestDist) {
					bestDist = dist;
					bestPos = seedPos;
					bestFound = true;
				}
			}
		}
	}

	if (bestFound) {
		dstPositions[pos] = uint4(bestPos.x, bestPos.y, bestPos.z, 255);
	} else {
		dstPositions[pos] = uint4(0, 0, 0, 0);
	}
}

#endif  // MODE_JFA_PASS

// ============================================================================
// MODE_JFA_OPTIMIZED
// Optimized JFA pass using groupshared memory for an 8x8x8 block with
// a 1-voxel border (10x10x10 total). Interior neighbors are read from
// shared memory; border cells fall back to global texture reads.
// ============================================================================
#ifdef MODE_JFA_OPTIMIZED

Texture3D<uint4> srcPositions : register(t0);
RWTexture3D<uint4> dstPositions : register(u0);

#define BLOCK_SIZE 8
#define SHARED_SIZE (BLOCK_SIZE + 2)

groupshared uint4 sharedData[SHARED_SIZE][SHARED_SIZE][SHARED_SIZE];

[numthreads(BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID,
	uint3 groupThreadID : SV_GroupThreadID,
	uint3 groupID : SV_GroupID)
{
	int3 pos = int3(dispatchThreadID);
	int3 localPos = int3(groupThreadID);
	int3 groupBase = int3(groupID) * BLOCK_SIZE;
	int halfSizeI = HALF_CASCADE_SIZE;

	// Load shared memory: each thread loads its own cell + 1 border offset
	int3 sharedIdx = localPos + int3(1, 1, 1);
	if (any(pos >= int3(halfSizeI, halfSizeI, halfSizeI))) {
		sharedData[sharedIdx.z][sharedIdx.y][sharedIdx.x] = uint4(0, 0, 0, 0);
	} else {
		sharedData[sharedIdx.z][sharedIdx.y][sharedIdx.x] = srcPositions[pos];
	}

	// Load border cells - threads at the edges of the group load extra cells
	// Each face of the block needs one layer of border data
	if (localPos.x == 0) {
		int3 borderGlobal = groupBase + int3(-1, localPos.y, localPos.z);
		uint4 val = uint4(0, 0, 0, 0);
		if (all(borderGlobal >= 0) && all(borderGlobal < int3(halfSizeI, halfSizeI, halfSizeI)))
			val = srcPositions[borderGlobal];
		sharedData[localPos.z + 1][localPos.y + 1][0] = val;
	}
	if (localPos.x == BLOCK_SIZE - 1) {
		int3 borderGlobal = groupBase + int3(BLOCK_SIZE, localPos.y, localPos.z);
		uint4 val = uint4(0, 0, 0, 0);
		if (all(borderGlobal >= 0) && all(borderGlobal < int3(halfSizeI, halfSizeI, halfSizeI)))
			val = srcPositions[borderGlobal];
		sharedData[localPos.z + 1][localPos.y + 1][SHARED_SIZE - 1] = val;
	}
	if (localPos.y == 0) {
		int3 borderGlobal = groupBase + int3(localPos.x, -1, localPos.z);
		uint4 val = uint4(0, 0, 0, 0);
		if (all(borderGlobal >= 0) && all(borderGlobal < int3(halfSizeI, halfSizeI, halfSizeI)))
			val = srcPositions[borderGlobal];
		sharedData[localPos.z + 1][0][localPos.x + 1] = val;
	}
	if (localPos.y == BLOCK_SIZE - 1) {
		int3 borderGlobal = groupBase + int3(localPos.x, BLOCK_SIZE, localPos.z);
		uint4 val = uint4(0, 0, 0, 0);
		if (all(borderGlobal >= 0) && all(borderGlobal < int3(halfSizeI, halfSizeI, halfSizeI)))
			val = srcPositions[borderGlobal];
		sharedData[localPos.z + 1][SHARED_SIZE - 1][localPos.x + 1] = val;
	}
	if (localPos.z == 0) {
		int3 borderGlobal = groupBase + int3(localPos.x, localPos.y, -1);
		uint4 val = uint4(0, 0, 0, 0);
		if (all(borderGlobal >= 0) && all(borderGlobal < int3(halfSizeI, halfSizeI, halfSizeI)))
			val = srcPositions[borderGlobal];
		sharedData[0][localPos.y + 1][localPos.x + 1] = val;
	}
	if (localPos.z == BLOCK_SIZE - 1) {
		int3 borderGlobal = groupBase + int3(localPos.x, localPos.y, BLOCK_SIZE);
		uint4 val = uint4(0, 0, 0, 0);
		if (all(borderGlobal >= 0) && all(borderGlobal < int3(halfSizeI, halfSizeI, halfSizeI)))
			val = srcPositions[borderGlobal];
		sharedData[SHARED_SIZE - 1][localPos.y + 1][localPos.x + 1] = val;
	}

	// Load corner cells for the 8 corners of the block
	if (localPos.x == 0 && localPos.y == 0 && localPos.z == 0) {
		// Load all 8 corners
		[unroll]
		for (int cz = 0; cz < 2; cz++) {
			[unroll]
			for (int cy = 0; cy < 2; cy++) {
				[unroll]
				for (int cx = 0; cx < 2; cx++) {
					int3 cornerShared = int3(cx * (SHARED_SIZE - 1), cy * (SHARED_SIZE - 1), cz * (SHARED_SIZE - 1));
					int3 cornerGlobal = groupBase + int3(cx * BLOCK_SIZE - 1 + cx, cy * BLOCK_SIZE - 1 + cy, cz * BLOCK_SIZE - 1 + cz);
					// Simplified: corner global = groupBase + corner offset
					cornerGlobal = groupBase + int3(
						cx == 0 ? -1 : BLOCK_SIZE,
						cy == 0 ? -1 : BLOCK_SIZE,
						cz == 0 ? -1 : BLOCK_SIZE);
					uint4 val = uint4(0, 0, 0, 0);
					if (all(cornerGlobal >= 0) && all(cornerGlobal < int3(halfSizeI, halfSizeI, halfSizeI)))
						val = srcPositions[cornerGlobal];
					sharedData[cornerShared.z][cornerShared.y][cornerShared.x] = val;
				}
			}
		}

		// Load edge border cells - edges shared between face borders
		// X-edges (along Y=0/max, Z=0/max)
		[unroll]
		for (int i = 0; i < BLOCK_SIZE; i++) {
			// Y=0, Z=0 edge
			{
				int3 eg = groupBase + int3(i, -1, -1);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[0][0][i + 1] = val;
			}
			// Y=max, Z=0 edge
			{
				int3 eg = groupBase + int3(i, BLOCK_SIZE, -1);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[0][SHARED_SIZE - 1][i + 1] = val;
			}
			// Y=0, Z=max edge
			{
				int3 eg = groupBase + int3(i, -1, BLOCK_SIZE);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[SHARED_SIZE - 1][0][i + 1] = val;
			}
			// Y=max, Z=max edge
			{
				int3 eg = groupBase + int3(i, BLOCK_SIZE, BLOCK_SIZE);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[SHARED_SIZE - 1][SHARED_SIZE - 1][i + 1] = val;
			}
			// Y-edges (X=0/max, Z=0/max)
			{
				int3 eg = groupBase + int3(-1, i, -1);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[0][i + 1][0] = val;
			}
			{
				int3 eg = groupBase + int3(BLOCK_SIZE, i, -1);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[0][i + 1][SHARED_SIZE - 1] = val;
			}
			{
				int3 eg = groupBase + int3(-1, i, BLOCK_SIZE);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[SHARED_SIZE - 1][i + 1][0] = val;
			}
			{
				int3 eg = groupBase + int3(BLOCK_SIZE, i, BLOCK_SIZE);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[SHARED_SIZE - 1][i + 1][SHARED_SIZE - 1] = val;
			}
			// Z-edges (X=0/max, Y=0/max)
			{
				int3 eg = groupBase + int3(-1, -1, i);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[i + 1][0][0] = val;
			}
			{
				int3 eg = groupBase + int3(BLOCK_SIZE, -1, i);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[i + 1][0][SHARED_SIZE - 1] = val;
			}
			{
				int3 eg = groupBase + int3(-1, BLOCK_SIZE, i);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[i + 1][SHARED_SIZE - 1][0] = val;
			}
			{
				int3 eg = groupBase + int3(BLOCK_SIZE, BLOCK_SIZE, i);
				uint4 val = uint4(0, 0, 0, 0);
				if (all(eg >= 0) && all(eg < int3(halfSizeI, halfSizeI, halfSizeI)))
					val = srcPositions[eg];
				sharedData[i + 1][SHARED_SIZE - 1][SHARED_SIZE - 1] = val;
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();

	if (any(pos >= int3(halfSizeI, halfSizeI, halfSizeI)))
		return;

	// Find closest seed among 27 neighbors using shared memory (step=1 for optimized pass)
	float bestDist = 1e20;
	int3 bestSeedPos = int3(0, 0, 0);
	bool bestFound = false;

	[unroll]
	for (int z = -1; z <= 1; z++) {
		[unroll]
		for (int y = -1; y <= 1; y++) {
			[unroll]
			for (int x = -1; x <= 1; x++) {
				int3 si = sharedIdx + int3(x, y, z);
				uint4 data = sharedData[si.z][si.y][si.x];

				if (data.w == 0)
					continue;

				int3 seedPos = int3(data.xyz);
				float3 diff = float3(pos - seedPos);
				float dist = dot(diff, diff);

				if (dist < bestDist) {
					bestDist = dist;
					bestSeedPos = seedPos;
					bestFound = true;
				}
			}
		}
	}

	if (bestFound) {
		dstPositions[pos] = uint4(bestSeedPos.x, bestSeedPos.y, bestSeedPos.z, 255);
	} else {
		dstPositions[pos] = uint4(0, 0, 0, 0);
	}
}

#endif  // MODE_JFA_OPTIMIZED

// ============================================================================
// MODE_JFA_UPSCALE
// Upscales the half-resolution JFA result to full resolution.
// For each full-res voxel, samples the 8 corners of the enclosing half-res
// cell and picks the closest seed. Solid voxels use their own position.
// ============================================================================
#ifdef MODE_JFA_UPSCALE

Texture3D<uint4> halfResPositions : register(t0);
Texture3D<uint> renderAlbedo : register(t1);
RWTexture3D<uint4> fullResPositions : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int3 pos = int3(dispatchThreadID);
	int gridSizeI = int(GridSize.x);

	if (any(pos >= int3(gridSizeI, gridSizeI, gridSizeI)))
		return;

	// Check if this full-res voxel is solid
	uint albedoVal = renderAlbedo[pos];
	if (albedoVal & 1u) {
		// Solid voxel: use own position as seed
		fullResPositions[pos] = uint4(pos.x, pos.y, pos.z, 255);
		return;
	}

	// Map full-res position to half-res space
	float3 halfPos = (float3(pos) + 0.5) * 0.5;
	int3 halfBase = int3(floor(halfPos - 0.5));
	int halfSizeI = HALF_CASCADE_SIZE;

	float bestDist = 1e20;
	int3 bestSeedPos = int3(0, 0, 0);
	bool bestFound = false;

	// Check the 8 corners of the enclosing half-res cell
	[unroll]
	for (int z = 0; z <= 1; z++) {
		[unroll]
		for (int y = 0; y <= 1; y++) {
			[unroll]
			for (int x = 0; x <= 1; x++) {
				int3 sampleCoord = halfBase + int3(x, y, z);
				sampleCoord = clamp(sampleCoord, int3(0, 0, 0), int3(halfSizeI - 1, halfSizeI - 1, halfSizeI - 1));

				uint4 data = halfResPositions[sampleCoord];

				if (data.w == 0)
					continue;

				// Half-res seed position mapped to full-res
				int3 seedFullRes = int3(data.xyz) * 2 + int3(1, 1, 1);
				float3 diff = float3(pos - seedFullRes);
				float dist = dot(diff, diff);

				if (dist < bestDist) {
					bestDist = dist;
					bestSeedPos = seedFullRes;
					bestFound = true;
				}
			}
		}
	}

	if (bestFound) {
		fullResPositions[pos] = uint4(
			clamp(bestSeedPos.x, 0, gridSizeI - 1),
			clamp(bestSeedPos.y, 0, gridSizeI - 1),
			clamp(bestSeedPos.z, 0, gridSizeI - 1),
			255);
	} else {
		fullResPositions[pos] = uint4(0, 0, 0, 0);
	}
}

#endif  // MODE_JFA_UPSCALE

// ============================================================================
// MODE_OCCLUSION
// Computes probe occlusion by checking geometry presence in 8 directional
// sectors (positive/negative along each axis, plus 2 diagonal sectors).
// Packs 4 occlusion values per voxel into R16 (4 bits each).
// ============================================================================
#ifdef MODE_OCCLUSION

Texture3D<uint> renderAlbedo : register(t0);
Texture3D<uint> renderGeomFacing : register(t1);
RWTexture3D<uint> occlusionTex : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int3 pos = int3(dispatchThreadID);
	int gridSizeI = int(GridSize.x);
	int probeDivisor = PROBE_DIVISOR;

	// Each probe covers a PROBE_DIVISOR^3 block of voxels
	int3 probeGridSize = int3(gridSizeI / probeDivisor, gridSizeI / probeDivisor, gridSizeI / probeDivisor);

	if (any(pos >= probeGridSize))
		return;

	int3 baseVoxel = pos * probeDivisor + ProbeOffset;

	// 8 directional sectors: +X, -X, +Y, -Y, +Z, -Z, and two diagonals
	// We pack 4 occlusion values per output voxel
	// Each value is a 4-bit count of solid geometry in that sector
	static const int3 sectorDirs[8] = {
		int3(1, 0, 0),
		int3(-1, 0, 0),
		int3(0, 1, 0),
		int3(0, -1, 0),
		int3(0, 0, 1),
		int3(0, 0, -1),
		int3(1, 1, 1),
		int3(-1, -1, -1)
	};

	uint occlusionPacked = 0;

	[unroll]
	for (uint s = 0; s < 4; s++) {
		uint sectorIdx = OcclusionIndex * 4 + s;
		if (sectorIdx >= 8)
			break;

		int3 dir = sectorDirs[sectorIdx];
		uint count = 0;
		uint total = 0;

		// Sample geometry in the sector
		for (int dz = 0; dz < probeDivisor; dz++) {
			for (int dy = 0; dy < probeDivisor; dy++) {
				for (int dx = 0; dx < probeDivisor; dx++) {
					int3 offset = int3(dx, dy, dz) - int3(probeDivisor / 2, probeDivisor / 2, probeDivisor / 2);

					// Check if offset is in the correct sector (dot with direction > 0)
					int dotVal = offset.x * dir.x + offset.y * dir.y + offset.z * dir.z;
					if (dotVal <= 0)
						continue;

					int3 samplePos = baseVoxel + offset;
					if (any(samplePos < 0) || any(samplePos >= int3(gridSizeI, gridSizeI, gridSizeI)))
						continue;

					total++;
					uint albedoVal = renderAlbedo[samplePos];
					if (albedoVal & 1u) {
						uint facing = renderGeomFacing[samplePos];
						// Check if geometry faces into the sector direction
						bool facesSector = false;
						if (dir.x > 0 && (facing & 0x01))
							facesSector = true;
						if (dir.x < 0 && (facing & 0x02))
							facesSector = true;
						if (dir.y > 0 && (facing & 0x04))
							facesSector = true;
						if (dir.y < 0 && (facing & 0x08))
							facesSector = true;
						if (dir.z > 0 && (facing & 0x10))
							facesSector = true;
						if (dir.z < 0 && (facing & 0x20))
							facesSector = true;
						if (facesSector) {
							count++;
						}
					}
				}
			}
		}

		// Normalize to 4-bit range [0, 15]
		uint occValue = 0;
		if (total > 0) {
			occValue = min((count * 15u) / total, 15u);
		}

		occlusionPacked |= (occValue << (s * 4));
	}

	occlusionTex[pos] = occlusionPacked;
}

#endif  // MODE_OCCLUSION

// ============================================================================
// MODE_STORE
// Final storage pass: computes SDF distance from JFA result, stores to
// sdfTex. For solid voxels, atomically appends a ProcessVoxel to the
// solidCellBuffer and updates the indirect dispatch counter.
// ============================================================================
#ifdef MODE_STORE

Texture3D<uint4> renderSdf : register(t0);
Texture3D<uint> renderAlbedo : register(t1);
Texture3D<uint> renderEmission : register(t2);
Texture3D<uint> renderGeomFacing : register(t3);

RWTexture3D<float> sdfTex : register(u0);
RWStructuredBuffer<ProcessVoxel> solidCellBuffer : register(u1);
RWBuffer<uint> dispatchBuffer : register(u2);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int3 pos = int3(dispatchThreadID);
	int gridSizeI = int(GridSize.x);

	if (any(pos >= int3(gridSizeI, gridSizeI, gridSizeI)))
		return;

	// Compute SDF distance from JFA nearest-seed result
	uint4 jfaData = renderSdf[pos];
	float dist = 0.0;

	if (jfaData.w != 0) {
		int3 seedPos = int3(jfaData.xyz);
		float3 diff = float3(pos - seedPos);
		dist = length(diff);
	} else {
		// No seed found - maximum distance
		dist = float(gridSizeI);
	}

	// Normalize distance to [0, 1] range for R8_UNORM storage
	float normalizedDist = saturate(dist / float(gridSizeI));
	sdfTex[pos] = normalizedDist;

	// Check if this voxel is solid
	uint albedoVal = renderAlbedo[pos];
	if (!(albedoVal & 1u))
		return;

	// Atomically increment the voxel counter (stored in dispatchBuffer[3])
	uint index;
	InterlockedAdd(dispatchBuffer[3], 1u, index);

	// Build and store the ProcessVoxel
	uint positionPacked = (pos.x & 0x7F) | ((pos.y & 0x7F) << 7) | ((pos.z & 0x7F) << 14);

	// Pack albedo: 5 bits per channel from the voxel data (bits 1-15 of renderAlbedo)
	uint albedoPacked = (albedoVal >> 1) & 0x7FFF;

	// Read emission and facing data
	uint emissionVal = renderEmission[pos];
	uint facingVal = renderGeomFacing[pos];

	// Compute neighbor connectivity bits
	uint neighborBits = 0;
	uint neighborIdx = 0;
	static const int3 neighborOffsets[26] = {
		int3(-1, -1, -1), int3(0, -1, -1), int3(1, -1, -1),
		int3(-1, 0, -1), int3(0, 0, -1), int3(1, 0, -1),
		int3(-1, 1, -1), int3(0, 1, -1), int3(1, 1, -1),
		int3(-1, -1, 0), int3(0, -1, 0), int3(1, -1, 0),
		int3(-1, 0, 0), int3(1, 0, 0),
		int3(-1, 1, 0), int3(0, 1, 0), int3(1, 1, 0),
		int3(-1, -1, 1), int3(0, -1, 1), int3(1, -1, 1),
		int3(-1, 0, 1), int3(0, 0, 1), int3(1, 0, 1),
		int3(-1, 1, 1), int3(0, 1, 1), int3(1, 1, 1)
	};

	[unroll]
	for (neighborIdx = 0; neighborIdx < 26; neighborIdx++) {
		int3 nPos = pos + neighborOffsets[neighborIdx];
		if (any(nPos < 0) || any(nPos >= int3(gridSizeI, gridSizeI, gridSizeI)))
			continue;
		uint nAlbedo = renderAlbedo[nPos];
		if (nAlbedo & 1u) {
			neighborBits |= (1u << neighborIdx);
		}
	}

	// Pack neighbor bits across the 4 ProcessVoxel fields:
	// position: bits [21..31] = neighbors [0..10]
	// albedo:   bits [21..31] = neighbors [11..21]
	// light:    bits [30..31] = neighbors [22..23]
	// lightAniso: bits [30..31] = neighbors [24..25]
	positionPacked |= ((neighborBits & 0x7FF) << 21);
	albedoPacked |= (((neighborBits >> 11) & 0x7FF) << 21);

	ProcessVoxel voxel;
	voxel.position = positionPacked;
	voxel.albedo = albedoPacked;
	voxel.light = emissionVal | (((neighborBits >> 22) & 0x3) << 30);
	voxel.lightAniso = facingVal | (((neighborBits >> 24) & 0x3) << 30);

	solidCellBuffer[index] = voxel;

	// Update indirect dispatch group count: dispatchBuffer[0] = ceil(count / 64)
	// We do this atomically by comparing with the current max
	uint newCount = index + 1;
	uint groupCount = (newCount + 63) / 64;
	uint dummy;
	InterlockedMax(dispatchBuffer[0], groupCount, dummy);
}

#endif  // MODE_STORE

// ============================================================================
// MODE_SCROLL
// Scrolls existing SDF data when a cascade moves. Remaps coordinates by the
// Scroll offset and writes the shifted data. Out-of-bounds voxels get max
// distance (1.0).
// ============================================================================
#ifdef MODE_SCROLL

RWTexture3D<float> sdfTex : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int3 pos = int3(dispatchThreadID);
	int gridSizeI = int(GridSize.x);

	if (any(pos >= int3(gridSizeI, gridSizeI, gridSizeI)))
		return;

	int3 srcPos = pos + Scroll;

	if (any(srcPos < 0) || any(srcPos >= int3(gridSizeI, gridSizeI, gridSizeI))) {
		// Out of bounds after scroll - set to maximum distance
		sdfTex[pos] = 1.0;
	} else {
		sdfTex[pos] = sdfTex[srcPos];
	}
}

#endif  // MODE_SCROLL

// ============================================================================
// MODE_SCROLL_OCCLUSION
// Scrolls existing occlusion data when a cascade moves. Same remapping
// logic as MODE_SCROLL but for the occlusion texture.
// ============================================================================
#ifdef MODE_SCROLL_OCCLUSION

RWTexture3D<uint> occlusionTex : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int3 pos = int3(dispatchThreadID);
	int gridSizeI = int(GridSize.x);
	int probeDivisor = PROBE_DIVISOR;
	int3 probeGridSize = int3(gridSizeI / probeDivisor, gridSizeI / probeDivisor, gridSizeI / probeDivisor);

	if (any(pos >= probeGridSize))
		return;

	int3 srcPos = pos + Scroll;

	if (any(srcPos < 0) || any(srcPos >= probeGridSize)) {
		// Out of bounds after scroll - clear occlusion
		occlusionTex[pos] = 0;
	} else {
		occlusionTex[pos] = occlusionTex[srcPos];
	}
}

#endif  // MODE_SCROLL_OCCLUSION
