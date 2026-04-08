/*
* This file accompanies NewFeature.h
* Please refer to the header for more information.
*
* ProfJack
* 2025-06-28
*/

#include "SceneGraphExplorer.h"

#include "Globals.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SceneGraphExplorer::Settings,
	Enabled)

////////////////////////////////////////////////////////////////////////////////////

void SceneGraphExplorer::RestoreDefaultSettings()
{
	settings = {};
}

void SceneGraphExplorer::LoadSettings(json& o_json)
{
	settings = o_json;
}

void SceneGraphExplorer::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SceneGraphExplorer::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
}

void SceneGraphExplorer::DrawOverlay()
{
	if (!settings.Enabled)
		return;

	auto* sceneGraph = RE::Main::GetSingleton()->WorldRootNode();

	DrawObject(sceneGraph, true);
}

void SceneGraphExplorer::DrawObject(RE::NiAVObject* object, bool root)
{
	if (!object)
		return;

	ImGuiTreeNodeFlags flag = root ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;

	auto* node = object->AsNode();

	if (!node || node->GetChildren().empty())
		flag |= ImGuiTreeNodeFlags_Leaf;

	ImGui::PushID(static_cast<int>(reinterpret_cast<intptr_t>(object)));

	if (ImGui::TreeNodeEx(std::format("{} \"{}\"", object->GetRTTI()->name, object->name.c_str()).c_str(), flag)) {
		ImGui::Text("Position (%.2f, %.2f, %.2f)", object->world.translate.x, object->world.translate.y, object->world.translate.z);

		if (object->controllers)
			ImGui::Text("Controller %s", object->controllers->GetRTTI()->name);

		if (object->extraDataSize > 0 && ImGui::TreeNodeEx("Extra Data")) {
			for (size_t i = 0; i < object->extraDataSize; i++) {
				auto* extraData = object->extra[i];

				if (!extraData)
					return;

				ImGui::Text("[%d] %s", i, extraData->GetRTTI()->name);
			}

			ImGui::TreePop();
		}

		if (node) {
			for (auto& child : node->GetChildren()) {
				DrawObject(child.get());
			}
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
}