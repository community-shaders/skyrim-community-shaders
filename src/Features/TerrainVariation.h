#pragma once

#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/** @brief Reduces terrain texture tiling artifacts by adding stochastic variation to texture sampling. */
struct TerrainVariation : Feature
{
private:
	static constexpr std::string_view MOD_ID = "148123";

public:
	virtual inline std::string GetName() override { return "Terrain Variation"; }
	virtual std::string GetDisplayName() override { return T("feature.terrain_variation.name", "Terrain Variation"); }
	/** @brief Returns the short identifier name. */
	virtual inline std::string GetShortName() override { return "TerrainVariation"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_VARIATION"; }
	/**
	 * @brief Returns true for Lighting shaders when the feature is loaded.
	 * @details LOD tiling remains gated by @c enableLODTerrainTilingFix.
	 */
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return loaded && shaderType == RE::BSShader::Type::Lighting;
	}
	virtual bool IsCore() const override { return false; };
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.terrain_variation.description", "Terrain Variation reduces the repeating pattern effect on terrain textures.\nThis technique creates more natural-looking terrain by adding variation to texture sampling."),
			{ T("feature.terrain_variation.key_feature_1", "Reduces terrain texture tiling"),
				T("feature.terrain_variation.key_feature_2", "Stochastic texture sampling"),
				T("feature.terrain_variation.key_feature_3", "Improved terrain visual quality"),
				T("feature.terrain_variation.key_feature_4", "Compatible with Extended Materials parallax") } };
	};

	struct alignas(16) Settings
	{
		uint32_t enableLODTerrainTilingFix = 1;
		uint32_t enableMeshSupport = 1;
		uint32_t pad[2]{};
	};

	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	/** @brief Draws the ImGui settings panel for Terrain Variation configuration. */
	virtual void DrawSettings() override;
	/** @brief Suppresses the default failed-load message display. */
	virtual bool DrawFailLoadMessage() const override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Collects the diffuse texture paths referenced by every landscape texture record. */
	virtual void DataLoaded() override;
	/** @brief Initializes the feature and applies shader settings after plugin load. */
	virtual void PostPostLoad() override;

	// Guards landscapeDiffusePaths, landscapeDiffuseFileNames, meshTextureCache and meshTextureKeepAlive.
	std::shared_mutex meshTextureMutex;
	// Canonical diffuse paths of every land texture record, relative to textures/.
	std::unordered_set<std::string> landscapeDiffusePaths;
	// File names of the land texture diffuses that live under a landscape/ directory.
	std::unordered_set<std::string> landscapeDiffuseFileNames;
	// Keyed on the interned BSFixedString pointer of a diffuse texture name.
	std::unordered_map<const char*, bool> meshTextureCache;
	// Keeps every cached key interned so its pointer cannot be recycled.
	std::vector<RE::BSFixedString> meshTextureKeepAlive;

	/**
	 * @brief Resolves whether a diffuse texture is painted on the landscape surface, memoising the result.
	 * @details Matches the exact diffuse path of a land texture record, or a texture under a landscape/
	 * directory that shares its file name with one (a mesh copy kept in a subfolder).
	 * @param a_name The interned diffuse texture name from the shader property.
	 * @return True if the texture qualifies for mesh stochastic variation.
	 */
	bool IsLandscapeDiffuseTexture(const RE::BSFixedString& a_name);

	/**
	 * @brief Flags qualifying non-landscape draws so the shader applies stochastic variation.
	 * @param a_pass The render pass being set up.
	 */
	void BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass);

	struct Hooks;
};
