#pragma once

/** @brief Simulates realistic ambient lighting by calculating sky occlusion via a 3D probe array. */
struct Skylighting : Feature
{
private:
	static constexpr std::string_view MOD_ID = "139352";

public:

	virtual inline std::string GetName() override { return "Skylighting"; }
	virtual std::string GetDisplayName() override { return T("feature.skylighting.name", "Skylighting"); }
	virtual inline std::string GetShortName() override { return "Skylighting"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "SKYLIGHTING"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.skylighting.description", "Simulates realistic ambient lighting by calculating sky occlusion and directional lighting, providing more accurate and natural illumination in outdoor environments."),
			{ T("feature.skylighting.key_feature_1", "Sky occlusion calculation for ambient lighting"),
				T("feature.skylighting.key_feature_2", "Directional skylighting based on environment geometry"),
				T("feature.skylighting.key_feature_3", "Enhanced ambient lighting for outdoor scenes"),
				T("feature.skylighting.key_feature_4", "Support for varying sky illumination intensities"),
				T("feature.skylighting.key_feature_5", "Integration with existing lighting systems") } };
	};

	virtual bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	virtual void RestoreDefaultSettings() override;
	/** @brief Draws the ImGui settings panel for Skylighting configuration. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	/** @brief Creates GPU resources including occlusion textures, probe arrays, and samplers. */
	virtual void SetupResources() override;
	/** @brief Releases cached compute shaders and recompiles them. */
	virtual void ClearShaderCache() override;
	/** @brief Compiles the probe update compute shader from HLSL source. */
	void CompileComputeShaders();

	/** @brief Dispatches the probe update compute shader during the prepass stage. */
	virtual void Prepass() override;

	/** @brief Installs rendering hooks and registers event handlers after plugin load. */
	virtual void PostPostLoad() override;

	//////////////////////////////////////////////////////////////////////////////////

	struct Settings
	{
		float MaxZenith = 3.1415926f / 2.f;  // 90 deg
		float MinDiffuseVisibility = 0.1f;
		float MinSpecularVisibility = 0.1f;
	} settings;

	struct SkylightingCB
	{
		REX::W32::XMFLOAT4X4 OcclusionViewProj;
		float4 OcclusionDir;

		float3 PosOffset;  // cell origin in camera model space
		uint _pad0;
		uint ArrayOrigin[3];  // xyz: array origin, w: max accum frames
		uint _pad1;
		int ValidMargin[4];

		float MinDiffuseVisibility;
		float MinSpecularVisibility;
		uint _pad2[2];
	};
	static_assert(sizeof(SkylightingCB) % 16 == 0);

	/**
	 * @brief Builds the skylighting constant buffer data from current probe array state.
	 * @param a_inWorld Whether the player is currently in an exterior worldspace.
	 * @return Populated SkylightingCB structure, or zeroed if not in world or map menu is open.
	 */
	SkylightingCB GetCommonBufferData(bool a_inWorld);

	winrt::com_ptr<ID3D11SamplerState> comparisonSampler = nullptr;

	Texture2D* texOcclusion = nullptr;
	Texture3D* texProbeArray = nullptr;
	Texture3D* texAccumFramesArray = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> probeUpdateCompute = nullptr;

	// misc parameters
	uint probeArrayDims[3] = { 256, 256, 128 };
	float occlusionDistance = 10000.f;

	// cached variables
	bool queuedResetSkylighting = true;
	bool inOcclusion = false;
	REX::W32::XMFLOAT4X4 OcclusionTransform;
	float4 OcclusionDir;
	uint frameCount = 0;

	/** @brief Clears the accumulation frames array to force a full rebuild of skylighting probes. */
	void ResetSkylighting();

	std::chrono::time_point<std::chrono::system_clock> lastUpdateTimer = std::chrono::system_clock::now();

	//////////////////////////////////////////////////////////////////////////////////

	// Hooks
	struct BSLightingShaderProperty_GetPrecipitationOcclusionMapRenderPassesImpl
	{
		static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, uint32_t renderMode, RE::BSGraphics::BSShaderAccumulator* accumulator);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/** @brief Renders the skylighting occlusion map using the precipitation rendering system. */
	void RenderOcclusion();

	struct Main_Precipitation_RenderOcclusion
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct SetViewFrustum
	{
		static void thunk(RE::NiCamera* a_camera, RE::NiFrustum* a_frustum);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Event handler
	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*);

		static bool Register()
		{
			static MenuOpenCloseEventHandler singleton;
			auto ui = globals::game::ui;

			if (!ui) {
				logger::error("UI event source not found");
				return false;
			}

			ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&singleton);

			logger::info("Registered {}", typeid(singleton).name());

			return true;
		}
	};
};
