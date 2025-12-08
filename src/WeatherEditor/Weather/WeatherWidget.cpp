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

		auto editorWindow = EditorWindow::GetSingleton();
		bool isLocked = editorWindow->IsWeatherLocked() && editorWindow->GetLockedWeather() == weather;

		// Weather lock and time controls (inline)
		float buttonWidth = ImGui::GetContentRegionAvail().x * 0.49f;
		
		// Weather lock controls
		if (isLocked) {
			if (ImGui::Button("Unlock Weather", ImVec2(buttonWidth, 0))) {
				editorWindow->UnlockWeather();
			}
		} else {
			if (ImGui::Button("Lock & Force This Weather", ImVec2(buttonWidth, 0))) {
				editorWindow->LockWeather(weather);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Force this weather to be active and prevent time-based weather changes");
			}
		}

		ImGui::SameLine();

		// Time pause toggle
		bool isPaused = editorWindow->IsTimePaused();
		if (isPaused) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
			if (ImGui::Button("Resume Time", ImVec2(-1, 0))) {
				editorWindow->ResumeTime();
			}
			ImGui::PopStyleColor(2);
		} else {
			if (ImGui::Button("Pause Time", ImVec2(-1, 0))) {
				editorWindow->PauseTime();
			}
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Pause/resume time progression in game");
		}

		auto menu = globals::menu;
		bool useIcons = menu && menu->GetSettings().Theme.ShowActionIcons &&
		                menu->uiIcons.applyToGame.texture &&
		                menu->uiIcons.saveSettings.texture &&
		                menu->uiIcons.loadSettings.texture;

		if (useIcons) {
			const float iconSize = ImGui::GetFrameHeight();
			const ImVec2 buttonSize(iconSize, iconSize);

			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));

			// Apply to game button - only show when auto-apply is disabled
			if (!editorWindow->settings.autoApplyChanges && menu->uiIcons.applyToGame.texture) {
				if (ImGui::ImageButton("##ApplyToGame", menu->uiIcons.applyToGame.texture, buttonSize)) {
					ApplyChanges();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}
				ImGui::SameLine();
			}

			// Save to file button - always visible
			if (ImGui::ImageButton("##SaveWeather", menu->uiIcons.saveSettings.texture, buttonSize)) {
				Save();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Save to file");
			}

			ImGui::SameLine();

			// Load from file button - always visible
			if (ImGui::ImageButton("##LoadWeather", menu->uiIcons.loadSettings.texture, buttonSize)) {
				Load();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Load from file");
			}

			ImGui::SameLine();

			// Delete button - always visible
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.3f, 1.0f));
			if (ImGui::ImageButton("##DeleteWeather", menu->uiIcons.deleteSettings.texture, buttonSize)) {
				if (editorWindow->settings.suppressDeleteWarning) {
					Delete();
				} else {
					ImGui::OpenPopup("ConfirmDelete");
				}
			}
			ImGui::PopStyleColor(2);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Delete saved file and revert to defaults");
			}

			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar();
		} else {
			// Text-based fallback buttons
			bool hasApplyButton = !editorWindow->settings.autoApplyChanges;
			int buttonCount = hasApplyButton ? 4 : 3;
			float textButtonWidth = ImGui::GetContentRegionAvail().x / buttonCount;
			
			// Apply button - only show when auto-apply is disabled
			if (hasApplyButton) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
				if (ImGui::Button("Apply", ImVec2(textButtonWidth * 0.97f, 0))) {
					ApplyChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}
				ImGui::SameLine();
			}
			
			// Save button - always visible
			if (ImGui::Button("Save", ImVec2(textButtonWidth * 0.97f, 0))) {
				Save();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Save to file");
			}
			
			ImGui::SameLine();
			
			// Load button - always visible
			if (ImGui::Button("Load", ImVec2(textButtonWidth * 0.97f, 0))) {
				Load();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Load from file");
			}
			
			ImGui::SameLine();
			
			// Delete button - always visible
			float deleteIconSize = ImGui::GetFrameHeight();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.3f, 1.0f));
			if (ImGui::ImageButton("##DeleteWeatherText", menu->uiIcons.deleteSettings.texture, ImVec2(deleteIconSize, deleteIconSize))) {
				if (editorWindow->settings.suppressDeleteWarning) {
					Delete();
				} else {
					ImGui::OpenPopup("ConfirmDelete");
				}
			}
			ImGui::PopStyleColor(2);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Delete saved file and revert to defaults");
			}
		}
		
		// Confirmation popup for delete
		if (ImGui::BeginPopupModal("ConfirmDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Are you sure you want to delete this file?");
			ImGui::Text("This will revert to vanilla/mod provided values.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			
			if (ImGui::Checkbox("Don't show this warning again", &editorWindow->settings.suppressDeleteWarning)) {
				// Save the preference immediately
				editorWindow->Save();
			}
			
			ImGui::Spacing();
			
			if (ImGui::Button("Yes", ImVec2(120, 0))) {
				Delete();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::SameLine();
			if (ImGui::Button("No", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		
		ImGui::Separator();

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
			if (ImGui::BeginTabItem("Basic")) {
				DrawProperties("Sun", { { "Sun Glare", INT8_SLIDER }, { "Sun Damage", INT8_SLIDER } });
				DrawProperties("Wind", { { "Wind Speed", UINT8_SLIDER }, { "Wind Direction", INT8_SLIDER }, { "Wind Direction Range", INT8_SLIDER } });
				DrawProperties("Precipitation", { { "Precipitation Begin Fade In", INT8_SLIDER }, { "Precipitation Begin Fade Out", INT8_SLIDER } });
				DrawProperties("Lightning", { { "Thunder Lightning Begin Fade In", INT8_SLIDER }, { "Thunder Lightning End Fade Out", INT8_SLIDER },
												{ "Thunder Lightning Frequency", INT8_SLIDER }, { "Lightning Color", COLOR3_PICKER } });
				DrawProperties("Visual Effects", { { "Visual Effect Begin", INT8_SLIDER }, { "Visual Effect End", INT8_SLIDER } });
				DrawProperties("Weather Transition", { { "Trans Delta", INT8_SLIDER } });
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Lighting (DALC)")) {
				DrawDALCSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Atmosphere Colors")) {
				DrawWeatherColorSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Clouds")) {
				DrawCloudSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Fog")) {
				DrawProperties("Fog", { { "Day Near", FLOAT_SLIDER }, { "Day Far", FLOAT_SLIDER }, { "Day Power", FLOAT_SLIDER }, { "Day Max", FLOAT_SLIDER },
										  { "Night Near", FLOAT_SLIDER }, { "Night Far", FLOAT_SLIDER }, { "Night Power", FLOAT_SLIDER }, { "Night Max", FLOAT_SLIDER } });
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Features")) {
				DrawFeatureSettings();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("ImageSpace")) {
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
		// No JSON data, load vanilla values
		logger::info("Weather {}: No JSON data, loading vanilla values", GetEditorID());
		LoadWeatherValues();
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
		for (int i = 0; i < RE::TESWeather::ColorTimes::kTotal; i++) {
			std::string label = ColorTimeLabel(i);

			if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
				ImGui::Spacing();
				if (DrawColorEdit(std::format("Specular##{}", label), settings.dalc[i].specular)) changed = true;
				ImGui::Spacing();
				if (DrawSliderFloat(std::format("Fresnel Power##{}", label), settings.dalc[i].fresnelPower)) changed = true;
				ImGui::Spacing();

				for (int j = 0; j < 3; j++) {
					if (DrawColorEdit(std::format("DALC X Max##{}", label), settings.dalc[i].directional[j].max)) changed = true;
					if (DrawColorEdit(std::format("DALC X Min##{}", label), settings.dalc[i].directional[j].min)) changed = true;
					ImGui::Spacing();
				}
			}
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

		for (int i = 0; i < ColorTypes::kTotal; i++) {
			std::string colorTypeLabel = ColorTypeLabel(i);

			if (ImGui::CollapsingHeader(colorTypeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
				for (int j = 0; j < ColorTimes::kTotal; j++) {
					if (DrawColorEdit(std::format("{}##{}", ColorTimeLabel(j), colorTypeLabel), settings.atmosphereColors[i].colorTimes[j])) changed = true;
					ImGui::Spacing();
				}
			}
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

				for (int j = 0; j < ColorTimes::kTotal; j++) {
					std::string colorTime = ColorTimeLabel(j).c_str();

					if (ImGui::CollapsingHeader(colorTime.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
						if (DrawColorEdit(std::format("Cloud Color##{}{}", colorTime, i), settings.clouds[i].color[j])) changed = true;
						ImGui::Spacing();
						if (DrawSliderFloat(std::format("Cloud Alpha##{}{}", colorTime, i), settings.clouds[i].cloudAlpha[j])) changed = true;
						ImGui::Spacing();
					}
				}
			}
		}
		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
			ApplyChanges();
		}
	}
}

void WeatherWidget::DrawProperties(std::string category, std::map<std::string, int> properties)
{
	bool& doesInherit = settings.inheritance[category];
	ImGui::Checkbox(std::format("Inherit From Parent##{}", category).c_str(), &doesInherit);

	if (doesInherit && HasParent()) {
		for (auto& p : properties) {
			InheritFromParent(p.first);
		}
	} else {
		doesInherit = false;
		bool changed = false;

		for (auto& p : properties) {
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

	const char* timeNames[] = { "Sunrise", "Day", "Sunset", "Night" };

	for (int timeIdx = 0; timeIdx < ColorTimes::kTotal; timeIdx++) {
		auto& imgSpace = settings.imageSpaces[timeIdx];
		RE::TESImageSpace* weatherImgSpace = weather->imageSpaces[timeIdx];

		ImGui::PushID(timeIdx);

		if (ImGui::CollapsingHeader(timeNames[timeIdx], ImGuiTreeNodeFlags_DefaultOpen)) {
			// Show which ImageSpace form is assigned
			if (weatherImgSpace) {
				ImGui::TextColored({ 0.7f, 0.7f, 0.7f, 1.0f }, "ImageSpace: %s (%08X)",
					weatherImgSpace->GetFormEditorID(),
					weatherImgSpace->GetFormID());
			} else {
				ImGui::TextColored({ 1.0f, 0.5f, 0.0f, 1.0f }, "No ImageSpace assigned");
			}

			ImGui::Spacing();

			// HDR Settings
			if (ImGui::TreeNode("HDR Settings")) {
				bool changed = false;
				changed |= ImGui::SliderFloat("Eye Adapt Speed", &imgSpace.hdrEyeAdaptSpeed, 0.0f, 10.0f, "%.3f");
				changed |= ImGui::SliderFloat("Bloom Blur Radius", &imgSpace.hdrBloomBlurRadius, 0.0f, 10.0f, "%.3f");
				changed |= ImGui::SliderFloat("Bloom Threshold", &imgSpace.hdrBloomThreshold, 0.0f, 10.0f, "%.3f");
				changed |= ImGui::SliderFloat("Bloom Scale", &imgSpace.hdrBloomScale, 0.0f, 10.0f, "%.3f");
				changed |= ImGui::SliderFloat("Sunlight Scale", &imgSpace.hdrSunlightScale, 0.0f, 10.0f, "%.3f");
				changed |= ImGui::SliderFloat("Sky Scale", &imgSpace.hdrSkyScale, 0.0f, 10.0f, "%.3f");

				if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
					SetImageSpaceValues();
				}

				ImGui::TreePop();
			}

			// Cinematic Settings
			if (ImGui::TreeNode("Cinematic Settings")) {
				bool changed = false;
				changed |= ImGui::SliderFloat("Saturation", &imgSpace.cinematicSaturation, 0.0f, 2.0f, "%.3f");
				changed |= ImGui::SliderFloat("Brightness", &imgSpace.cinematicBrightness, 0.0f, 2.0f, "%.3f");
				changed |= ImGui::SliderFloat("Contrast", &imgSpace.cinematicContrast, 0.0f, 2.0f, "%.3f");

				if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
					SetImageSpaceValues();
				}

				ImGui::TreePop();
			}

			// Tint Settings
			if (ImGui::TreeNode("Tint Settings")) {
				bool changed = false;
				changed |= ImGui::ColorEdit3("Tint Color", &imgSpace.tintColor.x);
				changed |= ImGui::SliderFloat("Tint Amount", &imgSpace.tintAmount, 0.0f, 1.0f, "%.3f");

				if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
					SetImageSpaceValues();
				}

				ImGui::TreePop();
			}

			// Depth of Field Settings
			if (ImGui::TreeNode("Depth of Field")) {
				bool changed = false;
				changed |= ImGui::SliderFloat("DOF Strength", &imgSpace.dofStrength, 0.0f, 10.0f, "%.3f");
				changed |= ImGui::SliderFloat("DOF Distance", &imgSpace.dofDistance, 0.0f, 10000.0f, "%.1f");
				changed |= ImGui::SliderFloat("DOF Range", &imgSpace.dofRange, 0.0f, 10000.0f, "%.1f");

				if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
					SetImageSpaceValues();
				}

				ImGui::TreePop();
			}

			ImGui::Spacing();
		}

		ImGui::PopID();
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
