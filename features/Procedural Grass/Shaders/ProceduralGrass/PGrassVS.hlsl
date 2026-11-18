#include "Common/FrameBuffer.hlsli"
#include "Common/Random.hlsli"

#define PSHADER
#include "Common/SharedData.hlsli"
#undef PSHADER

#include "ProceduralGrass/PGrassCommon.hlsli"

#define VSHADER
#define FRAMEBUFFER

StructuredBuffer<Blade> Blades : register(t0);
#if defined(HIGH_OUTER_VERTEX) || defined(BLADE_BATCH_SIZE)
ByteAddressBuffer IndirectArgs : register(t1);
#endif

#include "ProceduralGrass/PGrassMaterial.hlsli"
#include "ProceduralGrass/PGrassTierIO.hlsli"

GrassTierIO main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
	GrassTierIO o;

#if defined(BLADE_BATCH_SIZE)
	// Each instance draws a fixed batch of blades; discard the final batch's unused tail.
#	if defined(MID_OUTER_VERTEX) || defined(LOW_OUTER_VERTEX)
	const uint verticesPerBlade = 3u;
#	elif defined(MID_VERTEX)
	const uint verticesPerBlade = 5u;
#	else
	const uint verticesPerBlade = 4u;
#	endif

	instanceID = instanceID * BLADE_BATCH_SIZE + vertexID / verticesPerBlade;
	vertexID %= verticesPerBlade;

#	if defined(LOW_OUTER_VERTEX) || defined(MID_OUTER_VERTEX)
	[branch] if (instanceID >= IndirectArgs.Load(24u))
#	else
	[branch] if (instanceID >= IndirectArgs.Load(4u))
#	endif
	{
		o = (GrassTierIO)0;
		o.Position = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return o;
	}

#	if defined(LOW_OUTER_VERTEX) || defined(MID_OUTER_VERTEX)
	instanceID += IndirectArgs.Load(36u);
#	endif
#endif

#if defined(HIGH_OUTER_VERTEX)
	// D3D11 does not add StartInstanceLocation to SV_InstanceID. Outer High lives at the tail of the shared blade buffer.
	instanceID += IndirectArgs.Load(36u);
#endif

	Blade blade = Blades[instanceID];

#if defined(HIGH_OUTER_VERTEX)
	// Outer High shares Mid's shape: base, midpoint, and tip, with one triangle per half of a double blade.
	static const float LEVELS = 2.0f;
	static const float DOUBLE_LEVELS = 1.0f;
	static const float MID_LEVEL = 1.0f;
	bool isBlade1 = vertexID >= 4u;

#elif defined(HIGH_VERTEX)
	static const float LEVELS = 7.0f;
	static const float DOUBLE_LEVELS = 4.0f;
	static const float MID_LEVEL = 3.0f;
	bool isBlade1 = vertexID >> 3;

#elif defined(MID_OUTER_VERTEX)
	// Far, and Mid's straight single blades, use one tapered triangle.
	static const float LEVELS = 1.0f;
	static const float DOUBLE_LEVELS = 1.0f;
	static const float MID_LEVEL = 1.0f;
	bool isBlade1 = false;

#elif defined(MID_VERTEX)
	// Mid uses base, midpoint, and tip. Double blades use one triangle per half.
	static const float LEVELS = 2.0f;
	static const float DOUBLE_LEVELS = 1.0f;
	static const float MID_LEVEL = 1.0f;
	bool isBlade1 = vertexID >> 2;

#else  // LOW_VERTEX
	bool isBlade1 = false;
#endif

#if !defined(LOW_VERTEX)
	static const float INV_LEVELS = 1.0f / LEVELS;
	static const float INV_DOUBLE_LEVELS = 1.0f / DOUBLE_LEVELS;
	static const float INV_MID_LEVEL = 1.0f / MID_LEVEL;
#endif

	uint hashClumpAndGrassType = blade.hashClumpAndGrassType;
	uint grassTypeIndex = hashClumpAndGrassType & 0xFFu;
	uint clumpSeed = (hashClumpAndGrassType >> 8) & 0xFFu;

#if !defined(LOW_LOD)
#	if defined(DEPTH) || defined(MID_VERTEX)
	GrassGeneratorType bladeType = generatorGrassType[grassTypeIndex];
#	else
	GrassType bladeType = grassType[grassTypeIndex];
#	endif
#endif
	float3 rootViewPosition = float3(
		f16tof32(blade.posXY >> 16),
		f16tof32(blade.posXY),
		f16tof32(blade.posZWidthHeight >> 16));
#if defined(MID_LOD)
	float rootDistance = float(blade.tipDir >> 16) * (6144.0f / 65535.0f);
#endif

#if !defined(DEPTH) && defined(HIGH_LOD)
	uint packedCanopy = hashClumpAndGrassType >> 24;
	uint packedCanopyShadow = packedCanopy | ((blade.tipDir >> 16) & 0xFFu) << 8;
#endif

#if !defined(LOW_LOD)
	float2 tiltDir;
#endif

	int2 packedFacing = int2(blade.facingAndWind << 24, blade.facingAndWind << 16) >> 24;
	float2 randFacing = float2(packedFacing) * (1.0f / 127.0f);

#if defined(LOW_LOD)
	float randWidth = f16tof32(blade.facingAndWind >> 16);
	float2 tip = float2(f16tof32(blade.tipDir >> 16), f16tof32(blade.tipDir));
	float2 baseAxis = float2(f16tof32(blade.previousWind >> 16), f16tof32(blade.previousWind));
#else
	float windDisplacement = f16tof32(blade.facingAndWind >> 16);
#	if defined(HIGH_LOD) && !defined(DEPTH)
	float previousWindDisplacement = f16tof32(blade.previousWind);
#	endif

	uint packedBladeData = blade.previousWind >> 16;
	uint packedBladeColor = packedBladeData & 0xFFFu;
	float randBend = bladeType.stiffness * (0.25f + float(packedBladeData >> 12) * (1.6f / 15.0f));

#	if defined(HIGH_LOD)
	float clumpDensity = float((hashClumpAndGrassType >> 16) & 0xFu) * (1.0f / 15.0f);
#	else
	float clumpDensity = float(hashClumpAndGrassType >> 24) * (1.0f / 255.0f);
#	endif

	uint2 packedTilt = uint2(blade.tipDir & 0xFFu, (blade.tipDir >> 8) & 0xFFu);
	tiltDir = float2(packedTilt) * (2.0f / 255.0f) - 1.0f;
#endif

#if !defined(LOW_LOD)
	float randHeight = bladeType.height * (blade.posZWidthHeight & 0xFFu) * (1.0f / 255.0f);
	float widthScale = ((blade.posZWidthHeight >> 8) & 0xFFu) * (1.0f / 255.0f);
#	if defined(MID_LOD)
	// Evaluate distance widening per vertex to keep it continuous as the camera moves.
	float distanceWidth = lerp(0.4f, 1.0f, saturate((rootDistance - 1024.0f) * (1.0f / 3072.0f)));
	widthScale *= distanceWidth;
#	endif

	float randWidth = bladeType.width * 2.5f * lerp(0.45f, 1.3f, widthScale);
#endif

#if defined(MID_LOD)
#	if defined(MID_OUTER_VERTEX)
	// The generator only sends fully morphed blades here.
	static const float lowGeometryBlend = 1.0f;
#	else
	float lowGeometryBlend = GetMidLowBlend(rootDistance);
#	endif
#endif

#if defined(MID_OUTER_VERTEX)
	bool doubleBlade = false;  // Far and Mid's outer list render a single tapered blade.
#elif defined(LOW_LOD)
	bool doubleBlade = (blade.posZWidthHeight & 1u) != 0u;
#else
	bool doubleBlade = randHeight <= 45.0f;
#endif

#if defined(LOW_VERTEX)
#	if defined(LOW_OUTER_VERTEX)
	isBlade1 = false;
	bool rotateFirstBlade = false;
	float t = vertexID == 2u ? 1.0f : 0.0f;
#	else
	isBlade1 = doubleBlade && vertexID == 3u;
	bool rotateFirstBlade = doubleBlade && !isBlade1;
	float t = doubleBlade ? ((vertexID == 0u || vertexID == 3u) ? 1.0f : 0.0f) : (vertexID >= 2u ? 1.0f : 0.0f);
#	endif
#else
	bool rotateFirstBlade = doubleBlade && !isBlade1;

	// Double blades run tip-to-base-to-tip, with each half reaching t = 1.
	float rung = vertexID >> 1;
	bool upperHalf = rung >= MID_LEVEL;
	float bladeLevels = doubleBlade ? (upperHalf ? DOUBLE_LEVELS : MID_LEVEL) : LEVELS;
	float invBladeLevels = doubleBlade ? (upperHalf ? INV_DOUBLE_LEVELS : INV_MID_LEVEL) : INV_LEVELS;

	float level = abs(rung - doubleBlade * MID_LEVEL);
	float t = level * invBladeLevels;
#endif

	// Stabilize material sampling around the tapered blade's area-weighted mean.
	float appearanceT = 0.25f * (t + 1.0f);

	static const float COS_30 = 0.8660254f;
	static const float SIN_30 = 0.5f;
	float2 rotatedFacing = float2(dot(randFacing, float2(COS_30, -SIN_30)), dot(randFacing, float2(SIN_30, COS_30)));
	float2 facing = rotateFirstBlade ? rotatedFacing : randFacing;

#if !defined(LOW_LOD)
	float2 tip = tiltDir * randHeight;
	float2 midPoint = tip * bladeType.mid + float2(-tip.y, tip.x) * randBend;  // Bezier control point used by the PS tangent
#endif

#if defined(LOW_VERTEX)
#	if defined(LOW_OUTER_VERTEX)
	float sideSign = vertexID == 2u ? 0.0f : mad(float(vertexID), 2.0f, -1.0f);
#	else
	float sideSign = doubleBlade ? (vertexID == 1u ? -1.0f : (vertexID == 2u ? 1.0f : 0.0f)) : mad(float(vertexID & 1u), 2.0f, -1.0f);
#	endif
#else
	uint side = vertexID & 1u;
	float sideSign = mad(float(side), 2.0f, -1.0f) * (1.0f - step(bladeLevels, level));
#endif

#if defined(LOW_VERTEX)
	// Low has only base and tip vertices. The generator shares the packed base axis with both draws.
	float2 bladePosition = t * tip;
	float lowTaperScale = lerp(1.0f, 0.06f, t);
	float taper = randWidth * lowTaperScale;
	float2 bladeAxis = rotateFirstBlade ? float2(-facing.y, facing.x) * randWidth : baseAxis;

#else
	float t2 = t * t;
	float midWeight = mad(-2.0f, t2, 2.0f * t);
	float2 bladePosition = mad(midPoint, midWeight, t2 * tip);

	// Interpolate t squared to t to the fourth power across the fixed rungs without evaluating pow.
	float taperScale = mad(widthScale, t2 - 1.0f, 1.0f);
	float taper = randWidth * mad(-t2, taperScale, 1.0f);

#	if defined(MID_VERTEX)
	// Morph to Low's straight profile before Mid fades out.
	bladePosition = lerp(bladePosition, t * tip, lowGeometryBlend);
	taper = lerp(taper, randWidth * lerp(1.0f, 0.06f, t), lowGeometryBlend);
#	endif
#endif

	// Build the blade in camera-relative space, then apply the animated tip displacement.
#if defined(LOW_VERTEX)
	float2 sideAxis = bladeAxis;
	float2 bladeOffsetXY = mad(bladeAxis, sideSign * lowTaperScale, facing * bladePosition.x);
#else
	float2 sideAxis = float2(-facing.y, facing.x);
	float2 bladeOffsetXY = mad(sideAxis, taper * sideSign, facing * bladePosition.x);
#endif
	float3 positionOffset = float3(bladeOffsetXY, bladePosition.y);
	float windWeight = t * t;

#if defined(HIGH_LOD) && !defined(DEPTH)
	float3 previousPositionOffset = positionOffset;
#endif

#if defined(HIGH_LOD) || defined(MID_LOD)
	float2 windOffset = windDir * windDisplacement;
#	if defined(MID_LOD)
	windOffset *= 1.0f - lowGeometryBlend;
#	endif
	positionOffset.xy += windOffset * windWeight;
#	if defined(HIGH_LOD) && !defined(DEPTH)
	previousPositionOffset.xy += previousWindDir * previousWindDisplacement * windWeight;
#	endif
#endif

	float4 viewPos = float4(rootViewPosition + positionOffset, 1.0f);
#if defined(HIGH_LOD) && !defined(DEPTH)
	float4 previousViewPos = float4(rootViewPosition + previousPositionOffset + (FrameBuffer::CameraPosAdjust.xyz - FrameBuffer::CameraPreviousPosAdjust.xyz), 1.0f);
#endif

#if defined(PGRASS_CACHED_COLLISION)
#	if defined(MID_LOD)
	float3 collisionDisplacement = float3(f16tof32(blade.collisionData >> 16), f16tof32(blade.collisionData), f16tof32(blade.previousWind));
#	else
	float3 collisionDisplacement = float3(f16tof32(blade.collisionData.x >> 16), f16tof32(blade.collisionData.x), f16tof32(blade.collisionData.y >> 16));
	float3 previousCollisionDisplacement = float3(f16tof32(blade.collisionData.y), f16tof32(blade.collisionData.z >> 16), f16tof32(blade.collisionData.z));
#	endif
	float collisionWeight = t * t * (3.0f - 2.0f * t);
	viewPos.xyz += collisionDisplacement * collisionWeight;
#	if defined(HIGH_LOD) && !defined(DEPTH)
	previousViewPos.xyz += previousCollisionDisplacement * collisionWeight;
#	endif
#endif

	float4 clipPosition = mul(FrameBuffer::CameraViewProj, viewPos);

// Widen edge-on blades to keep their silhouette visible.
#if defined(MID_VERTEX) || defined(LOW_VERTEX) || defined(HIGH_VERTEX) || defined(HIGH_OUTER_VERTEX)
	// Push each edge toward the side of the screen its side axis projects to. A fixed screen direction would move the
	// edges together whenever the blade's positive side lies on the left of the screen.
	float2 sideAxisClip = float2(dot(FrameBuffer::CameraViewProj[0].xy, sideAxis), dot(FrameBuffer::CameraViewProj[3].xy, sideAxis));
	float screenSide = sideAxisClip.x * clipPosition.w - clipPosition.x * sideAxisClip.y < 0.0f ? -1.0f : 1.0f;
#endif
#if defined(MID_VERTEX) || defined(LOW_VERTEX)
	// Mid/Low pack one 4-bit factor for each double-blade facing.
	uint packedViewThicken = (hashClumpAndGrassType >> 16) & 0xFFu;
	uint viewThickenNibble = rotateFirstBlade ? packedViewThicken >> 4 : packedViewThicken & 0xFu;
	float viewThicken = float(viewThickenNibble) * (1.0f / 15.0f);
	clipPosition.x = mad(FrameBuffer::CameraProj._m00 * viewThicken * sideSign * screenSide * taper, miscParams.z, clipPosition.x);
#elif defined(HIGH_VERTEX) || defined(HIGH_OUTER_VERTEX)
	float viewThicken = float((hashClumpAndGrassType >> 20) & 0xFu) * (1.0f / 15.0f);
	clipPosition.x = mad(FrameBuffer::CameraProj._m00 * viewThicken * sideSign * screenSide * taper, miscParams.z, clipPosition.x);
#endif

	o.Position = clipPosition;

#if defined(DEPTH)
#	if defined(DEPTH_CLIP)
	o.BladeHeight = bladePosition.y;
#	endif
#else

	// Reuse w components for side, Bezier t, and root-relative height.
	o.CameraRelativePosition = float4(viewPos.xyz, mad(sideSign, 0.5f, 0.5f));

#	if defined(HIGH_LOD)
	o.PreviousCameraRelativePosition = float4(previousViewPos.xyz, t);
#	elif defined(LOW_LOD)
	o.BladeT = t;
#	endif

#	if defined(HIGH_LOD)
	o.WindLodDensity = float3(windOffset, float(packedCanopyShadow));
#	elif defined(MID_LOD)
	o.WindRootPosition = float4(windOffset, rootViewPosition.xy);
#	elif defined(LOW_LOD)
	// Low has no depth prepass; the PS reads the shadow mask where the root meets the ground.
	float4 rootClip = mul(FrameBuffer::CameraViewProj, float4(rootViewPosition, 1.0f));
	float2 rootPixel = (rootClip.xy / max(rootClip.w, 1.0f) * float2(0.5f, -0.5f) + 0.5f) / dynamicResolutionInverted;
	o.RootPosition = float4(rootViewPosition.xy, rootPixel);
#	endif

#	if defined(LOW_LOD)
	o.BezierTipAndMid = tip;
#	else
	o.BezierTipAndMid = float4(tip, midPoint);
#	endif

#	if defined(MID_LOD)
	o.BladeTDepth = float3(t, clipPosition.w, rootViewPosition.z);
	o.MaterialData = clumpSeed | (hashClumpAndGrassType >> 24) << 8 | uint(doubleBlade) << 16;
#	elif !defined(LOW_LOD)
#		if defined(HIGH_OUTER_VERTEX)
	// Outer High and Mid share geometry and the same sampled roughness curve.
	float roughness = GetDistantRoughness(bladeType, appearanceT);
#		else
	float roughness = lerp(bladeType.baseMinTipRoughnessStart.x, bladeType.baseMinTipRoughnessStart.y, smoothstep(0.0f, bladeType.baseMinTipRoughnessStart.w, appearanceT));
	roughness = lerp(roughness, bladeType.baseMinTipRoughnessStart.z, smoothstep(bladeType.baseMinTipRoughnessStart.x, 1.0f, appearanceT));
#		endif

	// Occlusion follows the blade's real height in the canopy; only colour uses the stabilized sample.
	float bladeAO = lerp(bladeType.minAO, 1.0f, t);
	float clumpAO = GetClumpAO(bladeType, clumpDensity, t);
	float heightOrT = bladePosition.y;
	o.AOThicknessRoughness = float4(bladeAO * clumpAO, lerp(bladeType.minMaxSubsurfaceOpacity.x, bladeType.minMaxSubsurfaceOpacity.y, appearanceT), roughness, heightOrT);
#	endif

#	if defined(MID_LOD)
	o.BladeParams = float4(facing, float(grassTypeIndex), asfloat(packedBladeColor | (blade.tipDir & 0xFFFF0000u)));
#	elif defined(LOW_LOD)
	// Reconstruct Low's material appearance only for visible pixels.
	uint packedLowData = (hashClumpAndGrassType & 0xFF00FFFFu) | (((blade.posZWidthHeight >> 1) & 0xFu) << 16);
	o.BladeParams = float4(facing, float(grassTypeIndex), asfloat(packedLowData));
#	else
	float3 stableClumpColor = GetStableClumpColor(bladeType, clumpSeed);
	float3 baseToTipColor = GetStabilizedBladeColor(bladeType, stableClumpColor, appearanceT);
	float bladeRand = frac((float)packedBladeColor * 0.61803398875f + 0.17f);
	// w: blade random value as f16 in the high bits, packed colour in the low 12 bits for the shadow seed.
	o.BladeParams = float4(facing, float(grassTypeIndex), asfloat(f32tof16(bladeRand) << 16 | packedBladeColor));
	o.BaseToTipColor = float4(baseToTipColor, clipPosition.w);

#	endif

#	if defined(SKYLIGHTING) && !defined(LOW_LOD)
#		if defined(MID_LOD)
	o.SkylightingRoot = blade.skylightingRoot;
#		else
	o.SkylightingVertexSH = float4(f16tof32(blade.skylightingSH0 >> 16), f16tof32(blade.skylightingSH0),
		f16tof32(blade.skylightingSH1 >> 16), f16tof32(blade.skylightingSH1));
#		endif
#	endif
#endif

	return o;
}
