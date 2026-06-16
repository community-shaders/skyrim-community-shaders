#pragma once

#include "Buffer.h"
#include "OverlayFeature.h"

struct LightLimitFix : OverlayFeature
{
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

		static void Install()
		{
			stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
			stl::write_vfunc<0x6, BSEffectShader_SetupGeometry>(RE::VTABLE_BSEffectShader[0]);
			stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);

			stl::write_thunk_call<ValidLight1>(REL::RelocationID(100994, 107781).address() + 0x92);
			stl::write_thunk_call<ValidLight2>(REL::RelocationID(100997, 107784).address() + REL::Relocate(0x139, 0x12A));
			stl::write_thunk_call<ValidLight3>(REL::RelocationID(101296, 108283).address() + REL::Relocate(0xB7, 0x7E));

			logger::info("[LLF] Installed hooks");
		}
	};

	virtual bool IsCore() const override { return true; }
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
