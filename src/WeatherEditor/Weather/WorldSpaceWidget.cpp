#include "WorldSpaceWidget.h"

#include "../EditorWindow.h"

WorldSpaceWidget::~WorldSpaceWidget()
{
}

void WorldSpaceWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
		// Draw header with search and Save/Load/Delete buttons
		DrawWidgetHeader("##WorldSpaceSearch", false, true, false, nullptr);
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
