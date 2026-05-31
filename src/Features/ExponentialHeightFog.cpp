#include "ExponentialHeightFog.h"

#include "WeatherVariableRegistry.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExponentialHeightFog::Settings,
	enabled,
	useDynamicCubemaps,
	startDistance,
	fogHeight,
	fogHeightFalloff,
	fogDensity,
	directionalInscatteringMultiplier,
	directionalInscatteringAnisotropy,
	inscatteringTint,
	cubemapMipLevel,
	sunlightAttenuationAmount,
	respectVanillaFogFade,
	disableVanillaFog,
	fogInscatteringColor,
	originalFogColorAmount)

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
	Util::WeatherUI::SliderFloat("Start Distance", this, "startDistance", &settings.startDistance, 0.0f, 100000.0f, "%.1f");
	Util::WeatherUI::SliderFloat("Fog Height", this, "fogHeight", &settings.fogHeight, -22000.0f, 22000.0f, "%.1f");
	Util::WeatherUI::SliderFloat("Fog Height Falloff", this, "fogHeightFalloff", &settings.fogHeightFalloff, 0.001f, 2.0f, "%.3f");
	Util::WeatherUI::ColorEdit4("Fog Inscattering Color", this, "fogInscatteringColor", (float*)&settings.fogInscatteringColor);
	Util::WeatherUI::SliderFloat("Original Fog Color Amount", this, "originalFogColorAmount", &settings.originalFogColorAmount, 0.0f, 1.0f, "%.2f");
	Util::WeatherUI::SliderFloat("Fog Density", this, "fogDensity", &settings.fogDensity, 0.0f, 1.0f, "%.3f");
	Util::WeatherUI::SliderFloat("Directional Light Inscattering Multiplier", this, "directionalInscatteringMultiplier", &settings.directionalInscatteringMultiplier, 0.0f, 10.0f, "%.2f");
	Util::WeatherUI::SliderFloat("Sunlight Attenuation Amount", this, "sunlightAttenuationAmount", &settings.sunlightAttenuationAmount, 0.0f, 1.0f, "%.2f");
	Util::WeatherUI::SliderFloat("Directional Light Inscattering Anisotropy", this, "directionalInscatteringAnisotropy", &settings.directionalInscatteringAnisotropy, -0.99f, 0.99f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Controls the asymmetry of inscattering via the Henyey-Greenstein phase function.\n"
			"Positive values produce forward scattering (glow around sun).\n"
			"Zero is isotropic. Negative values produce back scattering.");
	}
	ImGui::Checkbox("Disable Vanilla Fog", (bool*)&settings.disableVanillaFog);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Disables the vanilla fog entirely. Only exponential height fog will be applied.");
	}
	Util::WeatherUI::Checkbox("Apply Vanilla Fade", this, "respectVanillaFogFade", (bool*)&settings.respectVanillaFogFade);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Applies vanilla fade brightness to exponential height fog.");
	}
	ImGui::Checkbox("Use Dynamic Cubemaps for Inscattering", (bool*)&settings.useDynamicCubemaps);
	Util::WeatherUI::ColorEdit4("Inscattering Cubemap Tint", this, "inscatteringTint", (float*)&settings.inscatteringTint);
	ImGui::SliderFloat("Cubemap Mip Level", &settings.cubemapMipLevel, 1.0f, 7.0f, "%.1f");
}

void ExponentialHeightFog::RegisterWeatherVariables()
{
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->GetOrCreateFeatureRegistry(GetShortName());
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Start Distance",
		"startDistance",
		"Start distance of the fog, from the camera",
		&settings.startDistance,
		0.0f,
		0.0f, 100000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Height",
		"fogHeight",
		"Base height of the fog effect",
		&settings.fogHeight,
		0.0f,
		-22000.0f, 22000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Height Falloff",
		"fogHeightFalloff",
		"Height density factor controls how the density increases as height decreases",
		&settings.fogHeightFalloff,
		0.2f,
		0.001f, 2.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Fog Inscattering Color",
		"fogInscatteringColor",
		"Color added to the fog inscattering contribution",
		&settings.fogInscatteringColor,
		float4{ 0.0f, 0.0f, 0.0f, 1.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Original Fog Color Amount",
		"originalFogColorAmount",
		"Amount of the original fog color added to fog inscattering",
		&settings.originalFogColorAmount,
		1.0f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Density",
		"fogDensity",
		"Overall density of the fog",
		&settings.fogDensity,
		0.02f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Directional Inscattering Multiplier",
		"directionalInscatteringMultiplier",
		"Multiplier for directional light inscattering",
		&settings.directionalInscatteringMultiplier,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Sunlight Attenuation Amount",
		"sunlightAttenuationAmount",
		"Amount of fog attenuation applied to direct sunlight",
		&settings.sunlightAttenuationAmount,
		1.0f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Directional Inscattering Anisotropy",
		"directionalInscatteringAnisotropy",
		"Henyey-Greenstein asymmetry parameter. Positive = forward scattering, 0 = isotropic, negative = back scattering.",
		&settings.directionalInscatteringAnisotropy,
		0.7f,
		-0.99f, 0.99f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Inscattering Cubemap Tint",
		"inscatteringTint",
		"RGB tint for the inscattering cubemap with alpha for intensity",
		&settings.inscatteringTint,
		float4{ 1.0f, 1.0f, 1.0f, 1.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"respectVanillaFogFade",
		"Apply Vanilla Fade",
		"Apply vanilla fade brightness to exponential height fog",
		(bool*)&settings.respectVanillaFogFade,
		false,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;
		}));

	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"disableVanillaFog",
		"Disable Vanilla Fog",
		"Disables vanilla fog entirely, only exponential height fog is applied",
		(bool*)&settings.disableVanillaFog,
		false,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;
		}));
}
