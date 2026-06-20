#pragma once

/** @brief Provides seamless depth-based blending between terrain and objects to eliminate harsh transitions. */
struct TerrainBlending : Feature
{
private:
	static constexpr std::string_view MOD_ID = "157076";

public:
	virtual inline std::string GetName() override { return "Terrain Blending"; }
	virtual std::string GetDisplayName() override { return T("feature.terrain_blending.name", "Terrain Blending"); }
	/** @brief Returns the short identifier name. */
	virtual inline std::string GetShortName() override { return "TerrainBlending"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_BLENDING"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }
	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.terrain_blending.description", "Provides seamless blending between terrain and objects, eliminating harsh transitions where objects meet the ground for more natural-looking landscapes."),
			{ T("feature.terrain_blending.key_feature_1", "Seamless terrain-to-object blending transitions"),
				T("feature.terrain_blending.key_feature_2", "Advanced depth buffer manipulation for smooth integration"),
				T("feature.terrain_blending.key_feature_3", "Support for alternative terrain rendering modes"),
				T("feature.terrain_blending.key_feature_4", "Multi-pass rendering optimization for complex scenes"),
				T("feature.terrain_blending.key_feature_5", "Enhanced visual continuity in landscape interactions") } };
	};

	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	struct Settings
	{
		uint32_t Enabled = true;
		uint32_t pad[3];
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	/** @brief Draws the ImGui settings panel for Terrain Blending configuration. */
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	/** @brief Creates GPU resources including depth textures, blended depth buffers, and stencil states. */
	virtual void SetupResources() override;

	/** @brief Returns the terrain depth vertex shader, compiling on first use. */
	ID3D11VertexShader* GetTerrainVertexShader();
	/** @brief Returns the terrain depth-offset vertex shader, compiling on first use. */
	ID3D11VertexShader* GetTerrainOffsetVertexShader();

	ID3D11VertexShader* terrainVertexShader = nullptr;
	ID3D11VertexShader* terrainOffsetVertexShader = nullptr;

	/** @brief Returns the depth blend compute shader, compiling on first use. */
	ID3D11ComputeShader* GetDepthBlendShader();

	/** @brief Installs rendering hooks for depth and batch rendering after plugin load. */
	virtual void PostPostLoad() override;
	/** @brief Disables the vanilla land fade INI setting after game data is loaded. */
	virtual void DataLoaded() override;

	bool renderDepth = false;
	bool renderTerrainDepth = false;
	bool renderAltTerrain = false;

	RE::NiPoint3 eyePosition;

	struct RenderPass
	{
		RE::BSRenderPass* a_pass;
		uint32_t a_technique;
		bool a_alphaTest;
		uint32_t a_renderFlags;
	};

	std::vector<RenderPass> renderPasses;
	std::vector<RenderPass> terrainRenderPasses;

	/** @brief Applies terrain-specific vertex shader overrides and render target swaps during depth rendering. */
	void TerrainShaderHacks();

	/** @brief Clears the terrain depth stencil buffer to prepare for a new depth pass. */
	void ResetDepth();
	/** @brief Restores the original vertex shader and marks render targets dirty after terrain depth rendering. */
	void ResetTerrainDepth();
	/** @brief Blends the main and terrain depth buffers using a compute shader dispatch. */
	void BlendPrepassDepths();

	Texture2D* blendedDepthTexture = nullptr;
	Texture2D* blendedDepthTexture16 = nullptr;

	RE::BSGraphics::DepthStencilData terrainDepth;

	ID3D11DepthStencilState* terrainDepthStencilState = nullptr;

	ID3D11ShaderResourceView* depthSRVBackup = nullptr;
	ID3D11ShaderResourceView* prepassSRVBackup = nullptr;

	ID3D11ComputeShader* depthBlendShader = nullptr;

	/** @brief Releases all cached vertex and compute shaders for recompilation. */
	virtual void ClearShaderCache() override;

	/** @brief Executes deferred terrain and no-blend render passes with appropriate depth and alpha blending states. */
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
			stl::write_thunk_call<Main_RenderDepth>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x395, 0x395));

			// To manipulate the depth buffer write, depth testing, alpha blending
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

			logger::info("[Terrain Blending] Installed hooks");
		}
	};
};
