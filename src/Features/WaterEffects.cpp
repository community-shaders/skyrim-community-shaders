#include "WaterEffects.h"

#include <DDSTextureLoader.h>

void WaterEffects::DrawSettings()
{
	if (ImGui::TreeNodeEx("Parallax Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Parallax Height", &settings.ParallaxHeight, 0.0f, 0.5f, "%.3f");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Controls the depth of parallax occlusion mapping on flowmapped water.\n"
			                  "Higher values create more pronounced depth effect.\n"
			                  "Set to 0 to disable parallax.");
		}
		ImGui::TreePop();
	}
}

void WaterEffects::LoadSettings(json& o_json)
{
	if (o_json[GetShortName()].is_object()) {
		settings.ParallaxHeight = o_json[GetShortName()]["ParallaxHeight"];
	}
}

void WaterEffects::SaveSettings(json& o_json)
{
	o_json[GetShortName()]["ParallaxHeight"] = settings.ParallaxHeight;
}

void WaterEffects::RestoreDefaultSettings()
{
	settings.ParallaxHeight = 0.15f;
}

void WaterEffects::SetupResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\WaterEffects\\watercaustics.dds", nullptr, causticsView.put());
}

void WaterEffects::Prepass()
{
	auto context = globals::d3d::context;
	auto srv = causticsView.get();
	context->PSSetShaderResources(65, 1, &srv);
}

bool WaterEffects::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}
