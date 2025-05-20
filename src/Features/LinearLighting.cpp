#include "LinearLighting.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    LinearLighting::Settings,
    enableLinearLighting,
    enableGammaCorrection,
    preserveLightLuminance,
    lightGamma,
    colorGamma,
    ambientGamma);

void LinearLighting::DrawSettings()
{
    ImGui::Checkbox("Enable Linear Lighting", (bool*)&settings.enableLinearLighting);
    ImGui::Checkbox("Enable Gamma Correction", (bool*)&settings.enableGammaCorrection);
    ImGui::Checkbox("Preserve Light Luminance", (bool*)&settings.preserveLightLuminance);
    ImGui::Text("Gamma Settings");
    ImGui::SliderFloat("Light Gamma", &settings.lightGamma, 1.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Color Gamma", &settings.colorGamma, 1.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Ambient Gamma", &settings.ambientGamma, 1.0f, 3.0f, "%.2f");
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

LinearLighting::Settings LinearLighting::GetCommonBufferData()
{
    auto data = settings;
    data.enableLinearLighting = settings.enableLinearLighting && !tempDisable;
    data.enableGammaCorrection = settings.enableGammaCorrection;
    data.preserveLightLuminance = settings.preserveLightLuminance;
    data.lightGamma = settings.lightGamma;
    data.colorGamma = settings.colorGamma;
    data.ambientGamma = settings.ambientGamma;
    return data;
}