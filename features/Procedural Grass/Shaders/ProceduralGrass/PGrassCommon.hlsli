cbuffer GrassGlobals : register(b8)
{
	float2 _padClumpGrid;  // Clump grid sizes are per grass type.
	float cameraViewRow0Sum;
	float cameraViewRow1Sum;
	float2 dynamicResolutionInverted;

	float windSpeed;
	float previousWindSpeed;
	float2 windDir;
	float windAngle;

	float occlusionHalfExtent;
	float occlusionInvExtent;
	float2 previousWindDir;
	float grassPBRLightingScale;
	float4 occlusionParams;  // xy: top-down window centre in world space, zw: unused

	float4 grassAOParams;     // x: density map dim, y: unused, z: full-coverage encoding, w: canopy height (world units)
	float4 grassLightParams;  // x: density AO, y: canopy sky occlusion, z: unused, w: base canopy shading
	float4 grassFrameLight;   // xyz: resolved TRUE_PBR directional light, w: resolved grass brightness scale

	float4 farParams;          // x: loaded-grid half extent, yzw: unused
	float4 miscParams;         // x: grass map edge noise in world units, y: unused, z: view thicken, w: timer delta
	float4 grassTerrainBlend;  // x: blend strength, y: blend height (world units), z: normal blend, w: roughness blend

	float2 heightMapScale;   // world space -> terrain heightmap UV, pairs with heightMapOffset
	float2 heightMapOffset;  // -pos0.xy * heightMapScale
	float2 heightMapZRange;  // {pos0.z, pos1.z}; texels are normalised and lerp between these

	float2 debugFlags;           // x: bypass generator culling, y: colour blades by tier
	float4 grassPresenceParams;  // xy: world min-corner of the grass-id texture, z: 1/sample spacing, w: texture dim (density gather)
	float4 grassHiZParams;       // xy: valid base extent, z: near-tier geometry radius, w: trustworthy mip count; zero disables
	float2 grassLodOrigin;       // camera XY with a small dead zone, preventing stationary camera sway from moving LOD bands
	float windRotationScale;
	uint pad0;
	float4 frustumPlaneExtent;  // Left, right, bottom, top clip-plane extents for a unit world-space box.
	float4 grassHiZBounds;      // x: unused, y: near-tier clump reach, z: wind reach, w: High depth base cutoff; negative disables.
	float4 loadedLandBounds;    // xy: world min, zw: world max of the cells with attached LAND; terrain LOD is rendered outside.
	int4 terrainLiftOrigin;     // xy: world cell at the terrain lift map's window origin, zw: last frame's origin
	uint terrainLiftPhase;      // The quarter of the terrain lift map refreshed this frame
	float pad3;
	float pad1;
	float pad2;
}

/** @brief Decodes the terrain heightmap to world Z at a world position. */
float SampleTerrainHeight(Texture2D<float> heightMap, SamplerState heightSampler, float2 world2D)
{
	return lerp(heightMapZRange.x, heightMapZRange.y, heightMap.SampleLevel(heightSampler, world2D * heightMapScale + heightMapOffset, 0));
}

// The terrain lift map stores the lift above LAND multiplied by its validity weight, alongside the weight.
// It wraps a camera-centred window of world-aligned cells, so texels keep their values as the camera moves.
static const float TerrainLiftMax = 160.0f;
static const float TerrainLiftCellSize = 256.0f;
static const int TerrainLiftDim = 512;

/** @brief Keeps roots on LOD terrain at the loaded ring's edge and reaches LAND before the Mid handoff. */
float GetTerrainLiftBlend(float squareDistance)
{
	return smoothstep(6144.0f, 8192.0f, squareDistance);
}

struct GrassType
{
	float height;
	float width;
	float minSlope;
	float maxSlope;
	float stiffness;
	float rotationalStiffness;
	float tipWeight;

	float mid;

	float clumpDistanceFactor;
	float clumpHeightFactor;
	float clumpFacingFactor;
	float clumpAOStrength;
	float clumpColorStrength;
	float minAO;
	float specular;
	float clumpLeanFactor;

	float2 minMaxSubsurfaceOpacity;
	float clumpGridSize;
	float specularAnisotropy;         // OpenPBR specular_roughness_anisotropy, stretched across the blade
	float4 grassSurfParams;           // x: sheen (fuzz) strength, y: thin-subsurface anisotropy + 1, z: slope facing, w: sheen roughness
	float4 baseMinTipRoughnessStart;  // roughness at the base, at the smoothest point, and at the tip and t at which roughness bottoms out and starts climbing to the tip
	float4 midRoughnessPolynomial;    // x: cubic, y: quadratic, z: base; matches the authored curve at Mid's t={0,.5,1}
	float4 grassTypeLightParams;      // x: ground bounce, y: curved normal strength, z: unused, w: ambient desaturation

	float4 baseColor;
	float4 tipColor;
	float4 grassColorTipDry;
	float4 grassColorVar;  // x: hue variation, y: brightness variation, z: tip-dry strength, w: mottle strength
	float4 grassColorCool;
	float4 grassColorWarm;
	float4 grassBounceColor;
	float4 grassSubsurfaceColor;         // rgb: linear scattering tint divided by the reference blade colour, w: sheen (fuzz) tint toward the blade hue
	float4 fuzzDirectionalAlbedoParams;  // inverse width, centre, amplitude and offset of the MaterialX fuzz fit
};

#define GRASS_TYPE_COUNT 128

cbuffer GrassTypes : register(b9)
{
	GrassType grassType[GRASS_TYPE_COUNT];
}

/** @brief Darkens blade bases toward a clump's centre, where blades crowd together, and leaves the tips lit. */
float GetClumpAO(GrassType type, float clumpDensity, float bladeT)
{
	float baseWeight = 1.0f - saturate(bladeT);
	return lerp(1.0f, type.minAO, clumpDensity * type.clumpAOStrength * baseWeight * baseWeight);
}

float ApproximateGrassDistance(float2 offset)
{
	float2 distanceXY = abs(offset);
	return max(distanceXY.x, distanceXY.y) + min(distanceXY.x, distanceXY.y) * 0.375f;
}

// Mid draws two of High's four blades in each patch at High's width. Low and Far draw blades at this multiple of High's
// width. Keep in sync with PGrassCommon.h.
static const float DistantWidthScale = 1.5f;

/** @brief Share of a patch's extra slope slot to keep: the extra ground area a slope adds per unit of map area. */
float GetSlopeFillKeep(float terrainNormalZ)
{
	return saturate(1.0f / max(terrainNormalZ, 0.05f) - 1.0f);
}

// Mid hands off to Low from MidLowHandoffStart over MidLowHandoffBand. Keep in sync with PGrassCommon.h.
static const float MidLowHandoffStart = 6144.0f;
static const float MidLowHandoffBand = 1024.0f;

float GetMidLowBlend(float rootDistance)
{
	return smoothstep(4096.0f, 6144.0f, rootDistance);
}

#if defined(CSHADER) || defined(DEPTH) || defined(MID_VERTEX)
struct GrassGeneratorType
{
	float height;
	float width;
	float minSlope;
	float maxSlope;
	float stiffness;
	float rotationalStiffness;
	float tipWeight;
	float mid;
	float clumpDistanceFactor;
	float clumpHeightFactor;
	float clumpFacingFactor;
	float clumpLeanFactor;
	float clumpGridSize;
	float inverseClumpGridSize;
	float slopeFacing;
	float _pad1;
};

cbuffer GrassGeneratorTypes : register(b10)
{
	GrassGeneratorType generatorGrassType[GRASS_TYPE_COUNT];
}
#endif

struct Blade
{
	uint posXY;            // camera-relative x/y as two f16 values
	uint posZWidthHeight;  // camera-relative z as f16; low 16 are tier-specific geometry data
	uint facingAndWind;    // low 16: current facing as 2x SNORM8; high 16 is tier-specific
	uint previousWind;     // tier-specific geometry and motion data
	uint hashClumpAndGrassType;
	uint tipDir;

#if defined(MID_LOD)
	float3 skylightingRoot;  // Preserve the generator's full-precision probe position.
#elif !defined(LOW_LOD)
	uint skylightingSH0;  // x/y as f16
	uint skylightingSH1;  // z/w as f16
#endif

#if defined(PGRASS_CACHED_COLLISION)
#	if defined(MID_LOD)
	uint collisionData;  // current x and y as f16, with current z in previousWind
#	else
	uint3 collisionData;  // current.xyz and previous.xyz packed as six f16 values
#	endif
#endif
};

#if defined(__SKYLIGHTING_DEPENDENCY_HLSL__)
/** @brief Wrapping origin and camera-relative offset of the skylighting probe array this frame. */
struct SkylightingProbeFrame
{
	float3 offset;
	uint3 arrayOrigin;
	float3 extent;  // Half size minus one cell; samples clamp inside it
};

SkylightingProbeFrame GetSkylightingProbeFrame()
{
	SkylightingProbeFrame frame;
	float3 probeCell = round(FrameBuffer::CameraPosAdjust.xyz / Skylighting::CELL_SIZE);
	frame.offset = probeCell * Skylighting::CELL_SIZE - FrameBuffer::CameraPosAdjust.xyz;
	frame.arrayOrigin = (uint3)((int3)probeCell - (int3)(Skylighting::ARRAY_DIM / 2)) % Skylighting::ARRAY_DIM;
	frame.extent = Skylighting::ARRAY_SIZE * 0.5f - Skylighting::CELL_SIZE;
	return frame;
}

/** @brief Clamps a camera-relative position into the probe volume. */
float3 ClampToSkylightingProbes(SkylightingProbeFrame frame, float3 position)
{
	return clamp(position - frame.offset, -frame.extent, frame.extent) + frame.offset;
}
#endif
