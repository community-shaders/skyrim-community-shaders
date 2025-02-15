#pragma once

struct VolumetricLighting : Feature
{
public:
	static VolumetricLighting* GetSingleton()
	{
		static VolumetricLighting singleton;
		return &singleton;
	}

	struct Settings
	{
		uint EnabledVL = true;
	};

	Settings settings;

	bool enabledAtBoot = false;

	virtual inline std::string GetName() override { return "Volumetric Lighting"; }
	virtual inline std::string GetShortName() override { return "VolumetricLighting"; }

	virtual void Reset() override;

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void DataLoaded() override;
	virtual void PostPostLoad() override;

	std::map<std::string, Util::GameSetting> VLSettings{
		{ "iVolumetricLightingQuality:Display", { "Lighting Quality", "Adjusts the quality of volumetric lighting. [-1,-2] (off, low, mid, high).", REL::Relocate<uintptr_t>(0, 0, 0x1ed4030), 1, -1, 2 } },
	};

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
			if (singleton && !(Util::IsDynamicResolution() && singleton->settings.EnabledVL)) {
				a_this->CopyResource(a_renderTarget, a_renderTargetSource);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
