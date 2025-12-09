#include "WeatherWidget.h"

#include <format>

#include "../EditorWindow.h"
#include "State.h"
#include "Utils/UI.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Atmosphere, colorTimes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DirectionalColor, max, min)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DALC, specular, fresnelPower, directional)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Cloud, cloudLayerSpeedY, cloudLayerSpeedX, color, cloudAlpha, enabled, texturePath)

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
	inheritFlags,
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
	for (auto& [layerIndex, srv] : cloudTextureCache) {
		if (srv) {
			srv->Release();
		}
	}
	cloudTextureCache.clear();
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
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
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
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
		}

		auto editorWindow = EditorWindow::GetSingleton();
		auto& widgets = editorWindow->weatherWidgets;

		// Sets the parent widget if settings have been loaded.
		if (settings.parent != "None") {
			parent = GetParent();
			if (parent == nullptr)
				settings.parent = "None";
		}

		if (editorWindow->settings.enableInheritFromParent) {
			if (ImGui::BeginCombo("Parent", settings.parent.c_str())) {
				// Option for "None"
				if (ImGui::Selectable("None", parent == nullptr)) {
					parent = nullptr;
					settings.parent = "None";
				}

				for (int i = 0; i < widgets.size(); i++) {
					auto& widget = widgets[i];

					// Skip self-selection
					if (widget.get() == this)
						continue;

					// Option for each widget
					if (ImGui::Selectable(widget->GetEditorID().c_str(), parent == widget.get())) {
						parent = (WeatherWidget*)widget.get();
						settings.parent = widget->GetEditorID();
					}

					// Set default focus to the current parent
					if (parent == widget.get()) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Editor-only feature: Set a parent weather to copy settings from.");
				ImGui::TextUnformatted("Use 'Inherit From Parent' checkboxes to copy specific values.");
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "Note: This is NOT the same as cell lighting template inheritance.");
				ImGui::EndTooltip();
			}

			if (parent) {
				ImGui::SameLine();
				if (ImGui::Button("Inherit All")) {
					InheritAllFromParent();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Copy all parameter values from parent weather");
				}

				if (!parent->IsOpen()) {
					ImGui::SameLine();
					if (ImGui::Button("Open"))
						parent->SetOpen(true);
				}
			}
		}
	}  // Tab bar for organizing settings
	if (ImGui::BeginTabBar("WeatherSettingsTabs", ImGuiTabBarFlags_None)) {
		// Use activeTabOverride to auto-navigate to specific tab
		ImGuiTabItemFlags basicFlags = (activeTabOverride == "Basic") ? ImGuiTabItemFlags_SetSelected : 0;
		ImGuiTabItemFlags dalcFlags = (activeTabOverride == "Lighting (DALC)") ? ImGuiTabItemFlags_SetSelected : 0;
		ImGuiTabItemFlags atmosphereFlags = (activeTabOverride == "Atmosphere Colors") ? ImGuiTabItemFlags_SetSelected : 0;
		ImGuiTabItemFlags cloudsFlags = (activeTabOverride == "Clouds") ? ImGuiTabItemFlags_SetSelected : 0;
		ImGuiTabItemFlags fogFlags = (activeTabOverride == "Fog") ? ImGuiTabItemFlags_SetSelected : 0;
		ImGuiTabItemFlags featuresFlags = (activeTabOverride == "Features") ? ImGuiTabItemFlags_SetSelected : 0;
		ImGuiTabItemFlags recordsFlags = (activeTabOverride == "Records") ? ImGuiTabItemFlags_SetSelected : 0;
		if (!activeTabOverride.empty()) {
			activeTabOverride = "";  // Clear after use
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

		if (ImGui::BeginTabItem("Records", nullptr, recordsFlags)) {
			ImGui::Spacing();
			ImGui::TextWrapped("Form record references used by this weather.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			auto* editorWindow = EditorWindow::GetSingleton();

			bool recordChanged = false;
			bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();
			WeatherWidget* parentWidget = hasParent ? GetParent() : nullptr;

			// ImageSpace Records (per time of day)
			if (ImGui::CollapsingHeader("ImageSpace", ImGuiTreeNodeFlags_DefaultOpen)) {
				for (int i = 0; i < ColorTimes::kTotal; i++) {
					ImGui::PushID(i);
					std::string label = ColorTimeLabel(i);
					std::string inheritKey = "ImageSpace_" + std::to_string(i);

					// Inherit checkbox
					if (hasParent) {
						bool& inheritFlag = settings.inheritFlags[inheritKey];
						if (ImGui::Checkbox(("##inherit_" + inheritKey).c_str(), &inheritFlag)) {
							if (inheritFlag && parentWidget) {
								weather->imageSpaces[i] = parentWidget->weather->imageSpaces[i];
								recordChanged = true;
							}
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip(inheritFlag ? "Inheriting from parent" : "Inherit from parent");
						}
						ImGui::SameLine();
					}

					ImGui::Text("%s:", label.c_str());
					ImGui::SameLine(hasParent ? 120.0f : 100.0f);
					if (WeatherUtils::DrawFormPickerCached("##ImageSpace", weather->imageSpaces[i], editorWindow->imageSpaceWidgets, false, true)) {
						recordChanged = true;
					}  // Add "Open" button
					if (weather->imageSpaces[i]) {
						ImGui::SameLine();
						if (ImGui::SmallButton(std::format("Open##{}", i).c_str())) {
							for (auto& widget : editorWindow->imageSpaceWidgets) {
								if (widget->form == weather->imageSpaces[i]) {
									widget->SetOpen(true);
									break;
								}
							}
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("Open this ImageSpace for editing");
						}
					}

					ImGui::PopID();
				}
				ImGui::Spacing();
			}

			// Volumetric Lighting Records (per time of day)
			if (ImGui::CollapsingHeader("Volumetric Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
				for (int i = 0; i < ColorTimes::kTotal; i++) {
					ImGui::PushID(100 + i);
					std::string label = ColorTimeLabel(i);
					std::string inheritKey = "VolumetricLighting_" + std::to_string(i);

					// Inherit checkbox
					if (hasParent) {
						bool& inheritFlag = settings.inheritFlags[inheritKey];
						if (ImGui::Checkbox(("##inherit_" + inheritKey).c_str(), &inheritFlag)) {
							if (inheritFlag && parentWidget) {
								weather->volumetricLighting[i] = parentWidget->weather->volumetricLighting[i];
								recordChanged = true;
							}
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip(inheritFlag ? "Inheriting from parent" : "Inherit from parent");
						}
						ImGui::SameLine();
					}

					ImGui::Text("%s:", label.c_str());
					ImGui::SameLine(hasParent ? 120.0f : 100.0f);
					if (WeatherUtils::DrawFormPickerCached("##VolumetricLighting", weather->volumetricLighting[i], editorWindow->volumetricLightingWidgets, false, true)) {
						recordChanged = true;
					}  // Add "Open" button
					if (weather->volumetricLighting[i]) {
						ImGui::SameLine();
						if (ImGui::SmallButton(std::format("Open##{}", i).c_str())) {
							for (auto& widget : editorWindow->volumetricLightingWidgets) {
								if (widget->form == weather->volumetricLighting[i]) {
									widget->SetOpen(true);
									break;
								}
							}
						}
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("Open this Volumetric Lighting for editing");
						}
					}

					ImGui::PopID();
				}
				ImGui::Spacing();
			}

			// Precipitation Data
			if (ImGui::CollapsingHeader("Precipitation", ImGuiTreeNodeFlags_DefaultOpen)) {
				// Inherit checkbox
				if (hasParent) {
					bool& inheritFlag = settings.inheritFlags["Precipitation"];
					if (ImGui::Checkbox("##inherit_Precipitation", &inheritFlag)) {
						if (inheritFlag && parentWidget) {
							weather->precipitationData = parentWidget->weather->precipitationData;
							recordChanged = true;
						}
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip(inheritFlag ? "Inheriting from parent" : "Inherit from parent");
					}
					ImGui::SameLine();
				}

				ImGui::Text("Particle Shader:");
				ImGui::SameLine(hasParent ? 170.0f : 150.0f);
				if (WeatherUtils::DrawFormPickerCached("##Precipitation", weather->precipitationData, editorWindow->precipitationWidgets, false, true)) {
					recordChanged = true;
				}  // Add "Open" button
				if (weather->precipitationData) {
					ImGui::SameLine();
					if (ImGui::SmallButton("Open##Precip")) {
						for (auto& widget : editorWindow->precipitationWidgets) {
							if (widget->form == weather->precipitationData) {
								widget->SetOpen(true);
								break;
							}
						}
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Open this Precipitation for editing");
					}
				}

				ImGui::Spacing();
			}

			// Visual Effect (Reference Effect)
			if (ImGui::CollapsingHeader("Visual Effect", ImGuiTreeNodeFlags_DefaultOpen)) {
				// Inherit checkbox
				if (hasParent) {
					bool& inheritFlag = settings.inheritFlags["ReferenceEffect"];
					if (ImGui::Checkbox("##inherit_ReferenceEffect", &inheritFlag)) {
						if (inheritFlag && parentWidget) {
							weather->referenceEffect = parentWidget->weather->referenceEffect;
							recordChanged = true;
						}
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip(inheritFlag ? "Inheriting from parent" : "Inherit from parent");
					}
					ImGui::SameLine();
				}

				ImGui::Text("Reference Effect:");
				ImGui::SameLine(hasParent ? 170.0f : 150.0f);
				if (WeatherUtils::DrawFormPickerCached("##ReferenceEffect", weather->referenceEffect, editorWindow->referenceEffectWidgets, false, true)) {
					recordChanged = true;
				}  // Add "Open" button
				if (weather->referenceEffect) {
					ImGui::SameLine();
					if (ImGui::SmallButton("Open##RefEffect")) {
						for (auto& widget : editorWindow->referenceEffectWidgets) {
							if (widget->form == weather->referenceEffect) {
								widget->SetOpen(true);
								break;
							}
						}
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Open this Visual Effect for editing");
					}
				}

				ImGui::Spacing();
			}

			if (recordChanged) {
			}

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
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
			// Fallback to vanilla/game values on exception
			LoadWeatherValues();
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Some values failed to load for {}", GetEditorID()),
				ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
				3.0f);
			return;
		}
	} else {
		settings = vanillaSettings;
	}
	LoadFeatureSettings();
}

void WeatherWidget::SaveSettings()
{
	SaveFeatureSettings();

	try {
		js = settings;

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

	auto temp = std::find_if(widgets.begin(), widgets.end(), [&](const auto& w) { return w->GetEditorID() == settings.parent; });
	if (temp != widgets.end())
		return (WeatherWidget*)temp->get();

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
	uint32_t disabledBits = 0;
	for (size_t i = 0; i < TESWeather::kTotalLayers; i++) {
		auto& settingsCloud = settings.clouds[i];

		weather->cloudLayerSpeedX[i] = (int8_t)settingsCloud.cloudLayerSpeedX;
		weather->cloudLayerSpeedY[i] = (int8_t)settingsCloud.cloudLayerSpeedY;

		if (!settingsCloud.enabled) {
			disabledBits |= (1 << i);
		}

		auto& cloudColors = weather->cloudColorData[i];
		auto& cloudAlphas = weather->cloudAlpha[i];

		for (int j = 0; j < ColorTimes::kTotal; j++) {
			cloudAlphas[j] = settingsCloud.cloudAlpha[j];
			Float3ToColor(settingsCloud.color[j], cloudColors[j]);
		}
	}
	weather->cloudLayerDisabledBits = disabledBits;
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
		settingsCloud.enabled = !(weather->cloudLayerDisabledBits & (1 << i));
		settingsCloud.texturePath = weather->cloudTextures[i].textureName.c_str();

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
	auto editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();
	WeatherWidget* parentWidget = hasParent ? GetParent() : nullptr;

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

		// Parent values
		float3 parentSpecular[4] = {};
		float parentFresnel[4] = {};
		float3 parentDirXMax[4] = {}, parentDirXMin[4] = {};
		float3 parentDirYMax[4] = {}, parentDirYMin[4] = {};
		float3 parentDirZMax[4] = {}, parentDirZMin[4] = {};

		for (int i = 0; i < ColorTimes::kTotal; i++) {
			specularColors[i] = settings.dalc[i].specular;
			fresnelPowers[i] = settings.dalc[i].fresnelPower;
			directionalXMax[i] = settings.dalc[i].directional[0].max;
			directionalXMin[i] = settings.dalc[i].directional[0].min;
			directionalYMax[i] = settings.dalc[i].directional[1].max;
			directionalYMin[i] = settings.dalc[i].directional[1].min;
			directionalZMax[i] = settings.dalc[i].directional[2].max;
			directionalZMin[i] = settings.dalc[i].directional[2].min;

			if (parentWidget) {
				parentSpecular[i] = parentWidget->settings.dalc[i].specular;
				parentFresnel[i] = parentWidget->settings.dalc[i].fresnelPower;
				parentDirXMax[i] = parentWidget->settings.dalc[i].directional[0].max;
				parentDirXMin[i] = parentWidget->settings.dalc[i].directional[0].min;
				parentDirYMax[i] = parentWidget->settings.dalc[i].directional[1].max;
				parentDirYMin[i] = parentWidget->settings.dalc[i].directional[1].min;
				parentDirZMax[i] = parentWidget->settings.dalc[i].directional[2].max;
				parentDirZMin[i] = parentWidget->settings.dalc[i].directional[2].min;
			}
		}

		// Draw with per-parameter inheritance
		if (hasParent) {
			if (TOD::DrawTODColorRow("Specular", specularColors, settings.inheritFlags["DALC_Specular"], parentSpecular)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].specular = specularColors[i];
				changed = true;
			}

			if (TOD::DrawTODFloatRow("Fresnel Power", fresnelPowers, settings.inheritFlags["DALC_Fresnel"], parentFresnel, 0.0f, 10.0f)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].fresnelPower = fresnelPowers[i];
				changed = true;
			}
		} else {
			if (TOD::DrawTODColorRow("Specular", specularColors)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].specular = specularColors[i];
				changed = true;
			}

			if (TOD::DrawTODFloatRow("Fresnel Power", fresnelPowers, 0.0f, 10.0f)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].fresnelPower = fresnelPowers[i];
				changed = true;
			}
		}

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();

		// Directional colors with per-parameter inheritance
		if (hasParent) {
			if (TOD::DrawTODColorRow("Directional +X", directionalXMax, settings.inheritFlags["DALC_DirXMax"], parentDirXMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].max = directionalXMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional -X", directionalXMin, settings.inheritFlags["DALC_DirXMin"], parentDirXMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].min = directionalXMin[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional +Y", directionalYMax, settings.inheritFlags["DALC_DirYMax"], parentDirYMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].max = directionalYMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional -Y", directionalYMin, settings.inheritFlags["DALC_DirYMin"], parentDirYMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].min = directionalYMin[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional +Z", directionalZMax, settings.inheritFlags["DALC_DirZMax"], parentDirZMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].max = directionalZMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional -Z", directionalZMin, settings.inheritFlags["DALC_DirZMin"], parentDirZMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].min = directionalZMin[i];
				changed = true;
			}
		} else {
			if (TOD::DrawTODColorRow("Directional +X", directionalXMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].max = directionalXMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional -X", directionalXMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].min = directionalXMin[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional +Y", directionalYMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].max = directionalYMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional -Y", directionalYMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].min = directionalYMin[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional +Z", directionalZMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].max = directionalZMax[i];
				changed = true;
			}

			if (TOD::DrawTODColorRow("Directional -Z", directionalZMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].min = directionalZMin[i];
				changed = true;
			}
		}

		TOD::EndTODTable();
	}
	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void WeatherWidget::DrawWeatherColorSettings()
{
	auto editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();
	WeatherWidget* parentWidget = hasParent ? GetParent() : nullptr;

	bool changed = false;

	if (TOD::BeginTODTable("AtmosphereColors_Table")) {
		TOD::RenderTODHeader();

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();

		// Organized display order: group related sky/fog/lighting properties
		static const int displayOrder[] = {
			0,   // Sky Upper
			7,   // Sky Lower
			8,   // Horizon
			1,   // Fog Near
			12,  // Fog Far
			3,   // Ambient
			4,   // Sunlight
			5,   // Sun
			6,   // Stars
			9,   // Effect Lighting
			10,  // Cloud LOD Diffuse
			11,  // Cloud LOD Ambient
			13,  // Sky Statics
			14,  // Water Multiplier
			15,  // Sun Glare
			16,  // Moon Glare
			2,   // Unknown
		};

		for (int idx = 0; idx < ColorTypes::kTotal; idx++) {
			int i = displayOrder[idx];
			std::string colorTypeLabel = ColorTypeLabel(i);

			if (hasParent) {
				float3 parentColors[4];
				for (int j = 0; j < 4; j++)
					parentColors[j] = parentWidget->settings.atmosphereColors[i].colorTimes[j];

				std::string inheritKey = "Atmosphere_" + colorTypeLabel;
				if (TOD::DrawTODColorRow(colorTypeLabel.c_str(), settings.atmosphereColors[i].colorTimes, settings.inheritFlags[inheritKey], parentColors)) {
					changed = true;
				}
			} else {
				if (TOD::DrawTODColorRow(colorTypeLabel.c_str(), settings.atmosphereColors[i].colorTimes)) {
					changed = true;
				}
			}
		}

		TOD::EndTODTable();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void WeatherWidget::DrawCloudSettings()
{
	auto editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();
	WeatherWidget* parentWidget = hasParent ? GetParent() : nullptr;

	bool changed = false;
	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		std::string layer = std::format("Layer {}", i);

		// Default to open if this layer has a texture (is being used)
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
		if (!settings.clouds[i].texturePath.empty()) {
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}

		if (ImGui::CollapsingHeader(layer.c_str(), flags)) {
			ImGui::Indent(10.0f);
			ImGui::Spacing();

			bool layerEnabled = settings.clouds[i].enabled;

			// Begin horizontal layout for enable checkbox and sliders on left, texture on right
			ImGui::BeginGroup();

			if (ImGui::Checkbox(std::format("Enable##{}", layer).c_str(), &layerEnabled)) {
				settings.clouds[i].enabled = layerEnabled;
				changed = true;
			}

			ImGui::Spacing();
			ImGui::Spacing();

			// Make sliders 1/3 width
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
			if (WeatherUtils::DrawSliderInt8(std::format("Cloud Layer Speed Y##{}", layer), settings.clouds[i].cloudLayerSpeedY))
				changed = true;
			ImGui::Spacing();
			if (WeatherUtils::DrawSliderInt8(std::format("Cloud Layer Speed X##{}", layer), settings.clouds[i].cloudLayerSpeedX))
				changed = true;
			ImGui::PopItemWidth();

			ImGui::EndGroup();

			// Draw texture in upper right if available
			if (!settings.clouds[i].texturePath.empty()) {
				auto* texture = GetCloudTexture(i);
				if (texture) {
					ImGui::SameLine(0.0f, 20.0f);
					ImGui::BeginGroup();
					float textureSize = 128.0f;
					ImGui::Image((void*)texture, ImVec2(textureSize, textureSize));
					// Small grey subtext below image
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					ImGui::PushFont(ImGui::GetFont());
					ImGui::SetWindowFontScale(0.8f);
					float textWidth = ImGui::CalcTextSize(settings.clouds[i].texturePath.c_str()).x;
					if (textWidth > textureSize) {
						ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textureSize);
						ImGui::TextWrapped("%s", settings.clouds[i].texturePath.c_str());
						ImGui::PopTextWrapPos();
					} else {
						ImGui::Text("%s", settings.clouds[i].texturePath.c_str());
					}
					ImGui::SetWindowFontScale(1.0f);
					ImGui::PopFont();
					ImGui::PopStyleColor();
					ImGui::EndGroup();
				}
			}

			ImGui::Spacing();
			ImGui::Spacing();
			if (TOD::BeginTODTable((layer + "_TOD_Table").c_str())) {
				TOD::RenderTODHeader();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Separator();
				ImGui::TableSetColumnIndex(1);
				ImGui::Separator();

				if (hasParent) {
					float3 parentColors[4];
					float parentAlphas[4];
					for (int j = 0; j < 4; j++) {
						parentColors[j] = parentWidget->settings.clouds[i].color[j];
						parentAlphas[j] = parentWidget->settings.clouds[i].cloudAlpha[j];
					}

					std::string colorKey = std::format("Cloud{}_Color", i);
					std::string alphaKey = std::format("Cloud{}_Alpha", i);

					if (TOD::DrawTODColorRow("Cloud Color", settings.clouds[i].color, settings.inheritFlags[colorKey], parentColors)) {
						changed = true;
					}

					if (TOD::DrawTODFloatRow("Cloud Alpha", settings.clouds[i].cloudAlpha, settings.inheritFlags[alphaKey], parentAlphas, 0.0f, 1.0f)) {
						changed = true;
					}
				} else {
					if (TOD::DrawTODColorRow("Cloud Color", settings.clouds[i].color)) {
						changed = true;
					}

					if (TOD::DrawTODFloatRow("Cloud Alpha", settings.clouds[i].cloudAlpha, 0.0f, 1.0f)) {
						changed = true;
					}
				}

				TOD::EndTODTable();
			}

			ImGui::Spacing();
			ImGui::Unindent(10.0f);
		}
	}
	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void WeatherWidget::DrawFogSettings()
{
	auto editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();
	WeatherWidget* parentWidget = hasParent ? GetParent() : nullptr;

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
		if (hasParent) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
			ImGui::Checkbox("##FogNear", &settings.inheritFlags["Fog_Near"]);
			if (settings.inheritFlags["Fog_Near"]) {
				settings.fogProperties["Day Near"] = parentWidget->settings.fogProperties["Day Near"];
				settings.fogProperties["Night Near"] = parentWidget->settings.fogProperties["Night Near"];
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
		}
		ImGui::Text("Near");
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##FogDayNear", &settings.fogProperties["Day Near"], 0.0f, 1000000.0f, "%.0f"))
			changed = true;
		ImGui::TableSetColumnIndex(2);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##FogNightNear", &settings.fogProperties["Night Near"], 0.0f, 1000000.0f, "%.0f"))
			changed = true;

		// Far
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (hasParent) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
			ImGui::Checkbox("##FogFar", &settings.inheritFlags["Fog_Far"]);
			if (settings.inheritFlags["Fog_Far"]) {
				settings.fogProperties["Day Far"] = parentWidget->settings.fogProperties["Day Far"];
				settings.fogProperties["Night Far"] = parentWidget->settings.fogProperties["Night Far"];
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
		}
		ImGui::Text("Far");
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##FogDayFar", &settings.fogProperties["Day Far"], 0.0f, 1000000.0f, "%.0f"))
			changed = true;
		ImGui::TableSetColumnIndex(2);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##FogNightFar", &settings.fogProperties["Night Far"], 0.0f, 1000000.0f, "%.0f"))
			changed = true;

		// Power
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (hasParent) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
			ImGui::Checkbox("##FogPower", &settings.inheritFlags["Fog_Power"]);
			if (settings.inheritFlags["Fog_Power"]) {
				settings.fogProperties["Day Power"] = parentWidget->settings.fogProperties["Day Power"];
				settings.fogProperties["Night Power"] = parentWidget->settings.fogProperties["Night Power"];
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
		}
		ImGui::Text("Power");
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##FogDayPower", &settings.fogProperties["Day Power"], 0.0f, 10.0f, "%.3f"))
			changed = true;
		ImGui::TableSetColumnIndex(2);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##FogNightPower", &settings.fogProperties["Night Power"], 0.0f, 10.0f, "%.3f"))
			changed = true;

		// Max
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (hasParent) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
			ImGui::Checkbox("##FogMax", &settings.inheritFlags["Fog_Max"]);
			if (settings.inheritFlags["Fog_Max"]) {
				settings.fogProperties["Day Max"] = parentWidget->settings.fogProperties["Day Max"];
				settings.fogProperties["Night Max"] = parentWidget->settings.fogProperties["Night Max"];
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
			ImGui::SameLine();
		}
		ImGui::Text("Max");
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##FogDayMax", &settings.fogProperties["Day Max"], 0.0f, 1.0f, "%.3f"))
			changed = true;
		ImGui::TableSetColumnIndex(2);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##FogNightMax", &settings.fogProperties["Night Max"], 0.0f, 1.0f, "%.3f"))
			changed = true;

		ImGui::EndTable();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void WeatherWidget::DrawProperties(std::string category, std::map<std::string, int> properties)
{
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

	ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", category.c_str());

	bool changed = false;
	auto* editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();

	for (auto& p : properties) {
		// Filter individual properties based on search
		if (searchBuffer[0] != '\0' && !MatchesSearch(p.first)) {
			continue;
		}

		// Apply highlight effect if this setting should be highlighted
		if (ShouldHighlight(p.first)) {
			float elapsed = static_cast<float>(ImGui::GetTime()) - highlightStartTime;
			float alpha = 0.3f * (1.0f - std::abs(elapsed - 0.5f) * 2.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.6f, 1.0f, alpha));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.4f, 0.7f, 1.0f, alpha));
		}

		// Inherit checkbox
		if (hasParent) {
			bool& inheritFlag = settings.inheritFlags[p.first];
			if (ImGui::Checkbox(("##inherit_" + p.first).c_str(), &inheritFlag)) {
				if (inheritFlag) {
					InheritFromParent(p.first);
					changed = true;
				}
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(inheritFlag ? "Inheriting from parent" : "Inherit from parent");
			}
			ImGui::SameLine();
		}

		switch (p.second) {
		case 0:
			if (WeatherUtils::DrawSliderInt8(p.first, settings.weatherProperties[p.first]))
				changed = true;
			break;
		case 1:
			if (WeatherUtils::DrawColorEdit(p.first, settings.weatherColors[p.first]))
				changed = true;
			break;
		case 2:
			if (WeatherUtils::DrawSliderUint8(p.first, settings.weatherProperties[p.first]))
				changed = true;
			break;
		case 3:
			if (WeatherUtils::DrawSliderFloat(p.first, settings.fogProperties[p.first]))
				changed = true;
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

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

void WeatherWidget::InheritFromParent(const std::string& property)
{
	if (!HasParent())
		return;

	WeatherWidget* parentWidget = GetParent();
	if (!parentWidget)
		return;

	if (settings.weatherProperties.find(property) != settings.weatherProperties.end()) {
		settings.weatherProperties[property] = parentWidget->settings.weatherProperties[property];
	} else if (settings.weatherColors.find(property) != settings.weatherColors.end()) {
		settings.weatherColors[property] = parentWidget->settings.weatherColors[property];
	} else if (settings.fogProperties.find(property) != settings.fogProperties.end()) {
		settings.fogProperties[property] = parentWidget->settings.fogProperties[property];
	}
}

void WeatherWidget::InheritAllFromParent()
{
	if (!HasParent())
		return;

	WeatherWidget* parentWidget = GetParent();
	if (!parentWidget)
		return;

	// Copy all weather properties
	for (const auto& [key, value] : parentWidget->settings.weatherProperties) {
		settings.weatherProperties[key] = value;
	}

	// Copy all weather colors
	for (const auto& [key, value] : parentWidget->settings.weatherColors) {
		settings.weatherColors[key] = value;
	}

	// Copy all fog properties
	for (const auto& [key, value] : parentWidget->settings.fogProperties) {
		settings.fogProperties[key] = value;
	}

	// Copy atmosphere colors
	for (int i = 0; i < ColorTypes::kTotal; i++) {
		settings.atmosphereColors[i] = parentWidget->settings.atmosphereColors[i];
	}

	// Copy DALC settings
	for (int i = 0; i < ColorTimes::kTotal; i++) {
		settings.dalc[i] = parentWidget->settings.dalc[i];
	}

	// Copy cloud settings
	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		settings.clouds[i] = parentWidget->settings.clouds[i];
	}

	// Set all inherit flags to true
	settings.inheritFlags["DALC_Specular"] = true;
	settings.inheritFlags["DALC_Fresnel"] = true;
	settings.inheritFlags["DALC_DirXMax"] = true;
	settings.inheritFlags["DALC_DirXMin"] = true;
	settings.inheritFlags["DALC_DirYMax"] = true;
	settings.inheritFlags["DALC_DirYMin"] = true;
	settings.inheritFlags["DALC_DirZMax"] = true;
	settings.inheritFlags["DALC_DirZMin"] = true;

	settings.inheritFlags["Fog_Near"] = true;
	settings.inheritFlags["Fog_Far"] = true;
	settings.inheritFlags["Fog_Power"] = true;
	settings.inheritFlags["Fog_Max"] = true;

	// Atmosphere colors
	static const int displayOrder[] = { 0, 7, 8, 1, 12, 3, 4, 5, 6, 9, 10, 11, 13, 14, 15, 16, 2 };
	for (int idx = 0; idx < ColorTypes::kTotal; idx++) {
		int i = displayOrder[idx];
		std::string colorTypeLabel = ColorTypeLabel(i);
		settings.inheritFlags["Atmosphere_" + colorTypeLabel] = true;
	}

	// Cloud settings
	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		settings.inheritFlags[std::format("Cloud{}_Color", i)] = true;
		settings.inheritFlags[std::format("Cloud{}_Alpha", i)] = true;
	}

	// Apply the changes
	if (EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}

	EditorWindow::GetSingleton()->ShowNotification(
		std::format("Inherited all settings from {}", parentWidget->GetEditorID()),
		ImVec4(0.0f, 1.0f, 0.5f, 1.0f),
		3.0f);
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
}

void WeatherWidget::RevertChanges()
{
	LoadWeatherValues();
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
			ImGui::TextWrapped(
				"Note: Feature settings should be configured through the feature's own settings panel. "
				"This section shows which features have per-weather overrides.");

			ImGui::TreePop();
		}
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

	ID3D11ShaderResourceView* WeatherWidget::GetCloudTexture(int layerIndex)
	{
		if (cloudTextureCache.contains(layerIndex)) {
			return cloudTextureCache[layerIndex];
		}

		const auto& texturePath = settings.clouds[layerIndex].texturePath;
		if (texturePath.empty()) {
			return nullptr;
		}

		// Build resource path for BSA loading: Textures\path (relative to Data folder)
		// Note: Skyrim texture paths don't include .dds extension, so we add it
		std::string resourcePath = std::string("Textures\\") + texturePath;

		// Add .dds extension if not present
		if (resourcePath.size() < 4 || resourcePath.substr(resourcePath.size() - 4) != ".dds") {
			resourcePath += ".dds";
		}

		ID3D11ShaderResourceView* srv = nullptr;
		ImVec2 textureSize;

		if (Util::LoadDDSTextureFromFile(globals::d3d::device, resourcePath.c_str(), &srv, textureSize)) {
			cloudTextureCache[layerIndex] = srv;
			return srv;
		}

		return nullptr;
	}
