#pragma once

#include "../Widget.h"

class ImageSpaceWidget : public Widget
{
public:
	ImageSpaceWidget(RE::TESImageSpace* a_imageSpace) :
		imageSpace(a_imageSpace)
	{
		if (!a_imageSpace) {
			logger::error("ImageSpaceWidget created with null pointer");
			return;
		}
		form = a_imageSpace;
		LoadFromGameSettings();
		vanillaSettings = settings;
		originalSettings = settings;
	}

	~ImageSpaceWidget() override = default;

	void DrawWidget() override;
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;

	// Public type required by NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT macro
	struct Settings
	{
		// HDR Settings
		float hdrEyeAdaptSpeed = 0.0f;
		float hdrBloomBlurRadius = 0.0f;
		float hdrBloomThreshold = 0.0f;
		float hdrBloomScale = 0.0f;
		float hdrWhite = 0.0f;
		float hdrSunlightScale = 0.0f;
		float hdrSkyScale = 0.0f;

		// Cinematic Settings
		float cinematicSaturation = 0.0f;
		float cinematicBrightness = 0.0f;
		float cinematicContrast = 0.0f;

		// Tint Colors
		float3 tintColor = { 1.0f, 1.0f, 1.0f };
		float tintAmount = 0.0f;

		// Depth of Field
		float dofStrength = 0.0f;
		float dofDistance = 0.0f;
		float dofRange = 0.0f;
		bool operator==(const Settings&) const = default;
	};

private:
	void LoadFromGameSettings();

	RE::TESImageSpace* imageSpace = nullptr;

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
};
