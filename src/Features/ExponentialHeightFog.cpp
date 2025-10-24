#include "ExponentialHeightFog.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ExponentialHeightFog::Settings,
    enabled,
    fogHeight,
    fogHeightFalloff,
    fogDensityMultiplier)

void ExponentialHeightFog::RestoreDefaultSettings()
{
    settings = {};
}

void ExponentialHeightFog::LoadSettings(json& o_json)
{
    settings = o_json;
}

void ExponentialHeightFog::SaveSettings(json& o_json)
{
    o_json = settings;
}

void ExponentialHeightFog::DrawSettings()
{
    ImGui::Checkbox("Enable Exponential Height Fog", (bool*)&settings.enabled);
    ImGui::SliderFloat("Fog Height", &settings.fogHeight, -22000.0f, 22000.0f, "%.1f");
    ImGui::SliderFloat("Fog Height Falloff", &settings.fogHeightFalloff, 0.001f, 2.0f, "%.3f");
    ImGui::SliderFloat("Fog Density Multiplier", &settings.fogDensityMultiplier, 0.0f, 1.0f, "%.3f");
}