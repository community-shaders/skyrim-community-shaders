#pragma once

/** @brief Adds configurable volumetric lighting with god rays and atmospheric scattering effects. */
struct VolumetricLighting : Feature
{
public:
	/** @brief Dimensions for the volumetric lighting 3D texture. */
	struct TextureSize
	{
		int32_t Width = 320;
		int32_t Height = 192;
		int32_t Depth = 90;
	};

	struct Settings
	{
		bool ExteriorEnabled = true;
		int32_t ExteriorQuality = 2;
		TextureSize ExteriorCustomSize;
		bool InteriorEnabled = true;
		int32_t InteriorQuality = 2;
		TextureSize InteriorCustomSize;
	};

	Settings settings;

	virtual inline std::string GetName() override { return "Volumetric Lighting"; }
	virtual std::string GetDisplayName() override { return T("feature.volumetric_lighting.name", "Volumetric Lighting"); }
	/** @brief Returns the short identifier used for file paths and logging. */
	virtual inline std::string GetShortName() override { return "VolumetricLighting"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	/** @brief Returns a summary description and list of key features for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.volumetric_lighting.description", "Volumetric Lighting creates realistic light scattering effects through fog, dust, and atmospheric particles.\nThis adds dramatic god rays and atmospheric depth to both interior and exterior environments."),
			{ T("feature.volumetric_lighting.key_feature_1", "Realistic light scattering"),
				T("feature.volumetric_lighting.key_feature_2", "God rays and atmospheric effects"),
				T("feature.volumetric_lighting.key_feature_3", "Separate interior/exterior settings"),
				T("feature.volumetric_lighting.key_feature_4", "Configurable quality levels"),
				T("feature.volumetric_lighting.key_feature_5", "Enhanced atmospheric immersion") } };
	};

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	/** @brief Draws the ImGui settings panel for volumetric lighting configuration. */
	virtual void DrawSettings() override;
	/** @brief Handles post-data-load initialization. */
	virtual void DataLoaded() override;
	/** @brief Resolves game engine addresses and patches the raymarch dispatch loop. */
	virtual void PostPostLoad() override;
	/** @brief Creates the volumetric lighting constant buffer. */
	virtual void SetupResources() override;
	/** @brief Updates screen dimensions, detects interior/exterior transitions, and configures VL quality. */
	virtual void EarlyPrepass() override;

	virtual bool IsCore() const override { return true; };

	/**
	 * @brief Creates a BSImagespaceShader wrapping a compute shader for volumetric lighting passes.
	 * @param name The shader's internal name.
	 * @param fileName The FXP filename for the shader.
	 * @param computeShader The compute shader to wrap.
	 * @return The created BSImagespaceShader instance.
	 */
	static RE::BSImagespaceShader* CreateShader(const std::string_view& name, const std::string_view& fileName, RE::BSComputeShader* computeShader);
	/**
	 * @brief Returns the density generation compute shader, creating it on first call.
	 * @param computeShader The compute shader to wrap if creation is needed.
	 * @return The cached BSImagespaceShader for the generate pass.
	 */
	RE::BSImagespaceShader* GetOrCreateGenerateCS(RE::BSComputeShader* computeShader);
	/**
	 * @brief Returns the raymarching compute shader, creating it on first call.
	 * @param computeShader The compute shader to wrap if creation is needed.
	 * @return The cached BSImagespaceShader for the raymarch pass.
	 */
	RE::BSImagespaceShader* GetOrCreateRaymarchCS(RE::BSComputeShader* computeShader);
	/**
	 * @brief Returns the horizontal blur compute shader, creating it on first call.
	 * @param computeShader The compute shader to wrap if creation is needed.
	 * @return The cached BSImagespaceShader for the horizontal blur pass.
	 */
	RE::BSImagespaceShader* GetOrCreateBlurHCS(RE::BSComputeShader* computeShader);
	/**
	 * @brief Returns the vertical blur compute shader, creating it on first call.
	 * @param computeShader The compute shader to wrap if creation is needed.
	 * @return The cached BSImagespaceShader for the vertical blur pass.
	 */
	RE::BSImagespaceShader* GetOrCreateBlurVCS(RE::BSComputeShader* computeShader);
	/** @brief Binds the screen dimensions constant buffer to compute shader slot 1. */
	void SetDimensionsCB() const;
	/**
	 * @brief Calculates the thread group count for the horizontal blur dispatch.
	 * @param threadGroupCountX Output parameter set to the required X thread group count.
	 */
	void SetGroupCountsHCS(uint32_t& threadGroupCountX) const;
	/**
	 * @brief Calculates the thread group count for the vertical blur dispatch.
	 * @param threadGroupCountY Output parameter set to the required Y thread group count.
	 */
	void SetGroupCountsVCS(uint32_t& threadGroupCountY) const;

private:
	struct VolumetricLightingDescriptor
	{};

	static const char* FromUnits(int32_t value, int32_t unitScale);
	static VolumetricLightingDescriptor& GetVLDescriptor();
	static void SetVLQuality(VolumetricLightingDescriptor& descriptor, std::uint32_t quality);
	void DrawVolumetricLightingSettings(int32_t& quality, TextureSize& customSize, bool isInterior, bool inLocationType);
	TextureSize& FetchCurrentSizeInUnits(bool interior);
	void SetupVL();

	enum class Quality : uint8_t
	{
		Low,
		Medium,
		High,
		Custom,
		Count
	};

	const char* QualityNames[static_cast<uint8_t>(Quality::Count)] = { "Low", "Medium", "High", "Custom" };

	TextureSize exteriorSizeInUnits;
	TextureSize interiorSizeInUnits;
	TextureSize defaultSizeHigh;

	TextureSize* gVolumetricLightingSizeHigh = nullptr;
	TextureSize* gVolumetricLightingSizeMedium = nullptr;
	TextureSize* gVolumetricLightingSizeLow = nullptr;

	bool initialised = false;
	bool inInterior = false;
	bool inInteriorWithSun = false;

	struct VLData
	{
		int32_t screenX;
		int32_t screenY;
		int32_t screenXMin1;
		int32_t screenYMin1;
	};
	VLData vlData = VLData();
	ConstantBuffer* vlDataCB = nullptr;

	static constexpr int32_t BlurThreadGroupSizeX = 256;
	static constexpr int32_t BlurThreadGroupSizeY = 256;
	static constexpr int32_t BlurWindow = 12;

	RE::BSImagespaceShader* generateCS = nullptr;
	RE::BSImagespaceShader* raymarchCS = nullptr;
	RE::BSImagespaceShader* blurHCS = nullptr;
	RE::BSImagespaceShader* blurVCS = nullptr;
};
