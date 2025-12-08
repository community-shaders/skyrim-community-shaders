#include "WeatherWidget.h"

#include "../EditorWindow.h"
#include "State.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

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
	dalc,
	featureSettings)

WeatherWidget::~WeatherWidget()
{
}

void WeatherWidget::DrawWidget()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_MenuBar)) {
		DrawMenu();

		auto editorWindow = EditorWindow::GetSingleton();
		bool isLocked = editorWindow->IsWeatherLocked() && editorWindow->GetLockedWeather() == weather;

		// Weather lock controls
		if (isLocked) {
			if (ImGui::Button("Unlock Weather", ImVec2(-1, 0))) {
				editorWindow->UnlockWeather();
			}
		} else {
			if (ImGui::Button("Lock & Force This Weather", ImVec2(-1, 0))) {
				editorWindow->LockWeather(weather);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Force this weather to be active and prevent time-based weather changes");
			}
		}

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

		// Show Apply/Revert buttons only when auto-apply is disabled
		if (!editorWindow->settings.autoApplyChanges) {
			auto menu = globals::menu;
			bool useIcons = menu && menu->GetSettings().Theme.ShowActionIcons &&
			                menu->uiIcons.saveSettings.texture &&
			                menu->uiIcons.featureSettingRevert.texture;

			if (useIcons) {
				// Icon-based buttons
				const float iconSize = ImGui::GetFrameHeight();
				const ImVec2 buttonSize(iconSize, iconSize);

				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));

				if (ImGui::ImageButton("##ApplyWeather", menu->uiIcons.saveSettings.texture, buttonSize)) {
					ApplyChanges();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}

				ImGui::SameLine();

				if (ImGui::ImageButton("##RevertWeather", menu->uiIcons.featureSettingRevert.texture, buttonSize)) {
					RevertChanges();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Revert to saved values");
				}

				ImGui::PopStyleColor(2);
				ImGui::PopStyleVar();
			} else {
				// Text-based fallback buttons
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
				if (ImGui::Button("Apply", ImVec2(ImGui::GetContentRegionAvail().x * 0.49f, 0))) {
					ApplyChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Apply changes to the game");
				}
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
				if (ImGui::Button("Revert", ImVec2(-1, 0))) {
					RevertChanges();
				}
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Revert to saved values");
				}
			}
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

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void WeatherWidget::LoadSettings()
{
	if (!js.empty()) {
		settings = js;
	}
	LoadFeatureSettings();
}

void WeatherWidget::SaveSettings()
{
	SaveFeatureSettings();
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
	auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	
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
	if (ImGui::CollapsingHeader("Per-Feature Weather Settings", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
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
}
