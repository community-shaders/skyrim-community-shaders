#pragma once

#include "../Widget.h"

class LensFlareWidget : public Widget
{
public:
	LensFlareWidget(RE::BGSLensFlare* a_lensFlare) :
		lensFlare(a_lensFlare)
	{
		form = a_lensFlare;
	}

	~LensFlareWidget() override = default;

	void DrawWidget() override;
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;

	RE::BGSLensFlare* lensFlare = nullptr;

private:
	struct Settings
	{
		float fadeDistRadiusScale = 1.0f;
		float colorInfluence = 0.2f;
	};

	Settings settings;
	Settings originalSettings;
};
