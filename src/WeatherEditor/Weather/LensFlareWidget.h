#pragma once

#include "../Widget.h"

class LensFlareWidget : public Widget
{
public:
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

	void DrawWidget() override;
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;

	// Public type required by NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT macro
	struct Settings
	{
		float fadeDistRadiusScale = 1.0f;
		float colorInfluence = 0.2f;
		bool operator==(const Settings&) const = default;
	};

private:
	void LoadFromGameSettings();

	RE::BGSLensFlare* lensFlare = nullptr;

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
};
