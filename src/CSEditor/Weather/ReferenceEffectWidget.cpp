#include "ReferenceEffectWidget.h"
#include "../../I18n/I18n.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

#include <format>

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	namespace ReferenceEffectSetting
	{
		constexpr const char* kArtObject = "Art Object";
		constexpr const char* kEffectShader = "Effect Shader";
		constexpr const char* kFaceTarget = "Face Target";
		constexpr const char* kAttachToCamera = "Attach To Camera";
		constexpr const char* kInheritRotation = "Inherit Rotation";
	}
}

void ReferenceEffectWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##ReferenceEffectSearch", true, true);
		DrawSearchDropdown();
		BeginScrollableContent("##REScroll");
		{
			bool changed = false;

			auto editorWindow = EditorWindow::GetSingleton();
			auto drawFormPicker = [&](const char* label, auto& currentForm, const auto& widgets) {
				const char* displayLabel = label == ReferenceEffectSetting::kArtObject ?
				                               T(TKEY("art_object"), "Art Object") :
				                               T(TKEY("effect_shader"), "Effect Shader");
				const auto controlLabel = std::format("{}##{}", displayLabel, label);
				return DrawWithHighlight(label, [&]() {
					return WeatherUtils::DrawFormPickerCached(controlLabel.c_str(), currentForm, widgets, false, true);
				});
			};

			if (DrawIfMatchesSearch(ReferenceEffectSetting::kArtObject, [&](const char* label) {
					ImGui::SeparatorText(label);
					if (editorWindow->artObjectWidgets.empty()) {
						ImGui::TextDisabled("%s", T(TKEY("no_art_objects_available"), "No Art Objects available"));
						return false;
					}
					return drawFormPicker(label, settings.artObject, editorWindow->artObjectWidgets);
				}))
				changed = true;
			if (DrawIfMatchesSearch(ReferenceEffectSetting::kEffectShader, [&](const char* label) {
					ImGui::SeparatorText(label);
					if (editorWindow->effectShaderWidgets.empty()) {
						ImGui::TextDisabled("%s", T(TKEY("no_effect_shaders_available"), "No Effect Shaders available"));
						return false;
					}
					return drawFormPicker(label, settings.effectShader, editorWindow->effectShaderWidgets);
				}))
				changed = true;
			if (MatchesAnySearch({ ReferenceEffectSetting::kFaceTarget, ReferenceEffectSetting::kAttachToCamera, ReferenceEffectSetting::kInheritRotation })) {
				ImGui::SeparatorText(T(TKEY("flags"), "Flags"));
				if (WeatherUtils::DrawCheckbox(ReferenceEffectSetting::kFaceTarget, settings.faceTarget))
					changed = true;
				if (WeatherUtils::DrawCheckbox(ReferenceEffectSetting::kAttachToCamera, settings.attachToCamera))
					changed = true;
				if (WeatherUtils::DrawCheckbox(ReferenceEffectSetting::kInheritRotation, settings.inheritRotation))
					changed = true;
			}

			if (changed && editorWindow->settings.autoApplyChanges) {
				editorWindow->PushUndoState(this);
				ApplyChanges();
			}
		}
		EndScrollableContent();
	}
	ImGui::End();
}

#undef I18N_KEY_PREFIX

void ReferenceEffectWidget::LoadSettings()
{
	if (!referenceEffect)
		return;

	if (!js.empty()) {
		settings = vanillaSettings;
		try {
			if (js.contains("artObject")) {
				std::string formIDStr = js["artObject"].get<std::string>();
				if (formIDStr != "00000000") {
					uint32_t formID = std::stoul(formIDStr, nullptr, 16);
					settings.artObject = RE::TESForm::LookupByID<RE::BGSArtObject>(formID);
				} else {
					settings.artObject = nullptr;
				}
			}
			if (js.contains("effectShader")) {
				std::string formIDStr = js["effectShader"].get<std::string>();
				if (formIDStr != "00000000") {
					uint32_t formID = std::stoul(formIDStr, nullptr, 16);
					settings.effectShader = RE::TESForm::LookupByID<RE::TESEffectShader>(formID);
				} else {
					settings.effectShader = nullptr;
				}
			}
			if (js.contains("faceTarget"))
				settings.faceTarget = js["faceTarget"];
			if (js.contains("attachToCamera"))
				settings.attachToCamera = js["attachToCamera"];
			if (js.contains("inheritRotation"))
				settings.inheritRotation = js["inheritRotation"];
		} catch (const std::exception& e) {
			logger::error("ReferenceEffect {}: Failed to load from JSON: {}", GetEditorID(), e.what());
			settings = vanillaSettings;
		}
	} else {
		settings = vanillaSettings;
	}

	originalSettings = settings;
	ApplyChanges();
}

void ReferenceEffectWidget::LoadFromGameSettings()
{
	if (!referenceEffect)
		return;
	settings.artObject = referenceEffect->data.artObject;
	settings.effectShader = referenceEffect->data.effectShader;
	settings.faceTarget = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kFaceTarget);
	settings.attachToCamera = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kAttachToCamera);
	settings.inheritRotation = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kInheritRotation);
}

void ReferenceEffectWidget::SaveSettings()
{
	js["artObject"] = settings.artObject ? std::format("{:08X}", settings.artObject->GetFormID()) : "00000000";
	js["effectShader"] = settings.effectShader ? std::format("{:08X}", settings.effectShader->GetFormID()) : "00000000";
	js["faceTarget"] = settings.faceTarget;
	js["attachToCamera"] = settings.attachToCamera;
	js["inheritRotation"] = settings.inheritRotation;
	originalSettings = settings;
}

void ReferenceEffectWidget::ApplyChanges()
{
	if (!referenceEffect)
		return;

	referenceEffect->data.artObject = settings.artObject;
	referenceEffect->data.effectShader = settings.effectShader;

	referenceEffect->data.flags.reset();
	if (settings.faceTarget)
		referenceEffect->data.flags.set(RE::BGSReferenceEffect::Flag::kFaceTarget);
	if (settings.attachToCamera)
		referenceEffect->data.flags.set(RE::BGSReferenceEffect::Flag::kAttachToCamera);
	if (settings.inheritRotation)
		referenceEffect->data.flags.set(RE::BGSReferenceEffect::Flag::kInheritRotation);

	Widget::ForceCurrentWeatherReinit();
}

void ReferenceEffectWidget::RevertChanges()
{
	settings = vanillaSettings;
	ApplyChanges();
}

bool ReferenceEffectWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}

std::vector<Widget::SearchResult> ReferenceEffectWidget::CollectSearchableSettings() const
{
	return {
		{ WeatherUtils::TranslateControlLabel(ReferenceEffectSetting::kArtObject), "", ReferenceEffectSetting::kArtObject },
		{ WeatherUtils::TranslateControlLabel(ReferenceEffectSetting::kEffectShader), "", ReferenceEffectSetting::kEffectShader },
		{ WeatherUtils::TranslateControlLabel(ReferenceEffectSetting::kFaceTarget), "", ReferenceEffectSetting::kFaceTarget },
		{ WeatherUtils::TranslateControlLabel(ReferenceEffectSetting::kAttachToCamera), "", ReferenceEffectSetting::kAttachToCamera },
		{ WeatherUtils::TranslateControlLabel(ReferenceEffectSetting::kInheritRotation), "", ReferenceEffectSetting::kInheritRotation },
	};
}
