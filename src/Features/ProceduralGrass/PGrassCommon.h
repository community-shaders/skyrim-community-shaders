#pragma once

namespace PGrassCommon
{
	/** @brief Grass level-of-detail tiers, nearest first. */
	enum class Tier : uint8_t
	{
		High,
		Mid,
		Low,
	};

	inline constexpr uint32_t LowBladeBatchSize = 64;
	inline constexpr uint32_t MidBladeBatchSize = 32;

	// Low draws blades at this multiple of High's width; keep in sync with PGrassCommon.hlsli.
	inline constexpr float DistantWidthScale = 1.5f;
	// Mid draws two of High's four base lanes per patch at High's width.
	inline constexpr uint32_t MidPatchBladeCount = 2;

	/** @brief Returns the game's default landscape texture used when a LAND quadrant has no base texture. */
	RE::TESLandTexture* GetDefaultLandTexture();
	float GetGrassTexturePctThreshold();

	inline uint32_t QuadrantSampleHash(int32_t cellX, int32_t cellY, uint32_t quadrant, uint32_t sample)
	{
		const auto mix = [](uint32_t hash, uint32_t value) {
			for (uint32_t shift = 0; shift < 32; shift += 8) {
				hash ^= (value >> shift) & 0xFFu;
				hash *= 16777619u;
			}
			return hash;
		};

		uint32_t hash = 2166136261u;
		hash = mix(hash, static_cast<uint32_t>(cellX));
		hash = mix(hash, static_cast<uint32_t>(cellY));
		hash = mix(hash, quadrant);
		return mix(hash, sample);
	}

	/** @brief Weighted grass-type variants assigned to one landscape texture. */
	struct GrassTextureSelection
	{
		std::vector<uint8_t> ids;       // Global type id per variant; 0 is bare
		std::vector<float> cumulative;  // Running weight sum
		float total = 0.0f;

		bool operator==(const GrassTextureSelection&) const = default;

		/** @brief True when any variant with a positive weight grows grass. */
		bool HasWeightedGrass() const
		{
			if (total <= 0.0f)
				return false;
			float previous = 0.0f;
			for (size_t i = 0; i < ids.size(); ++i) {
				if (ids[i] != 0u && cumulative[i] > previous)
					return true;
				previous = cumulative[i];
			}
			return false;
		}

		/** @brief Picks a variant by its weight from a uniform 32-bit hash. */
		uint8_t Select(uint32_t hash) const
		{
			const float r = (hash * (1.0f / 4294967296.0f)) * total;
			for (size_t i = 0; i < ids.size(); ++i) {
				if (r < cumulative[i])
					return ids[i];
			}
			return ids.back();
		}
	};

	inline constexpr uint64_t GrassHashOffsetBasis = 14695981039346656037ull;
	inline constexpr uint64_t GrassHashPrime = 1099511628211ull;

	inline void GrassHashBytes(uint64_t& hash, const void* data, size_t byteCount)
	{
		const auto* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < byteCount; ++i) {
			hash ^= bytes[i];
			hash *= GrassHashPrime;
		}
	}

	template <class T>
	inline void GrassHashValue(uint64_t& hash, const T& value)
	{
		GrassHashBytes(hash, &value, sizeof(value));
	}

	constexpr uint64_t GrassCellKey(int32_t cellX, int32_t cellY)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32) |
		       static_cast<uint32_t>(cellY);
	}

	constexpr uint64_t GrassQuadrantKey(int32_t quadrantX, int32_t quadrantY)
	{
		return GrassCellKey(quadrantX, quadrantY);
	}

	// A LAND quadrant carries a 17x17 grid of texture-blend samples, so 128 world units apart.
	inline constexpr uint32_t QuadrantGrassPitch = 17;
	inline constexpr uint32_t QuadrantGrassSamples = QuadrantGrassPitch * QuadrantGrassPitch;
	inline constexpr uint32_t QuadrantCellPitch = QuadrantGrassPitch - 1;

	// The terrain material uses one base texture and five overlay slots.
	inline constexpr uint32_t LandscapeOverlayCount = 5;

	/** @brief Returns one neighbouring grass id for a bare sample; reads use the original map so the fill cannot spread farther. */
	inline uint8_t FindAdjacentGrassId(const uint8_t* ids, uint32_t width, uint32_t height, uint32_t x, uint32_t y, int32_t worldSampleX, int32_t worldSampleY)
	{
		static constexpr int8_t offsets[8][2] = {
			{ -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 },
			{ 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }
		};
		const uint32_t first = (static_cast<uint32_t>(worldSampleX) * 73856093u ^ static_cast<uint32_t>(worldSampleY) * 19349663u) & 7u;

		for (uint32_t i = 0; i < 8; ++i) {
			const auto& offset = offsets[(first + i) & 7u];
			const int32_t sampleX = static_cast<int32_t>(x) + offset[0];
			const int32_t sampleY = static_cast<int32_t>(y) + offset[1];
			if (sampleX < 0 || sampleY < 0 || sampleX >= static_cast<int32_t>(width) || sampleY >= static_cast<int32_t>(height))
				continue;

			const uint8_t id = ids[static_cast<size_t>(sampleY) * width + sampleX];
			if (id != 0)
				return id;
		}

		return 0;
	}

	/** @brief Fills texture gaps within one LAND quadrant, preserving explicit bare selections. */
	inline std::array<uint8_t, QuadrantGrassSamples> FillQuadrantGrassIds(const uint8_t* ids, int32_t quadrantX, int32_t quadrantY, const bool* bare = nullptr)
	{
		std::array<uint8_t, QuadrantGrassSamples> filled{};
		if (!ids)
			return filled;

		const int32_t worldSampleBaseX = quadrantX * static_cast<int32_t>(QuadrantCellPitch);
		const int32_t worldSampleBaseY = quadrantY * static_cast<int32_t>(QuadrantCellPitch);
		for (uint32_t y = 0; y < QuadrantGrassPitch; ++y) {
			for (uint32_t x = 0; x < QuadrantGrassPitch; ++x) {
				const uint32_t sample = y * QuadrantGrassPitch + x;
				filled[sample] = ids[sample];
				if (filled[sample] == 0u && (!bare || !bare[sample]))
					filled[sample] = FindAdjacentGrassId(ids, QuadrantGrassPitch, QuadrantGrassPitch, x, y,
						worldSampleBaseX + static_cast<int32_t>(x), worldSampleBaseY + static_cast<int32_t>(y));
			}
		}
		return filled;
	}

	/** @brief Unions shared LAND samples deterministically, preserving either quadrant's grass coverage. */
	inline uint8_t MergeGrassIds(uint8_t first, uint8_t second)
	{
		return first == 0u ? second : (second == 0u ? first : std::min(first, second));
	}

	/**
	 * @brief Landscape textures of one LAND quadrant, as TESObjectLAND::LoadedLandData stores them.
	 * A layer whose record names form 0 already resolves to the default landscape texture.
	 */
	struct QuadrantLandTextures
	{
		const RE::TESLandTexture* base = nullptr;
		std::array<const RE::TESLandTexture*, LandscapeOverlayCount> layers{};
	};

	/**
	 * @brief Builds one quadrant's filled grass ids from its texture blend.
	 * Each sample takes the strongest configured or grass-growing texture whose share passes fTexturePctThreshold:Grass,
	 * then that texture's weighted variant. Texture gaps take one neighbouring id. Loaded and streamed
	 * LAND both go through here, so the near and far tiers agree on every sample.
	 * @param percents Per-sample overlay opacity in 0-255 units with @p percentStride bytes per sample, as the engine
	 *                 stores TESObjectLAND::LoadedLandData::percents (opacity * 255, truncated).
	 * @param getSelection Returns the weighted selection overriding a texture, or null to use its own grass list.
	 */
	template <class GetSelection>
	std::array<uint8_t, QuadrantGrassSamples> BuildQuadrantGrassIds(const QuadrantLandTextures& textures, const uint8_t* percents, uint32_t percentStride,
		int32_t cellX, int32_t cellY, uint32_t quadrant, bool ignoreGrassMap, GetSelection&& getSelection)
	{
		std::array<uint8_t, QuadrantGrassSamples> ids{};
		if (ignoreGrassMap) {
			ids.fill(1u);
			return ids;
		}

		const RE::TESLandTexture* defaultTexture = GetDefaultLandTexture();
		const float thresholdPercent = GetGrassTexturePctThreshold() * 255.0f;
		const RE::TESLandTexture* baseTexture = textures.base && textures.base->formID != 0 ? textures.base : defaultTexture;

		struct Candidate
		{
			const RE::TESLandTexture* texture;
			const GrassTextureSelection* selection;
			bool growsGrass;
		};
		const auto describe = [&](const RE::TESLandTexture* texture) {
			const GrassTextureSelection* selection = texture ? getSelection(texture) : nullptr;
			const bool grows = texture && ((selection && selection->total > 0.0f) || !texture->textureGrassList.empty());
			return Candidate{ texture, selection, grows };
		};
		const Candidate base = describe(baseTexture);
		std::array<Candidate, LandscapeOverlayCount> layers;
		for (uint32_t layer = 0; layer < LandscapeOverlayCount; ++layer) {
			const auto* texture = textures.layers[layer];
			layers[layer] = describe(texture && texture->formID == 0 ? defaultTexture : texture);
		}

		std::array<bool, QuadrantGrassSamples> bare{};
		for (uint32_t v = 0; v < QuadrantGrassSamples; ++v) {
			const uint8_t* samplePercents = percents + static_cast<size_t>(v) * percentStride;
			int32_t overlayTotal = 0;
			for (uint32_t layer = 0; layer < LandscapeOverlayCount; ++layer)
				if (layers[layer].texture)
					overlayTotal += samplePercents[layer];

			const Candidate* best = nullptr;
			int32_t bestPercent = -1;
			const auto consider = [&](const Candidate& candidate, const int32_t percent) {
				if (candidate.growsGrass && percent > 0 && static_cast<float>(percent) >= thresholdPercent && percent > bestPercent) {
					bestPercent = percent;
					best = &candidate;
				}
			};
			consider(base, std::max(255 - overlayTotal, 0));
			for (uint32_t layer = 0; layer < LandscapeOverlayCount; ++layer)
				consider(layers[layer], samplePercents[layer]);

			if (best) {
				ids[v] = best->selection && best->selection->total > 0.0f ? best->selection->Select(QuadrantSampleHash(cellX, cellY, quadrant, v)) : uint8_t{ 1 };
				bare[v] = ids[v] == 0u;
			}
		}

		return FillQuadrantGrassIds(ids.data(), cellX * 2 + static_cast<int32_t>(quadrant % 2), cellY * 2 + static_cast<int32_t>(quadrant / 2), bare.data());
	}

	inline constexpr int32_t HighTierQuadrantRadius = 2;
	inline constexpr int32_t MidTierQuadrantRadius = 4;  // Extend Mid this far to avoid popping when the player moves between Mid and Low tiers.
	// Mid hands off to Low over this distance. Mid's blades are fully straightened by then and rarely cover a pixel, so
	// the band is short. Keep in sync with PGrassCommon.hlsli.
	inline constexpr float MidLowHandoffBand = 1024.0f;
	inline constexpr int32_t LowTierQuadrantRadius = 5;
	inline constexpr int32_t LowTierStreamGuardQuadrants = 1;
	inline constexpr int32_t StreamGuardCells = 1;

	// Terrain lift map; mirrors PGrassCommon.hlsli. The window spans 16 cells on every side of the camera.
	inline constexpr int32_t TerrainLiftDim = 512;
	inline constexpr float TerrainLiftCellSize = 256.0f;

	// Quadrants in an md<=r square (r in each of x and y), one per tier's renderer buffer.
	constexpr uint32_t QuadrantSquare(int32_t r) { return static_cast<uint32_t>((2 * r + 1) * (2 * r + 1)); }

	inline constexpr uint32_t HighTierQuadrantCap = QuadrantSquare(HighTierQuadrantRadius);
	inline constexpr uint32_t MidTierQuadrantCap = QuadrantSquare(MidTierQuadrantRadius);
	inline constexpr uint32_t LowTierQuadrantCap = QuadrantSquare(LowTierQuadrantRadius + LowTierStreamGuardQuadrants);

	inline constexpr float QuadrantNoHeight = -3.0e38f;

	/** @brief One LAND quadrant slot of a tier's list. A default-constructed slot is empty. */
	struct Quadrant
	{
		int32_t cellX = 0;
		int32_t cellY = 0;
		uint32_t x = 0;
		uint32_t y = 0;
		uint64_t cacheVersion = 0;
		bool inUse = false;
		const uint8_t* grassIds = nullptr;  // filled grass ids
		const float* heights = nullptr;     // null when the LAND is unloaded
		float2 worldPos;                    // cached lower-left world XY
		float minHeight = QuadrantNoHeight;
		float maxHeight = QuadrantNoHeight;

		int32_t WorldX() const { return cellX * 2 + static_cast<int32_t>(x); }
		int32_t WorldY() const { return cellY * 2 + static_cast<int32_t>(y); }
		bool HasLand() const { return maxHeight > QuadrantNoHeight && minHeight <= maxHeight; }

		bool operator==(const Quadrant&) const = default;
	};

	struct alignas(16) QuadrantData
	{
		float2 quadWorldPos;
		uint quadrantHash;  // CPU-precomputed iqint3(quadX, quadY) for randomisation
		uint flags;
	};
	STATIC_ASSERT_ALIGNAS_16(QuadrantData);

	template <std::size_t N>
	struct alignas(16) QuadrantDataArray
	{
		// Per-tier LOD cross-fade bands, so a quadrant dithers in/out at tier boundaries instead of popping.
		float4 lodFadeIn;  // x: fade-in start dist, y: 1/range, z: unused, w: fade-out endpoint
		float4 lodFadeOut;
		QuadrantData data[N];
	};

	struct alignas(16) GrassGlobals
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
		float occlusionInvExtent;  // 1 / (2 * half extent), used by the top-down density map UV transform
		float2 previousWindDir;
		float grassPBRLightingScale;  // TRUE_PBR's lighting scale, resolved for the active Linear Lighting mode.
		float4 occlusionParams;       // xy: top-down window centre in world space, zw: unused

		float4 grassAOParams;     // x: density map dim, y: unused, z: full-coverage encoding, w: canopy height (world units)
		float4 grassLightParams;  // x: density AO, y: canopy sky occlusion, z: unused, w: base canopy shading
		float4 grassFrameLight;   // xyz: resolved TRUE_PBR directional light, w: resolved grass brightness scale

		float4 farParams;          // x: loaded-grid half extent, yzw: unused
		float4 miscParams;         // x: grass map edge noise in world units, y: unused, z: view thicken, w: timer delta
		float4 grassTerrainBlend;  // x: blend strength, y: blend height (world units), z: normal blend, w: roughness blend

		float2 heightMapScale;   // world space -> terrain heightmap UV, pairs with heightMapOffset
		float2 heightMapOffset;  // -pos0.xy * heightMapScale
		float2 heightMapZRange;  // {pos0.z, pos1.z}; texels are normalised and lerp between these

		float2 debugFlags;           // x: bypass every cull in the generator, y: colour blades by tier
		float4 grassPresenceParams;  // xy: world min-corner of the grass-id texture, z: 1/sample spacing, w: texture dim (density gather)
		float4 grassHiZParams;       // xy: valid base extent, z: near-tier geometry radius, w: trustworthy mip count; zero disables
		float2 grassLodOrigin;       // camera XY with a small dead zone, preventing stationary camera sway from moving LOD bands
		float windRotationScale;
		uint32_t pad0;
		float4 frustumPlaneExtent;     // L1 extent of left, right, bottom, and top clip planes.
		float4 grassHiZBounds;         // x: unused, y: near-tier clump reach, z: wind reach, w: High depth base cutoff; negative disables.
		float4 loadedLandBounds;       // xy: world min, zw: world max of the cells with attached LAND; terrain LOD is rendered outside.
		int32_t terrainLiftOrigin[4];  // xy: world cell at the terrain lift map's window origin, zw: last frame's origin
		uint32_t terrainLiftPhase;     // The quarter of the terrain lift map refreshed this frame
		float pad3;
		float pad1;
		float pad2;
	};
	STATIC_ASSERT_ALIGNAS_16(GrassGlobals);
	static_assert(offsetof(GrassGlobals, grassPBRLightingScale) == 60);
	static_assert(offsetof(GrassGlobals, grassFrameLight) == 112);
	static_assert(sizeof(GrassGlobals) == 336);

	struct alignas(16) GrassType
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
	STATIC_ASSERT_ALIGNAS_16(GrassType);

	// HLSL cbuffer packing keeps float2 inside one 16-byte register, so these offsets must match it.
	static_assert(offsetof(GrassType, minMaxSubsurfaceOpacity) == 64);
	static_assert(offsetof(GrassType, grassSurfParams) == 80);
	static_assert(offsetof(GrassType, fuzzDirectionalAlbedoParams) == 272);
	static_assert(sizeof(GrassType) == 288);

	// Slot 0 = bare, slot 1 = the base/default type, leaving 126 total slots for loaded per-texture variants.
	inline constexpr uint32_t MaxGrassTypes = 128;

	struct GrassTypesArray
	{
		GrassType grassType[MaxGrassTypes];
	};
	static_assert(sizeof(GrassTypesArray) <= 65536);

	// Compact type data used by blade generation.
	struct alignas(16) GrassGeneratorType
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
	STATIC_ASSERT_ALIGNAS_16(GrassGeneratorType);
	static_assert(sizeof(GrassGeneratorType) == 64);
	static_assert(offsetof(GrassGeneratorType, slopeFacing) == 56);

	struct GrassGeneratorTypesArray
	{
		GrassGeneratorType grassType[MaxGrassTypes];
	};

	struct Blade
	{
		uint posXY;            // camera-relative x/y as two f16 values
		uint posZWidthHeight;  // camera-relative z as f16; low 16 are tier-specific geometry data
		uint facingAndWind;    // low 16: current facing as 2x SNORM8; high 16 is tier-specific
		uint previousWind;     // tier-specific geometry and motion data
		uint hashClumpAndGrassType;
		uint tipDir;  // tier-specific packed tilt and lighting or distance data
	};
	static_assert(sizeof(Blade) == 24);

	struct BladeSkylit
	{
		Blade blade;
		uint skylightingSH0;
		uint skylightingSH1;
	};
	static_assert(sizeof(BladeSkylit) == 32);

	struct BladeMid
	{
		Blade blade;
		float3 skylightingRoot;
	};
	static_assert(sizeof(BladeMid) == 36);

	// Mid has no previous-position output.
	struct BladeMidCollision
	{
		BladeMid blade;
		uint collisionData;
	};
	static_assert(sizeof(BladeMidCollision) == 40);

	struct BladeSkylitCollision
	{
		BladeSkylit blade;
		uint collisionData[3];
	};
	static_assert(sizeof(BladeSkylitCollision) == 44);

}
