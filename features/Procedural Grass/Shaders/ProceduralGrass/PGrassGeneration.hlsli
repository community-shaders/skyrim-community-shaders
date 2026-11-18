#ifndef __PGRASS_GENERATION_HLSLI__
#define __PGRASS_GENERATION_HLSLI__

// One thread's patch after its LOD, grass-map, terrain, and occlusion tests, ready to emit its candidates.
struct PatchCandidates
{
	uint2 patchPos;
	uint quadrant;
	uint quadrantHash;
	float2 quadWorldPos;
	uint bladeIndex;
	// Flags are 0 or 1: bool members here compiled wrongly at /O3 on AMD.
	uint hasLand;
	uint insideFrustum;
	uint cullsDisabled;
	uint useBasePath;
	uint3 baseHash;
	float2 baseWorldPos2D;
	float2 baseMapSamplePos;
	uint baseGrassCell;
	float2 terrainSlope;
	float baseWorldZ;
	float terrainNormalZ;
	float extraKeep;  // The slope fill's keep share.
	uint candidateCount;
};

/** @brief Resolves a thread's patch and its shared terrain plane. Returns false when the patch emits nothing. */
bool PreparePatchCandidates(uint3 dispatch, out PatchCandidates candidates)
{
	candidates = (PatchCandidates)0;

	uint patch = dispatch.x;
	uint bladeTask = LoadBladeTask(dispatch.z);
	uint bladeIndex = (bladeTask >> WORK_LANE_SHIFT) & 0xFu;
	uint quadrant = bladeTask & WORK_QUADRANT_MASK;

	bool hasLand = (bladeTask & WORK_HAS_LAND) != 0u;
	bool insideFrustum = (bladeTask & WORK_INSIDE_FRUSTUM) != 0u;

	// Full-quadrant dispatches round up to whole groups, so also reject the tail past the last patch. Far's compact work
	// generates only its share of the tile's or quadrant's patches, spread over them.
	bool patchResolved = ResolveTilePatch(bladeTask, patch);
	if (!patchResolved)
		return false;

	bool cullsDisabled = debugFlags.x > 0.5f;
	QuadrantData quadrantData = data[quadrant];
	uint2 patchPos = uint2(patch % PATCHES_PER_ROW, patch / PATCHES_PER_ROW);
	uint quadrantHash = quadrantData.quadrantHash;
	if (!cullsDisabled && (bladeTask & WORK_FULL_GRASS) == 0u && !PatchHasGrass(patchPos, quadrant))
		return false;

	// Preserve the base blade slot's position and seed.
	uint3 baseHash;
	uint2 gridPos = BaseGridPosition(patchPos, bladeIndex);
	baseHash = Random::pcg3d(uint3(gridPos, quadrantHash));
	float2 baseQuadPos2D = BaseQuadrantPosition(gridPos, baseHash);
	float2 baseWorldPos2D = baseQuadPos2D + quadrantData.quadWorldPos;
	float2 baseMapSamplePos = GrassMapSamplePos(baseQuadPos2D, baseWorldPos2D, baseHash);
	uint baseGrassCell = 0u;
	bool useBasePath = true;
#if !defined(LOW_LOD)
	if (!cullsDisabled) {
		float2 baseLodXY = baseWorldPos2D - grassLodOrigin;
		float baseDistSq = dot(baseLodXY, baseLodXY);
		float baseCullDist = lodFadeIn.w;
		if (baseDistSq >= baseCullDist * baseCullDist)
			useBasePath = false;
	}
#endif

	if (useBasePath) {
		baseGrassCell = LoadGrassCell(baseMapSamplePos, quadrant);

		if (!cullsDisabled && baseGrassCell == 0u)
			useBasePath = false;
	}

#if SLOPE_EXTRA_BLADES > 0
	if (!useBasePath && bladeIndex >= SLOPE_EXTRA_BLADES)
		return false;
#else
	if (!useBasePath)
		return false;
#endif

	// Share the sampled surface plane when deciding how many slope extras to generate.
	float2 terrainSlope;
	float baseWorldZ = TerrainHeightSlopeAt(terrainSlope, baseWorldPos2D, quadrantData.quadWorldPos, quadrant, hasLand);
	if (useBasePath && IsPatchOccluded(baseWorldPos2D, baseWorldZ, terrainSlope, quadrant, hasLand, cullsDisabled))
		return false;

	candidates.patchPos = patchPos;
	candidates.quadrant = quadrant;
	candidates.quadrantHash = quadrantHash;
	candidates.quadWorldPos = quadrantData.quadWorldPos;
	candidates.bladeIndex = bladeIndex;
	candidates.hasLand = hasLand ? 1u : 0u;
	candidates.insideFrustum = insideFrustum ? 1u : 0u;
	candidates.cullsDisabled = cullsDisabled ? 1u : 0u;
	candidates.useBasePath = useBasePath ? 1u : 0u;
	candidates.baseHash = baseHash;
	candidates.baseWorldPos2D = baseWorldPos2D;
	candidates.baseMapSamplePos = baseMapSamplePos;
	candidates.baseGrassCell = baseGrassCell;
	candidates.terrainSlope = terrainSlope;
	candidates.baseWorldZ = baseWorldZ;
	candidates.terrainNormalZ = rsqrt(dot(terrainSlope, terrainSlope) + 1.0f);
	candidates.candidateCount = 1u + SLOPE_EXTRA_BLADES;

#if SLOPE_EXTRA_BLADES > 0
	// Reject slope extras before grass typing, clumping, LOD, occlusion, wind, and packing.
	float baseSlopeKeep = GetSlopeFillKeep(candidates.terrainNormalZ);
	candidates.extraKeep = baseSlopeKeep;
#endif
	return true;
}

/** @brief Builds one candidate: the patch's base blade at index 0, or one of its extras. Returns false when it is rejected. */
bool BuildPatchCandidate(PatchCandidates candidates, uint candidateIndex, out Blade blade, out bool outerGeometry)
{
	blade = (Blade)0;
	outerGeometry = false;

	bool isBase = candidateIndex == 0;
	uint3 candidateHash = candidates.baseHash;
	float2 candidateWorldPos = candidates.baseWorldPos2D;
	float2 candidateMapSamplePos = candidates.baseMapSamplePos;
	uint packedGrassCell = candidates.baseGrassCell;
	float candidateWorldZ = candidates.baseWorldZ;
	bool cullsDisabled = candidates.cullsDisabled != 0u;

	if (isBase) {
		if (candidates.useBasePath == 0u)
			return false;
	} else {
#if SLOPE_EXTRA_BLADES > 0
		uint emitExtraIndex = candidateIndex - 1;
		if ((emitExtraIndex % PATCH_BLADE_COUNT) != candidates.bladeIndex)
			return false;

		candidateHash = ExtraCandidateHash(candidates.patchPos, emitExtraIndex, candidates.quadrantHash);
		float slopeRoll = float(candidateHash.z) * UINT_TO_FLOAT;
		if (!cullsDisabled && slopeRoll > candidates.extraKeep)
			return false;

		float2 candidateQuadPos = ExtraCandidateQuadPos(candidates.patchPos, candidateHash);
		candidateWorldPos = candidateQuadPos + candidates.quadWorldPos;
		candidateMapSamplePos = GrassMapSamplePos(candidateQuadPos, candidateWorldPos, candidateHash);
		packedGrassCell = LoadGrassCell(candidateMapSamplePos, candidates.quadrant);
		if (!cullsDisabled && packedGrassCell == 0u)
			return false;
		candidateWorldZ = candidates.baseWorldZ + dot(candidates.terrainSlope, candidateWorldPos - candidates.baseWorldPos2D);
#else
		return false;
#endif
	}

	return BuildBlade(candidateHash, candidateMapSamplePos, candidateWorldPos, candidateWorldZ, candidates.terrainSlope, candidates.terrainNormalZ,
		candidates.quadWorldPos, candidates.quadrant, candidates.hasLand != 0u, packedGrassCell, cullsDisabled, candidates.insideFrustum != 0u, isBase, blade, outerGeometry);
}

// Stages a built blade in this thread's next group slot.
void StageBlade(Blade blade, bool outerGeometry, uint groupIndex, inout uint2 emittedBladeCounts)
{
	uint slot = (emittedBladeCounts.x + emittedBladeCounts.y) * THREADGROUP_SIZE + groupIndex;
	GroupBlades[slot] = blade;
#if defined(PGRASS_OUTER_LIST)
	GroupBladeOuter[slot] = outerGeometry ? 1u : 0u;
#endif

	if (outerGeometry)
		emittedBladeCounts.y++;
	else
		emittedBladeCounts.x++;
}

void GenerateThreadBlades(uint3 dispatch, uint groupIndex, out uint2 emittedBladeCounts)
{
	emittedBladeCounts = 0u;

	PatchCandidates candidates;
	if (!PreparePatchCandidates(dispatch, candidates))
		return;

	// Keep one emit path, unrolled: AMD's driver has miscompiled the loop form of the Mid generator, passing the extra
	// candidate a wrong position so that it emitted no blades.
	[unroll] for (uint candidateIndex = 0; candidateIndex < 1u + SLOPE_EXTRA_BLADES; ++candidateIndex)
	{
		Blade blade;
		bool outerGeometry;
		if (BuildPatchCandidate(candidates, candidateIndex, blade, outerGeometry))
			StageBlade(blade, outerGeometry, groupIndex, emittedBladeCounts);
	}
}

#endif
