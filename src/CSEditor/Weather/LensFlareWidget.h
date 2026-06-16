#pragma once

#include "../Widget.h"

/**
 * @brief Widget for editing lens flare fade distance and color influence settings.
 */
class LensFlareWidget : public Widget
{
public:
	/**
	 * @brief Constructs a lens flare widget for the given form.
	 * @param a_lensFlare The BGSLensFlare form to edit.
	 */
	LensFlareWidget(RE::BGSLensFlare* a_lensFlare) :
		lensFlare(a_lensFlare)
	{
		form = a_lensFlare;
		if (lensFlare) {
			LoadFromGameSettings();
			vanillaSettings = settings;
			originalSettings = settings;
		}
	}

	~LensFlareWidget() override = default;

	/** @brief Renders the lens flare editor UI with fade distance and color controls. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "Lens Flare"; }

	/** @brief Deserializes lens flare settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current lens flare settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Writes the current settings into the game's BGSLensFlare form. */
	void ApplyChanges() override;

	/** @brief Reverts settings to vanilla values and re-applies them to the game data. */
	void RevertChanges() override;

	/** @brief Returns true if the current settings differ from the last saved state. */
	bool HasUnsavedChanges() const override;

	/** @brief Collects all searchable lens flare settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

	RE::BGSLensFlare* lensFlare = nullptr;

private:
	void LoadFromGameSettings();

	struct Settings
	{
		float fadeDistRadiusScale = 1.0f;
		float colorInfluence = 0.2f;
		bool operator==(const Settings&) const = default;
	};

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
};
