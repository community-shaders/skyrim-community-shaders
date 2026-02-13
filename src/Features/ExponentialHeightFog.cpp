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
	Util::WeatherUI::SliderFloat("Start Distance", this, "startDistance", &settings.startDistance, 0.0f, 100000.0f, "%.1f");
	Util::WeatherUI::SliderFloat("Fog Height", this, "fogHeight", &settings.fogHeight, -22000.0f, 22000.0f, "%.1f");
	Util::WeatherUI::SliderFloat("Fog Height Falloff", this, "fogHeightFalloff", &settings.fogHeightFalloff, 0.001f, 2.0f, "%.3f");
	Util::WeatherUI::SliderFloat("Fog Density", this, "fogDensity", &settings.fogDensity, 0.0f, 1.0f, "%.3f");
	Util::WeatherUI::SliderFloat("Directional Light Inscattering Multiplier", this, "directionalInscatteringMultiplier", &settings.directionalInscatteringMultiplier, 0.0f, 10.0f, "%.2f");
	Util::WeatherUI::SliderFloat("Directional Light Inscattering Exponent", this, "directionalInscatteringExponent", &settings.directionalInscatteringExponent, 1.0f, 128.0f, "%.2f");
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
		"Directional Inscattering Exponent",
		"directionalInscatteringExponent",
		"Controls the size of the directional inscattering cone",
		&settings.directionalInscatteringExponent,
		4.0f,
		1.0f, 128.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Inscattering Cubemap Tint",
		"inscatteringTint",
		"RGB tint for the inscattering cubemap with alpha for intensity",
		&settings.inscatteringTint,
		float4{ 1.0f, 1.0f, 1.0f, 1.0f }));
}