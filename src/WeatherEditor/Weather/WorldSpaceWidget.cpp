#include "WorldSpaceWidget.h"

#include "../EditorWindow.h"

WorldSpaceWidget::~WorldSpaceWidget()
{
}

void WorldSpaceWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		DrawMenu();

		auto editorWindow = EditorWindow::GetSingleton();

		if (!editorWindow->settings.autoApplyChanges) {
			auto menu = globals::menu;
			bool useIcons = menu && menu->GetSettings().Theme.ShowActionIcons &&
			                menu->uiIcons.saveSettings.texture &&
			                menu->uiIcons.featureSettingRevert.texture;

			if (useIcons) {
				const float iconSize = ImGui::GetFrameHeight();
				const ImVec2 buttonSize(iconSize, iconSize);

				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));

				if (ImGui::ImageButton("##ApplyWorldSpace", menu->uiIcons.saveSettings.texture, buttonSize)) {
					ApplyChanges();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}

				ImGui::SameLine();

				if (ImGui::ImageButton("##RevertWorldSpace", menu->uiIcons.featureSettingRevert.texture, buttonSize)) {
					RevertChanges();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Revert to saved values");
				}

				ImGui::PopStyleColor(2);
				ImGui::PopStyleVar();
			} else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
				if (ImGui::Button("Apply", ImVec2(ImGui::GetContentRegionAvail().x * 0.49f, 0))) {
					ApplyChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
				if (ImGui::Button("Revert", ImVec2(-1, 0))) {
					RevertChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Revert to saved values");
				}
			}
		}
		ImGui::Separator();
	}
	ImGui::End();
}

void WorldSpaceWidget::ApplyChanges()
{
	SaveSettings();
}

void WorldSpaceWidget::RevertChanges()
{
	LoadSettings();
}

void WorldSpaceWidget::LoadSettings()
{
	// Empty - no settings to load
}

void WorldSpaceWidget::SaveSettings()
{
	// Empty - no settings to save
}
