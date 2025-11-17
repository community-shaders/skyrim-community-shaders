#include "WeatherWidget.h"

#include "../EditorWindow.h"

#include <algorithm>

#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::WeatherColor, colorTimes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DirectionalColor, max, min)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DALC, specular, fresnelPower, directional)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Settings,
	currentParentBuffer,
	transDelta,

	sunGlare,
	sunDamage,

	precipitationBeginFadeIn,
	precipitationEndFadeOut,

	thunderLightningBeginFadeIn,
	thunderLightningEndFadeOut,
	thunderLightningFrequency,
	lightningColor,

	visualEffectBegin,
	visualEffectEnd,

	windDirection,
	windDirectionRange,
	windSpeed,

	weatherColors,
	dalc,
	fog,

	inheritWindSettings,
	inheritSunSettings,
	inheritPrecipitationSettings,
	inheritLightningSettings,
	inheritVisualEffectSettings,
	inheritTransDeltaSettings,
	inheritDALCSettings,
	inheritWeatherColorSettings,
	inheritFogSettings)

WeatherWidget::~WeatherWidget()
{
}

void WeatherWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		DrawMenu();

		auto editorWindow = EditorWindow::GetSingleton();
		auto& widgets = editorWindow->weatherWidgets;

		// Sets the parent widget if settings have been loaded.
		if (settings.currentParentBuffer != "None") {
			parent = GetParent();
			if (parent == nullptr)
				settings.currentParentBuffer = "None";
			strncpy(currentParentBuffer, settings.currentParentBuffer.c_str(), sizeof(settings.currentParentBuffer));
		}

		if (ImGui::BeginCombo("Parent", currentParentBuffer)) {
			// Option for "None"
			if (ImGui::Selectable("None", parent == nullptr)) {
				parent = nullptr;
				settings.currentParentBuffer = "None";
				strncpy(currentParentBuffer, settings.currentParentBuffer.c_str(), sizeof(settings.currentParentBuffer));
			}

			for (int i = 0; i < widgets.size(); i++) {
				auto& widget = widgets[i];

				// Skip self-selection
				if (widget == this)
					continue;

				// Option for each widget
				if (ImGui::Selectable(widget->GetEditorID().c_str(), parent == widget)) {
					parent = (WeatherWidget*)widget;
					settings.currentParentBuffer = widget->GetEditorID();
					strncpy(currentParentBuffer, settings.currentParentBuffer.c_str(), sizeof(settings.currentParentBuffer) - 1);
					currentParentBuffer[sizeof(currentParentBuffer) - 1] = '\0';  // Ensure null-termination
				}

				// Set default focus to the current parent
				if (parent == widget) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (parent && !parent->IsOpen()) {
			ImGui::SameLine();
			if (ImGui::Button("Open"))
				parent->SetOpen(true);
		}

		DrawSunSettings();

		DrawSeparator();

		DrawWindSettings();

		DrawSeparator();

		DrawPrecipitationSettings();

		DrawSeparator();

		DrawLightningSettings();

		DrawSeparator();

		DrawVisualEffectSettings();

		DrawSeparator();

		DrawDALCSettings();

		DrawSeparator();

		DrawWeatherColorSettings();

		DrawSeparator();

		DrawCloudSettings();

		DrawSeparator();

		DrawFogSettings();

		DrawSeparator();

		ImGui::Checkbox("Inherit From Parent##transDelta", &settings.inheritTransDeltaSettings);
		ImGui::SameLine();
		ImGui::Spacing();
		ImGui::SameLine();
		ImGui::SliderInt("Trans Delta", &settings.transDelta, -128, 127);

		if (settings.inheritTransDeltaSettings && HasParent()) {
			settings.transDelta = GetParent()->settings.transDelta;
		}

		SetWeatherValues();
	}
	ImGui::End();
}

void WeatherWidget::LoadSettings()
{
	if (!js.empty()) {
		settings = js;
		strncpy(currentParentBuffer, settings.currentParentBuffer.c_str(), sizeof(settings.currentParentBuffer));
		currentParentBuffer[sizeof(currentParentBuffer) - 1] = '\0';  // Ensure null-termination
	}
}

void WeatherWidget::SaveSettings()
{
	js = settings;
}

WeatherWidget* WeatherWidget::GetParent()
{
	auto editorWindow = EditorWindow::GetSingleton();
	auto& widgets = editorWindow->weatherWidgets;

	auto temp = std::find_if(widgets.begin(), widgets.end(), [&](Widget* w) { return w->GetEditorID() == settings.currentParentBuffer; });
	if (temp != widgets.end())
		return (WeatherWidget*)*temp;

	return nullptr;
}

bool WeatherWidget::HasParent()
{
	return settings.currentParentBuffer != "None";
}

void WeatherWidget::SetWeatherValues()
{
	weather->data.transDelta = (int8_t)settings.transDelta;

	// Sun
	weather->data.sunGlare = (int8_t)settings.sunGlare;
	weather->data.sunDamage = (int8_t)settings.sunDamage;

	// Precipitation
	weather->data.precipitationBeginFadeIn = (int8_t)settings.precipitationBeginFadeIn;
	weather->data.precipitationEndFadeOut = (int8_t)settings.precipitationEndFadeOut;

	// Lightning
	weather->data.thunderLightningBeginFadeIn = (int8_t)settings.thunderLightningBeginFadeIn;
	weather->data.thunderLightningEndFadeOut = (int8_t)settings.thunderLightningEndFadeOut;
	weather->data.thunderLightningFrequency = (int8_t)settings.thunderLightningFrequency;
	Float3ToColor(settings.lightningColor, weather->data.lightningColor);

	// Visual Effects
	weather->data.visualEffectBegin = (int8_t)settings.visualEffectBegin;
	weather->data.visualEffectEnd = (int8_t)settings.visualEffectEnd;

	// Wind
	weather->data.windSpeed = (uint8_t)settings.windSpeed;
	weather->data.windDirection = (int8_t)settings.windDirection;
	weather->data.windDirectionRange = (int8_t)settings.windDirectionRange;

	// Weather
	for (size_t i = 0; i < ColorTypes::kTotal; i++) {
		for (size_t j = 0; j < ColorTimes::kTotal; j++) {
			auto& color = weather->colorData[i][j];
			Float3ToColor(settings.weatherColors[i].colorTimes[j], color);
		}
	}

	//DALC
	for (size_t i = 0; i < ColorTimes::kTotal; i++) {
		auto& dalc = weather->directionalAmbientLightingColors[i];
		auto& settingsDalc = settings.dalc[i];
		dalc.fresnelPower = settingsDalc.fresnelPower;

		Float3ToColor(settingsDalc.specular, dalc.specular);

		Float3ToColor(settingsDalc.directional[0].max, dalc.directional.x.max);
		Float3ToColor(settingsDalc.directional[0].min, dalc.directional.x.min);

		Float3ToColor(settingsDalc.directional[1].max, dalc.directional.y.max);
		Float3ToColor(settingsDalc.directional[1].min, dalc.directional.y.min);

		Float3ToColor(settingsDalc.directional[2].max, dalc.directional.z.max);
		Float3ToColor(settingsDalc.directional[2].min, dalc.directional.z.min);
	}

	// Clouds
	for (size_t i = 0; i < TESWeather::kTotalLayers; i++) {
		auto& settingsCloud = settings.clouds[i];

		weather->cloudLayerSpeedX[i] = settingsCloud.cloudLayerSpeedX;
		weather->cloudLayerSpeedY[i] = settingsCloud.cloudLayerSpeedY;

		auto& cloudColors = weather->cloudColorData[i];
		auto& cloudAlphas = weather->cloudAlpha[i];

		for (int j = 0; j < ColorTimes::kTotal; j++) {
			cloudAlphas[j] = settingsCloud.cloudAlpha[j];
			Float3ToColor(settingsCloud.color[j], cloudColors[j]);
		}
	}

	// Fog
	weather->fogData = settings.fog;
}

void WeatherWidget::LoadWeatherValues()
{
	settings.transDelta = weather->data.transDelta;

	// Sun
	settings.sunGlare = weather->data.sunGlare;
	settings.sunDamage = weather->data.sunDamage;

	// Precipitation
	settings.precipitationBeginFadeIn = weather->data.precipitationBeginFadeIn;
	settings.precipitationEndFadeOut = weather->data.precipitationEndFadeOut;

	// Lightning
	settings.thunderLightningBeginFadeIn = weather->data.thunderLightningBeginFadeIn;
	settings.thunderLightningEndFadeOut = weather->data.thunderLightningEndFadeOut;
	settings.thunderLightningFrequency = weather->data.thunderLightningFrequency;
	ColorToFloat3(weather->data.lightningColor, settings.lightningColor);

	// Visual Effects
	settings.visualEffectBegin = weather->data.visualEffectBegin;
	settings.visualEffectEnd = weather->data.visualEffectEnd;

	// Wind
	settings.windSpeed = weather->data.windSpeed;
	settings.windDirection = weather->data.windDirection;
	settings.windDirectionRange = weather->data.windDirectionRange;

	// Weather color
	for (size_t i = 0; i < ColorTypes::kTotal; i++) {
		for (size_t j = 0; j < ColorTimes::kTotal; j++) {
			auto& color = weather->colorData[i][j];
			ColorToFloat3(color, settings.weatherColors[i].colorTimes[j]);
		}
	}

	// DALC
	for (size_t i = 0; i < ColorTimes::kTotal; i++) {
		auto& dalc = weather->directionalAmbientLightingColors[i];
		auto& settingsDalc = settings.dalc[i];
		dalc.fresnelPower = settingsDalc.fresnelPower;

		ColorToFloat3(dalc.specular, settingsDalc.specular);

		ColorToFloat3(dalc.directional.x.max, settingsDalc.directional[0].max);
		ColorToFloat3(dalc.directional.x.min, settingsDalc.directional[0].min);

		ColorToFloat3(dalc.directional.y.max, settingsDalc.directional[1].max);
		ColorToFloat3(dalc.directional.y.min, settingsDalc.directional[1].min);

		ColorToFloat3(dalc.directional.z.max, settingsDalc.directional[2].max);
		ColorToFloat3(dalc.directional.z.min, settingsDalc.directional[2].min);
	}

	// Clouds
	for (size_t i = 0; i < TESWeather::kTotalLayers; i++) {
		auto& settingsCloud = settings.clouds[i];

		settingsCloud.cloudLayerSpeedX = weather->cloudLayerSpeedX[i];
		settingsCloud.cloudLayerSpeedY = weather->cloudLayerSpeedY[i];

		auto& cloudColors = weather->cloudColorData[i];
		auto& cloudAlphas = weather->cloudAlpha[i];

		for (int j = 0; j < ColorTimes::kTotal; j++) {
			settingsCloud.cloudAlpha[j] = cloudAlphas[j];
			ColorToFloat3(cloudColors[j], settingsCloud.color[j]);
		}
	}

	// Fog
	settings.fog = weather->fogData;
}

void WeatherWidget::DrawWindSettings()
{
	if (ImGui::CollapsingHeader("Wind settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##wind", &settings.inheritWindSettings);

		if (settings.inheritWindSettings && HasParent()) {
			settings.windSpeed = GetParent()->settings.windSpeed;
			settings.windDirection = GetParent()->settings.windDirection;
			settings.windDirectionRange = GetParent()->settings.windDirectionRange;
		} else {
			settings.inheritWindSettings = false;
			ImGui::SliderInt("Wind Direction", &settings.windDirection, -128, 127);
			ImGui::SliderInt("Wind Direction Range", &settings.windDirectionRange, -128, 127);
			ImGui::SliderInt("Wind Speed", &settings.windSpeed, 0, 255);
		}
	}
}

void WeatherWidget::DrawDALCSettings()
{
	if (ImGui::CollapsingHeader("DALC settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##dalc", &settings.inheritDALCSettings);

		if (settings.inheritDALCSettings && HasParent()) {
			for (size_t i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
				settings.dalc[i] = GetParent()->settings.dalc[i];
			}
		} else {
			settings.inheritDALCSettings = false;
			for (int i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
				std::string label = ColorTimeLabel(i);

				if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
					ImGui::Spacing();
					ImGui::ColorEdit3(std::format("Specular##{}", label).c_str(), (float*)&settings.dalc[i].specular);
					ImGui::Spacing();
					ImGui::SliderFloat(std::format("Fresnel Power##{}", label).c_str(), &settings.dalc[i].fresnelPower, -127, 128);
					ImGui::Spacing();
					ImGui::ColorEdit3(std::format("DALC X Max##{}", label).c_str(), (float*)&settings.dalc[i].directional[0].max);
					ImGui::ColorEdit3(std::format("DALC X Min##{}", label).c_str(), (float*)&settings.dalc[i].directional[0].min);
					ImGui::Spacing();
					ImGui::ColorEdit3(std::format("DALC Y Max##{}", label).c_str(), (float*)&settings.dalc[i].directional[1].max);
					ImGui::ColorEdit3(std::format("DALC Y Min##{}", label).c_str(), (float*)&settings.dalc[i].directional[1].min);
					ImGui::Spacing();
					ImGui::ColorEdit3(std::format("DALC Z Max##{}", label).c_str(), (float*)&settings.dalc[i].directional[2].max);
					ImGui::ColorEdit3(std::format("DALC Z Min##{}", label).c_str(), (float*)&settings.dalc[i].directional[2].min);
					ImGui::Spacing();
				}
			}
		}
	}
}

void WeatherWidget::DrawWeatherColorSettings()
{
	if (ImGui::CollapsingHeader("Weather Color settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##weatherColor", &settings.inheritWeatherColorSettings);

		if (settings.inheritWeatherColorSettings && HasParent()) {
			for (size_t i = 0; i < ColorTypes::kTotal; i++) {
				settings.weatherColors[i] = GetParent()->settings.weatherColors[i];
			}
		} else {
			settings.inheritWeatherColorSettings = false;
			for (int i = 0; i < ColorTypes::kTotal; i++) {
				std::string colorTypeLabel = ColorTypeLabel(i);
				if (ImGui::CollapsingHeader(colorTypeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
					for (int j = 0; j < ColorTimes::kTotal; j++) {
						ImGui::Spacing();
						ImGui::ColorEdit3(std::format("{}##{}", ColorTimeLabel(j), colorTypeLabel).c_str(), (float*)&settings.weatherColors[i].colorTimes[j]);
						ImGui::Spacing();
					}
				}
			}
		}
	}
}

void WeatherWidget::DrawCloudSettings()
{
	if (ImGui::CollapsingHeader("Cloud settings", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##cloud", &settings.inheritDALCSettings);

		if (settings.inheritCloudSettings && HasParent()) {
			for (size_t i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
				settings.dalc[i] = GetParent()->settings.dalc[i];
			}
		} else {
			settings.inheritCloudSettings = false;
			for (int i = 0; i < TESWeather::kTotalLayers; i++) {
				std::string layer = std::format("Layer {}", i);

				if (ImGui::CollapsingHeader(layer.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
					ImGui::Spacing();
					ImGui::SliderInt(std::format("Cloud Layer Speed Y##{}", layer).c_str(), (int*)&settings.clouds[i].cloudLayerSpeedY, -127, 128);
					ImGui::Spacing();
					ImGui::SliderInt(std::format("Cloud Layer Speed X##{}", layer).c_str(), (int*)&settings.clouds[i].cloudLayerSpeedX, -127, 128);
					for (int j = 0; j < ColorTimes::kTotal; j++) {
						std::string colorTime = ColorTimeLabel(j).c_str();
						if (ImGui::CollapsingHeader(colorTime.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
							ImGui::Spacing();
							ImGui::ColorEdit3(std::format("Cloud Color##{}{}", colorTime, i).c_str(), (float*)&settings.clouds[i].color[j]);
							ImGui::Spacing();
							ImGui::SliderFloat(std::format("Cloud Alpha##{}{}", colorTime, i).c_str(), &settings.clouds[i].cloudAlpha[j], -127, 128);
							ImGui::Spacing();
						}
					}
				}
			}
		}
	}
}

void WeatherWidget::DrawLightningSettings()
{
	if (ImGui::CollapsingHeader("Lightning settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##lightning", &settings.inheritLightningSettings);

		if (settings.inheritLightningSettings && HasParent()) {
			settings.thunderLightningBeginFadeIn = GetParent()->settings.thunderLightningBeginFadeIn;
			settings.thunderLightningEndFadeOut = GetParent()->settings.thunderLightningEndFadeOut;
			settings.thunderLightningFrequency = GetParent()->settings.thunderLightningFrequency;

			settings.lightningColor = GetParent()->settings.lightningColor;
		} else {
			settings.inheritLightningSettings = false;
			ImGui::SliderInt("Thunder Lightning Begin Fade In", &settings.thunderLightningBeginFadeIn, -128, 127);
			ImGui::SliderInt("Thunder Lightning End Fade Out", &settings.thunderLightningEndFadeOut, -128, 127);
			ImGui::SliderInt("Thunder Lightning Frequency", &settings.thunderLightningFrequency, -128, 127);
			ImGui::ColorEdit3("Lightning Color", (float*)&settings.lightningColor);
		}
	}
}

void WeatherWidget::DrawFogSettings()
{
	if (ImGui::CollapsingHeader("Fog settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##wind", &settings.inheritFogSettings);

		if (settings.inheritFogSettings && HasParent()) {
			settings.fog = GetParent()->settings.fog;
		} else {
			settings.inheritFogSettings = false;
			ImGui::SliderFloat("Day Near", &settings.fog.dayNear, 0, 500000);
			ImGui::SliderFloat("Day Far", &settings.fog.dayFar, 0, 500000);
			ImGui::SliderFloat("Night Near", &settings.fog.nightNear, 0, 500000);
			ImGui::SliderFloat("Night far", &settings.fog.nightFar, 0, 500000);
			ImGui::SliderFloat("Day Power", &settings.fog.dayPower, 0, 500000);
			ImGui::SliderFloat("Night Power", &settings.fog.nightPower, 0, 500000);
			ImGui::SliderFloat("Day Max", &settings.fog.dayMax, 0, 500000);
			ImGui::SliderFloat("Night Max", &settings.fog.nightMax, 0, 500000);
		}
	}
}

void WeatherWidget::DrawVisualEffectSettings()
{
	if (ImGui::CollapsingHeader("Visual Effect settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##visualEffect", &settings.inheritVisualEffectSettings);

		if (settings.inheritVisualEffectSettings && HasParent()) {
			settings.visualEffectBegin = GetParent()->settings.visualEffectBegin;
			settings.visualEffectBegin = GetParent()->settings.visualEffectEnd;
		} else {
			settings.inheritVisualEffectSettings = false;
			ImGui::SliderInt("Visual Effect Begin", &settings.visualEffectBegin, -128, 127);
			ImGui::SliderInt("Visual Effect End", &settings.visualEffectEnd, -128, 127);
		}
	}
}

void WeatherWidget::DrawSunSettings()
{
	if (ImGui::CollapsingHeader("Sun settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##sun", &settings.inheritSunSettings);

		if (settings.inheritSunSettings && HasParent()) {
			settings.sunGlare = GetParent()->settings.sunGlare;
			settings.sunDamage = GetParent()->settings.sunDamage;
		} else {
			settings.inheritSunSettings = false;
			ImGui::SliderInt("Sun Glare", &settings.sunGlare, -128, 127);
			ImGui::SliderInt("Sun Damage", &settings.sunDamage, -128, 127);
		}
	}
}

void WeatherWidget::DrawPrecipitationSettings()
{
	if (ImGui::CollapsingHeader("Precipitation settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Checkbox("Inherit From Parent##precipitation", &settings.inheritPrecipitationSettings);

		if (settings.inheritPrecipitationSettings && HasParent()) {
			settings.precipitationBeginFadeIn = GetParent()->settings.precipitationBeginFadeIn;
			settings.precipitationEndFadeOut = GetParent()->settings.precipitationEndFadeOut;
		} else {
			settings.inheritPrecipitationSettings = false;
			ImGui::SliderInt("Precipitation Begin Fade In", &settings.precipitationBeginFadeIn, -128, 127);
			ImGui::SliderInt("Precipitation End Fade Out", &settings.precipitationEndFadeOut, -128, 127);
		}
	}
}

void WeatherWidget::DrawSeparator()
{
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}
