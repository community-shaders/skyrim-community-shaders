#pragma once

struct LinearLighting : Feature
{
	static LinearLighting* GetSingleton()
	{
		static LinearLighting singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Linear Lighting"; }
	virtual std::string GetDisplayName() override { return T("feature.linear_lighting.name", "Linear Lighting"); }
	virtual inline std::string GetShortName() override { return "LinearLighting"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.linear_lighting.description", "Linear Lighting does internal color space conversion to improve lighting calculation accuracy."),
			{ T("feature.linear_lighting.key_feature_1", "Managed color input encoding"),
				T("feature.linear_lighting.key_feature_2", "Corrects lighting calculations"),
				T("feature.linear_lighting.key_feature_3", "Makes PBR really work") } };
	};

	virtual bool IsCore() const override { return true; };

	/** @brief ENABLE_LL is a compile-time define; emit it only when the feature is enabled. */
	virtual inline std::string_view GetShaderDefineName() override { return "ENABLE_LL"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return IsLinearLightingActive(); }
	virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() override;
	virtual std::vector<std::pair<std::string_view, std::string_view>> GetCommonShaderDefines() override;

	static constexpr float FireEffectCurveMin = 0.1f;
	static constexpr float FireEffectCurveMax = 8.0f;

	struct Settings
	{
		uint enableLinearLighting = true;
		uint enableACEScg = false;
		float gameGamma = 1.6f;
		float diffuseGamma = 2.2f;
		float diffuseMidReflectance = 0.2871746f;
		float diffuseWhiteReflectance = 1.0f;

		// Lighting multipliers
		float directionalLightMult = 1.0f;
		float pointLightMult = 1.0f;
		float ambientMult = 1.0f;
		float emitColorMult = 1.0f;
		float glowmapMult = 0.66f;

		// Effect multipliers
		float effectLightingMult = 0.32f;
		float membraneEffectMult = 1.0f;
		float bloodEffectMult = 1.0f;
		float projectedEffectMult = 1.0f;
		float deferredEffectMult = 1.0f;
		float otherEffectMult = 1.0f;

		// Effect classes Effects 11 derives from shader flags
		float particleEffectMult = 1.0f;
		float lightSpriteEffectMult = 1.0f;
		float fireEffectMult = 1.0f;
		float fireEffectCurve = 1.0f;
	} settings;

	struct alignas(16) PerFrameData
	{
		uint isMainOrLoadingMenu;
		float diffuseGamma;
		float directionalLightMult;
		float pointLightMult;
		float ambientMult;
		float glowmapMult;
		float effectLightingMult;
		float membraneEffectMult;
		float bloodEffectMult;
		float projectedEffectMult;
		float deferredEffectMult;
		float otherEffectMult;
		float particleEffectMult;
		float lightSpriteEffectMult;
		float fireEffectMult;
		float fireEffectCurve;
		float diffuseCurve;
		float diffuseWhiteReflectance;
		float2 pad;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrameData);

	/** @brief Draws the ImGui settings UI for color management and lighting multiplier configuration. */
	virtual void DrawSettings() override;
	virtual void PostSetupResources() override;
	/** @brief Applies pending shader settings at the frame boundary. */
	virtual void Reset() override;
	virtual void ClearShaderCache() override;
	virtual void ModifySharedLighting(SharedLighting& lighting) override;
	virtual void Load() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	/** @brief Populates and returns the per-frame constant buffer data with color-management and multiplier settings. */
	PerFrameData GetCommonBufferData();

	bool IsLinearLightingActive() const;
	bool IsACEScgActive() const { return IsLinearLightingActive() && configuredACEScg; }
	RE::NiColor SRGBToWorking(RE::NiColor color) const;
	void SRGBToWorking(float* color) const;
	RE::NiColor LightColorToWorking(const RE::NiLight* light, bool effect = false) const;
	bool IsSunlightOverridden(const RE::NiLight* light) const;
	float GetSunlightScale(const RE::NiLight* light) const;
	void SetSunlightColor(RE::NiLight* light, RE::NiColor color);
	void ClearSunlightColor(const RE::NiLight* light);

private:
	void ApplyGameGamma();

	std::string gameGammaDefine = "1.6";
	float configuredGameGamma = 1.6f;
	bool applyGameGamma = false;
	const RE::NiLight* workingSunlight = nullptr;
	RE::NiColor workingSunlightColor{};

	bool configuredLinearLighting = false;
	bool configuredACEScg = false;
	bool resourcesReady = false;
};
