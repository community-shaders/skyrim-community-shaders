#include "ExponentialHeightFog.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ExponentialHeightFog::Settings,
    enabled,
    useDynamicCubemaps,
    startDistance,
    fogHeight,
    fogHeightFalloff,
    fogDensity,
    directionalInscatteringMultiplier,
    directionalInscatteringExponent,
    inscatteringTint,
    cubemapMipLevel)

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
    ImGui::SliderFloat("Start Distance", &settings.startDistance, 0.0f, 100000.0f, "%.1f");
    ImGui::SliderFloat("Fog Height", &settings.fogHeight, -22000.0f, 22000.0f, "%.1f");
    ImGui::SliderFloat("Fog Height Falloff", &settings.fogHeightFalloff, 0.001f, 2.0f, "%.3f");
    ImGui::SliderFloat("Fog Density", &settings.fogDensity, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Directional Light Inscattering Multiplier", &settings.directionalInscatteringMultiplier, 0.0f, 10.0f, "%.2f");
    ImGui::SliderFloat("Directional Light Inscattering Exponent", &settings.directionalInscatteringExponent, 1.0f, 128.0f, "%.2f");
    ImGui::Checkbox("Use Dynamic Cubemaps for Inscattering", (bool*)&settings.useDynamicCubemaps);
    ImGui::ColorEdit3("Inscattering Cubemap Tint", (float*)&settings.inscatteringTint);
    ImGui::SliderFloat("Inscattering Cubemap Tint Alpha", &settings.inscatteringTint.w, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Cubemap Mip Level", &settings.cubemapMipLevel, 1.0f, 7.0f, "%.1f");
}