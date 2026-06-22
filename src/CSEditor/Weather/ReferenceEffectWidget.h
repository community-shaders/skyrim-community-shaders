#pragma once

#include "../Widget.h"

/**
 * @brief Widget for editing visual effect (BGSReferenceEffect) form data.
 *
 * Allows picking art objects and effect shaders, and toggling behavioral
 * flags such as face-target and attach-to-camera. Requires manual apply
 * because changes trigger a weather reinit.
 */
class ReferenceEffectWidget : public Widget
{
public:
	/**
	 * @brief Constructs a reference effect widget for the given form.
	 * @param a_referenceEffect The BGSReferenceEffect form to edit.
	 */
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

	/** @brief Renders the visual effect editor UI with form pickers and flag checkboxes. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "Visual Effect"; }

	/** @brief Returns true because form reference changes trigger a weather reinit. */
	bool RequiresManualApply() const override { return true; }

	/** @brief Deserializes reference effect settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current reference effect settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Writes the current settings into the game's BGSReferenceEffect and triggers a weather reinit. */
	void ApplyChanges() override;

	/** @brief Reverts settings to vanilla values and re-applies them to the game data. */
	void RevertChanges() override;

	/** @brief Returns true if the current settings differ from the last saved state. */
	bool HasUnsavedChanges() const override;

	/** @brief Collects all searchable reference effect settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

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
