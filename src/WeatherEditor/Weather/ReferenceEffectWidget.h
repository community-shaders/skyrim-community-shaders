#pragma once

#include "../Widget.h"

class ReferenceEffectWidget : public Widget
{
public:
	ReferenceEffectWidget(RE::BGSReferenceEffect* a_referenceEffect) :
		referenceEffect(a_referenceEffect)
	{
		form = a_referenceEffect;
		if (referenceEffect) {
			LoadFromGameSettings();
			vanillaSettings = settings;
			originalSettings = settings;
		}
	}

	~ReferenceEffectWidget() override = default;

	void DrawWidget() override;
	const char* GetWidgetTypeName() const override { return "Visual Effect"; }
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;

	RE::BGSReferenceEffect* referenceEffect = nullptr;

private:
	void LoadFromGameSettings();

	struct Settings
	{
		RE::BGSArtObject* artObject = nullptr;
		RE::TESEffectShader* effectShader = nullptr;
		bool faceTarget = false;
		bool attachToCamera = false;
		bool inheritRotation = false;
		bool operator==(const Settings&) const = default;
	};

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;

	std::vector<RE::BGSArtObject*> artObjectArray;
	std::vector<RE::TESEffectShader*> effectShaderArray;
};
