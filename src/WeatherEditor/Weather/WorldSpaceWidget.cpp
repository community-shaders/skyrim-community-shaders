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
			bool useIcons = !editorWindow->settings.useTextButtons && menu && menu->GetSettings().Theme.ShowActionIcons &&
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
				const float buttonHeight = ImGui::GetFrameHeight();
				
				ImVec2 applySize = ImGui::CalcTextSize("Apply");
				applySize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
				applySize.y = buttonHeight;
				
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
				if (ImGui::Button("Apply", applySize)) {
					ApplyChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}
				
				ImGui::SameLine();
				
				ImVec2 revertSize = ImGui::CalcTextSize("Revert");
				revertSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
				revertSize.y = buttonHeight;
				
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
				if (ImGui::Button("Revert", revertSize)) {
					RevertChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Revert to saved values");
				}
			}
		}
		ImGui::Separator();

		// Search bar (activatable with Ctrl+F)
		BeginWidgetSearchBar(searchBuffer, sizeof(searchBuffer), searchActive);
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
