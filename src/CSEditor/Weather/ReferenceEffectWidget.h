#pragma once

#include "../Widget.h"

/** @brief The BGSReferenceEffect fields a ReferenceEffectWidget edits. */
struct ReferenceEffectSettings
{
	RE::BGSArtObject* artObject = nullptr;
	RE::TESEffectShader* effectShader = nullptr;
	bool faceTarget = false;
	bool attachToCamera = false;
	bool inheritRotation = false;
	bool operator==(const ReferenceEffectSettings&) const = default;
};

/**
 * @brief Widget for editing visual effect (BGSReferenceEffect) form data.
 *
 * Allows picking art objects and effect shaders, and toggling behavioral
 * flags such as face-target and attach-to-camera.
 */
class ReferenceEffectWidget : public SettingsWidget<ReferenceEffectSettings>
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
			CaptureVanillaSettings();
		}
	}

	~ReferenceEffectWidget() override = default;

	/** @brief Renders the visual effect editor UI with form pickers and flag checkboxes. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "Visual Effect"; }

	/** @brief Deserializes reference effect settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current reference effect settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Writes the current settings into the game's BGSReferenceEffect and refreshes the live sky effect. */
	void ApplyChanges() override;

	/** @brief Collects all searchable reference effect settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

	RE::BGSReferenceEffect* referenceEffect = nullptr;

private:
	void LoadFromGameSettings();

	/** @brief Restarts the current weather's sky effect when the form just changed or it still shows other art.
	 *  @param a_recordChanged Whether the apply just rewrote the form's art, shader or flags. */
	void RestartStaleSkyEffect(bool a_recordChanged) const;

	std::vector<RE::BGSArtObject*> artObjectArray;
	std::vector<RE::TESEffectShader*> effectShaderArray;
};
