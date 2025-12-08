#include "WorldSpaceWidget.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WorldSpaceWidget::Settings, temp)

WorldSpaceWidget::~WorldSpaceWidget()
{
}

void WorldSpaceWidget::DrawWidget()
{
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		DrawMenu();
	}
	ImGui::End();
}

void WorldSpaceWidget::LoadSettings()
{
	if (!js.empty()) {
		settings = js;
	}
}

void WorldSpaceWidget::SaveSettings()
{
	js = settings;
}
