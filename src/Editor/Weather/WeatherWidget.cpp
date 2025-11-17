#include "WeatherWidget.h"

#include "../EditorWindow.h"

#include <algorithm>

#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Atmosphere, colorTimes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DirectionalColor, max, min)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DALC, specular, fresnelPower, directional)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Settings,
	parent,
	inheritance,
	weatherProperties,
	weatherColors,
	fogProperties,

	weatherColors,
	dalc)

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
		if (settings.parent != "None") {
			parent = GetParent();
			if (parent == nullptr)
				settings.parent = "None";
		}

		if (ImGui::BeginCombo("Parent", settings.parent.c_str())) {
			// Option for "None"
			if (ImGui::Selectable("None", parent == nullptr)) {
				parent = nullptr;
				settings.parent = "None";
			}

			for (int i = 0; i < widgets.size(); i++) {
				auto& widget = widgets[i];

				// Skip self-selection
				if (widget == this)
					continue;

				// Option for each widget
				if (ImGui::Selectable(widget->GetEditorID().c_str(), parent == widget)) {
					parent = (WeatherWidget*)widget;
					settings.parent = widget->GetEditorID();
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

		DrawProperties("Sun", { { "Sun Glare", INT8_SLIDER }, { "Sun Damage", INT8_SLIDER } });
		DrawProperties("Wind", { { "Wind Speed", UINT8_SLIDER }, { "Wind Direction", INT8_SLIDER }, { "Wind Direction Range", INT8_SLIDER } });
		DrawProperties("Precipitation", { { "Precipitation Begin Fade In", INT8_SLIDER }, { "Precipitation Begin Fade Out", INT8_SLIDER } });

		DrawProperties("Lightning", { { "Thunder Lightning Begin Fade In", INT8_SLIDER }, { "Thunder Lightning End Fade Out", INT8_SLIDER },
										{ "Thunder Lightning Frequency", INT8_SLIDER }, { "Lightning Color", COLOR3_PICKER } });

		DrawProperties("Visual Effects", { { "Visual Effect Begin", INT8_SLIDER }, { "Visual Effect End", INT8_SLIDER } });

		DrawDALCSettings();

		DrawWeatherColorSettings();

		DrawCloudSettings();

		DrawProperties("Fog", { { "Day Near", FLOAT_SLIDER }, { "Day Far", FLOAT_SLIDER }, { "Day Power", FLOAT_SLIDER }, { "Day Max", FLOAT_SLIDER },
								  { "Night Near", FLOAT_SLIDER }, { "Night Far", FLOAT_SLIDER }, { "Night Power", FLOAT_SLIDER }, { "Night Max", FLOAT_SLIDER } });

		DrawProperties("Weather Transition", { { "Trans Delta", INT8_SLIDER } });
	}
	ImGui::End();
}

void WeatherWidget::LoadSettings()
{
	if (!js.empty()) {
		settings = js;
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

	auto temp = std::find_if(widgets.begin(), widgets.end(), [&](Widget* w) { return w->GetEditorID() == settings.parent; });
	if (temp != widgets.end())
		return (WeatherWidget*)*temp;

	return nullptr;
}

bool WeatherWidget::HasParent() const
{
	return settings.parent != "None";
}

void WeatherWidget::SetWeatherValues()
{
	std::map<std::string, int>& weatherProps = settings.weatherProperties;
	std::map<std::string, float3>& weatherColors = settings.weatherColors;
	std::map<std::string, float>& fogProperties = settings.fogProperties;

	auto& data = weather->data;
	auto& colorData = weather->colorData;
	auto& fogData = weather->fogData;

	weather->data.transDelta = (int8_t)weatherProps["Trans Delta"];

	// Sun
	data.sunGlare = (int8_t)weatherProps["Sun Glare"];
	data.sunDamage = (int8_t)weatherProps["Sun Damage"];

	// Precipitation
	data.precipitationBeginFadeIn = (int8_t)weatherProps["Precipitation Begin Fade In"];
	data.precipitationEndFadeOut = (int8_t)weatherProps["Precipitation End Fade Out"];

	// Lightning
	data.thunderLightningBeginFadeIn = (int8_t)weatherProps["Thunder Lightning Begin Fade In"];
	data.thunderLightningEndFadeOut = (int8_t)weatherProps["Thunder Lightning End Fade Out"];
	data.thunderLightningFrequency = (int8_t)weatherProps["Thunder Lightning Frequency"];
	Float3ToColor(weatherColors["Lightning Color"], weather->data.lightningColor);

	// Visual Effects
	data.visualEffectBegin = (int8_t)weatherProps["Visual Effect Begin"];
	data.visualEffectEnd = (int8_t)weatherProps["Visual Effect End"];

	// Wind
	data.windSpeed = (uint8_t)weatherProps["Wind Speed"];
	data.windDirection = (int8_t)weatherProps["Wind Direction"];
	data.windDirectionRange = (int8_t)weatherProps["Wind Direction Range"];

	// Fog
	fogData.dayNear = fogProperties["Day Near"];
	fogData.dayFar = fogProperties["Day Far"];
	fogData.dayPower = fogProperties["Day Power"];
	fogData.dayMax = fogProperties["Day Max"];
	fogData.nightNear = fogProperties["Night Near"];
	fogData.nightFar = fogProperties["Night Far"];
	fogData.nightPower = fogProperties["Night Power"];
	fogData.nightMax = fogProperties["Night Max"];

	// Atmosphere colors
	for (size_t i = 0; i < ColorTypes::kTotal; i++) {
		for (size_t j = 0; j < ColorTimes::kTotal; j++) {
			auto& color = colorData[i][j];
			Float3ToColor(settings.atmosphereColors[i].colorTimes[j], color);
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

		weather->cloudLayerSpeedX[i] = (int8_t)settingsCloud.cloudLayerSpeedX;
		weather->cloudLayerSpeedY[i] = (int8_t)settingsCloud.cloudLayerSpeedY;

		auto& cloudColors = weather->cloudColorData[i];
		auto& cloudAlphas = weather->cloudAlpha[i];

		for (int j = 0; j < ColorTimes::kTotal; j++) {
			cloudAlphas[j] = settingsCloud.cloudAlpha[j];
			Float3ToColor(settingsCloud.color[j], cloudColors[j]);
		}
	}
}

void WeatherWidget::LoadWeatherValues()
{
	std::map<std::string, int>& weatherProps = settings.weatherProperties;
	std::map<std::string, float3>& weatherColors = settings.weatherColors;
	std::map<std::string, float>& fogProperties = settings.fogProperties;

	const auto& data = weather->data;
	const auto& colorData = weather->colorData;
	const auto& fogData = weather->fogData;

	weatherProps["Trans Delta"] = data.transDelta;

	// Sun
	weatherProps["Sun Glare"] = data.sunGlare;
	weatherProps["Sun Damage"] = data.sunDamage;

	// Precipitation
	weatherProps["Precipitation Begin Fade In"] = data.precipitationBeginFadeIn;
	weatherProps["Precipitation End Fade Out"] = data.precipitationEndFadeOut;

	// Lightning
	weatherProps["Thunder Lightning Begin Fade In"] = data.thunderLightningBeginFadeIn;
	weatherProps["Thunder Lightning End Fade Out"] = data.thunderLightningEndFadeOut;
	weatherProps["Thunder Lightning Frequency"] = data.thunderLightningFrequency;
	ColorToFloat3(data.lightningColor, weatherColors["Lightning Color"]);

	// Visual Effects
	weatherProps["Visual Effect Begin"] = data.visualEffectBegin;
	weatherProps["Visual Effect End"] = data.visualEffectEnd;

	// Wind
	weatherProps["Wind Speed"] = data.windSpeed;
	weatherProps["Wind Direction"] = data.windDirection;
	weatherProps["Wind Direction Range"] = data.windDirectionRange;

	// Fog
	fogProperties["Day Near"] = fogData.dayNear;
	fogProperties["Day Far"] = fogData.dayFar;
	fogProperties["Night Near"] = fogData.nightNear;
	fogProperties["Night Far"] = fogData.nightFar;
	fogProperties["Day Power"] = fogData.dayPower;
	fogProperties["Night Power"] = fogData.nightPower;
	fogProperties["Day Max"] = fogData.dayMax;
	fogProperties["Night Max"] = fogData.nightMax;

	// Atmosphere color
	for (size_t i = 0; i < ColorTypes::kTotal; i++) {
		for (size_t j = 0; j < ColorTimes::kTotal; j++) {
			auto& color = colorData[i][j];
			ColorToFloat3(color, settings.atmosphereColors[i].colorTimes[j]);
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
}

void WeatherWidget::DrawDALCSettings()
{
	if (ImGui::CollapsingHeader("DALC settings", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		bool& doesInherit = settings.inheritance["DALC"];
		ImGui::Checkbox("Inherit From Parent##dalc", &doesInherit);

		if (doesInherit && HasParent()) {
			for (size_t i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
				settings.dalc[i] = GetParent()->settings.dalc[i];
			}
		} else {
			doesInherit = false;
			for (int i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
				std::string label = ColorTimeLabel(i);

				if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
					ImGui::Spacing();
					DrawColorEdit(std::format("Specular##{}", label), settings.dalc[i].specular);
					ImGui::Spacing();
					DrawSliderFloat(std::format("Fresnel Power##{}", label), settings.dalc[i].fresnelPower);
					ImGui::Spacing();

					for (int j = 0; j < 3; j++) {
						DrawColorEdit(std::format("DALC X Max##{}", label), settings.dalc[i].directional[j].max);
						DrawColorEdit(std::format("DALC X Min##{}", label), settings.dalc[i].directional[j].min);
						ImGui::Spacing();
					}
				}
			}
		}
	}
}

void WeatherWidget::DrawWeatherColorSettings()
{
	if (ImGui::CollapsingHeader("Atmosphere Colors", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		bool& doesInherit = settings.inheritance["Atmosphere Colors"];
		ImGui::Checkbox("Inherit From Parent##atmosphereColors", &doesInherit);

		if (&doesInherit && HasParent()) {
			for (size_t i = 0; i < ColorTypes::kTotal; i++) {
				settings.atmosphereColors[i] = GetParent()->settings.atmosphereColors[i];
			}
		} else {
			doesInherit = false;

			for (int i = 0; i < ColorTypes::kTotal; i++) {
				std::string colorTypeLabel = ColorTypeLabel(i);

				if (ImGui::CollapsingHeader(colorTypeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
					for (int j = 0; j < ColorTimes::kTotal; j++) {
						DrawColorEdit(std::format("{}##{}", ColorTimeLabel(j), colorTypeLabel), settings.atmosphereColors[i].colorTimes[j]);
						ImGui::Spacing();
					}
				}
			}
		}
	}
}

void WeatherWidget::DrawCloudSettings()
{
	if (ImGui::CollapsingHeader("Clouds Properties", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		bool& doesInherit = settings.inheritance["Clouds"];
		ImGui::Checkbox("Inherit From Parent##cloud", &doesInherit);

		if (doesInherit && HasParent()) {
			for (size_t i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
				settings.dalc[i] = GetParent()->settings.dalc[i];
			}
		} else {
			doesInherit = false;
			for (int i = 0; i < TESWeather::kTotalLayers; i++) {
				std::string layer = std::format("Layer {}", i);

				if (ImGui::CollapsingHeader(layer.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
					DrawSliderInt8(std::format("Cloud Layer Speed Y##{}", layer), settings.clouds[i].cloudLayerSpeedY);
					ImGui::Spacing();
					DrawSliderInt8(std::format("Cloud Layer Speed X##{}", layer), settings.clouds[i].cloudLayerSpeedX);

					for (int j = 0; j < ColorTimes::kTotal; j++) {
						std::string colorTime = ColorTimeLabel(j).c_str();

						if (ImGui::CollapsingHeader(colorTime.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
							DrawColorEdit(std::format("Cloud Color##{}{}", colorTime, i), settings.clouds[i].color[j]);
							ImGui::Spacing();
							DrawSliderFloat(std::format("Cloud Alpha##{}{}", colorTime, i), settings.clouds[i].cloudAlpha[j]);
							ImGui::Spacing();
						}
					}
				}
			}
		}
	}
}

void WeatherWidget::DrawProperties(std::string category, std::map<std::string, int> properties)
{
	if (ImGui::CollapsingHeader(std::format("{} Properties", category).c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		bool& doesInherit = settings.inheritance[category];
		ImGui::Checkbox(std::format("Inherit From Parent##{}", category).c_str(), &doesInherit);

		if (doesInherit && HasParent()) {
			for (auto& p : properties) {
				InheritFromParent(p.first);
			}
		} else {
			doesInherit = false;

			for (auto& p : properties) {
				switch (p.second) {
				case 0:
					DrawSliderInt8(p.first, settings.weatherProperties[p.first]);
					break;
				case 1:
					DrawColorEdit(p.first, settings.weatherColors[p.first]);
					break;
				case 2:
					DrawSliderUint8(p.first, settings.weatherProperties[p.first]);
				case 3:
					DrawSliderFloat(p.first, settings.fogProperties[p.first]);
				default:
					break;
				}
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

void WeatherWidget::InheritFromParent(const std::string& property)
{
	if (settings.weatherProperties.find(property) != settings.weatherProperties.end()) {
		settings.weatherProperties[property] = GetParent()->settings.weatherProperties[property];
	} else if (settings.weatherColors.find(property) != settings.weatherColors.end()) {
		settings.weatherColors[property] = GetParent()->settings.weatherColors[property];
	}
}