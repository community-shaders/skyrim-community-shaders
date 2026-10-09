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
		bool colorManagementEnabled = false;
		bool acescgEnabled = false;
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
		uint pad0;
		uint pad1;
	};
	STATIC_ASSERT_ALIGNAS_16(LightData);

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
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	/** @brief Populates and returns the per-frame constant buffer data for light visualization settings. */
	PerFrame GetCommonBufferData();

	struct alignas(16) StrictLightDataCB
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint pad0;
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

	/** @brief Installs shader setup geometry hooks and the inverse-square light creation and luminance hooks. */
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

		static void Install();
	};

	virtual bool IsCore() const override { return true; }

	/**
	 * @brief Calculates the effective radius of an inverse-square light based on its intensity and cutoff.
	 * @param intensity The light's intensity value.
	 * @param shadowCaster Whether the light casts shadows (uses a tighter cutoff).
	 * @param cutoffOverride Per-light cutoff override from the light form data.
	 * @param size The physical size of the light source.
	 * @return The computed light radius in game units.
	 */
	static float CalculateRadius(float intensity, bool shadowCaster, float cutoffOverride, float size);

	/**
	 * @brief Builds the LightData for a light from its runtime data, applying inverse-square parameters when flagged.
	 * @param bsLight The game's BSLight instance.
	 * @param niLight The underlying NiLight with runtime extension data.
	 * @return The populated LightData.
	 */
	LightData ProcessLight(RE::BSLight* bsLight, RE::NiLight* niLight) const;

	/**
	 * @brief Computes the inverse-square attenuation at a given distance with smooth fade-out.
	 * @param distance Distance from the light source.
	 * @param radius The effective light radius.
	 * @param size The physical size of the light source.
	 * @return The attenuation factor in [0, 1].
	 */
	static float GetAttenuation(float distance, float radius, float size);

	/** @brief Hook that intercepts point light creation to inject inverse-square light extension data. */
	struct CreatePointLight
	{
		static RE::NiPointLight* thunk(RE::TESObjectLIGH* ligh, RE::TESObjectREFR* refr, RE::NiAVObject* root, bool forceDynamic, bool useLightRadius, bool affectRequesterOnly);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Hook that overrides BSLight luminance calculation to use inverse-square attenuation. */
	struct BSLight_GetLuminance
	{
		static float thunk(RE::BSLight* bsLight, RE::NiPoint3* targetPosition, RE::NiLight* refLight);
		static inline REL::Relocation<decltype(thunk)> func;
	};

private:
	static constexpr float DefaultCutoff = 0.05f;
	static constexpr float DefaultShadowCasterCutoff = 0.022f;

	static constexpr float Scale = 0.8f;
	static constexpr float MetresToUnits = 70.f;
	static constexpr float MetresToUnitsSq = MetresToUnits * MetresToUnits;
	static constexpr float ScaledUnitsSq = Scale * MetresToUnitsSq;
	static constexpr float FadeZoneBase = 4.5f * Scale * MetresToUnits;

	static void SetExtLightData(RE::NiLight* niLight, const RE::TESObjectLIGH* ligh);

	static inline float SmoothStep(float edge0, float edge1, float x);

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
