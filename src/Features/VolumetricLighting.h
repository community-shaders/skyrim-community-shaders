#pragma once

struct VolumetricLighting : Feature
{
public:
	static VolumetricLighting* GetSingleton()
	{
		static VolumetricLighting singleton;
		return &singleton;
	}

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

	bool enabledAtBoot = false;

	virtual inline std::string GetName() override { return "Volumetric Lighting"; }
	virtual inline std::string GetShortName() override { return "VolumetricLighting"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Volumetric Lighting creates realistic light scattering effects through fog, dust, and atmospheric particles.\n"
			"This adds dramatic god rays and atmospheric depth to both interior and exterior environments.",
			{ "Realistic light scattering",
				"God rays and atmospheric effects",
				"Separate interior/exterior settings",
				"Configurable quality levels",
				"Enhanced atmospheric immersion" }
		};
	}

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void DataLoaded() override;
	virtual void PostPostLoad() override;
	virtual void EarlyPrepass() override;

	std::map<std::string, Util::GameSetting> hiddenVRSettings{
		{ "bEnableVolumetricLighting:Display", { "Enable VL Shaders (INI) ",
												   "Enables volumetric lighting effects by creating shaders. "
												   "Needed at startup. ",
												   0x1ed63d8, true, false, true } },
		{ "bVolumetricLightingEnable:Display", { "Enable VL (INI))", "Enables volumetric lighting. ", 0x3485360, true, false, true } },
		{ "bVolumetricLightingUpdateWeather:Display", { "Enable Volumetric Lighting (Weather) (INI) ",
														  "Enables volumetric lighting for weather. "
														  "Only used during startup and used to set bVLWeatherUpdate.",
														  0x3485361, true, false, true } },
		{ "bVLWeatherUpdate", { "Enable VL (Weather)", "Enables volumetric lighting for weather.", 0x3485363, true, false, true } },
		{ "bVolumetricLightingEnabled_143232EF0", { "Enable VL (Papyrus) ",
													  "Enables volumetric lighting. "
													  "This is the Papyrus command. ",
													  REL::Relocate<uintptr_t>(0x3232ef0, 0, 0x3485362), true, false, true } },
	};

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };

	// hooks

	struct CopyResource
	{
		static void thunk(ID3D11DeviceContext* a_this, ID3D11Resource* a_renderTarget, ID3D11Resource* a_renderTargetSource)
		{
			// In VR with dynamic resolution enabled, there's a bug with the depth stencil.
			// The depth stencil passed to IsFullScreenVR is scaled down incorrectly.
			// The fix is to stop a CopyResource from replacing kMAIN_COPY with kMAIN after
			// ISApplyVolumetricLighting because it clobbers a properly scaled kMAIN_COPY.
			// The kMAIN_COPY does not appear to be used in the remaining frame after
			// ISApplyVolumetricLighting except for IsFullScreenVR.
			// But, the copy might have to be done manually later after IsFullScreenVR if
			// used in the next frame.

			auto* singleton = GetSingleton();
			if (singleton && !(Util::IsDynamicResolution() && *singleton->bEnableVolumetricLighting)) {
				a_this->CopyResource(a_renderTarget, a_renderTargetSource);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct RenderDepth
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

private:
	struct VolumetricLightingDescriptor
	{};

	static const char* FromUnits(int32_t value, int32_t unitScale);
	static VolumetricLightingDescriptor& GetVLDescriptor();
	static void SetVLQuality(VolumetricLightingDescriptor& descriptor, std::uint32_t quality);
	static void RenderVolumetricLighting(VolumetricLightingDescriptor* descriptor, RE::NiCamera* camera, bool flag);

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

	bool* bEnableVolumetricLighting = nullptr;
	TextureSize* gVolumetricLightingSizeHigh = nullptr;
	TextureSize* gVolumetricLightingSizeMedium = nullptr;
	TextureSize* gVolumetricLightingSizeLow = nullptr;

	bool initialised = false;
	bool inInterior = false;
	bool inInteriorWithSunShadows = false;
};
