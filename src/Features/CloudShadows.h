#pragma once

struct CloudShadows : Feature
{
private:
	static constexpr std::string_view MOD_ID = "139185";

public:
	static constexpr int kMaxCloudDecks = 32;

	struct alignas(16) Settings
	{
		float Opacity = 0.5f;
		float pad[3]{};
	};

	Settings settings;

	virtual inline std::string GetName() override { return "Cloud Shadows"; }
	virtual std::string GetDisplayName() override { return T("feature.cloud_shadows.name", "Cloud Shadows"); }
	virtual inline std::string GetShortName() override { return "CloudShadows"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kSky; }
	virtual inline std::string_view GetShaderDefineName() override { return "CLOUD_SHADOWS"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.cloud_shadows.description", "Adds realistic cloud shadows that move across the landscape, creating dynamic lighting changes as clouds pass overhead, enhancing atmospheric immersion."),
			{ T("feature.cloud_shadows.key_feature_1", "Dynamic cloud shadow projection on terrain and objects"),
				T("feature.cloud_shadows.key_feature_2", "Configurable shadow opacity for artistic control"),
				T("feature.cloud_shadows.key_feature_3", "Real-time shadow movement synchronized with cloud motion"),
				T("feature.cloud_shadows.key_feature_4", "Cubemap-based shadow calculation for accurate projection"),
				T("feature.cloud_shadows.key_feature_5", "Enhanced sky rendering integration") } };
	};

	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	bool overrideSky = false;
	/** @brief Set by any sky draw into the reflections cubemap, so its face's chain starts even without clouds. */
	bool reflectionSkyDraw = false;
	/**
	 * @brief Applies sky shader render state overrides for cloud shadow capture.
	 *
	 * Starts the face's deck chain on any reflection sky draw. When overrideSky is also
	 * set, redirects rendering to the cloud occlusion cubemap with the matching blend state and depth.
	 */
	void SkyShaderHacks();

	/** @brief Zeroed cubemap standing in for "no cloud deck has drawn yet" at the head of a chain. */
	Texture2D* texOcclusionBase = nullptr;

	/** @brief Per-deck running occlusion: deck N's texture holds every deck drawn up to and including N. */
	Texture2D* texOcclusionChain[kMaxCloudDecks] = {};
	ID3D11RenderTargetView* occlusionChainRTVs[kMaxCloudDecks][6] = {};

	/** @brief Per-face composite of the whole deck chain, rebuilt as each cubemap face finishes. */
	Texture2D* texCubemapCloudOcc = nullptr;
	/** @brief Frozen snapshot of the composite, bound at t25 so the live composite can keep being written. */
	Texture2D* texCubemapCloudOccCopy = nullptr;

	UINT cubemapMipLevels = 1;
	int currentDeckForDraw = 0;

	int chainLastDeck[6] = { -1, -1, -1, -1, -1, -1 };
	int previouslyRenderedSide = -1;

	ID3D11BlendState* cloudShadowBlendState = nullptr;

	/** @brief Creates cubemap textures, SRVs, RTVs, and blend state for cloud shadow rendering. */
	virtual void SetupResources() override;

	/** @brief Draws the ImGui settings UI for cloud shadow opacity. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	Settings GetCommonBufferData();

	/**
	 * @brief Starts a new frame's deck chain for a cubemap face, once per face per frame.
	 * @param side Cubemap face index (0-5).
	 */
	void CheckResourcesSide(int side);
	/** @brief Publishes a face's finished deck chain into the composite cubemap. */
	void PropagateToCompletion(int side);
	/**
	 * @brief Finds which of the sky's cloud decks a render pass belongs to.
	 * @return Deck index, or -1 if the pass is not an active cloud deck.
	 */
	int FindCloudDeck(RE::BSRenderPass* Pass);
	void ModifySky(RE::BSRenderPass* Pass);

	/** @brief Copies the cloud occlusion cubemap and binds it as a shader resource for the reflections prepass. */
	virtual void ReflectionsPrepass() override;
	/** @brief Flushes the last cubemap face's deck chain into the composite. */
	virtual void EarlyPrepass() override;

	/** @brief Installs the BSSkyShader hooks after all plugins have loaded. */
	virtual inline void PostPostLoad() override { Hooks::Install(); }

	struct Hooks
	{
		struct BSSkyShader_SetupMaterial
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSSkyShader_SetupMaterial>(RE::VTABLE_BSSkyShader[0]);
			logger::info("[Cloud Shadows] Installed hooks");
		}
	};
};
