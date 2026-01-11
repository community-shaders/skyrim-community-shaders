// Example Feature: Demonstrates the Weather Variable Registration Pattern
//
// This is a minimal example showing how features can support per-weather settings.
// Simply register your variables once during RegisterWeatherVariables() and the weather system handles the rest.

#include "ExampleWeatherFeature.h"
#include "WeatherVariableRegistry.h"

void ExampleWeatherFeature::RegisterWeatherVariables()
{
	// Get or create the registry for this feature
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
	                     ->GetOrCreateFeatureRegistry(GetShortName());

	// Register a simple float variable
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Intensity",                          // JSON key name
		"Effect Intensity",                   // Display name for UI
		"Controls how strong the effect is",  // Tooltip/description
		&settings.intensity,                  // Pointer to the actual variable
		1.0f,                                 // Default value
		0.0f, 2.0f                            // Min/max range (optional)
		));

	// Register a float3 color variable
	registry->RegisterVariable(std::make_shared<WeatherVariables::Float3Variable>(
		"Color",
		"Effect Color",
		"RGB color values for the effect",
		&settings.color,
		float3{ 1.0f, 0.5f, 0.2f }  // Default orange color
		));

	// Register a float4 variable (could be RGBA color or other 4-component data)
	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"TintColor",
		"Tint Color",
		"RGBA tint color with alpha",
		&settings.tintColor,
		float4{ 1.0f, 1.0f, 1.0f, 1.0f }  // Default white with full opacity
		));

	// Register a custom type with manual lerp function
	// This example shows a bool that transitions at the halfway point
	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"Enabled",
		"Enable Effect",
		"Toggle the effect on/off",
		&settings.enabled,
		true,  // Default enabled
		[](const bool& from, const bool& to, float factor) {
			// Custom lerp: switch to target value when more than halfway through transition
			return factor > 0.5f ? to : from;
		}));

	// That's it! No need to override:
	// - SupportsWeather() - automatically detected via HasWeatherSupport()
	// - UpdateSettingsFromWeathers() - handled by registry
	// - SaveSettings() / LoadSettings() for weather data - automatic
}

void ExampleWeatherFeature::DrawSettings()
{
	ImGui::TextWrapped("This is an example feature demonstrating the weather variable registration system.");
	ImGui::Spacing();

	// Draw regular feature settings UI
	if (ImGui::SliderFloat("Intensity", &settings.intensity, 0.0f, 2.0f)) {
		// Settings changed - if you want to save immediately, call:
		// State::GetSingleton()->Save();
	}

	if (ImGui::ColorEdit3("Color", &settings.color.x)) {
		// Color changed
	}

	if (ImGui::ColorEdit4("Tint Color", &settings.tintColor.x)) {
		// Tint changed
	}

	if (ImGui::Checkbox("Enabled", &settings.enabled)) {
		// Enabled state changed
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::TextWrapped(
		"Note: When weather-specific settings exist for the current weather, "
		"they will automatically override these base settings. The weather system "
		"handles all interpolation during weather transitions.");
}

void ExampleWeatherFeature::LoadSettings(json& o_json)
{
	// Load base settings (non-weather-specific)
	if (o_json.contains("Intensity"))
		settings.intensity = o_json["Intensity"];
	if (o_json.contains("Color"))
		settings.color = o_json["Color"];
	if (o_json.contains("TintColor"))
		settings.tintColor = o_json["TintColor"];
	if (o_json.contains("Enabled"))
		settings.enabled = o_json["Enabled"];
}

void ExampleWeatherFeature::SaveSettings(json& o_json)
{
	// Save base settings (non-weather-specific)
	o_json["Intensity"] = settings.intensity;
	o_json["Color"] = settings.color;
	o_json["TintColor"] = settings.tintColor;
	o_json["Enabled"] = settings.enabled;
}
