#pragma once

#include "Buffer.h"

struct SnowDeformation : Feature
{
public:
	virtual inline std::string GetName() override { return "Snow Deformation"; }
	virtual std::string GetDisplayName() override { return T("feature.snow_deformation.name", "Snow Deformation"); }
	virtual inline std::string GetShortName() override { return "SnowDeformation"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SNOW_DEFORMATION"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	/** @brief The lighting shader samples the deformation map on landscape draws. */
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return shaderType == RE::BSShader::Type::Lighting;
	}

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.snow_deformation.description", "Maintains a persistent deformation map around the player so snow can be visibly compressed by actors moving through it, leaving lasting trails."),
			{ T("feature.snow_deformation.key_feature_1", "Persistent world-space deformation map following the player"),
				T("feature.snow_deformation.key_feature_2", "Trails carved by the player, NPCs and creatures"),
				T("feature.snow_deformation.key_feature_3", "Configurable snow refill over time"),
				T("feature.snow_deformation.key_feature_4", "Compute-shader based, low performance impact") } };
	};

	// Square world-space deformation window following the camera in whole-texel
	// steps, stored toroidally (physical texel = world texel masked by dim - 1).
	// Texel value = normalized depression depth, 0 = untouched snow, 1 =
	// compressed to the ground. World size is runtime (deformWorldSize),
	// resolution is fixed, so trench detail coarsens with range.
	static constexpr uint kTextureDim = 2048;
	static constexpr uint kMaxStamps = 128;

	/** @brief Skyrim world units per meter (1 unit ≈ 1.43 cm). Range sliders are in meters. */
	static constexpr float kUnitsPerMeter = 70.0f;

	struct Settings
	{
		bool EnableSnowDeformation = true;
		bool ShowDebugTexture = false;
		/** @brief Scale on Havok collision-shape radii (20 = 1.0x = the shapes' actual size). */
		float StampRadius = 20.0f;
		/** @brief Seconds for compressed snow to fully recover. 0 disables refilling. */
		float RefillTime = 700.0f;
		/** @brief Only refill while the current weather is snowing, so trails persist through clear spells and interiors. */
		bool RefillOnlyWhenSnowing = true;
		/** @brief Trenches render range in meters (converted via kUnitsPerMeter). Applying a change clears the map: content is scale-relative. */
		float RangeTrenchesM = 100.0f;
	};

	/** @brief GPU-side settings, appended to the shared FeatureData cbuffer (b6). Layout must match SnowDeformationSettings in SharedData.hlsli. */
	struct alignas(16) SettingsGPU
	{
		float2 WindowOrigin;
		float InvWorldSize;
		uint EnableSnowDeformation;

		uint DebugTerrainOverlay;
		/** @brief Physical texel of the window's logical (0,0) in the toroidal map. */
		DirectX::XMINT2 MapOrigin;
		float padSnow;
	};
	STATIC_ASSERT_ALIGNAS_16(SettingsGPU);

	/**
	 * @brief Returns this frame's GPU settings for the shared FeatureData buffer.
	 *
	 * Also advances the deformation window when a_inWorld is true. The origin
	 * must be computed here (during State::UpdateSharedData, before Prepass) so
	 * the constant buffer and the scrolled texture agree within a frame.
	 */
	SettingsGPU GetCommonBufferData(bool a_inWorld);

	/** @brief Per-dispatch constants for the deformation update. Layout must match PerFrame in DeformationUpdateCS.hlsl. */
	struct alignas(16) PerFrame
	{
		float2 WindowOrigin;
		DirectX::XMINT2 MapOrigin;

		float TexelSize;
		uint StampCount;
		float RefillAmount;
		uint RefillRowStart;

		/** @brief Logical rects (x0, y0, w, h) the scroll or a clear reassigned; RingCS zeroes them. */
		DirectX::XMINT4 RingRects[2];
		uint RingTotalTexels;
		uint RefillRowCount;
		uint pad[2];

		/** @brief Per stamp: xy capsule segment end (current position), z depth, w 1 / radius^2. */
		float4 Stamps[kMaxStamps];
		/** @brief Per stamp: xy capsule segment start (the shape's previous position), z radius. */
		float4 StampEnds[kMaxStamps];
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	Settings settings;

	ConstantBuffer* perFrame = nullptr;
	/** @brief The deformation map, toroidal and updated in place. */
	Texture2D* deformationTexture = nullptr;

	/** @brief SRV of the deformation map, for shader sampling and debug UI. */
	ID3D11ShaderResourceView* GetDeformationSRV() const { return deformationTexture ? deformationTexture->srv.get() : nullptr; }
	/** @brief World XY of the corner of texel (0,0) of the current deformation window. */
	float2 GetWindowOrigin() const { return windowOrigin; }

	/** @brief Creates the deformation map, the stamp tile list and the per-frame constant buffer. */
	virtual void SetupResources() override;

	/**
	 * @brief Per-frame update: gathers stamps, then clears the texels the window
	 * scrolled onto, refills one band of rows and applies the stamps, each only
	 * where there is work.
	 */
	virtual void Prepass() override;

	/** @brief Compiles the update passes on first use; false while they are unavailable. */
	bool EnsureUpdateShaders();
	ID3D11ComputeShader* ringCS = nullptr;
	ID3D11ComputeShader* refillCS = nullptr;
	ID3D11ComputeShader* stampCS = nullptr;
	ID3D11ComputeShader* stampAllCS = nullptr;
	/** @brief Set when a compile fails, so it is not retried every frame; cleared by ClearShaderCache. */
	bool updateShadersFailed = false;
	virtual void ClearShaderCache() override;

	/** @brief Draws the ImGui settings UI, including the debug view of the deformation map. Implemented in SnowDeformation/Menu.cpp. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Installs both landscape hooks; the TESObjectLAND detour attaches after TruePBR's so it sees the final quad materials. Implemented in SnowDeformation/TerrainData.cpp. */
	virtual void PostPostLoad() override;

	/** @brief Caches a "tile is snow material" bitmask per landscape quad material, for the terrain shader's per-tile snow detection. */
	void TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	/** @brief Publishes the cached snow mask for the material about to be drawn via ExtraFeatureDescriptor bits 11-16. */
	void BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material);

	/** @brief Diagnostics shown in the settings UI: landscape materials that hit/missed the snow-mask cache at draw time. */
	std::atomic<uint64_t> landMaskHits{ 0 };
	std::atomic<uint64_t> landMaskMisses{ 0 };

	/** @brief Thread-safe size read for the settings UI. */
	size_t snowMasksSizeForUI()
	{
		const std::shared_lock lock(snowMaskMutex);
		return snowMasks.size();
	}

	/** @brief Runtime-only diagnostic toggle; not persisted in settings JSON. */
	bool debugTerrainOverlay = false;

protected:
	/** @brief Fills perFrameData.Stamps from the player and nearby loaded actors. Implemented in SnowDeformation/Stamping.cpp. */
	void GatherStamps(PerFrame& perFrameData);

	std::unordered_map<uintptr_t, uint8_t> snowMasks;
	std::shared_mutex snowMaskMutex;

	float2 windowOrigin = { 0, 0 };
	/** @brief World texel index of the window's logical (0,0); its low bits are the physical origin. */
	DirectX::XMINT2 windowOriginTexel = { 0, 0 };
	DirectX::XMINT2 mapOrigin = { 0, 0 };
	DirectX::XMINT2 pendingScrollDelta = { 0, 0 };
	bool clearRequested = true;

	/** @brief Smallest refill applied in one sweep: two R16F steps just below 1.0, so it always moves a stored value. */
	static constexpr float kRefillStep = 1.0f / 1024.0f;
	/** @brief A refill sweep covers the map in this many bands of rows, one band per frame. */
	static constexpr uint kRefillBands = 32;
	/** @brief Refill accumulated since the last sweep started. */
	float refillBank = 0.0f;
	/** @brief Refill every texel receives in the running sweep. */
	float refillSweepAmount = 0.0f;
	/** @brief Next band of the running sweep; -1 when no sweep runs. */
	int refillBand = -1;

	/** @brief Stamp-pass tile list capacity; past it the pass covers the whole map. */
	static constexpr uint kStampTileCap = 8192;
	winrt::com_ptr<ID3D11Buffer> stampTileBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView> stampTileSRV;
	std::vector<uint32_t> stampTiles;
	std::vector<uint32_t> stampTileBits;
	/** @brief Fills stampTiles with the physical 8x8 tiles the stamps touch; false on overflow. */
	bool BuildStampTiles(const PerFrame& a_data);

	// ---- Runtime render-distance state (driven by the Range* settings) ----
	/** @brief Deformation window world size (2x the Trenches range). Changing it clears the map. */
	float deformWorldSize = 14000.0f;
	bool trenchRangeDirty = false;
	bool rangeInitApplied = false;

public:
	/** @brief Applies pending range-setting changes (trench window resize + map clear). Called at Prepass start; the first call applies loaded settings. */
	void ApplyRangeSettings();

protected:
	/** @brief Trail history per collision shape: key = (formID << 16) | traversal index. */
	std::unordered_map<uint64_t, float2> stampPrevPositions;

	/** @brief An actor's collision shapes in skeleton-walk order, with their radii. */
	struct ActorShapes
	{
		struct Shape
		{
			RE::NiPointer<RE::bhkNiCollisionObject> object;
			float radius = 0.0f;
		};
		RE::NiPointer<RE::NiAVObject> root;
		uint32_t builtFrame = 0;
		uint32_t seenFrame = 0;
		std::vector<Shape> shapes;
	};
	/** @brief Frames an actor's cached shapes live before the skeleton is walked again. */
	static constexpr uint32_t kActorShapeCacheFrames = 30;
	std::unordered_map<uint32_t, ActorShapes> actorShapeCache;
	uint32_t gatherFrame = 0;

	/** @brief GatherStamps' per-frame working sets, cleared rather than rebuilt each frame. */
	std::unordered_map<uint64_t, float2> stampAnchorScratch;
	std::unordered_set<uint32_t> anchoredRefScratch;

	/** @brief Last moving 3D-root position per loose prop (formID), and the scan cycle it was last seen in. */
	struct PropAnchor
	{
		RE::NiPoint3 pos;
		uint32_t cycle = 0;
	};
	std::unordered_map<uint32_t, PropAnchor> propPrevPositions;
	/** @brief Frames it takes to walk every cell in range once. */
	static constexpr uint32_t kPropScanInterval = 6;
	uint32_t propScanFrame = 0;
	uint32_t propScanCycle = 0;
	std::vector<RE::TESObjectCELL*> propScanCells;
	/** @brief Props found moving in each slice's walk, revisited every frame until that slice is walked again. */
	std::vector<RE::ObjectRefHandle> propScanMovers[kPropScanInterval];

	/** @brief Stillness latch per corpse (formID). Once settled, only a large accumulated displacement (dragging, explosions) wakes it, so ragdoll micro-drift cannot re-trench under a buried corpse. Erased when the actor is seen alive again. */
	struct CorpseRest
	{
		uint16_t stillFrames = 0;
		bool settled = false;
	};
	std::unordered_map<uint32_t, CorpseRest> corpseRestStates;
};
