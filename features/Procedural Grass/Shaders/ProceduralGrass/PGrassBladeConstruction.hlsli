#ifndef __PGRASS_BLADE_CONSTRUCTION_HLSLI__
#define __PGRASS_BLADE_CONSTRUCTION_HLSLI__

// Smooth signed wind variation on a 512-unit world grid.
float CalculateWindNoise(float2 worldPosition)
{
	return Random::ValueNoise2D(worldPosition * (1.0f / 512.0f)) * 2.0f - 1.0f;
}

// Vanilla grass's gust waveform with smooth field variation and stable per-blade offsets.
float CalculateWindDisplacement(float2 worldPosition, float timer, float speed, float bladeHeight, float windNoise, float bladePhase, float bladeStrength)
{
	float gustAngle = 0.4f * ((worldPosition.x + worldPosition.y) * -0.0078125f + timer) + windNoise * 0.5f + bladePhase;

	float gustSin, gustCos;
	sincos(gustAngle, gustSin, gustCos);

	float gust0 = 0.2f * cos(Math::PI * gustCos);
	float gust1 = sin(Math::PI * gustSin);
	float gust2 = sin(Math::TAU * gustSin);
	float gustStrength = max(0.35f, (1.0f + windNoise * 0.35f) * bladeStrength);

	// Taller blades receive a stronger gust response. 150 units matches the maximum possible grass height.
	float heightResponse = bladeHeight * lerp(0.55f, 1.20f, saturate(bladeHeight * (1.0f / 150.0f)));
	return heightResponse * speed * gustStrength * ((gust1 + gust2) * 0.3f + gust0) * 0.5f;
}

float CalculateWindAdjustedAngle(float clumpedAngle, float angle, float rotationScale, float rotationalStiffness, float scaledWidth, float bladeHeight)
{
	float diff = angle - clumpedAngle;
	if (diff > Math::PI)
		diff -= Math::TAU;
	else if (diff < -Math::PI)
		diff += Math::TAU;

	float alignment = cos(diff) * 0.5f + 0.5f;
	float rotationFactor = lerp(0.2f, 1.0f, alignment * 0.5f);
	float totalRotation = rotationFactor * rotationScale * scaledWidth * bladeHeight;
	float reducedRotation = totalRotation * rcp(rotationalStiffness * totalRotation + 1.0f);
	float clampedRotation = min(reducedRotation, abs(diff)) * sign(diff);
	return clumpedAngle + clampedRotation;
}

bool PassesBladeLOD(float2 bladeWorldPos2D, bool cullsDisabled)
{
	if (cullsDisabled)
		return true;

	float2 lodOffset = abs(bladeWorldPos2D - grassLodOrigin);

#if defined(MID_LOD)
	float lodDistance = length(lodOffset);
	float inRamp = saturate((lodDistance - lodFadeIn.x) * lodFadeIn.y);
	float outRamp = lerp(1.0f, lodFadeOut.z, saturate((lodDistance - lodFadeOut.x) * lodFadeOut.y));

	float dither = LodDither(bladeWorldPos2D);
	return !((inRamp < 1.0f && dither <= 1.0f - inRamp) || dither > outRamp);
#else
	float lodDistanceSq = dot(lodOffset, lodOffset);
#	if defined(LOW_LOD)
	float lodFadeOutDistance = max(lodOffset.x, lodOffset.y);
#	else
	float lodFadeOutDistanceSq = lodDistanceSq;
#	endif

	float lodFadeInEnd = lodFadeIn.x + rcp(max(lodFadeIn.y, 1.0e-6f));
	float lodFadeInStartSq = lodFadeIn.x * lodFadeIn.x;
	float lodFadeInEndSq = lodFadeInEnd * lodFadeInEnd;

#	if defined(LOW_LOD)
	bool beforeFadeOut = lodFadeOutDistance <= lodFadeOut.x;
	bool afterFadeOut = lodFadeOutDistance >= lodFadeIn.w;
#	else
	float lodFadeOutStartSq = lodFadeOut.x * lodFadeOut.x;
	float lodFadeOutEndSq = lodFadeIn.w * lodFadeIn.w;
	bool beforeFadeOut = lodFadeOutDistanceSq <= lodFadeOutStartSq;
	bool afterFadeOut = lodFadeOutDistanceSq >= lodFadeOutEndSq;
#	endif

#	if defined(LOW_LOD)
	// Mid is only generated in the loaded cells. Beyond them Low has no tier to hand off to, so it stays whole
	// rather than thinning toward a Mid that is not there.
	if (any(bladeWorldPos2D < loadedLandBounds.xy) || any(bladeWorldPos2D > loadedLandBounds.zw)) {
		lodFadeInStartSq = 0.0f;
		lodFadeInEndSq = 0.0f;
	}

	if (lodDistanceSq <= lodFadeInStartSq)
		return false;
#	endif

	if (lodDistanceSq >= lodFadeInEndSq && beforeFadeOut)
		return true;

	float dither = LodDither(bladeWorldPos2D);
	if (lodDistanceSq >= lodFadeInEndSq && afterFadeOut)
		return dither <= lodFadeOut.z;

	float lodDistance = sqrt(lodDistanceSq);
	float inRamp = lodDistanceSq >= lodFadeInEndSq ? 1.0f : saturate((lodDistance - lodFadeIn.x) * lodFadeIn.y);
#	if defined(LOW_LOD)
	float outRamp = lerp(1.0f, lodFadeOut.z, smoothstep(0.0f, 1.0f, (lodFadeOutDistance - lodFadeOut.x) * lodFadeOut.y));
#	else
	float outRamp = lerp(1.0f, lodFadeOut.z, saturate((lodDistance - lodFadeOut.x) * lodFadeOut.y));
#	endif
#	if defined(LOW_LOD)
	if ((inRamp < 1.0f && dither <= 1.0f - inRamp) || dither > outRamp)
#	elif defined(HIGH_LOD)
	if (dither > min(inRamp, outRamp))
#	endif
		return false;
	return true;
#endif
}

// Finish one base or slope-fill candidate after establishing its terrain plane.
bool BuildBlade(uint3 initialHash, float2 mapSamplePos, float2 initialWorldPos2D, float bladeWorldZ, float2 terrainSlope, float terrainNormalZ,
	float2 quadWorldPos, uint quadrant, bool hasLand, uint packedGrassCell, bool cullsDisabled, bool insideFrustum, bool baseCandidate, out Blade b, out bool outerGeometry)
{
	b = (Blade)0;
	outerGeometry = false;

	uint3 hash = initialHash;
	float2 bladeWorldPos2D = initialWorldPos2D;
	float typeRandom = float(hash.z) * UINT_TO_FLOAT;

#if !defined(LOW_LOD)
	// Tier dithering controls density, so distance culling begins at the fade endpoint.
	if (!baseCandidate && !cullsDisabled) {
		float cullDistance = lodFadeIn.w;
		float2 cullOffset = bladeWorldPos2D - grassLodOrigin;
		if (dot(cullOffset, cullOffset) >= cullDistance * cullDistance)
			return false;
	}
#endif

	// Fetch the grass type after culling to avoid the four-sample lookup for rejected blades.
	uint type = ComputeGrassType(packedGrassCell, mapSamplePos, typeRandom, bladeWorldPos2D, float(hash.z & 0xFFFFu) * (1.0f / 65536.0f));
	if (type == 0u && !cullsDisabled)
		return false;
	type = max(type, 1u);

	GrassGeneratorType generatorType = generatorGrassType[type];
#if !defined(LOW_LOD)
	if (!cullsDisabled && (terrainNormalZ < generatorType.maxSlope || terrainNormalZ > generatorType.minSlope))
		return false;
#endif
#if defined(LOW_LOD)
	// Clump density reaches zero at half a grid cell, bounding the inward fade's displacement.
	float clumpReach = generatorType.clumpGridSize * 0.1125f * abs(generatorType.clumpDistanceFactor);
	float innerCullRadius = max(lodFadeIn.x - clumpReach - 1.0f, 0.0f);
	float2 initialLodOffset = bladeWorldPos2D - grassLodOrigin;
	if (!cullsDisabled && lodFadeIn.y > 0.0f && dot(initialLodOffset, initialLodOffset) < innerCullRadius * innerCullRadius)
		return false;
#endif

	float3 worldPos;
	float3 viewPos;

	// Delay the nine-cell clump search until after the inexpensive rejection tests.
	uint clumpRand;
	float clumpDist;
	float2 clumpDir;
	Random::FindNearestVoronoi2D(bladeWorldPos2D * generatorType.inverseClumpGridSize, clumpRand, clumpDist, clumpDir);

	// Height, facing, lean, and colour belong to the whole Voronoi cell. Only the pull and base AO fall off with distance.
	float clumpDensity = 1.0f - smoothstep(0.15f, 0.50f, clumpDist);

	hash = Random::pcg3d(hash);
	float clumpDistRand = float(hash.x) * UINT_TO_FLOAT;
	float heightRand = float(hash.y) * UINT_TO_FLOAT;
	float angleRand = float(hash.z) * UINT_TO_FLOAT;

	// Pull every near blade toward its Voronoi feature to hide the regular candidate lattice.
	float clumpPull = lerp(0.025f, 0.225f, clumpDistRand);
	float2 clumpDisplace = clumpDir * generatorType.clumpGridSize * clumpPull * generatorType.clumpDistanceFactor * clumpDensity;
	bladeWorldPos2D += clumpDisplace;

#if defined(LOW_LOD)
	if (!PassesBladeLOD(bladeWorldPos2D, cullsDisabled))
		return false;
	float2 displacedQuadPos = bladeWorldPos2D - quadWorldPos;
	if (hasLand && all(displacedQuadPos >= 0.0f) && all(displacedQuadPos < 2048.0f)) {
		bladeWorldZ = TerrainHeightSlopeAt(terrainSlope, bladeWorldPos2D, quadWorldPos, quadrant, true);
		terrainNormalZ = rsqrt(dot(terrainSlope, terrainSlope) + 1.0f);
	} else {
		bladeWorldZ += dot(terrainSlope, clumpDisplace);
	}
	if (!cullsDisabled && (terrainNormalZ < generatorType.maxSlope || terrainNormalZ > generatorType.minSlope))
		return false;
#else
	bladeWorldZ += dot(terrainSlope, clumpDisplace);
#endif

	worldPos = float3(bladeWorldPos2D, bladeWorldZ);
	viewPos = worldPos - FrameBuffer::CameraPosAdjust.xyz;

	if (!insideFrustum) {
		// A root outside the frustum can still produce visible blade geometry near the edge.
		float widthExtent = generatorType.width * 2.5f * 1.3f;
#if defined(LOW_LOD)
		widthExtent *= DistantWidthScale * (1.0f + miscParams.z);
#else
		widthExtent *= 1.0f + miscParams.z;
#endif
		float geometryExtent = generatorType.height + widthExtent;
#if defined(LOW_LOD)
		geometryExtent += GetTerrainLiftReach(bladeWorldPos2D, 0.0f);
#endif
		if (!cullsDisabled && IsOutsideFrustum(viewPos, geometryExtent))
			return false;
	}

#if !defined(LOW_LOD)
	if (!PassesBladeLOD(bladeWorldPos2D, cullsDisabled))
		return false;
#endif

#if defined(LOW_LOD)
	if (!cullsDisabled)
		viewPos.z += GetTerrainLift(bladeWorldPos2D, bladeWorldZ);
#endif

	// Height generation is deferred until after rejection because the frustum test uses type bounds.
	// Blades take a share of their clump's height, so neighbouring clumps stand at different heights.
	float clumpHeightRandom = float(clumpRand) * UINT_TO_FLOAT;
	float unscaledHeight = 0.45f + lerp(heightRand, clumpHeightRandom, generatorType.clumpHeightFactor) * 0.55f;
	float randHeight = generatorType.height * unscaledHeight;

	// Store the authored width variation. High also bakes its stable distance widening below.
	float unscaledWidth = 1.0f;
	float widthRand = frac(heightRand * 1.618f + angleRand * 0.5f);
	unscaledWidth *= lerp(0.6f, 1.0f, widthRand);
	float scaledWidth = unscaledWidth * generatorType.width * 2.5f;

	hash = Random::pcg3d(hash);
	float clumpedAngle = angleRand * Math::TAU;
	// Positive factors splay a clump away from its centre; negative factors turn it inward.
	float2 clumpFacingDir = clumpDir * -sign(generatorType.clumpFacingFactor);
	clumpedAngle = Math::LerpAngle(clumpedAngle, atan2(clumpFacingDir.y, clumpFacingDir.x), abs(generatorType.clumpFacingFactor));

	// Every tier shares the per-clump lean, since a common direction changes how distant clumps shade.
	[branch] if (generatorType.clumpLeanFactor > 0.0f)
	{
		uint leanState = clumpRand;
		float clumpLeanAngle = float(Random::pcg(leanState)) * UINT_TO_FLOAT * Math::TAU;
		clumpedAngle = Math::LerpAngle(clumpedAngle, clumpLeanAngle, generatorType.clumpLeanFactor);
	}
	if (generatorType.slopeFacing > 0.0f) {
		float steepness = sqrt(saturate(1.0f - terrainNormalZ * terrainNormalZ));
		if (steepness > 1e-4f)
			clumpedAngle = Math::LerpAngle(clumpedAngle, atan2(-terrainSlope.y, -terrainSlope.x), generatorType.slopeFacing * steepness);
	}

	// Turn toward the wind without rotating beyond it.
	float windAdjustedAngle = CalculateWindAdjustedAngle(clumpedAngle, windAngle, windRotationScale, generatorType.rotationalStiffness, scaledWidth, randHeight);
#if defined(HIGH_LOD) || defined(MID_LOD)
	float windNoise = CalculateWindNoise(bladeWorldPos2D);
	float bladeWindPhase = (float(hash.x) * UINT_TO_FLOAT - 0.5f) * 0.45f;
	float bladeWindStrength = lerp(0.65f, 1.35f, float(hash.y) * UINT_TO_FLOAT);
	float windDisplacement = CalculateWindDisplacement(bladeWorldPos2D, SharedData::Timer, windSpeed, randHeight, windNoise, bladeWindPhase, bladeWindStrength);
#	if defined(HIGH_LOD)
	float previousWindDisplacement = CalculateWindDisplacement(bladeWorldPos2D, SharedData::Timer - miscParams.w, previousWindSpeed, randHeight, windNoise, bladeWindPhase, bladeWindStrength);
#	else
	float previousWindDisplacement = 0.0f;
#	endif
#else
	float windDisplacement = 0.0f;
	float previousWindDisplacement = 0.0f;
#endif

	float facingSin, facingCos;
	sincos(windAdjustedAngle, facingSin, facingCos);
	float2 randFacing = float2(facingCos, facingSin);

	float storedWidth = unscaledWidth;

#if defined(HIGH_LOD)
	float appearanceDistance = ApproximateGrassDistance(bladeWorldPos2D - grassLodOrigin);
	float detailFade = 1.0f - smoothstep(512.0f, 1536.0f, appearanceDistance);
	outerGeometry = float(hash.z) * UINT_TO_FLOAT >= detailFade;
	float distanceWidth = lerp(0.4f, 1.0f, saturate((appearanceDistance - 1024.0f) * (1.0f / 3072.0f)));
	storedWidth *= distanceWidth;
	uint packedWidth = (uint)round(saturate(storedWidth) * 255.0f);
#else
	uint packedWidth = (uint)(unscaledWidth * 255.0f);
#endif

	uint packedHeight = (uint)(unscaledHeight * 255.0f);
#if defined(LOW_LOD)
	float lowDrawHeight = generatorType.height * float(packedHeight) * (1.0f / 255.0f);
	uint storedHeight = lowDrawHeight <= 45.0f;
#	if defined(LOW_OUTER_GEOMETRY)
	// Switch single blades after the Mid handoff, only when their tip is narrower than half a pixel.
	float outerStart = lodFadeIn.x + rcp(max(lodFadeIn.y, 1.0e-6f));
	float outerKeep = saturate((length(bladeWorldPos2D - grassLodOrigin) - outerStart) * (1.0f / 1024.0f));
	float lowTipWidth = generatorType.width * 2.5f * DistantWidthScale * lerp(0.45f, 1.3f, float(packedWidth) * (1.0f / 255.0f)) * 0.06f;
	float tipViewDepth = mul(FrameBuffer::CameraViewProjUnjittered, float4(viewPos, 1.0f)).w - generatorType.height;
	float projectedTipWidth = lowTipWidth * max(cameraViewRow0Sum + abs(FrameBuffer::CameraProj._m00) * miscParams.z, cameraViewRow1Sum) /
	                          (max(tipViewDepth, 1.0f) * min(dynamicResolutionInverted.x, dynamicResolutionInverted.y));
	outerKeep *= 1.0f - smoothstep(0.25f, 0.5f, projectedTipWidth);
	outerGeometry = storedHeight == 0u && float(hash.z) * UINT_TO_FLOAT < outerKeep;
#	endif
#else
	uint storedHeight = packedHeight;
#endif
	b.posXY = f32tof16(viewPos.x) << 16 | f32tof16(viewPos.y);
	b.posZWidthHeight = f32tof16(viewPos.z) << 16 | packedWidth << 8 | storedHeight;

	// Keep the geometry hash independent of the camera-dependent view-thickening byte.
	uint stableBladeHash = (hash.z << 12) | ((clumpRand & 15u) << 8) | type;

#if defined(MID_LOD) || defined(HIGH_LOD) || defined(LOW_LOD)
	// Every near tier widens a blade toward the view as it turns edge-on, from either side, for each double-blade facing.
	float2 viewOffset = grassLodOrigin - bladeWorldPos2D;
	float2 viewDirection = viewOffset * rsqrt(max(dot(viewOffset, viewOffset), 1.0e-4f));
	float2 rotatedFacing = float2(randFacing.x * 0.8660254f - randFacing.y * 0.5f, randFacing.x * 0.5f + randFacing.y * 0.8660254f);
	float2 viewDotNormal2 = float2(dot(randFacing, viewDirection), dot(rotatedFacing, viewDirection));
	viewDotNormal2 *= viewDotNormal2;
	float2 viewThicken = saturate(1.0f - viewDotNormal2 * viewDotNormal2);
	uint2 packedViewThickens = (uint2)round(viewThicken * 15.0f);
#endif
#if defined(MID_LOD) || defined(LOW_LOD)
	uint packedViewThicken = packedViewThickens.x | packedViewThickens.y << 4;
	uint packedClumpDensity = (uint)round(clumpDensity * 255.0f);
	uint hashClumpAndGrassType = type | (clumpRand & 0xFFu) << 8 | packedViewThicken << 16 | packedClumpDensity << 24;
#elif defined(HIGH_LOD)
	// High has one nibble for both facings; the canopy takes the top byte.
	uint packedClumpDensity = (uint)round(clumpDensity * 15.0f);
	uint packedViewThicken = max(packedViewThickens.x, packedViewThickens.y);
	uint hashClumpAndGrassType = type | (clumpRand & 0xFFu) << 8 | packedClumpDensity << 16 | packedViewThicken << 20;
#else
	uint hashClumpAndGrassType = stableBladeHash;
#endif

	// Precompute the blade tilt.
	uint2 tiltHash = Random::pcg2d(uint2(stableBladeHash, 0u));
	float randTilt = generatorType.tipWeight * (float(tiltHash.x) * UINT_TO_FLOAT * 1.4f + 0.30f);

	uint packedRandBend = (uint)round(saturate(float(tiltHash.y) * UINT_TO_FLOAT) * 15.0f);

#if !defined(LOW_LOD)
	// Only detailed materials consume the packed per-blade colour. Outer High blades skip it, but their statistical
	// blade shadows still seed from these bits, so give them a stable random value instead of one shared by every blade.
	uint packedBladeColor = 0u;
#	if defined(HIGH_GEOMETRY_LOD)
	packedBladeColor = tiltHash.y & 0xFFFu;
	[branch] if (!outerGeometry)
#	endif
	{
		GrassType surfaceType = grassType[type];
		float bladeColorRand = float(tiltHash.x) * UINT_TO_FLOAT;
		float bladeValueRand = float(tiltHash.y) * UINT_TO_FLOAT;
		float3 hueTint = lerp(surfaceType.grassColorCool.rgb, surfaceType.grassColorWarm.rgb, bladeColorRand);
		float bladeValue = 1.0f + (bladeValueRand * 2.0f - 1.0f) * surfaceType.grassColorVar.y;
		float3 perBladeColor = lerp(1.0f, hueTint, surfaceType.grassColorVar.x) * bladeValue;

		float clumpColorRand = (float(clumpRand & 0xFFu) + 0.5f) * (1.0f / 256.0f);
		float clumpValueRand = (float((clumpRand >> 8) & 0xFFu) + 0.5f) * (1.0f / 256.0f);
		float3 clumpTint = lerp(surfaceType.grassColorCool.rgb, surfaceType.grassColorWarm.rgb, clumpColorRand);
		float clumpValue = 1.0f + (clumpValueRand * 2.0f - 1.0f) * surfaceType.grassColorVar.y * 0.75f;
		perBladeColor *= lerp(1.0f, clumpTint * clumpValue, surfaceType.clumpColorStrength);

		// Pack blade-wide colour variation.
		uint3 packedColor = (uint3)round(saturate(perBladeColor * 0.5f) * 15.0f);
		packedBladeColor = packedColor.x | packedColor.y << 4u | packedColor.z << 8u;
	}
	uint packedBladeData = packedBladeColor | packedRandBend << 12u;
#endif

	float tiltSin, tiltCos;
	sincos(randTilt, tiltSin, tiltCos);
	int2 packedFacing = (int2)round(clamp(randFacing, -1.0f, 1.0f) * 127.0f);

#if defined(LOW_LOD)
	uint lowTiltX = f32tof16(tiltSin);
	uint lowTiltY = f32tof16(tiltCos);
	float2 lowTip = float2(f16tof32(lowTiltX), f16tof32(lowTiltY)) * lowDrawHeight;
	float lowWidthScale = float(packedWidth) * (1.0f / 255.0f);
	float lowRandWidth = generatorType.width * 2.5f * DistantWidthScale * lerp(0.45f, 1.3f, lowWidthScale);
	float2 packedFacingValue = float2(packedFacing) * (1.0f / 127.0f);
	float packedWidthValue = f16tof32(f32tof16(lowRandWidth));
	float2 lowBaseAxis = float2(-packedFacingValue.y, packedFacingValue.x) * packedWidthValue;
	b.posZWidthHeight = f32tof16(viewPos.z) << 16 | packedRandBend << 1 | storedHeight;
	float3 packedRoot = float3(f16tof32(b.posXY >> 16), f16tof32(b.posXY), f16tof32(b.posZWidthHeight >> 16));

	// Test the geometry the VS will draw: packed root, tip along the facing, and view-thickened width.
	float3 packedTip = packedRoot + float3(packedFacingValue * lowTip.x, lowTip.y);
	float bladeRadius = packedWidthValue * (1.0f + miscParams.z) + 1.0f;
	if (IsBladeOccluded(packedRoot, packedTip, bladeRadius, cullsDisabled)) {
		if (storedHeight == 0u)
			return false;
		float2 turnedFacing = float2(dot(packedFacingValue, float2(0.8660254f, -0.5f)), dot(packedFacingValue, float2(0.5f, 0.8660254f)));
		float3 turnedTip = packedRoot + float3(turnedFacing * lowTip.x, lowTip.y);
		if (IsBladeOccluded(packedRoot, turnedTip, bladeRadius, cullsDisabled))
			return false;
	}
#endif

#if defined(HIGH_LOD)
	float2 densityUV = (bladeWorldPos2D - occlusionParams.xy) * occlusionInvExtent + 0.5f;
	float onMapDensity = 1.0f;
	float densityEdgeFade = 0.0f;
	if (all(densityUV >= 0.0f) && all(densityUV <= 1.0f)) {
		uint densityDimension = max((uint)grassAOParams.x, 1u);
		uint2 densityTexel = min(uint2(densityUV * densityDimension), densityDimension - 1u);
		float bladeCount = GrassDensityTexture[densityTexel];
		onMapDensity = saturate(bladeCount / max(grassAOParams.z, 1.0f));
		densityEdgeFade = saturate(min(min(densityUV.x, 1.0f - densityUV.x), min(densityUV.y, 1.0f - densityUV.y)) * 10.0f);
	}

	float canopyDensity = lerp(1.0f, onMapDensity, densityEdgeFade);
	float canopyAODensity = onMapDensity * densityEdgeFade;
	uint packedCanopy = (uint)round(canopyDensity * 15.0f) | (uint)round(canopyAODensity * 15.0f) << 4;
	hashClumpAndGrassType |= packedCanopy << 24;

	float worldShadow = 1.0f;
#	if defined(TERRAIN_SHADOWS)
	worldShadow *= TerrainShadows::GetTerrainShadow(worldPos, LinearSampler);
#	endif
#	if defined(CLOUD_SHADOWS)
	worldShadow *= CloudShadows::GetCloudShadowMult(viewPos, LinearSampler);
#	endif

	uint2 packedTilt = (uint2)round(saturate(float2(tiltSin, tiltCos) * 0.5f + 0.5f) * 255.0f);
	uint packedWorldShadow = (uint)round(saturate(worldShadow) * 255.0f);
	b.tipDir = packedTilt.x | packedTilt.y << 8 | packedWorldShadow << 16;
#elif defined(MID_LOD)
	uint2 packedTilt = (uint2)round(saturate(float2(tiltSin, tiltCos) * 0.5f + 0.5f) * 255.0f);
	float appearanceDistance = ApproximateGrassDistance(bladeWorldPos2D - grassLodOrigin);
	uint packedLodDistance = (uint)round(saturate(appearanceDistance * (1.0f / 6144.0f)) * 65535.0f);
	b.tipDir = packedTilt.x | packedTilt.y << 8 | packedLodDistance << 16;
#	if defined(MID_OUTER_GEOMETRY)
	// Once fully morphed to Low's straight profile, a single blade's middle rung lies on the edges between base and
	// tip, so one triangle draws it. Double blades keep the inner geometry for their two halves.
	bool midDoubleBlade = generatorType.height * float(packedHeight) * (1.0f / 255.0f) <= 45.0f;
	outerGeometry = packedLodDistance == 65535u && !midDoubleBlade;
#	endif
#else
	b.tipDir = f32tof16(lowTip.x) << 16 | f32tof16(lowTip.y);
#endif
	b.hashClumpAndGrassType = hashClumpAndGrassType;

#if defined(LOW_LOD)
	b.facingAndWind = (uint)(packedFacing.x & 0xFF) | (uint)(packedFacing.y & 0xFF) << 8 | f32tof16(lowRandWidth) << 16;
	b.previousWind = f32tof16(lowBaseAxis.x) << 16 | f32tof16(lowBaseAxis.y);
#else
	b.facingAndWind = (uint)(packedFacing.x & 0xFF) | (uint)(packedFacing.y & 0xFF) << 8 | f32tof16(windDisplacement) << 16;
	b.previousWind = packedBladeData << 16 | f32tof16(previousWindDisplacement);
#endif

#if defined(MID_LOD)
	b.skylightingRoot = viewPos;
#elif !defined(LOW_LOD)
#	if defined(SKYLIGHTING)
	SkylightingProbeFrame probes = GetSkylightingProbeFrame();
	sh2 skylightingSH = Skylighting::SampleWithOrigin(viewPos, float3(0.0f, 0.0f, 1.0f), probes.offset, probes.arrayOrigin);

	b.skylightingSH0 = f32tof16(skylightingSH.x) << 16 | f32tof16(skylightingSH.y);
	b.skylightingSH1 = f32tof16(skylightingSH.z) << 16 | f32tof16(skylightingSH.w);
#	else
	b.skylightingSH0 = 0u;
	b.skylightingSH1 = 0u;
#	endif
#endif

#if defined(PGRASS_CACHED_COLLISION)
	// Cache one tip collision sample. The VS scales it smoothly from the anchored root.
	float2 collisionTip = float2(tiltSin, tiltCos) * randHeight;
	float3 collisionTipViewPos = viewPos + float3(randFacing * collisionTip.x, collisionTip.y);
	collisionTipViewPos.xy += windDir * windDisplacement;

	float3 collisionDisplacement;
	float collisionNearFactor = smoothstep(2048.0f, 0.0f, length(collisionTipViewPos.xy));

#	if defined(MID_LOD)
	float3 unusedPreviousCollisionDisplacement;
	GrassCollision::GetDisplacedPosition(collisionTipViewPos, viewPos, 1.0f, collisionNearFactor, true, 0.75f,
		collisionDisplacement, unusedPreviousCollisionDisplacement);
	b.collisionData = f32tof16(collisionDisplacement.x) << 16 | f32tof16(collisionDisplacement.y);
	b.previousWind = packedBladeData << 16 | f32tof16(collisionDisplacement.z);
#	else
	float3 previousCollisionDisplacement;
	GrassCollision::GetDisplacedPosition(collisionTipViewPos, viewPos, 1.0f, collisionNearFactor, true, 0.75f,
		collisionDisplacement, previousCollisionDisplacement);

	b.collisionData.x = f32tof16(collisionDisplacement.x) << 16 | f32tof16(collisionDisplacement.y);
	b.collisionData.y = f32tof16(collisionDisplacement.z) << 16 | f32tof16(previousCollisionDisplacement.x);
	b.collisionData.z = f32tof16(previousCollisionDisplacement.y) << 16 | f32tof16(previousCollisionDisplacement.z);
#	endif
#endif

	return true;
}

#endif
