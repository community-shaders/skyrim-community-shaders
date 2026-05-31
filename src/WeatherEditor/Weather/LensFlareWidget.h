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
	const char* GetWidgetTypeName() const override { return "Lens Flare"; }
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;
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
