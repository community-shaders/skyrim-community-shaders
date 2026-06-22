#pragma once

#include "../Widget.h"

/**
 * @brief Widget for editing TESImageSpace HDR, cinematic, tint, and depth-of-field settings.
 *
 * Each weather record references up to four ImageSpace forms (one per time of day).
 * This widget exposes the full ImageSpace data struct for live editing.
 */
class ImageSpaceWidget : public Widget
{
public:
	RE::TESImageSpace* imageSpace = nullptr;

	/**
	 * @brief Constructs an ImageSpace widget for the given form.
	 * @param a_imageSpace The ImageSpace form to edit. Must not be null.
	 */
	ImageSpaceWidget(RE::TESImageSpace* a_imageSpace)
	{
		if (!a_imageSpace) {
			logger::error("ImageSpaceWidget created with null pointer");
			return;
		}
		form = a_imageSpace;
		imageSpace = a_imageSpace;
		LoadFromGameSettings();
		vanillaSettings = settings;
		originalSettings = settings;
	}

	struct Settings
	{
		// HDR Settings
		float hdrEyeAdaptSpeed = 0.0f;
		float hdrEyeAdaptStrength = 0.0f;
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

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;

	~ImageSpaceWidget();

	/** @brief Renders the ImageSpace editor UI with HDR, cinematic, tint, and DOF controls. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "ImageSpace"; }

	/** @brief Deserializes ImageSpace settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current ImageSpace settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Returns true if the current settings differ from the last saved state. */
	bool HasUnsavedChanges() const override;

	/** @brief Collects all searchable ImageSpace settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

	/** @brief Writes the current widget settings into the game's TESImageSpace data struct. */
	void SetImageSpaceValues();

	/** @brief Reads the game's TESImageSpace data struct into the widget settings. */
	void LoadImageSpaceValues();

	/** @brief Initializes widget settings from the current game ImageSpace data. */
	void LoadFromGameSettings();

	/** @brief Applies the current settings to the game's ImageSpace form. */
	void ApplyChanges() override;

	/** @brief Reverts settings to vanilla values and re-applies them to the game data. */
	void RevertChanges() override;
};
