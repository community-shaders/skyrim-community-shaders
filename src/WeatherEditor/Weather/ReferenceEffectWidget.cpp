#include "ReferenceEffectWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

void ReferenceEffectWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
		DrawWidgetHeader("##ReferenceEffectSearch", false, true);

		bool changed = false;

		auto editorWindow = EditorWindow::GetSingleton();

		ImGui::SeparatorText("Art Object");
		if (editorWindow->artObjectWidgets.empty()) {
			ImGui::TextDisabled("No Art Objects available");
		} else {
			if (WeatherUtils::DrawFormPickerCached("Art Object", settings.artObject, editorWindow->artObjectWidgets, false, true))
				changed = true;
		}

		ImGui::SeparatorText("Effect Shader");
		if (editorWindow->effectShaderWidgets.empty()) {
			ImGui::TextDisabled("No Effect Shaders available");
		} else {
			if (WeatherUtils::DrawFormPickerCached("Effect Shader", settings.effectShader, editorWindow->effectShaderWidgets, false, true))
				changed = true;
		}

		ImGui::SeparatorText("Flags");
		if (ImGui::Checkbox("Face Target", &settings.faceTarget))
			changed = true;
		if (ImGui::Checkbox("Attach To Camera", &settings.attachToCamera))
			changed = true;
		if (ImGui::Checkbox("Inherit Rotation", &settings.inheritRotation))
			changed = true;

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}
	ImGui::End();
}

void ReferenceEffectWidget::LoadSettings()
{
	if (!referenceEffect)
		return;

	if (!js.empty()) {
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
		}
	} else {
		settings.artObject = referenceEffect->data.artObject;
		settings.effectShader = referenceEffect->data.effectShader;
		settings.faceTarget = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kFaceTarget);
		settings.attachToCamera = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kAttachToCamera);
		settings.inheritRotation = referenceEffect->data.flags.any(RE::BGSReferenceEffect::Flag::kInheritRotation);
	}

	originalSettings = settings;
}

void ReferenceEffectWidget::SaveSettings()
{
	js["artObject"] = settings.artObject ? std::format("{:08X}", settings.artObject->GetFormID()) : "00000000";
	js["effectShader"] = settings.effectShader ? std::format("{:08X}", settings.effectShader->GetFormID()) : "00000000";
	js["faceTarget"] = settings.faceTarget;
	js["attachToCamera"] = settings.attachToCamera;
	js["inheritRotation"] = settings.inheritRotation;
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

	originalSettings = settings;
}

void ReferenceEffectWidget::RevertChanges()
{
	settings = originalSettings;
}

bool ReferenceEffectWidget::HasUnsavedChanges() const
{
	return settings.artObject != originalSettings.artObject ||
	       settings.effectShader != originalSettings.effectShader ||
	       settings.faceTarget != originalSettings.faceTarget ||
	       settings.attachToCamera != originalSettings.attachToCamera ||
	       settings.inheritRotation != originalSettings.inheritRotation;
}
