#include "LinearLighting.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    LinearLighting::Settings,
    enableLinearLighting,
    enableGammaCorrection);

void LinearLighting::DrawSettings()
{
    ImGui::Checkbox("Enable Linear Lighting", (bool*)&settings.enableLinearLighting);
    ImGui::Checkbox("Enable Gamma Correction", (bool*)&settings.enableGammaCorrection);
}

void LinearLighting::LoadSettings(json& o_json)
{
    settings = o_json;
}

void LinearLighting::SaveSettings(json& o_json)
{
    o_json = settings;
}

void LinearLighting::RestoreDefaultSettings()
{
    settings = {};
}

void LinearLighting::PostPostLoad()
{
    MenuOpenCloseEventHandler::Register();
}