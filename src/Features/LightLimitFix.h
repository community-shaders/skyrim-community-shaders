#pragma once

#include "Buffer.h"
#include "OverlayFeature.h"

struct LightLimitFix : OverlayFeature
{
	static constexpr uint MAX_LIGHTS = 1024;

	struct ParticleLightConfig
	{
		bool cull = false;
	};

	struct ParticleLightConfigStore
	{
		ankerl::unordered_dense::map<std::string, ParticleLightConfig> configs;

		void Load();
	};

	struct ResolvedParticleLight
	{
		RE::NiPoint3 position;
		RE::NiColorA color;
		float radius;
	};

	struct VertexColorCacheEntry
	{
		bool valid = false;
		bool applyEffectMaterialTint = true;
		ParticleLightConfig config{};
		RE::NiColorA baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	ParticleLightConfigStore particleLightConfigs;

	eastl::hash_map<RE::BSGeometry*, VertexColorCacheEntry> vertexColorCache;
	eastl::vector<ResolvedParticleLight> queuedParticleLights;
	eastl::vector<ResolvedParticleLight> currentParticleLights;
	std::shared_mutex particleLightsMutex;

	bool CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t a_technique);

public:
	virtual inline std::string GetName() override { return "Light Limit Fix"; }
	virtual std::string GetDisplayName() override { return T("feature.light_limit_fix.name", "Light Limit Fix"); }
	virtual inline std::string GetShortName() override { return "LightLimitFix"; }
	virtual inline std::string_view GetShaderDefineName() override { return "LIGHT_LIMIT_FIX"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.light_limit_fix.description", "Light Limit Fix removes the vanilla game's 4-light limit, allowing unlimited dynamic lights in scenes.\nThis dramatically improves lighting quality and enables more realistic illumination scenarios."),
			{ T("feature.light_limit_fix.key_feature_1", "Removes 4-light limit"),
				T("feature.light_limit_fix.key_feature_2", "Unlimited dynamic lights"),
				T("feature.light_limit_fix.key_feature_3", "Improved lighting quality"),
				T("feature.light_limit_fix.key_feature_4", "Enhanced visual realism") } };
	};

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	/** @brief Flags describing light properties for clustered rendering. */
	enum class LightFlags : std::uint32_t
	{
		PortalStrict = (1 << 0),
		Shadow = (1 << 1),
		Simple = (1 << 2),
		ShadowCaster = (1 << 3),
		LocalShadow = (1 << 4),

		Initialised = (1 << 8),
		Disabled = (1 << 9),
		InverseSquare = (1 << 10),
		Linear = (1 << 11),
	};

	struct PositionOpt
	{
		float3 data;
		uint pad0;
	};

	struct alignas(16) LightData
	{
		float3 color;
		float fade = 1.0f;
		float radius;
		float invRadius;
		float fadeZone;
		float sizeBias;
		PositionOpt positionWS;
		uint128_t roomFlags = uint32_t(0);
		stl::enumeration<LightFlags> lightFlags;
		uint32_t shadowMaskIndex = 0;
		uint32_t localShadowIndex = 0;
		uint pad1;
	};
	STATIC_ASSERT_ALIGNAS_16(LightData);

	static constexpr uint32_t SHADOW_MASK_CHANNEL_COUNT = 4;
	static constexpr uint32_t NO_SHADOW_MASK_INDEX = 255;
	static constexpr uint32_t ENGINE_SHADOW_SLOTS = 4;
	static constexpr uint32_t ENGINE_SHADOW_MAP_SLICES = 8;
	static constexpr uint32_t MIN_LOCAL_SHADOW_SLOTS = 4;
	static constexpr uint32_t MAX_LOCAL_SHADOW_SLOTS = 64;
	static constexpr uint64_t LOCAL_SHADOW_MAX_CACHE_BYTES = 2048ull * 1024ull * 1024ull;
	static constexpr uint32_t LOCAL_SHADOW_SWEEP_INTERVAL = 30;
	static constexpr uint32_t LOCAL_SHADOW_EVICT_AGE = 120;
	static constexpr uint32_t LOCAL_SHADOW_REJECT_MAX_FRAMES = 120;
	static constexpr uint32_t LOCAL_SHADOW_CAMERA_HOLD_FRAMES = 60;
	static constexpr uint32_t LOCAL_SHADOW_STATIC_STARVE_FRAMES = 60;
	static constexpr float LOCAL_SHADOW_AGE_URGENCY = 64.0f;
	static constexpr float LOCAL_SHADOW_ACTOR_SCORE = 1000.0f;
	static constexpr uint32_t LOCAL_SHADOW_TYPE_SPOT = 0;
	static constexpr uint32_t LOCAL_SHADOW_TYPE_HEMISPHERE = 1;
	static constexpr uint32_t LOCAL_SHADOW_TYPE_OMNI = 2;
	static constexpr float LOCAL_SHADOW_ACTOR_EXTENT = 96.0f;
	static constexpr float LOCAL_SHADOW_ANIMATION_SPEED = 90.0f;
	static constexpr float LOCAL_SHADOW_MAX_SLACK = 24.0f;
	static constexpr float LOCAL_SHADOW_DEFAULT_POISSON_RADIUS = 4.0f;
	static constexpr float LOCAL_SHADOW_TELEPORT_DISTANCE = 128.0f;
	static constexpr float LOCAL_SHADOW_TELEPORT_RADIUS_FRACTION = 0.25f;
	static constexpr std::array<uint32_t, 3> LOCAL_SHADOW_RESOLUTION_OPTIONS = { 512, 1024, 2048 };
	static constexpr std::array<uint32_t, 3> LOCAL_SHADOW_SAMPLE_OPTIONS = { 1, 4, 8 };
	static constexpr uint32_t LOCAL_SHADOW_MIN_RESOLUTION = 128;
	static constexpr float LOCAL_SHADOW_FILTER_SCALE_MIN = 0.25f;
	static constexpr float LOCAL_SHADOW_FILTER_SCALE_MAX = 2.0f;
	static constexpr float LOCAL_SHADOW_MAX_POISSON_RADIUS = 16.0f;
	/** @brief Base depth bias per engine texel, scaled by the light's shadowBiasScale to match the engine's own bias. */
	static constexpr float LOCAL_SHADOW_DEPTH_BIAS = 0.00025f;
	static constexpr float LOCAL_SHADOW_DEFAULT_SPOT_FALLOFF = 2.0f;
	/** @brief Score tiers: never-rendered casters outrank moved casters, which outrank actor-lit and static casters. */
	static constexpr float LOCAL_SHADOW_NEWCOMER_SCORE = 1000000.0f;
	static constexpr float LOCAL_SHADOW_MOVED_SCORE = 100000.0f;
	static constexpr float LOCAL_SHADOW_MOVE_THRESHOLD = 12.0f;
	static constexpr float LOCAL_SHADOW_MOVE_RADIUS_FRACTION = 0.02f;
	static constexpr float LOCAL_SHADOW_STATIC_IMPORTANCE_BASE = 0.25f;
	static constexpr float LOCAL_SHADOW_ACTOR_MAX_SPEED = 64.0f;
	static constexpr float LOCAL_SHADOW_ACTOR_REST_SPEED = 0.5f;
	static constexpr float LOCAL_SHADOW_ACTOR_PROXIMITY_DISTANCE = 512.0f;
	static constexpr float LOCAL_SHADOW_ACTOR_EDGE_WEIGHT = 0.25f;
	static constexpr float LOCAL_SHADOW_ACTOR_STALENESS_WEIGHT = 0.15f;
	static constexpr float LOCAL_SHADOW_ACTOR_STICKY_BONUS = 0.5f;
	static constexpr float LOCAL_SHADOW_INTERVAL_EMA_WEIGHT = 0.3f;
	static constexpr float LOCAL_SHADOW_INTERVAL_EMA_MAX = 60.0f;
	static constexpr uint32_t LOCAL_SHADOW_REJECT_BASE_FRAMES = 15;
	static constexpr uint32_t LOCAL_SHADOW_REJECT_MAX_STREAK = 4;
	static constexpr float LOCAL_SHADOW_MAX_FRAME_TIME = 0.1f;
	static constexpr uint32_t LOCAL_SHADOW_LOG_INTERVAL_FRAMES = 600;
	/** @brief Must match numthreads in LocalShadowCopyCS.hlsl. */
	static constexpr uint32_t LOCAL_SHADOW_COPY_GROUP_SIZE = 8;
	/** @brief Squared eye offset above which the pass is treated as the rebased first-person pass. */
	static constexpr float FIRST_PERSON_EYE_OFFSET_SQUARED = 1.0f;

	struct alignas(16) LocalShadowData
	{
		float4x4 ShadowProj;
		float4 Params;
		float4 Params2;
		float4 Origin;
	};
	STATIC_ASSERT_ALIGNAS_16(LocalShadowData);

	struct alignas(16) LocalShadowCopyCB
	{
		uint SourceSlice;
		uint TargetSlice;
		uint Scale;
		uint TargetSize;
	};
	STATIC_ASSERT_ALIGNAS_16(LocalShadowCopyCB);

	/** @brief Per-light bookkeeping for the local shadow cache and the caster rotation. */
	struct LocalShadowCaster
	{
		RE::BSShadowLight* light = nullptr;
		RE::NiLight* niLight = nullptr;
		int32_t slice = -1;
		uint32_t lastSeenFrame = 0;
		uint32_t lastEvaluatedFrame = 0;
		uint32_t lastEligibleFrame = 0;
		uint32_t lastRenderedFrame = 0;
		uint32_t rejectUntilFrame = 0;
		uint32_t rejectStreak = 0;
		RE::NiPoint3 position{};
		RE::NiPoint3 renderedPosition{};
		RE::NiMatrix3 rotation{};
		RE::NiMatrix3 renderedRotation{};
		float radius = 0.0f;
		float importance = 0.0f;
		float score = -1.0f;
		float actorImportance = 0.0f;
		float actorSpeed = 0.0f;
		float intervalEma = 1.0f;
		bool hidden = false;
		bool dynamic = false;
		float4x4 shadowProj{};
		float4 shadowParams{};
		float4 shadowParams2{};
	};

	void AddParticleLightsToBuffer(eastl::vector<LightData>& a_lightsData);

	struct ClusterAABB
	{
		float4 minPoint;
		float4 maxPoint;
	};

	struct alignas(16) LightGrid
	{
		uint offset;
		uint lightCount;
		uint pad0[2];
	};
	STATIC_ASSERT_ALIGNAS_16(LightGrid);

	struct alignas(16) LightBuildingCB
	{
		float LightsNear;
		float LightsFar;
		uint pad0[2];
		uint ClusterSize[4];
	};
	STATIC_ASSERT_ALIGNAS_16(LightBuildingCB);

	struct alignas(16) LightCullingCB
	{
		uint LightCount;
		uint pad[3];
		uint ClusterSize[4];
	};
	STATIC_ASSERT_ALIGNAS_16(LightCullingCB);

	struct alignas(16) PerFrame
	{
		uint EnableLightsVisualisation;
		uint LightsVisualisationMode;
		float pad0[2];
		uint ClusterSize[4];
		uint pad1;
		uint LocalShadowSamples;
		float LocalShadowFilterRadius;
		float LocalShadowTexelSize;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	/** @brief Populates and returns the per-frame constant buffer data for light visualization settings. */
	PerFrame GetCommonBufferData();

	struct alignas(16) StrictLightDataCB
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint FirstPerson;
		float4 WorldEyePosition;
		LightData StrictLights[15];
	};
	STATIC_ASSERT_ALIGNAS_16(StrictLightDataCB);

	StrictLightDataCB strictLightDataTemp;

	ConstantBuffer* strictLightDataCB = nullptr;

	bool previousEnableLightsVisualisation = settings.EnableLightsVisualisation;
	bool currentEnableLightsVisualisation = settings.EnableLightsVisualisation;

	ID3D11ComputeShader* clusterBuildingCS = nullptr;
	ID3D11ComputeShader* clusterCullingCS = nullptr;

	ConstantBuffer* lightBuildingCB = nullptr;
	ConstantBuffer* lightCullingCB = nullptr;

	eastl::unique_ptr<Buffer> lights = nullptr;
	eastl::unique_ptr<Buffer> clusters = nullptr;
	eastl::unique_ptr<Buffer> lightIndexCounter = nullptr;
	eastl::unique_ptr<Buffer> lightIndexList = nullptr;
	eastl::unique_ptr<Buffer> lightGrid = nullptr;

	std::uint32_t lightCount = 0;
	float lightsNear = 1;
	float lightsFar = 16384;

	RE::NiPoint3 eyePositionCached{};
	bool wasEmpty = false;
	bool wasWorld = false;
	bool wasFirstPerson = false;
	RE::NiPoint3 previousWorldEyePosition{};
	int previousRoomIndex = -1;
	uint previousShadowBitMask = 0;

	Util::FrameChecker frameChecker;

	/** @brief Creates GPU buffers, compute shaders, and constant buffers for clustered lighting. */
	virtual void SetupResources() override;

	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Draws the ImGui settings UI for light limit fix configuration and debug visualization. */
	virtual void DrawSettings() override;
	/** @brief Draws the debug overlay warning when light visualization is enabled. */
	virtual void DrawOverlay() override;
	/** @brief Returns whether the debug overlay should be displayed. */
	virtual bool IsOverlayVisible() const override { return settings.EnableLightsVisualisation; }

	/** @brief Installs shader setup geometry hooks for lighting, effect, and water shaders. */
	virtual void PostPostLoad() override;
	/** @brief Unlocks the vanilla magic light limit on data load. */
	virtual void DataLoaded() override;
	/** @brief Recompiles the cluster building and culling compute shaders. */
	virtual void ClearShaderCache() override;

	/**
	 * @brief Calculates the distance from the camera to a light for culling purposes.
	 * @param a_lightPosition World-space position of the light.
	 * @param a_radius The light's effective radius.
	 * @return The effective distance for sorting/culling.
	 */
	float CalculateLightDistance(float3 a_lightPosition, float a_radius);
	/**
	 * @brief Sets the world-space position of a light relative to the cached eye position.
	 * @param a_light The light data struct to update.
	 * @param a_initialPosition The light's world-space position.
	 * @param a_cached Whether to use the cached eye position or recompute it.
	 */
	void SetLightPosition(LightLimitFix::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached = true);
	/** @brief Gathers all active scene lights and uploads them to the GPU light buffer. */
	void UpdateLights();
	/** @brief Rebuilds the light cluster structure and performs GPU light culling. */
	void UpdateStructure();
	/** @brief Runs the light update and binds clustered light SRVs for the frame. */
	virtual void Prepass() override;
	/** @brief Copies the shadow maps the engine just rendered into the local shadow cache. */
	virtual void EarlyPrepass() override;

	eastl::vector<LocalShadowCaster> localShadowCasters;
	ankerl::unordered_dense::map<RE::BSShadowLight*, uint32_t> localShadowCasterLookup;
	eastl::vector<RE::BSShadowLight*> localShadowAllowed;
	eastl::vector<RE::BSShadowLight*> localShadowSliceOwner;
	struct LocalShadowActor
	{
		RE::NiPoint3 position;
		float speed = 0.0f;
	};

	eastl::vector<LocalShadowActor> localShadowActors;
	ankerl::unordered_dense::map<RE::FormID, RE::NiPoint3> localShadowActorHistory;
	ankerl::unordered_dense::map<RE::FormID, RE::NiPoint3> localShadowActorHistoryNext;
	eastl::vector<LocalShadowData> localShadowUpload;
	bool localShadowSelecting = false;
	bool localShadowSunActive = false;
	uint32_t localShadowFrame = 0;
	RE::NiPoint3 localShadowCameraPosition{};

	eastl::unique_ptr<Texture2D> localShadowCache = nullptr;
	eastl::unique_ptr<Buffer> localShadowBuffer = nullptr;
	ConstantBuffer* localShadowCopyCB = nullptr;
	ID3D11ComputeShader* localShadowCopyCS = nullptr;
	uint32_t localShadowCacheSlots = 0;
	uint32_t localShadowRequestedSlots = 0;
	uint32_t localShadowCacheResolution = 0;
	uint32_t localShadowEngineResolution = 0;
	DXGI_FORMAT localShadowCacheFormat = DXGI_FORMAT_UNKNOWN;
	uint32_t localShadowEngineMipLevels = 1;
	uint32_t localShadowEngineSlices = 0;
	bool localShadowDirectCopy = false;
	RE::Setting* poissonRadiusScaleSetting = nullptr;
	bool poissonRadiusScaleLookedUp = false;

	uint32_t localShadowStatTracked = 0;
	uint32_t localShadowStatCached = 0;
	uint32_t localShadowStatRendered = 0;

	/** @brief True when local shadows are enabled and the cache texture exists. */
	bool IsLocalShadowCacheActive() const { return settings.EnableLocalShadows && localShadowCache; }
	/** @brief Engine shadow slots left for local lights once the sun takes its slot. */
	uint32_t GetEngineShadowCapacity() const { return localShadowSunActive ? ENGINE_SHADOW_SLOTS - 1 : ENGINE_SHADOW_SLOTS; }
	/** @brief True when the engine found the caster in range within the camera hold window. */
	static bool IsLocalShadowCasterInView(const LocalShadowCaster& a_caster, uint32_t a_frame);
	/** @brief Texel size of the cache format, which is always R16_UNORM or R32_FLOAT. */
	static uint32_t GetLocalShadowBytesPerTexel(DXGI_FORMAT a_format) { return a_format == DXGI_FORMAT_R16_UNORM ? 2 : 4; }

	/**
	 * @brief Picks which shadow casters the engine may render this frame so the cache covers every caster over time.
	 * Runs before the engine selects its (at most four) shadow-casting lights.
	 */
	void ScheduleLocalShadowCasters();
	/**
	 * @brief Records the engine's own range test for a caster and hides casters not scheduled this frame.
	 * @param a_light The shadow light being evaluated by the engine.
	 * @param a_result The engine's UpdateCamera result.
	 * @return The result the engine should see.
	 */
	bool FilterLocalShadowCaster(RE::BSShadowLight* a_light, const RE::NiCamera* a_camera, bool a_result);
	/** @brief Copies this frame's engine shadow map slices into the cache and uploads the projection data. */
	void CopyLocalShadowMaps();
	/** @brief Binds the local shadow cache and projection buffer for pixel shaders. */
	void BindLocalShadowResources();
	/** @brief Creates or recreates the cache texture and projection buffer to match the settings and the engine shadow map. */
	void EnsureLocalShadowResources(ID3D11Texture2D* a_engineShadowMaps);
	/** @brief Releases the cache resources and forgets every slice assignment. */
	void ReleaseLocalShadowResources();
	/** @brief Finds a free cache slice or reclaims the least recently rendered reclaimable one; an actor-lit caster may take the least important static caster's slice. */
	int32_t AcquireLocalShadowSlice(RE::BSShadowLight* a_light, uint32_t a_frame);
	/** @brief True when taking a slice from this owner cannot remove a shadow that is on screen. */
	static bool IsLocalShadowSliceReclaimable(const LocalShadowCaster* a_owner, uint32_t a_frame);
	/** @brief Looks up the tracked caster entry for a light, or nullptr. */
	LocalShadowCaster* FindLocalShadowCaster(RE::BSShadowLight* a_light);
	/** @brief Flags a light as an engine shadow-mask light only when it owns one of the four mask channels. */
	static void TryAssignShadowMask(LightData& a_light, RE::BSShadowLight* a_shadowLight);
	/** @brief Returns the shadow mask channel of a light, or NO_SHADOW_MASK_INDEX when it has none. */
	static uint32_t GetShadowMaskIndex(RE::BSShadowLight* a_shadowLight);

	/** @brief Adjusts the saturation of an RGB color value. */
	static inline float3 Saturation(float3 color, float saturation);
	/**
	 * @brief Checks whether a BSLight is valid (non-null and not hidden).
	 * @param a_light The light to validate.
	 * @return True if the light is valid for processing.
	 */
	static inline bool IsValidLight(RE::BSLight* a_light);
	/**
	 * @brief Checks whether a BSLight is a global (non-portal-strict) light.
	 * @param a_light The light to check.
	 * @return True if the light is global and not restricted to a portal.
	 */
	static inline bool IsGlobalLight(RE::BSLight* a_light);

	struct Settings
	{
		bool EnableParticleLights = true;
		bool EnableParticleLightsCulling = true;
		bool EnableLightsVisualisation = false;
		uint LightsVisualisationMode = 0;
		bool EnableLocalShadows = true;
		uint LocalShadowSlots = 16;
		uint LocalShadowResolution = 0;
		uint LocalShadowSamples = 8;
		float LocalShadowFilterScale = 1.0f;
	};

	uint clusterSize[3] = { 16 };

	Settings settings;

	/** @brief Pre-geometry setup: initializes strict light data and determines the room index for the pass. */
	void BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass);

	/** @brief Collects portal-strict point lights from the render pass into the strict light constant buffer. */
	void BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass);

	/** @brief Post-geometry setup: uploads the strict light constant buffer to the GPU if changed. */
	void BSLightingShader_SetupGeometry_After(RE::BSRenderPass* a_pass);

	eastl::hash_map<RE::NiNode*, uint8_t> roomNodes;

	/** @brief Contains vtable hooks for BSLightingShader, BSEffectShader, and BSWaterShader geometry setup. */
	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSEffectShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSWaterShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <int N>
		struct ValidLight
		{
			static bool thunk(RE::BSShaderProperty* a_property, RE::BSLight* a_light)
			{
				return func(a_property, a_light) && (a_light->portalStrict || !a_light->portalGraph || a_light->IsShadowLight());
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		using ValidLight1 = ValidLight<1>;
		using ValidLight2 = ValidLight<2>;
		using ValidLight3 = ValidLight<3>;

		template <int N>
		struct BSBatchRenderer_RenderPassImmediately
		{
			static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		using RenderPass1 = BSBatchRenderer_RenderPassImmediately<1>;
		using RenderPass2 = BSBatchRenderer_RenderPassImmediately<2>;
		using RenderPass3 = BSBatchRenderer_RenderPassImmediately<3>;

		struct BSGeometry_Destroy
		{
			static void thunk(RE::BSGeometry* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct CalculateActiveShadowCasterLights
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShadowParabolicLight_UpdateCamera
		{
			static bool thunk(RE::BSShadowLight* This, const RE::NiCamera* a_camera);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShadowFrustumLight_UpdateCamera
		{
			static bool thunk(RE::BSShadowLight* This, const RE::NiCamera* a_camera);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install();
	};

	virtual bool IsCore() const override { return true; }

private:
	VertexColorCacheEntry GetParticleLightConfig(RE::BSRenderPass* a_pass);
	bool QueueParticleLight(RE::BSRenderPass* a_pass, VertexColorCacheEntry& a_reference);
};

template <>
struct fmt::formatter<LightLimitFix::LightData>
{
	// Presentation format: 'f' - fixed.
	char presentation = 'f';

	// Parses format specifications of the form ['f'].
	constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator
	{
		auto it = ctx.begin(), end = ctx.end();
		if (it != end && (*it == 'f'))
			presentation = *it++;

		// Check if reached the end of the range:
		if (it != end && *it != '}')
			throw format_error("invalid format");

		// Return an iterator past the end of the parsed range:
		return it;
	}

	// Formats the point p using the parsed format specification (presentation)
	// stored in this formatter.
	auto format(const LightLimitFix::LightData& l, format_context& ctx) const -> format_context::iterator
	{
		// ctx.out() is an output iterator to write to.
		return fmt::format_to(ctx.out(), "{{address {:x} color {} radius {} posWS {}}}",
			reinterpret_cast<uintptr_t>(&l),
			(Vector3)l.color,
			l.radius,
			(Vector3)l.positionWS.data);
	}
};
