/*
* This file accompanies NewFeature.h
* Please refer to the header for more information.
*
* ProfJack
* 2025-06-28
*/

#include "SceneGraphExplorer.h"

#include "../I18n/I18n.h"
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

#define I18N_KEY_PREFIX "feature.scene_graph_explorer."

void SceneGraphExplorer::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &settings.Enabled);
}

#undef I18N_KEY_PREFIX

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

	auto childCountLabel = node ? std::format(" ({})", node->GetChildren().size()) : "";

	const auto& flags = object->GetFlags();

	const bool hidden = flags.all(RE::NiAVObject::Flag::kHidden);

	if (hidden)
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));

	if (ImGui::TreeNodeEx(std::format("{} \"{}\"{} [0x{:08X}]", object->GetRTTI()->name, object->name.c_str(), childCountLabel, reinterpret_cast<uintptr_t>(object)).c_str(), flag)) {
		ImGui::Text("Position (%.2f, %.2f, %.2f)", object->world.translate.x, object->world.translate.y, object->world.translate.z);

		std::bitset<32> flagsBits = flags.underlying();

		if (ImGui::BeginCombo(std::format("{} set", flagsBits.count()).c_str() , "Flags")) {
			for (const auto& [value, name] : magic_enum::enum_entries<RE::NiAVObject::Flag>()) {
				ImGui::Selectable(name.data(), flags.all(value));
			}

			ImGui::EndCombo();
		}

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

	if (hidden)
		ImGui::PopStyleColor();

	ImGui::PopID();
}