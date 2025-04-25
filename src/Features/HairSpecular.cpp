#include "HairSpecular.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    HairSpecular::Settings,
    Enabled,
    HairGlossiness,
    SpecularMult,
    DiffuseMult)

void HairSpecular::DrawSettings()
{
    ImGui::Checkbox("Enabled", (bool*)&settings.Enabled);
    ImGui::SliderFloat("Glossiness", &settings.HairGlossiness, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Specular Multiplier", &settings.SpecularMult, 0.0f, 10.0f);
    ImGui::SliderFloat("Diffuse Multiplier", &settings.DiffuseMult, 0.0f, 10.0f);
}

void HairSpecular::LoadSettings(json& o_json)
{
    settings = o_json;
}

void HairSpecular::SaveSettings(json& o_json)
{
    o_json = settings;
}

void HairSpecular::RestoreDefaultSettings()
{
    settings = {};
}