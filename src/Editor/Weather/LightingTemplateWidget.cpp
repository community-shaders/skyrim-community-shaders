#include "LightingTemplateWidget.h"

LightingTemplateWidget::~LightingTemplateWidget()
{
}

void LightingTemplateWidget::DrawWidget()
{
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("Menu")) {
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}
	}
	ImGui::End();
}

void LightingTemplateWidget::LoadSettings()
{
}

void LightingTemplateWidget::SaveSettings()
{
}