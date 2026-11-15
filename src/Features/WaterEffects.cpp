#include "WaterEffects.h"

#include <DDSTextureLoader.h>

#include "I18n/I18n.h"
#include "Utils/D3D.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.water_effects."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WaterEffects::Settings,
	physicalReflections)

void WaterEffects::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("physical_reflections"), "Physically Based Reflections"), (bool*)&settings.physicalReflections);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("physical_reflections_tooltip"),
							  "Uses a physically based water fresnel and a sharp sun disk highlight.\n"
							  "Disable to use the fresnel and sun specular from the water type."));
	}
}

#undef I18N_KEY_PREFIX

void WaterEffects::LoadSettings(json& o_json)
{
	settings = o_json;
}

void WaterEffects::SaveSettings(json& o_json)
{
	o_json = settings;
}

void WaterEffects::RestoreDefaultSettings()
{
	settings = {};
}

void WaterEffects::SetupResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	winrt::com_ptr<ID3D11Resource> causticsTexture;
	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\WaterEffects\\watercaustics.dds", causticsTexture.put(), causticsView.put());
	Util::SetResourceName(causticsTexture.get(), "WaterEffects::Caustics");
	Util::SetResourceName(causticsView.get(), "WaterEffects::Caustics SRV");
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
