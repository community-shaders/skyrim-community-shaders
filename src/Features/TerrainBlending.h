#pragma once

struct TerrainBlending : Feature
{
private:
	static constexpr std::string_view MOD_ID = "157076";

public:
	virtual inline std::string GetName() override { return "Terrain Blending"; }
	virtual inline std::string GetShortName() override { return "TerrainBlending"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_BLENDING"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Provides seamless blending between terrain and objects, eliminating harsh transitions where objects meet the ground for more natural-looking landscapes.",
			{ "Seamless terrain-to-object blending transitions",
				"Advanced depth buffer manipulation for smooth integration",
				"Support for alternative terrain rendering modes",
				"Multi-pass rendering optimization for complex scenes",
				"Enhanced visual continuity in landscape interactions" }
		};
	}
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }
	virtual bool SupportsVR() override { return true; }

	struct Settings
	{
		uint32_t Enabled = true;
		uint32_t pad[3];
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;

	ID3D11VertexShader* GetTerrainVertexShader();
	ID3D11VertexShader* GetTerrainOffsetVertexShader();

	ID3D11VertexShader* terrainVertexShader = nullptr;
	ID3D11VertexShader* terrainOffsetVertexShader = nullptr;

	ID3D11ComputeShader* GetDepthBlendShader();

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;

	bool renderDepth = false;
	bool renderTerrainDepth = false;
	bool renderAltTerrain = false;

	RE::NiPoint3 averageEyePosition;

	struct RenderPass
	{
		RE::BSRenderPass* a_pass;
		uint32_t a_technique;
		bool a_alphaTest;
		uint32_t a_renderFlags;
	};

	std::vector<RenderPass> renderPasses;
	std::vector<RenderPass> terrainRenderPasses;

	void TerrainShaderHacks();

	void ResetDepth();
	void ResetTerrainDepth();
	void BlendPrepassDepths();

	Texture2D* blendedDepthTexture = nullptr;
	Texture2D* blendedDepthTexture16 = nullptr;
	Texture2D* mainDepthCopy = nullptr;  // R32_FLOAT snapshot written inline by DepthBlend CS, replaces CopyResource

	ID3D11ShaderResourceView* GetBlendedDepthSRV() const
	{
		if (blendedDepthTexture && blendedDepthTexture->srv)
			return blendedDepthTexture->srv.get();
		return nullptr;
	}

	RE::BSGraphics::DepthStencilData terrainDepth;

	ID3D11DepthStencilState* terrainDepthStencilState = nullptr;

	ID3D11ShaderResourceView* depthSRVBackup = nullptr;
	ID3D11ShaderResourceView* prepassSRVBackup = nullptr;

	ID3D11ComputeShader* depthBlendShader = nullptr;

	virtual void ClearShaderCache() override;

	void RenderTerrainBlendingPasses();
	void OnBeginTechnique(RE::BSShader* a_shader, uint32_t a_pixelDescriptor, uint32_t a_callerRva = 0);
	void OnShadowmaskPhaseEnd();
	void OnUtilitySetupGeometry(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_renderFlags, uint32_t a_callerRva = 0);
	void OnShaderPropertySetupGeometry(RE::BSShaderProperty* a_shaderProperty, RE::BSGeometry* a_geometry, bool a_result, uint32_t a_callerRva = 0);
	void OnSetDirtyStates(bool a_isCompute, uint32_t a_callerRva = 0);

	struct Hooks
	{
		struct Main_RenderDepth
		{
			static void thunk(bool a1, bool a2);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderShadowmasks
		{
			static void thunk(bool a1);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer__RenderPassImmediately
		{
			static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSUtilityShader_SetupGeometry
		{
			static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderProperty_SetupGeometry
		{
			static bool thunk(RE::BSShaderProperty* a_shaderProperty, RE::BSGeometry* a_geometry);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShader_BeginTechnique
		{
			static bool thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSGraphics_SetDirtyStates
		{
			static void thunk(bool isCompute);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			// To know when we are rendering z-prepass depth vs shadows depth
			stl::write_thunk_call<Main_RenderDepth>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x395, 0x395, 0x2EE));

			// To know when shadowmask phase ends (for releasing engine hook overrides)
			stl::detour_thunk<Main_RenderShadowmasks>(REL::RelocationID(100422, 107140));

			// To manipulate the depth buffer write, depth testing, alpha blending
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

			// Engine path: late Utility setup hook so slot rebinding survives to draw.
			stl::write_vfunc<0x6, BSUtilityShader_SetupGeometry>(RE::VTABLE_BSUtilityShader[0]);

			// Engine path: even later material/property setup hook for final slot correction.
			stl::write_vfunc<0x27, BSShaderProperty_SetupGeometry>(RE::VTABLE_BSShaderProperty[0]);

			// Chained on top of Hooks.cpp's detours to intercept BeginTechnique/SetDirtyStates
			// for engine SRV slot override during the shadowmask phase.
			stl::detour_thunk<BSShader_BeginTechnique>(REL::RelocationID(101341, 108328));
			stl::detour_thunk<BSGraphics_SetDirtyStates>(REL::RelocationID(75580, 77386));

			logger::info("[Terrain Blending] Installed hooks");
		}
	};
};
