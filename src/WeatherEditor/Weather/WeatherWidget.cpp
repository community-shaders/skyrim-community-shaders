#include "WeatherWidget.h"

#include "../EditorWindow.h"
#include "State.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Atmosphere, colorTimes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DirectionalColor, max, min)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DALC, specular, fresnelPower, directional)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Cloud, cloudLayerSpeedY, cloudLayerSpeedX, color, cloudAlpha)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::ImageSpaceSettings,
	hdrEyeAdaptSpeed,
	hdrBloomBlurRadius,
	hdrBloomThreshold,
	hdrBloomScale,
	hdrSunlightScale,
	hdrSkyScale,
	cinematicSaturation,
	cinematicBrightness,
	cinematicContrast,
	tintColor,
	tintAmount,
	dofStrength,
	dofDistance,
	dofRange)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Settings,
	parent,
	inheritance,
	weatherProperties,
	weatherColors,
	fogProperties,
	atmosphereColors,
	dalc,
	clouds,
	imageSpaces,
	featureSettings)

WeatherWidget::~WeatherWidget()
{
}

void WeatherWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {

		// Draw header with search and all buttons
		DrawWidgetHeader("##WeatherSearch", false, true, true, weather);
		
		// Update search results when search buffer changes
		if (searchActive) {
			UpdateSearchResults();
		}
		
		// Show search results dropdown
		if (searchBuffer[0] != '\0' && !searchResults.empty()) {
			// Find the search input position for dropdown placement
			ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y));
			ImGui::SetNextWindowSize(ImVec2(200.0f * 1.5f, 0));
			ImGui::SetNextWindowFocus();
			if (ImGui::Begin("##SearchDropdown", nullptr, 
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | 
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
				
				for (size_t i = 0; i < std::min(size_t(5), searchResults.size()); ++i) {
					const auto& result = searchResults[i];
					std::string label = std::format("{} ({})", result.displayName, result.tabName);
					
					if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
						NavigateToSetting(result);
						searchBuffer[0] = '\0';
						searchResults.clear();
					}
				}
				
				if (searchResults.size() > 5) {
					ImGui::Separator();
					ImGui::TextDisabled("... %zu more results", searchResults.size() - 5);
				}
				
				// Close dropdown if clicking outside or pressing Escape
				if (!ImGui::IsWindowFocused() || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
					searchBuffer[0] = '\0';
					searchResults.clear();
				}
			}
			ImGui::End();
		}

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

		// Tab bar for organizing settings
		if (ImGui::BeginTabBar("WeatherSettingsTabs", ImGuiTabBarFlags_None)) {
			// Use activeTabOverride to auto-navigate to specific tab
			ImGuiTabItemFlags basicFlags = (activeTabOverride == "Basic") ? ImGuiTabItemFlags_SetSelected : 0;
			ImGuiTabItemFlags dalcFlags = (activeTabOverride == "Lighting (DALC)") ? ImGuiTabItemFlags_SetSelected : 0;
			ImGuiTabItemFlags atmosphereFlags = (activeTabOverride == "Atmosphere Colors") ? ImGuiTabItemFlags_SetSelected : 0;
			ImGuiTabItemFlags cloudsFlags = (activeTabOverride == "Clouds") ? ImGuiTabItemFlags_SetSelected : 0;
			ImGuiTabItemFlags fogFlags = (activeTabOverride == "Fog") ? ImGuiTabItemFlags_SetSelected : 0;
			ImGuiTabItemFlags featuresFlags = (activeTabOverride == "Features") ? ImGuiTabItemFlags_SetSelected : 0;
			ImGuiTabItemFlags imageSpaceFlags = (activeTabOverride == "ImageSpace") ? ImGuiTabItemFlags_SetSelected : 0;
			if (!activeTabOverride.empty()) {
				activeTabOverride = ""; // Clear after use
			}
			
			if (ImGui::BeginTabItem("Basic", nullptr, basicFlags)) {
				DrawProperties("Sun", { { "Sun Glare", INT8_SLIDER }, { "Sun Damage", INT8_SLIDER } });
				DrawProperties("Wind", { { "Wind Speed", UINT8_SLIDER }, { "Wind Direction", INT8_SLIDER }, { "Wind Direction Range", INT8_SLIDER } });
				DrawProperties("Precipitation", { { "Precipitation Begin Fade In", INT8_SLIDER }, { "Precipitation Begin Fade Out", INT8_SLIDER } });
				DrawProperties("Lightning", { { "Thunder Lightning Begin Fade In", INT8_SLIDER }, { "Thunder Lightning End Fade Out", INT8_SLIDER },
												{ "Thunder Lightning Frequency", INT8_SLIDER }, { "Lightning Color", COLOR3_PICKER } });
				DrawProperties("Visual Effects", { { "Visual Effect Begin", INT8_SLIDER }, { "Visual Effect End", INT8_SLIDER } });
				DrawProperties("Weather Transition", { { "Trans Delta", INT8_SLIDER } });
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Lighting (DALC)", nullptr, dalcFlags)) {
				DrawDALCSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Atmosphere Colors", nullptr, atmosphereFlags)) {
				DrawWeatherColorSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Clouds", nullptr, cloudsFlags)) {
				DrawCloudSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Fog", nullptr, fogFlags)) {
				DrawFogSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Features", nullptr, featuresFlags)) {
				DrawFeatureSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("ImageSpace", nullptr, imageSpaceFlags)) {
				DrawImageSpaceSettings();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void WeatherWidget::LoadSettings()
{
	bool hadErrors = false;
	
	if (!js.empty()) {
		try {
			// Attempt to load settings from JSON
			settings = js;
			
			// Validate that critical fields were loaded correctly
			if (js.contains("weatherProperties") && settings.weatherProperties.empty() && !js["weatherProperties"].empty()) {
				logger::warn("Weather {}: weatherProperties loaded but appears empty, reverting to vanilla values", GetEditorID());
				hadErrors = true;
			}
			if (js.contains("weatherColors") && settings.weatherColors.empty() && !js["weatherColors"].empty()) {
				logger::warn("Weather {}: weatherColors loaded but appears empty, reverting to vanilla values", GetEditorID());
				hadErrors = true;
			}
			if (js.contains("fogProperties") && settings.fogProperties.empty() && !js["fogProperties"].empty()) {
				logger::warn("Weather {}: fogProperties loaded but appears empty, reverting to vanilla values", GetEditorID());
				hadErrors = true;
			}
			
			if (hadErrors) {
				// Fallback to vanilla/game values
				LoadWeatherValues();
				EditorWindow::GetSingleton()->ShowNotification(
					std::format("Some values failed to load for {}", GetEditorID()),
					ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
					3.0f);
			} else {
				logger::info("Weather {}: Loaded settings - {} weather properties, {} colors, {} fog properties",
					GetEditorID(),
					settings.weatherProperties.size(),
					settings.weatherColors.size(),
					settings.fogProperties.size());
			}
			
		} catch (const nlohmann::json::exception& e) {
			logger::error("Weather {}: Failed to deserialize settings from JSON: {}", GetEditorID(), e.what());
			logger::error("JSON content: {}", js.dump(2));
			// Fallback to vanilla/game values on exception
			LoadWeatherValues();
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Some values failed to load for {}", GetEditorID()),
				ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
				3.0f);
			return;
		}
	} else {
		// No JSON data, restore cached vanilla values
		logger::info("Weather {}: No JSON data, restoring cached vanilla values", GetEditorID());
		settings = vanillaSettings;
	}
	LoadFeatureSettings();
}

void WeatherWidget::SaveSettings()
{
	SaveFeatureSettings();
	
	try {
		js = settings;
		
		// Log what we're saving for debugging
		logger::info("Weather {}: Saving settings - {} weather properties, {} colors, {} fog properties",
			GetEditorID(),
			settings.weatherProperties.size(),
			settings.weatherColors.size(),
			settings.fogProperties.size());
			
		// Validate serialization worked
		if (js.is_null()) {
			logger::error("Weather {}: Serialization produced null JSON!", GetEditorID());
		} else if (!js.contains("weatherProperties")) {
			logger::error("Weather {}: Serialized JSON missing weatherProperties field!", GetEditorID());
		} else if (!js.contains("atmosphereColors")) {
			logger::error("Weather {}: Serialized JSON missing atmosphereColors field!", GetEditorID());
		} else if (!js.contains("clouds")) {
			logger::error("Weather {}: Serialized JSON missing clouds field!", GetEditorID());
		}
		
	} catch (const nlohmann::json::exception& e) {
		logger::error("Weather {}: Failed to serialize settings to JSON: {}", GetEditorID(), e.what());
	}
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

	// ImageSpace
	SetImageSpaceValues();
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

	// ImageSpace
	LoadImageSpaceValues();
}

void WeatherWidget::DrawDALCSettings()
{
	bool& doesInherit = settings.inheritance["DALC"];
	ImGui::Checkbox("Inherit From Parent##dalc", &doesInherit);

	if (doesInherit && HasParent()) {
		for (size_t i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
			settings.dalc[i] = GetParent()->settings.dalc[i];
		}
	} else {
		doesInherit = false;
		bool changed = false;

		if (TOD::BeginTODTable("DALC_TOD_Table")) {
			TOD::RenderTODHeader();
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Separator();
			ImGui::TableSetColumnIndex(1);
			ImGui::Separator();

			// Prepare arrays for TOD rendering
			float3 specularColors[4];
			float fresnelPowers[4];
			float3 directionalXMax[4], directionalXMin[4];
			float3 directionalYMax[4], directionalYMin[4];
			float3 directionalZMax[4], directionalZMin[4];

			for (int i = 0; i < ColorTimes::kTotal; i++) {
				specularColors[i] = settings.dalc[i].specular;
				fresnelPowers[i] = settings.dalc[i].fresnelPower;
				directionalXMax[i] = settings.dalc[i].directional[0].max;
				directionalXMin[i] = settings.dalc[i].directional[0].min;
				directionalYMax[i] = settings.dalc[i].directional[1].max;
				directionalYMin[i] = settings.dalc[i].directional[1].min;
				directionalZMax[i] = settings.dalc[i].directional[2].max;
				directionalZMin[i] = settings.dalc[i].directional[2].min;
			}

			if (TOD::DrawTODColorRow("Specular", specularColors)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].specular = specularColors[i];
				changed = true;
			}

			if (TOD::DrawTODSliderRow("Fresnel Power", fresnelPowers, 0.0f, 10.0f)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].fresnelPower = fresnelPowers[i];
				changed = true;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Separator();
			ImGui::TableSetColumnIndex(1);
			ImGui::Separator();

			if (TOD::DrawTODColorRow("Directional X Max", directionalXMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].max = directionalXMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional X Min", directionalXMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].min = directionalXMin[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional Y Max", directionalYMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].max = directionalYMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional Y Min", directionalYMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].min = directionalYMin[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional Z Max", directionalZMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].max = directionalZMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional Z Min", directionalZMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].min = directionalZMin[i];
				changed = true;
			}

			TOD::EndTODTable();
		}
		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}
}

void WeatherWidget::DrawWeatherColorSettings()
{
	bool& doesInherit = settings.inheritance["Atmosphere Colors"];
	ImGui::Checkbox("Inherit From Parent##atmosphereColors", &doesInherit);

	if (&doesInherit && HasParent()) {
		for (size_t i = 0; i < ColorTypes::kTotal; i++) {
			settings.atmosphereColors[i] = GetParent()->settings.atmosphereColors[i];
		}
	} else {
		doesInherit = false;
		bool changed = false;

		if (TOD::BeginTODTable("AtmosphereColors_Table")) {
			TOD::RenderTODHeader();
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Separator();
			ImGui::TableSetColumnIndex(1);
			ImGui::Separator();

			for (int i = 0; i < ColorTypes::kTotal; i++) {
				std::string colorTypeLabel = ColorTypeLabel(i);

				if (TOD::DrawTODColorRow(colorTypeLabel.c_str(), settings.atmosphereColors[i].colorTimes)) {
					changed = true;
				}
			}

			TOD::EndTODTable();
		}

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}
}

void WeatherWidget::DrawCloudSettings()
{
	bool& doesInherit = settings.inheritance["Clouds"];
	ImGui::Checkbox("Inherit From Parent##cloud", &doesInherit);

	if (doesInherit && HasParent()) {
		for (size_t i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
			settings.dalc[i] = GetParent()->settings.dalc[i];
		}
	} else {
		doesInherit = false;
		bool changed = false;
		for (int i = 0; i < TESWeather::kTotalLayers; i++) {
			std::string layer = std::format("Layer {}", i);

			if (ImGui::CollapsingHeader(layer.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
				if (DrawSliderInt8(std::format("Cloud Layer Speed Y##{}", layer), settings.clouds[i].cloudLayerSpeedY)) changed = true;
				ImGui::Spacing();
				if (DrawSliderInt8(std::format("Cloud Layer Speed X##{}", layer), settings.clouds[i].cloudLayerSpeedX)) changed = true;
				ImGui::Spacing();

				if (TOD::BeginTODTable((layer + "_TOD_Table").c_str())) {
					TOD::RenderTODHeader();
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Separator();
					ImGui::TableSetColumnIndex(1);
					ImGui::Separator();

					if (TOD::DrawTODColorRow("Cloud Color", settings.clouds[i].color)) {
						changed = true;
					}

					if (TOD::DrawTODSliderRow("Cloud Alpha", settings.clouds[i].cloudAlpha, 0.0f, 1.0f)) {
						changed = true;
					}

					TOD::EndTODTable();
				}
			}
		}
		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}
}

void WeatherWidget::DrawFogSettings()
{
	bool& doesInherit = settings.inheritance["Fog"];
	ImGui::Checkbox("Inherit From Parent##fog", &doesInherit);

	if (doesInherit && HasParent()) {
		settings.fogProperties = GetParent()->settings.fogProperties;
	} else {
		doesInherit = false;
		bool changed = false;

		if (ImGui::BeginTable("FogTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("Day", ImGuiTableColumnFlags_WidthFixed, 250.0f);
			ImGui::TableSetupColumn("Night", ImGuiTableColumnFlags_WidthFixed, 250.0f);
			
			// Header row
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Day");
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("Night");
			
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Separator();
			ImGui::TableSetColumnIndex(1);
			ImGui::Separator();
			ImGui::TableSetColumnIndex(2);
			ImGui::Separator();

			// Near
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Near");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##FogDayNear", &settings.fogProperties["Day Near"], 0.0f, 50000.0f, "%.0f"))
				changed = true;
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##FogNightNear", &settings.fogProperties["Night Near"], 0.0f, 50000.0f, "%.0f"))
				changed = true;

			// Far
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Far");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##FogDayFar", &settings.fogProperties["Day Far"], 0.0f, 50000.0f, "%.0f"))
				changed = true;
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##FogNightFar", &settings.fogProperties["Night Far"], 0.0f, 50000.0f, "%.0f"))
				changed = true;

			// Power
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Power");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##FogDayPower", &settings.fogProperties["Day Power"], 0.0f, 50000.0f, "%.2f"))
				changed = true;
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##FogNightPower", &settings.fogProperties["Night Power"], 0.0f, 50000.0f, "%.2f"))
				changed = true;

			// Max
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Max");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##FogDayMax", &settings.fogProperties["Day Max"], 0.0f, 50000.0f, "%.2f"))
				changed = true;
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##FogNightMax", &settings.fogProperties["Night Max"], 0.0f, 50000.0f, "%.2f"))
				changed = true;

			ImGui::EndTable();
		}

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}
}

void WeatherWidget::DrawProperties(std::string category, std::map<std::string, int> properties)
{
	bool& doesInherit = settings.inheritance[category];
	
	// Check if any property matches search (only check if search is active)
	bool hasMatchingProperty = false;
	if (searchBuffer[0] != '\0') {
		hasMatchingProperty = MatchesSearch(category);
		if (!hasMatchingProperty) {
			for (auto& p : properties) {
				if (MatchesSearch(p.first)) {
					hasMatchingProperty = true;
					break;
				}
			}
		}
		// Skip this entire category if nothing matches
		if (!hasMatchingProperty) {
			return;
		}
	}
	
	ImGui::Checkbox(std::format("Inherit From Parent##{}", category).c_str(), &doesInherit);

	if (doesInherit && HasParent()) {
		for (auto& p : properties) {
			InheritFromParent(p.first);
		}
	} else {
		doesInherit = false;
		bool changed = false;

		for (auto& p : properties) {
			// Filter individual properties based on search
			if (searchBuffer[0] != '\0' && !MatchesSearch(p.first)) {
				continue;
			}
			
			// Apply highlight effect if this setting should be highlighted
			if (ShouldHighlight(p.first)) {
				float elapsed = static_cast<float>(ImGui::GetTime()) - highlightStartTime;
				float alpha = 0.3f * (1.0f - std::abs(elapsed - 0.5f) * 2.0f); // Fade in/out over 1 second
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.6f, 1.0f, alpha));
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.4f, 0.7f, 1.0f, alpha));
			}
			
			switch (p.second) {
			case 0:
				if (DrawSliderInt8(p.first, settings.weatherProperties[p.first])) changed = true;
				break;
			case 1:
				if (DrawColorEdit(p.first, settings.weatherColors[p.first])) changed = true;
				break;
			case 2:
				if (DrawSliderUint8(p.first, settings.weatherProperties[p.first])) changed = true;
				break;
			case 3:
				if (DrawSliderFloat(p.first, settings.fogProperties[p.first])) changed = true;
				break;
			default:
				break;
			}
			
			if (ShouldHighlight(p.first)) {
				ImGui::PopStyleColor(2);
			}
		}

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
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

void WeatherWidget::SaveFeatureSettings()
{
	auto* weatherManager = WeatherManager::GetSingleton();
	
	for (const auto& [featureName, featureJson] : settings.featureSettings) {
		if (!featureJson.empty()) {
			weatherManager->SaveSettingsToWeather(weather, featureName, featureJson);
		}
	}
}

void WeatherWidget::LoadFeatureSettings()
{
	auto* weatherManager = WeatherManager::GetSingleton();
	auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	
	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature || !feature->loaded) {
			continue;
		}

		std::string featureName = feature->GetShortName();
		
		// Check if feature has registered weather variables
		if (!globalRegistry->HasWeatherSupport(featureName)) {
			continue;
		}

		json featureJson;
		if (weatherManager->LoadSettingsFromWeather(weather, featureName, featureJson)) {
			settings.featureSettings[featureName] = featureJson;
		}
	}
}

void WeatherWidget::ApplyChanges()
{
	SetWeatherValues();
	logger::info("Applied changes to weather: {}", GetEditorID());
}

void WeatherWidget::RevertChanges()
{
	LoadWeatherValues();
	logger::info("Reverted changes for weather: {}", GetEditorID());
}

void WeatherWidget::DrawFeatureSettings()
{
	ImGui::TextWrapped("Configure feature-specific settings that will be applied when this weather is active.");
	ImGui::Spacing();

	auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();

	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature || !feature->loaded) {
			continue;
		}

		std::string featureName = feature->GetShortName();
		
		// Check if feature has registered weather variables
		if (!globalRegistry->HasWeatherSupport(featureName)) {
			continue;
		}

		std::string displayName = feature->GetName();

		if (ImGui::TreeNode(displayName.c_str())) {
			ImGui::Text("Feature: %s", featureName.c_str());
			ImGui::Spacing();

			// Show if settings exist for this feature
			bool hasSettings = settings.featureSettings.find(featureName) != settings.featureSettings.end() &&
			                   !settings.featureSettings[featureName].empty();

			if (hasSettings) {
				ImGui::TextColored({ 0.0f, 1.0f, 0.0f, 1.0f }, "Has weather-specific settings");
				
				if (ImGui::Button("Clear Settings")) {
					settings.featureSettings[featureName] = json::object();
				}
				ImGui::SameLine();
				if (ImGui::Button("View JSON")) {
					ImGui::OpenPopup("FeatureJSON");
				}

				if (ImGui::BeginPopup("FeatureJSON")) {
					ImGui::Text("Settings JSON:");
					ImGui::Separator();
					std::string jsonStr = settings.featureSettings[featureName].dump(2);
					ImGui::TextWrapped("%s", jsonStr.c_str());
					ImGui::EndPopup();
				}
			} else {
				ImGui::TextColored({ 0.7f, 0.7f, 0.7f, 1.0f }, "No weather-specific settings");
			}

			ImGui::Spacing();
			ImGui::TextWrapped("Note: Feature settings should be configured through the feature's own settings panel. "
			                   "This section shows which features have per-weather overrides.");

			ImGui::TreePop();
		}
	}
}

void WeatherWidget::DrawImageSpaceSettings()
{
	if (!weather) {
		ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.0f }, "Weather object is null!");
		return;
	}

	ImGui::TextWrapped("Configure ImageSpace (post-processing) effects for different times of day.");
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	bool changed = false;

	if (TOD::BeginTODTable("ImageSpace_TOD_Table")) {
		TOD::RenderTODHeader();
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();

		// Prepare arrays for TOD rendering
		float hdrEyeAdaptSpeed[4];
		float hdrBloomBlurRadius[4];
		float hdrBloomThreshold[4];
		float hdrBloomScale[4];
		float hdrSunlightScale[4];
		float hdrSkyScale[4];
		float cinematicSaturation[4];
		float cinematicBrightness[4];
		float cinematicContrast[4];
		float3 tintColor[4];
		float tintAmount[4];
		float dofStrength[4];
		float dofDistance[4];
		float dofRange[4];

		for (int i = 0; i < ColorTimes::kTotal; i++) {
			hdrEyeAdaptSpeed[i] = settings.imageSpaces[i].hdrEyeAdaptSpeed;
			hdrBloomBlurRadius[i] = settings.imageSpaces[i].hdrBloomBlurRadius;
			hdrBloomThreshold[i] = settings.imageSpaces[i].hdrBloomThreshold;
			hdrBloomScale[i] = settings.imageSpaces[i].hdrBloomScale;
			hdrSunlightScale[i] = settings.imageSpaces[i].hdrSunlightScale;
			hdrSkyScale[i] = settings.imageSpaces[i].hdrSkyScale;
			cinematicSaturation[i] = settings.imageSpaces[i].cinematicSaturation;
			cinematicBrightness[i] = settings.imageSpaces[i].cinematicBrightness;
			cinematicContrast[i] = settings.imageSpaces[i].cinematicContrast;
			tintColor[i] = settings.imageSpaces[i].tintColor;
			tintAmount[i] = settings.imageSpaces[i].tintAmount;
			dofStrength[i] = settings.imageSpaces[i].dofStrength;
			dofDistance[i] = settings.imageSpaces[i].dofDistance;
			dofRange[i] = settings.imageSpaces[i].dofRange;
		}

		// HDR Settings
		if (TOD::DrawTODSliderRow("Eye Adapt Speed", hdrEyeAdaptSpeed, 0.0f, 10.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].hdrEyeAdaptSpeed = hdrEyeAdaptSpeed[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("Bloom Blur Radius", hdrBloomBlurRadius, 0.0f, 10.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].hdrBloomBlurRadius = hdrBloomBlurRadius[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("Bloom Threshold", hdrBloomThreshold, 0.0f, 10.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].hdrBloomThreshold = hdrBloomThreshold[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("Bloom Scale", hdrBloomScale, 0.0f, 10.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].hdrBloomScale = hdrBloomScale[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("Sunlight Scale", hdrSunlightScale, 0.0f, 10.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].hdrSunlightScale = hdrSunlightScale[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("Sky Scale", hdrSkyScale, 0.0f, 10.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].hdrSkyScale = hdrSkyScale[i];
			changed = true;
		}

		// Separator
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();

		// Cinematic Settings
		if (TOD::DrawTODSliderRow("Saturation", cinematicSaturation, 0.0f, 2.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].cinematicSaturation = cinematicSaturation[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("Brightness", cinematicBrightness, 0.0f, 2.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].cinematicBrightness = cinematicBrightness[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("Contrast", cinematicContrast, 0.0f, 2.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].cinematicContrast = cinematicContrast[i];
			changed = true;
		}

		// Separator
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();

		// Tint Settings
		if (TOD::DrawTODColorRow("Tint Color", tintColor)) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].tintColor = tintColor[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("Tint Amount", tintAmount, 0.0f, 1.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].tintAmount = tintAmount[i];
			changed = true;
		}

		// Separator
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();

		// Depth of Field
		if (TOD::DrawTODSliderRow("DOF Strength", dofStrength, 0.0f, 10.0f, "%.3f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].dofStrength = dofStrength[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("DOF Distance", dofDistance, 0.0f, 10000.0f, "%.1f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].dofDistance = dofDistance[i];
			changed = true;
		}

		if (TOD::DrawTODSliderRow("DOF Range", dofRange, 0.0f, 10000.0f, "%.1f")) {
			for (int i = 0; i < ColorTimes::kTotal; i++)
				settings.imageSpaces[i].dofRange = dofRange[i];
			changed = true;
		}

		TOD::EndTODTable();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		SetImageSpaceValues();
	}
}

void WeatherWidget::LoadImageSpaceValues()
{
	if (!weather)
		return;

	for (int timeIdx = 0; timeIdx < ColorTimes::kTotal; timeIdx++) {
		RE::TESImageSpace* imageSpace = weather->imageSpaces[timeIdx];
		if (!imageSpace)
			continue;

		auto& imgSettings = settings.imageSpaces[timeIdx];
		auto& data = imageSpace->data;

		// HDR
		imgSettings.hdrEyeAdaptSpeed = data.hdr.eyeAdaptSpeed;
		imgSettings.hdrBloomBlurRadius = data.hdr.bloomBlurRadius;
		imgSettings.hdrBloomThreshold = data.hdr.bloomThreshold;
		imgSettings.hdrBloomScale = data.hdr.bloomScale;
		imgSettings.hdrSunlightScale = data.hdr.sunlightScale;
		imgSettings.hdrSkyScale = data.hdr.skyScale;

		// Cinematic
		imgSettings.cinematicSaturation = data.cinematic.saturation;
		imgSettings.cinematicBrightness = data.cinematic.brightness;
		imgSettings.cinematicContrast = data.cinematic.contrast;

		// Tint
		imgSettings.tintColor.x = data.tint.color.red;
		imgSettings.tintColor.y = data.tint.color.green;
		imgSettings.tintColor.z = data.tint.color.blue;
		imgSettings.tintAmount = data.tint.amount;

		// Depth of Field
		imgSettings.dofStrength = data.depthOfField.strength;
		imgSettings.dofDistance = data.depthOfField.distance;
		imgSettings.dofRange = data.depthOfField.range;
	}
}

void WeatherWidget::SetImageSpaceValues()
{
	if (!weather)
		return;

	for (int timeIdx = 0; timeIdx < ColorTimes::kTotal; timeIdx++) {
		RE::TESImageSpace* imageSpace = weather->imageSpaces[timeIdx];
		if (!imageSpace)
			continue;

		auto& imgSettings = settings.imageSpaces[timeIdx];
		auto& data = imageSpace->data;

		// HDR
		data.hdr.eyeAdaptSpeed = imgSettings.hdrEyeAdaptSpeed;
		data.hdr.bloomBlurRadius = imgSettings.hdrBloomBlurRadius;
		data.hdr.bloomThreshold = imgSettings.hdrBloomThreshold;
		data.hdr.bloomScale = imgSettings.hdrBloomScale;
		data.hdr.sunlightScale = imgSettings.hdrSunlightScale;
		data.hdr.skyScale = imgSettings.hdrSkyScale;

		// Cinematic
		data.cinematic.saturation = imgSettings.cinematicSaturation;
		data.cinematic.brightness = imgSettings.cinematicBrightness;
		data.cinematic.contrast = imgSettings.cinematicContrast;

		// Tint
		data.tint.color.red = imgSettings.tintColor.x;
		data.tint.color.green = imgSettings.tintColor.y;
		data.tint.color.blue = imgSettings.tintColor.z;
		data.tint.amount = imgSettings.tintAmount;

		// Depth of Field
		data.depthOfField.strength = imgSettings.dofStrength;
		data.depthOfField.distance = imgSettings.dofDistance;
		data.depthOfField.range = imgSettings.dofRange;
	}
}

void WeatherWidget::UpdateSearchResults()
{
	searchResults.clear();
	
	if (searchBuffer[0] == '\0')
		return;
	
	std::string searchTerm = searchBuffer;
	
	// Search in Basic tab properties
	std::vector<std::pair<std::string, std::map<std::string, int>>> basicCategories = {
		{ "Sun", { { "Sun Glare", 0 }, { "Sun Damage", 0 } } },
		{ "Wind", { { "Wind Speed", 0 }, { "Wind Direction", 0 }, { "Wind Direction Range", 0 } } },
		{ "Precipitation", { { "Precipitation Begin Fade In", 0 }, { "Precipitation Begin Fade Out", 0 } } },
		{ "Lightning", { { "Thunder Lightning Begin Fade In", 0 }, { "Thunder Lightning End Fade Out", 0 }, 
						{ "Thunder Lightning Frequency", 0 }, { "Lightning Color", 1 } } },
		{ "Visual Effects", { { "Visual Effect Begin", 0 }, { "Visual Effect End", 0 } } },
		{ "Weather Transition", { { "Trans Delta", 0 } } }
	};
	
	for (const auto& [category, properties] : basicCategories) {
		for (const auto& [propName, type] : properties) {
			if (ContainsStringIgnoreCase(propName, searchTerm)) {
				searchResults.push_back({ propName, "Basic", propName });
			}
		}
	}
	
	// Search in Fog tab
	std::vector<std::string> fogProperties = {
		"Day Near", "Day Far", "Day Power", "Day Max",
		"Night Near", "Night Far", "Night Power", "Night Max"
	};
	for (const auto& propName : fogProperties) {
		if (ContainsStringIgnoreCase(propName, searchTerm)) {
			searchResults.push_back({ propName, "Fog", propName });
		}
	}
	
	// Search in DALC settings
	std::vector<std::string> dalcSettings = {
		"Fresnel Power", "Specular",
		"Directional X Max", "Directional X Min",
		"Directional Y Max", "Directional Y Min",
		"Directional Z Max", "Directional Z Min"
	};
	for (const auto& setting : dalcSettings) {
		if (ContainsStringIgnoreCase(setting, searchTerm)) {
			searchResults.push_back({ setting, "Lighting (DALC)", setting });
		}
	}
	
	// Search in Atmosphere Colors
	for (int i = 0; i < ColorTypes::kTotal; i++) {
		std::string colorType = ColorTypeLabel(i);
		if (ContainsStringIgnoreCase(colorType, searchTerm)) {
			searchResults.push_back({ colorType, "Atmosphere Colors", colorType });
		}
	}
	
	// Search in Cloud settings
	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		std::string layer = std::format("Layer {}", i);
		if (ContainsStringIgnoreCase(layer, searchTerm) || 
			ContainsStringIgnoreCase("Cloud", searchTerm)) {
			searchResults.push_back({ std::format("Cloud {}", layer), "Clouds", layer });
		}
	}
	
	// Search in ImageSpace settings
	std::vector<std::string> imageSpaceSettings = {
		"Eye Adapt Speed", "Bloom Blur Radius", "Bloom Threshold", "Bloom Scale",
		"Sunlight Scale", "Sky Scale", "Saturation", "Brightness", "Contrast",
		"Tint Color", "Tint Amount", "DOF Strength", "DOF Distance", "DOF Range"
	};
	for (const auto& setting : imageSpaceSettings) {
		if (ContainsStringIgnoreCase(setting, searchTerm)) {
			searchResults.push_back({ setting, "ImageSpace", setting });
		}
	}
}

void WeatherWidget::NavigateToSetting(const SearchResult& result)
{
	activeTabOverride = result.tabName;
	highlightedSetting = result.settingId;
	highlightStartTime = static_cast<float>(ImGui::GetTime());
}

bool WeatherWidget::ShouldHighlight(const std::string& settingId) const
{
	if (highlightedSetting != settingId)
		return false;
	
	float elapsed = static_cast<float>(ImGui::GetTime()) - highlightStartTime;
	return elapsed < 1.0f; // Highlight for 1 second
}

