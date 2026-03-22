#pragma once

#include "Buffer.h"
#include "LightLimitFix/ShadowCasterManager.h"
#include "OverlayFeature.h"

struct LightLimitFix : OverlayFeature
{
private:
	static constexpr std::string_view MOD_ID = "99548";

public:
	virtual inline std::string GetName() override { return "Light Limit Fix"; }
	virtual inline std::string GetShortName() override { return "LightLimitFix"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "LIGHT_LIMIT_FIX"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Light Limit Fix removes the vanilla game's 4-light limit, allowing unlimited dynamic lights in scenes. "
			"It also extends shadow support to all point and spot lights, with configurable filtering from fast hard-edged shadows up to contact-hardened soft shadows (PCSS).",
			{ "Removes 4-light limit",
				"Unlimited dynamic lights",
				"Shadow support for point and spot lights",
				"PCF and PCSS soft shadow filtering",
				"Improved lighting quality" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

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
		PositionOpt positionWS[2];
		uint128_t roomFlags = uint32_t(0);
		stl::enumeration<LightFlags> lightFlags;
		uint32_t shadowMapIndex = 0;
		float2 pad0;
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

	/// Per-slot debug metadata recorded during CopyPointShadowData.
	struct ShadowSlotInfo
	{
		uint32_t type = 0;    ///< Shadow type: 0=spot/frustum, 1=hemisphere, 2=omnidirectional
		float radius = 0.0f;  ///< Light radius (world units)
		bool valid = false;   ///< true when this slot was written this frame
	};

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
		// Shadow sampling (configurable)
		uint32_t FilterMode;
		float KernelScale;
		float LightSize;
		uint32_t ShadowMapSlots;  // total shadow map texture-array capacity
		// Cluster config (computed)
		uint ClusterSize[4];
		// Debug (last)
		uint EnableLightsVisualisation;
		uint LightsVisualisationMode;
		float pad1[2];
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

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

	int eyeCount = !REL::Module::IsVR() ? 1 : 2;
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

	RE::NiPoint3 eyePositionCached[2]{};
	bool wasEmpty = false;
	bool wasWorld = false;
	int previousRoomIndex = -1;
	uint previousShadowBitMask = 0;

	Util::FrameChecker frameChecker;

	// Point/spot shadow resources (t100, t101, s14) — moved from Deferred.
	// perShadows is lazily allocated in CopyPointShadowData() since shadowMapSlots
	// is not known until Deferred::SetupResources() runs (after Feature::SetupResources()).
	Buffer* perShadows = nullptr;
	uint32_t perShadowsCapacity = 0;

	// Per-frame shadow accounting (displayed in DrawSettings Statistics tree).
	uint32_t shadowLightCount = 0;            // distinct lights processed (including dropped)
	uint32_t shadowSlotUsage = 0;             // texture-array slots consumed by successful lights
	uint32_t shadowUnshadowedLightCount = 0;  // lights that exceeded slot capacity

	/// Per-slot metadata rebuilt every frame in CopyPointShadowData.
	/// Indexed by shadow-map texture-array slot.  Used for visualization mode 8 legend.
	std::vector<ShadowSlotInfo> shadowSlotInfos;

	/// Slots in this set have their ShadowParam.y zeroed so the shader returns 1.0
	/// (fully lit / no shadow) for that caster.  Toggled from the debug overlay.
	std::unordered_set<uint32_t> suppressedSlots;

	ID3D11SamplerState* shadowCmpSampler = nullptr;  // PCF comparison sampler (s14)

	/// Generate a text legend mapping each shadow-map slot index to its golden-ratio hue
	/// and light type.  Used for RenderDoc capture comments when mode 8 is active.
	std::string BuildShadowSlotColorLegend() const;

	virtual void SetupResources() override;

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void DrawSettings() override;
	virtual void DrawOverlay() override;
	virtual bool IsOverlayVisible() const override { return settings.EnableLightsVisualisation || !suppressedSlots.empty(); }

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;
	virtual void ClearShaderCache() override;

	float CalculateLightDistance(float3 a_lightPosition, float a_radius);
	void SetLightPosition(LightLimitFix::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached = true);
	void UpdateLights();
	void UpdateStructure();
	virtual void EarlyPrepass() override;
	virtual void Prepass() override;
	void CopyPointShadowData();

	static inline float3 Saturation(float3 color, float saturation);
	static inline bool IsValidLight(RE::BSLight* a_light);
	static inline bool IsGlobalLight(RE::BSLight* a_light);

	struct Settings
	{
		// Shadow sampling
		uint32_t FilterMode = 0;   // 0=cheap 2×2, 1=PCF Poisson disc, 2=PCSS
		float KernelScale = 3.0f;  // scales the base PCF kernel radius
		float LightSize = 10.0f;   // PCSS virtual light size in shadow-map pixels
		// Debug (last)
		bool EnableLightsVisualisation = false;
		uint LightsVisualisationMode = 0;

		// Shadow caster scheduling (ShadowCasterManager)
		ShadowCasterManager::Settings ShadowSettings;
	};

	uint clusterSize[3] = { 16 };

	Settings settings;

	void BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass);

	void BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass);

	void BSLightingShader_SetupGeometry_After(RE::BSRenderPass* a_pass);

	eastl::hash_map<RE::NiNode*, uint8_t> roomNodes;

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
			stl::write_thunk_call<ValidLight2>(REL::RelocationID(100997, 107784).address() + REL::Relocate(0x139, 0x12A, 0x133));
			stl::write_thunk_call<ValidLight3>(REL::RelocationID(101296, 108283).address() + REL::Relocate(0xB7, 0x7E));

			logger::info("[LLF] Installed hooks");
		}
	};

	virtual bool SupportsVR() override { return true; };
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
		return fmt::format_to(ctx.out(), "{{address {:x} color {} radius {} posWS {} {}}}",
			reinterpret_cast<uintptr_t>(&l),
			(Vector3)l.color,
			l.radius,
			(Vector3)l.positionWS[0].data, (Vector3)l.positionWS[1].data);
	}
};
