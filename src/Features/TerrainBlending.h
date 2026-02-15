#pragma once

struct TerrainBlending : Feature
{
public:
	virtual inline std::string GetName() override { return "Terrain Blending"; }
	virtual inline std::string GetShortName() override { return "TerrainBlending"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_BLENDING"; }
	virtual std::string_view GetCategory() const override { return "Landscape & Textures"; }
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
	virtual std::vector<FeatureConstraints::Constraint> GetActiveConstraints() const override;

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

	RE::BSGraphics::DepthStencilData terrainDepth;

	ID3D11DepthStencilState* terrainDepthStencilState = nullptr;

	ID3D11ShaderResourceView* depthSRVBackup = nullptr;
	ID3D11ShaderResourceView* prepassSRVBackup = nullptr;

	ID3D11ComputeShader* depthBlendShader = nullptr;

	virtual void ClearShaderCache() override;

	void RenderTerrainBlendingPasses();

	struct Hooks
	{
		struct Main_RenderDepth
		{
			static void thunk(bool a1, bool a2);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer__RenderPassImmediately
		{
			static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			// To know when we are rendering z-prepass depth vs shadows depth
			stl::write_thunk_call<Main_RenderDepth>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x395, 0x395, 0x2EE));

			// To manipulate the depth buffer write, depth testing, alpha blending
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

			logger::info("[Terrain Blending] Installed hooks");
		}
	};
};
