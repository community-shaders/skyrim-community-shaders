#include "WeatherWidget.h"

#include <format>

#include "imgui_internal.h"

#include "../EditorWindow.h"
#include "FeatureIssues.h"
#include "State.h"
#include "Utils/UI.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Atmosphere, colorTimes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DirectionalColor, max, min)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::DALC, specular, fresnelPower, directional)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Cloud, cloudLayerSpeedY, cloudLayerSpeedX, color, cloudAlpha, enabled, texturePath)

namespace
{
	namespace WeatherTab
	{
		constexpr const char* kBasic = "Basic";
		constexpr const char* kDalc = "Lighting (DALC)";
		constexpr const char* kAtmosphere = "Atmosphere Colors";
		constexpr const char* kClouds = "Clouds";
		constexpr const char* kFog = "Fog";
		constexpr const char* kRecords = "Records";
	}

	namespace WeatherSetting
	{
		constexpr const char* kSpecular = "Specular";
		constexpr const char* kFresnelPower = "Fresnel Power";
		constexpr const char* kDirectionalXMax = "Directional X Max";
		constexpr const char* kDirectionalXMin = "Directional X Min";
		constexpr const char* kDirectionalYMax = "Directional Y Max";
		constexpr const char* kDirectionalYMin = "Directional Y Min";
		constexpr const char* kDirectionalZMax = "Directional Z Max";
		constexpr const char* kDirectionalZMin = "Directional Z Min";
		constexpr const char* kDayNear = "Day Near";
		constexpr const char* kDayFar = "Day Far";
		constexpr const char* kDayPower = "Day Power";
		constexpr const char* kDayMax = "Day Max";
		constexpr const char* kNightNear = "Night Near";
		constexpr const char* kNightFar = "Night Far";
		constexpr const char* kNightPower = "Night Power";
		constexpr const char* kNightMax = "Night Max";
	}

	namespace WeatherDisplay
	{
		constexpr const char* kDirectionalXMax = "Directional +X";
		constexpr const char* kDirectionalXMin = "Directional -X";
		constexpr const char* kDirectionalYMax = "Directional +Y";
		constexpr const char* kDirectionalYMin = "Directional -Y";
		constexpr const char* kDirectionalZMax = "Directional +Z";
		constexpr const char* kDirectionalZMin = "Directional -Z";
	}

	namespace WeatherRecord
	{
		constexpr const char* kImageSpace = "ImageSpace";
		constexpr const char* kVolumetricLighting = "Volumetric Lighting";
		constexpr const char* kPrecipitation = "Precipitation";
		constexpr const char* kVisualEffect = "Visual Effect";
		constexpr int kImageSpaceIdOffset = 0;
		constexpr int kVolumetricLightingIdOffset = 100;
	}

	namespace WeatherInherit
	{
		constexpr const char* kDalcSpecular = "DALC_Specular";
		constexpr const char* kDalcFresnel = "DALC_Fresnel";
		constexpr const char* kDalcDirXMax = "DALC_DirXMax";
		constexpr const char* kDalcDirXMin = "DALC_DirXMin";
		constexpr const char* kDalcDirYMax = "DALC_DirYMax";
		constexpr const char* kDalcDirYMin = "DALC_DirYMin";
		constexpr const char* kDalcDirZMax = "DALC_DirZMax";
		constexpr const char* kDalcDirZMin = "DALC_DirZMin";
		constexpr const char* kFogNear = "Fog_Near";
		constexpr const char* kFogFar = "Fog_Far";
		constexpr const char* kFogPower = "Fog_Power";
		constexpr const char* kFogMax = "Fog_Max";
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeatherWidget::Settings,
	parent,
	inheritFlags,
	weatherProperties,
	weatherColors,
	fogProperties,
	atmosphereColors,
	dalc,
	clouds,
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
	WeatherUtils::SetCurrentWidget(this);
	const float scale = Util::GetUIScale();
	if (BeginWidgetWindow()) {
		// Draw header with search and all buttons
		DrawWidgetHeader("##WeatherSearch", false, true, true, weather);
		DrawSearchDropdown();

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
				if (Util::ButtonWithFlash("Inherit All")) {
					InheritAllFromParent();
				}
				Util::AddTooltip("Copy all parameter values from parent weather");

				if (!parent->IsOpen()) {
					ImGui::SameLine();
					if (Util::ButtonWithFlash("Open"))
						parent->SetOpen(true);
				}
			}
		}
	}

	// Tab bar for organizing settings
	if (ImGui::BeginTabBar("WeatherSettingsTabs", ImGuiTabBarFlags_None)) {
		const ImGuiTabItemFlags basicFlags = GetTabFlagsForOverride(WeatherTab::kBasic);
		const ImGuiTabItemFlags dalcFlags = GetTabFlagsForOverride(WeatherTab::kDalc);
		const ImGuiTabItemFlags atmosphereFlags = GetTabFlagsForOverride(WeatherTab::kAtmosphere);
		const ImGuiTabItemFlags cloudsFlags = GetTabFlagsForOverride(WeatherTab::kClouds);
		const ImGuiTabItemFlags fogFlags = GetTabFlagsForOverride(WeatherTab::kFog);
		const ImGuiTabItemFlags featuresFlags = GetTabFlagsForOverride("Features");
		const ImGuiTabItemFlags recordsFlags = GetTabFlagsForOverride(WeatherTab::kRecords);

		if (ImGui::BeginTabItem(WeatherTab::kBasic, nullptr, basicFlags)) {
			BeginScrollableContent("##BasicScroll");
			DrawProperties("Sun", { { "Sun Damage", UINT8_SLIDER } });
			DrawProperties("Wind", { { "Wind Speed", UINT8_SLIDER }, { "Wind Direction", UINT8_SLIDER }, { "Wind Direction Range", UINT8_SLIDER } });
			DrawProperties("Precipitation", { { "Precipitation Begin Fade In", UINT8_SLIDER }, { "Precipitation End Fade Out", UINT8_SLIDER } });
			DrawProperties("Lightning", { { "Thunder Lightning Begin Fade In", UINT8_SLIDER }, { "Thunder Lightning End Fade Out", UINT8_SLIDER },
											{ "Thunder Lightning Frequency", UINT8_SLIDER }, { "Lightning Color", COLOR3_PICKER } });
			DrawProperties("Visual Effects", { { "Visual Effect Begin", UINT8_SLIDER }, { "Visual Effect End", UINT8_SLIDER } });
			DrawProperties("Weather Transition", { { "Trans Delta", UINT8_SLIDER } });
			EndScrollableContent();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(WeatherTab::kDalc, nullptr, dalcFlags)) {
			BeginScrollableContent("##DALCScroll");
			DrawDALCSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(WeatherTab::kAtmosphere, nullptr, atmosphereFlags)) {
			BeginScrollableContent("##AtmosphereScroll");
			DrawWeatherColorSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(WeatherTab::kClouds, nullptr, cloudsFlags)) {
			BeginScrollableContent("##CloudsScroll");
			DrawCloudSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(WeatherTab::kFog, nullptr, fogFlags)) {
			BeginScrollableContent("##FogScroll");
			DrawFogSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Features", nullptr, featuresFlags)) {
			BeginScrollableContent("##FeaturesScroll");
			DrawFeatureSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(WeatherTab::kRecords, nullptr, recordsFlags)) {
			BeginScrollableContent("##RecordsScroll");
			ImGui::Spacing();
			ImGui::TextWrapped("Form record references used by this weather.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			auto* editorWindow = EditorWindow::GetSingleton();

			bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();
			WeatherWidget* parentWidget = hasParent ? GetParent() : nullptr;
			const float todLabelOffset = (hasParent ? 120.0f : 100.0f) * scale;
			const float formLabelOffset = (hasParent ? 170.0f : 150.0f) * scale;
			const float pickerWidth = 225.0f * scale;
			auto makeTimeRecordLabel = [](const char* recordType, int colorTime) {
				return std::format("{} {}", recordType, ColorTimeLabel(colorTime));
			};
			auto anyTimeRecordMatches = [&](const char* recordType) {
				for (int i = 0; i < ColorTimes::kTotal; i++) {
					if (MatchesSearch(makeTimeRecordLabel(recordType, i)))
						return true;
				}
				return false;
			};
			auto pushRecordHighlight = [&](const std::string& recordId) {
				return PushHighlightIfNeeded(recordId);
			};
			auto drawOpenButton = [](auto& recordRef, auto& widgets, const std::string& buttonId, const char* tooltip) {
				if (!recordRef)
					return;
				ImGui::SameLine();
				if (ImGui::SmallButton(buttonId.c_str())) {
					for (auto& widget : widgets) {
						if (widget->form == recordRef) {
							widget->SetOpen(true);
							break;
						}
					}
				}
				Util::AddTooltip(tooltip);
			};
			auto drawInheritCheckbox = [&](const std::string& inheritKey, auto& recordRef, auto& parentRef) {
				if (!hasParent)
					return;
				bool& inheritFlag = settings.inheritFlags[inheritKey];
				ImGui::Checkbox(("##inherit_" + inheritKey).c_str(), &inheritFlag);
				if (inheritFlag && parentWidget && recordRef != parentRef) {
					recordRef = parentRef;
					pendingReinit = true;
				}
				Util::AddTooltip(inheritFlag ? "Inheriting from parent" : "Inherit from parent");
				ImGui::SameLine();
			};
			auto drawTimeRecordSection = [&](const char* sectionLabel, int idOffset, const char* inheritPrefix, auto& recordRefs, auto& parentRefs, auto& widgets, const char* pickerId, const char* openTooltip) {
				if (!anyTimeRecordMatches(sectionLabel))
					return;
				if (ShouldOpenSearchSection())
					ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				if (!ImGui::CollapsingHeader(sectionLabel, ImGuiTreeNodeFlags_DefaultOpen))
					return;
				for (int i = 0; i < ColorTimes::kTotal; i++) {
					std::string rowId = makeTimeRecordLabel(sectionLabel, i);
					if (!MatchesSearch(rowId))
						continue;

					ImGui::PushID(idOffset + i);
					std::string label = ColorTimeLabel(i);
					std::string inheritKey = std::format("{}_{}", inheritPrefix, i);
					const bool recordHighlighted = pushRecordHighlight(rowId);

					drawInheritCheckbox(inheritKey, recordRefs[i], parentRefs[i]);
					ImGui::Text("%s:", label.c_str());
					ImGui::SameLine(todLabelOffset);
					if (WeatherUtils::DrawFormPickerCached(pickerId, recordRefs[i], widgets, false, true, pickerWidth)) {
						pendingReinit = true;
					}
					drawOpenButton(recordRefs[i], widgets, std::format("Open##{}", i), openTooltip);

					if (recordHighlighted)
						PopHighlightIfNeeded(rowId, recordHighlighted);
					ImGui::PopID();
				}
				ImGui::Spacing();
			};
			auto drawSingleRecordSection = [&](const char* sectionLabel, const char* recordId, const char* inheritKey, const char* valueLabel, const char* pickerId, auto& recordRef, auto& parentRef, auto& widgets, const char* buttonId, const char* openTooltip) {
				if (!MatchesSearch(recordId))
					return;
				if (ShouldOpenSearchSection())
					ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				if (!ImGui::CollapsingHeader(sectionLabel, ImGuiTreeNodeFlags_DefaultOpen))
					return;
				const bool recordHighlighted = pushRecordHighlight(recordId);

				drawInheritCheckbox(inheritKey, recordRef, parentRef);
				ImGui::Text("%s:", valueLabel);
				ImGui::SameLine(formLabelOffset);
				if (WeatherUtils::DrawFormPickerCached(pickerId, recordRef, widgets, false, true, pickerWidth)) {
					pendingReinit = true;
				}
				drawOpenButton(recordRef, widgets, buttonId, openTooltip);

				if (recordHighlighted)
					PopHighlightIfNeeded(recordId, recordHighlighted);
				ImGui::Spacing();
			};

			auto* parentImageSpaceRefs = parentWidget ? parentWidget->settings.imageSpaceRefs : settings.imageSpaceRefs;
			auto* parentVolumetricLightingRefs = parentWidget ? parentWidget->settings.volumetricLightingRefs : settings.volumetricLightingRefs;
			auto* parentPrecipitationData = parentWidget ? parentWidget->settings.precipitationData : settings.precipitationData;
			auto* parentReferenceEffect = parentWidget ? parentWidget->settings.referenceEffect : settings.referenceEffect;
			drawTimeRecordSection(WeatherRecord::kImageSpace, WeatherRecord::kImageSpaceIdOffset, WeatherRecord::kImageSpace, settings.imageSpaceRefs, parentImageSpaceRefs, editorWindow->imageSpaceWidgets, "##ImageSpace", "Open this ImageSpace for editing");
			drawTimeRecordSection(WeatherRecord::kVolumetricLighting, WeatherRecord::kVolumetricLightingIdOffset, "VolumetricLighting", settings.volumetricLightingRefs, parentVolumetricLightingRefs, editorWindow->volumetricLightingWidgets, "##VolumetricLighting", "Open this Volumetric Lighting for editing");
			drawSingleRecordSection(WeatherRecord::kPrecipitation, WeatherRecord::kPrecipitation, WeatherRecord::kPrecipitation, "Particle Shader", "##Precipitation", settings.precipitationData, parentPrecipitationData, editorWindow->precipitationWidgets, "Open##Precip", "Open this Precipitation for editing");
			drawSingleRecordSection(WeatherRecord::kVisualEffect, WeatherRecord::kVisualEffect, "ReferenceEffect", WeatherRecord::kVisualEffect, "##ReferenceEffect", settings.referenceEffect, parentReferenceEffect, editorWindow->referenceEffectWidgets, "Open##RefEffect", "Open this Visual Effect for editing");

			if (pendingReinit) {
				ApplyChanges();
			}

			EndScrollableContent();
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
				settings = vanillaSettings;
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

			// Record form references (resolved by matching widget EditorID)
			// Three cases: key missing -> vanilla, key present with "" -> explicit None (nullptr), key present with id -> lookup form
			auto* editorWindow = EditorWindow::GetSingleton();
			auto loadRef = [&]<typename T>(const std::string& key, const std::vector<std::unique_ptr<Widget>>& widgets, T* vanillaValue) -> T* {
				if (!js.contains(key) || !js[key].is_string())
					return vanillaValue;
				const std::string id = js[key].get<std::string>();
				if (id.empty())
					return nullptr;
				auto* f = WeatherUtils::FindFormByEditorID(id, widgets);
				return f ? static_cast<T*>(f) : vanillaValue;
			};
			for (size_t i = 0; i < ColorTimes::kTotal; i++) {
				settings.imageSpaceRefs[i] = loadRef(std::format("imageSpaceRef_{}", i), editorWindow->imageSpaceWidgets, vanillaSettings.imageSpaceRefs[i]);
				settings.volumetricLightingRefs[i] = loadRef(std::format("volumetricLightingRef_{}", i), editorWindow->volumetricLightingWidgets, vanillaSettings.volumetricLightingRefs[i]);
			}
			settings.precipitationData = loadRef("precipitationDataRef", editorWindow->precipitationWidgets, vanillaSettings.precipitationData);
			settings.referenceEffect = loadRef("referenceEffectRef", editorWindow->referenceEffectWidgets, vanillaSettings.referenceEffect);

		} catch (const nlohmann::json::exception& e) {
			logger::error("Weather {}: Failed to deserialize settings from JSON: {}", GetEditorID(), e.what());
			// Fallback to vanilla/game values on exception
			settings = vanillaSettings;
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Some values failed to load for {}", GetEditorID()),
				ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
				3.0f);
			return;
		}
	} else {
		settings = vanillaSettings;
	}
	InitializeInheritFlags();
	if (!js.empty()) {
		LoadFeatureSettings();
	}
	originalSettings = settings;
	pendingReinit = true;
	ApplyChanges();
}

void WeatherWidget::SaveSettings()
{
	SaveFeatureSettings();

	try {
		js = settings;

		// Record form references (serialized as widget EditorIDs for load-order independence)
		auto* editorWindow = EditorWindow::GetSingleton();
		for (size_t i = 0; i < ColorTimes::kTotal; i++) {
			js[std::format("imageSpaceRef_{}", i)] = WeatherUtils::FindEditorIDByForm(settings.imageSpaceRefs[i], editorWindow->imageSpaceWidgets);
			js[std::format("volumetricLightingRef_{}", i)] = WeatherUtils::FindEditorIDByForm(settings.volumetricLightingRefs[i], editorWindow->volumetricLightingWidgets);
		}
		js["precipitationDataRef"] = WeatherUtils::FindEditorIDByForm(settings.precipitationData, editorWindow->precipitationWidgets);
		js["referenceEffectRef"] = WeatherUtils::FindEditorIDByForm(settings.referenceEffect, editorWindow->referenceEffectWidgets);

		if (js.is_null()) {
			logger::error("Weather {}: Serialization produced null JSON!", GetEditorID());
		} else if (!js.contains("weatherProperties")) {
			logger::error("Weather {}: Serialized JSON missing weatherProperties field!", GetEditorID());
		} else if (!js.contains("atmosphereColors")) {
			logger::error("Weather {}: Serialized JSON missing atmosphereColors field!", GetEditorID());
		} else if (!js.contains("clouds")) {
			logger::error("Weather {}: Serialized JSON missing clouds field!", GetEditorID());
		} else {
			originalSettings = settings;
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

	weather->data.transDelta = static_cast<uint8_t>(weatherProps["Trans Delta"]);

	// Sun
	data.sunGlare = static_cast<uint8_t>(weatherProps["Sun Glare"]);
	data.sunDamage = static_cast<uint8_t>(weatherProps["Sun Damage"]);

	// Precipitation
	data.precipitationBeginFadeIn = static_cast<uint8_t>(weatherProps["Precipitation Begin Fade In"]);
	data.precipitationEndFadeOut = static_cast<uint8_t>(weatherProps["Precipitation End Fade Out"]);

	// Lightning
	data.thunderLightningBeginFadeIn = static_cast<uint8_t>(weatherProps["Thunder Lightning Begin Fade In"]);
	data.thunderLightningEndFadeOut = static_cast<uint8_t>(weatherProps["Thunder Lightning End Fade Out"]);
	data.thunderLightningFrequency = static_cast<int8_t>(static_cast<uint8_t>(weatherProps["Thunder Lightning Frequency"]));
	Float3ToColor(weatherColors["Lightning Color"], weather->data.lightningColor);

	// Visual Effects
	data.visualEffectBegin = static_cast<uint8_t>(weatherProps["Visual Effect Begin"]);
	data.visualEffectEnd = static_cast<uint8_t>(weatherProps["Visual Effect End"]);

	// Wind
	data.windSpeed = static_cast<uint8_t>(weatherProps["Wind Speed"]);
	data.windDirection = static_cast<uint8_t>(weatherProps["Wind Direction"]);
	data.windDirectionRange = static_cast<uint8_t>(weatherProps["Wind Direction Range"]);

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

		weather->cloudLayerSpeedX[i] = static_cast<int8_t>(settingsCloud.cloudLayerSpeedX);
		weather->cloudLayerSpeedY[i] = static_cast<int8_t>(settingsCloud.cloudLayerSpeedY);

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

	// Record form references
	for (size_t i = 0; i < ColorTimes::kTotal; i++) {
		weather->imageSpaces[i] = settings.imageSpaceRefs[i];
		weather->volumetricLighting[i] = settings.volumetricLightingRefs[i];
	}
	weather->precipitationData = settings.precipitationData;
	weather->referenceEffect = settings.referenceEffect;

	// If this weather is currently active, immediately apply feature settings to game memory
	auto* weatherManager = WeatherManager::GetSingleton();
	if (weatherManager->GetCurrentWeathers().currentWeather == weather) {
		auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
		json emptyWeather;

		for (const auto& [featureName, featureSettings] : settings.featureSettings) {
			if (!featureSettings.value("__enabled", false) || !globalRegistry->HasWeatherSupport(featureName)) {
				continue;
			}

			// Filter out __enabled flag and apply settings
			json filteredSettings = featureSettings;
			filteredSettings.erase("__enabled");
			globalRegistry->UpdateFeatureFromWeathers(featureName, emptyWeather, filteredSettings, 1.0f);
		}
	}
}

void WeatherWidget::InitializeInheritFlags()
{
	auto& flags = settings.inheritFlags;
	auto ensureFlag = [&](const std::string& key) {
		flags.try_emplace(key, false);
	};

	static const char* kFixedKeys[] = {
		"Precipitation",
		"ReferenceEffect",
		WeatherInherit::kDalcSpecular,
		WeatherInherit::kDalcFresnel,
		WeatherInherit::kDalcDirXMax,
		WeatherInherit::kDalcDirXMin,
		WeatherInherit::kDalcDirYMax,
		WeatherInherit::kDalcDirYMin,
		WeatherInherit::kDalcDirZMax,
		WeatherInherit::kDalcDirZMin,
		WeatherInherit::kFogNear,
		WeatherInherit::kFogFar,
		WeatherInherit::kFogPower,
		WeatherInherit::kFogMax,
		"Sun Damage",
		"Wind Speed",
		"Wind Direction",
		"Wind Direction Range",
		"Precipitation Begin Fade In",
		"Precipitation End Fade Out",
		"Thunder Lightning Begin Fade In",
		"Thunder Lightning End Fade Out",
		"Thunder Lightning Frequency",
		"Lightning Color",
		"Visual Effect Begin",
		"Visual Effect End",
		"Trans Delta",
	};
	for (const char* key : kFixedKeys) {
		ensureFlag(key);
	}

	for (int i = 0; i < ColorTimes::kTotal; i++) {
		ensureFlag(std::format("ImageSpace_{}", i));
		ensureFlag(std::format("VolumetricLighting_{}", i));
	}

	for (int i = 0; i < ColorTypes::kTotal; i++) {
		ensureFlag(std::format("Atmosphere_{}", ColorTypeLabel(i)));
	}

	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		ensureFlag(std::format("Cloud{}_Color", i));
		ensureFlag(std::format("Cloud{}_Alpha", i));
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
	weatherProps["Thunder Lightning Frequency"] = static_cast<uint8_t>(data.thunderLightningFrequency);
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
		settingsDalc.fresnelPower = dalc.fresnelPower;

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

	// Record form references
	for (size_t i = 0; i < ColorTimes::kTotal; i++) {
		settings.imageSpaceRefs[i] = weather->imageSpaces[i];
		settings.volumetricLightingRefs[i] = weather->volumetricLighting[i];
	}
	settings.precipitationData = weather->precipitationData;
	settings.referenceEffect = weather->referenceEffect;
}

void WeatherWidget::DrawDALCSettings()
{
	auto editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();
	WeatherWidget* parentWidget = hasParent ? GetParent() : nullptr;

	bool changed = false;

	if (TOD::BeginTODTable("DALC_TOD_Table")) {
		TOD::RenderTODHeader();
		TOD::DrawTODSeparator();

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
		auto drawDalcColor = [&](const char* settingId, const char* label, float3 (&values)[4], bool* inheritFlag = nullptr, float3* parentValues = nullptr) {
			return DrawIfMatchesSearch(settingId, [&](const char*) {
				return DrawWithHighlight(settingId, [&]() {
					return inheritFlag ?
						TOD::DrawTODColorRow(label, values, *inheritFlag, parentValues) :
						TOD::DrawTODColorRow(label, values);
				});
			});
		};
		auto drawDalcFloat = [&](const char* settingId, const char* label, float (&values)[4], bool* inheritFlag = nullptr, float* parentValues = nullptr) {
			return DrawIfMatchesSearch(settingId, [&](const char*) {
				return DrawWithHighlight(settingId, [&]() {
					return inheritFlag ?
						TOD::DrawTODFloatRow(label, values, *inheritFlag, parentValues, 0.0f, 10.0f) :
						TOD::DrawTODFloatRow(label, values, 0.0f, 10.0f);
				});
			});
		};

		if (hasParent && parentWidget) {
			if (drawDalcColor(WeatherSetting::kSpecular, WeatherSetting::kSpecular, specularColors, &settings.inheritFlags[WeatherInherit::kDalcSpecular], parentSpecular)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].specular = specularColors[i];
				changed = true;
			}

			if (drawDalcFloat(WeatherSetting::kFresnelPower, WeatherSetting::kFresnelPower, fresnelPowers, &settings.inheritFlags[WeatherInherit::kDalcFresnel], parentFresnel)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].fresnelPower = fresnelPowers[i];
				changed = true;
			}
		} else {
			if (drawDalcColor(WeatherSetting::kSpecular, WeatherSetting::kSpecular, specularColors)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].specular = specularColors[i];
				changed = true;
			}

			if (drawDalcFloat(WeatherSetting::kFresnelPower, WeatherSetting::kFresnelPower, fresnelPowers)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].fresnelPower = fresnelPowers[i];
				changed = true;
			}
		}

		TOD::DrawTODSeparator();

		// Directional colors with per-parameter inheritance
		if (hasParent && parentWidget) {
			if (drawDalcColor(WeatherSetting::kDirectionalXMax, WeatherDisplay::kDirectionalXMax, directionalXMax, &settings.inheritFlags[WeatherInherit::kDalcDirXMax], parentDirXMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].max = directionalXMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalXMin, WeatherDisplay::kDirectionalXMin, directionalXMin, &settings.inheritFlags[WeatherInherit::kDalcDirXMin], parentDirXMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].min = directionalXMin[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalYMax, WeatherDisplay::kDirectionalYMax, directionalYMax, &settings.inheritFlags[WeatherInherit::kDalcDirYMax], parentDirYMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].max = directionalYMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalYMin, WeatherDisplay::kDirectionalYMin, directionalYMin, &settings.inheritFlags[WeatherInherit::kDalcDirYMin], parentDirYMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].min = directionalYMin[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalZMax, WeatherDisplay::kDirectionalZMax, directionalZMax, &settings.inheritFlags[WeatherInherit::kDalcDirZMax], parentDirZMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].max = directionalZMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalZMin, WeatherDisplay::kDirectionalZMin, directionalZMin, &settings.inheritFlags[WeatherInherit::kDalcDirZMin], parentDirZMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].min = directionalZMin[i];
				changed = true;
			}
		} else {
			if (drawDalcColor(WeatherSetting::kDirectionalXMax, WeatherDisplay::kDirectionalXMax, directionalXMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].max = directionalXMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalXMin, WeatherDisplay::kDirectionalXMin, directionalXMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].min = directionalXMin[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalYMax, WeatherDisplay::kDirectionalYMax, directionalYMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].max = directionalYMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalYMin, WeatherDisplay::kDirectionalYMin, directionalYMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].min = directionalYMin[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalZMax, WeatherDisplay::kDirectionalZMax, directionalZMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].max = directionalZMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalZMin, WeatherDisplay::kDirectionalZMin, directionalZMin)) {
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
		TOD::DrawTODSeparator();

		// Organized display order: group related sky/fog/lighting properties
		static const int displayOrder[] = {
			0,   // Sky Upper
			7,   // Sky Lower
			8,   // Horizon
			1,   // Fog Near
			12,  // Fog Far
			3,   // Ambient
			4,   // Sunlight
			15,  // Sun Glare
			5,   // Sun
			6,   // Stars
			16,  // Moon Glare
			9,   // Effect Lighting
			10,  // Cloud LOD Diffuse
			11,  // Cloud LOD Ambient
			13,  // Sky Statics
			14,  // Water Multiplier
			2,   // Unknown
		};

		for (int idx = 0; idx < ColorTypes::kTotal; idx++) {
			int i = displayOrder[idx];
			std::string colorTypeLabel = ColorTypeLabel(i);

			DrawSearchSectionIfMatches(colorTypeLabel, [&](const char* label) {
				if (DrawWithHighlight(label, [&]() {
						if (hasParent && parentWidget) {
							float3 parentColors[4];
							for (int j = 0; j < 4; j++)
								parentColors[j] = parentWidget->settings.atmosphereColors[i].colorTimes[j];

							std::string inheritKey = "Atmosphere_" + colorTypeLabel;
							return TOD::DrawTODColorRow(label, settings.atmosphereColors[i].colorTimes, settings.inheritFlags[inheritKey], parentColors);
						} else {
							return TOD::DrawTODColorRow(label, settings.atmosphereColors[i].colorTimes);
						}
					})) {
					changed = true;
				}
			});
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
	bool enableChanged = false;

	// OpenOnArrow|OpenOnDoubleClick prevents accidental collapse when clicking
	// the [Enabled] badge area that overlaps the right side of the header.
	constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	constexpr char kEnabledBadge[] = "[Enabled]";

	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		std::string layer = std::format("Layer {}", i);
		std::string layerId = std::format("Cloud {}", layer);
		if (!MatchesSearch(layerId))
			continue;

		bool layerEnabled = settings.clouds[i].enabled;

		if (!layerEnabled) {
			ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
		}

		// Label is constant so the storage ID never changes — open/closed state always persists.
		// [Enabled] badge is overlaid on the header via the draw list instead of altering the label.
		float headerScreenY = ImGui::GetCursorScreenPos().y;
		bool isTarget = IsHighlighted(layerId);
		if (isTarget) {
			ImGui::SetNextItemOpen(true);
			const ImVec4 targetHeaderColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
			ImGui::PushStyleColor(ImGuiCol_Header, targetHeaderColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, targetHeaderColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, targetHeaderColor);
		}
		bool layerOpen = ImGui::CollapsingHeader(layer.c_str(), flags);
		if (isTarget) {
			ImGui::PopStyleColor(3);
			if (scrollToHighlighted) {
				ImGui::SetScrollHereY(0.5f);
				scrollToHighlighted = false;
			}
		}

		if (!layerEnabled)
			ImGui::PopStyleColor(3);

		if (layerEnabled) {
			const ImVec2 badgeSize = ImGui::CalcTextSize(kEnabledBadge);
			const float headerHeight = ImGui::GetFrameHeight();
			const float badgePadding = ImGui::GetStyle().FramePadding.x;
			const ImVec2 badgePos = {
				ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x - badgeSize.x - badgePadding,
				headerScreenY + (headerHeight - badgeSize.y) * 0.5f
			};
			ImGui::GetWindowDrawList()->AddText(badgePos, ImGui::GetColorU32(ImGuiCol_CheckMark), kEnabledBadge);
		}

		if (layerOpen) {
			const float scale = Util::GetUIScale();
			ImGui::Indent(10.0f * scale);
			ImGui::Spacing();

			// Begin horizontal layout for enable checkbox and sliders on left, texture on right
			ImGui::BeginGroup();

			if (ImGui::Checkbox(std::format("Enable##{}", layer).c_str(), &layerEnabled)) {
				settings.clouds[i].enabled = layerEnabled;
				enableChanged = true;
				changed = true;
			}

			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
			if (WeatherUtils::DrawSliderInt8(std::format("Cloud Layer Speed Y##{}", layer), settings.clouds[i].cloudLayerSpeedY))
				changed = true;
			ImGui::Spacing();
			if (WeatherUtils::DrawSliderInt8(std::format("Cloud Layer Speed X##{}", layer), settings.clouds[i].cloudLayerSpeedX))
				changed = true;
			ImGui::PopItemWidth();

			ImGui::EndGroup();

			// Draw texture centered in remaining space to the right of the controls
			if (!settings.clouds[i].texturePath.empty()) {
				auto* texture = GetCloudTexture(i);
				if (texture) {
					float textureSize = 128.0f * scale;
					float groupEndX = ImGui::GetItemRectMax().x;
					float availRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x - groupEndX;
					float offset = std::max(20.0f * scale, (availRight - textureSize) * 0.5f);
					ImGui::SameLine(0.0f, offset);
					ImGui::BeginGroup();
					ImGui::Image((void*)texture, ImVec2(textureSize, textureSize));
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textureSize);
					ImGui::TextWrapped("%s", settings.clouds[i].texturePath.c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
					ImGui::EndGroup();
				}
			}

			ImGui::Spacing();
			ImGui::Spacing();
			if (TOD::BeginTODTable((layer + "_TOD_Table").c_str(), 120.0f)) {
				TOD::RenderTODHeader();
				TOD::DrawTODSeparator();
				auto drawCloudTODRows = [&](auto drawRows) {
					DrawWithHighlight(layerId, [&]() {
						drawRows();
					});
				};

				if (hasParent && parentWidget) {
					float3 parentColors[4];
					float parentAlphas[4];
					for (int j = 0; j < 4; j++) {
						parentColors[j] = parentWidget->settings.clouds[i].color[j];
						parentAlphas[j] = parentWidget->settings.clouds[i].cloudAlpha[j];
					}

					std::string colorKey = std::format("Cloud{}_Color", i);
					std::string alphaKey = std::format("Cloud{}_Alpha", i);

					drawCloudTODRows([&]() {
						if (TOD::DrawTODColorRow("Cloud Color", settings.clouds[i].color, settings.inheritFlags[colorKey], parentColors)) {
							changed = true;
						}

						if (TOD::DrawTODFloatRow("Cloud Alpha", settings.clouds[i].cloudAlpha, settings.inheritFlags[alphaKey], parentAlphas, 0.0f, 1.0f)) {
							changed = true;
						}
					});
				} else {
					drawCloudTODRows([&]() {
						if (TOD::DrawTODColorRow("Cloud Color", settings.clouds[i].color)) {
							changed = true;
						}

						if (TOD::DrawTODFloatRow("Cloud Alpha", settings.clouds[i].cloudAlpha, 0.0f, 1.0f)) {
							changed = true;
						}
					});
				}

				TOD::EndTODTable();
			}

			ImGui::Spacing();
			ImGui::Unindent(10.0f * scale);
		}
	}
	if (enableChanged) {
		// Apply enable/disable immediately for instant feedback, regardless of autoApplyChanges.
		editorWindow->PushUndoState(this);
		pendingReinit = true;
		ApplyChanges();
	} else if (changed && editorWindow->settings.autoApplyChanges) {
		editorWindow->PushUndoState(this);
		ApplyChanges();
	}
}

void WeatherWidget::DrawFogSettings()
{
	auto editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();
	WeatherWidget* parentWidget = hasParent ? GetParent() : nullptr;

	bool changed = false;
	const bool nearMatches = MatchesAnySearch({ WeatherSetting::kDayNear, WeatherSetting::kNightNear });
	const bool farMatches = MatchesAnySearch({ WeatherSetting::kDayFar, WeatherSetting::kNightFar });
	const bool powerMatches = MatchesAnySearch({ WeatherSetting::kDayPower, WeatherSetting::kNightPower });
	const bool maxMatches = MatchesAnySearch({ WeatherSetting::kDayMax, WeatherSetting::kNightMax });
	const bool anyFogMatches = nearMatches || farMatches || powerMatches || maxMatches;
	if (!anyFogMatches)
		return;

	auto pushFogRowHighlight = [&](const char* dayId, const char* nightId) -> const char* {
		if (PushHighlightIfNeeded(dayId)) {
			return dayId;
		}
		if (PushHighlightIfNeeded(nightId)) {
			return nightId;
		}
		return nullptr;
	};
	auto popFogRowHighlight = [&](const char* highlightId) {
		if (highlightId)
			PopHighlightIfNeeded(highlightId, true);
	};
	auto inheritFogPair = [&](const char* inheritKey, const char* dayKey, const char* nightKey) {
		if (!parentWidget)
			return;
		if (!settings.inheritFlags[inheritKey])
			return;

		const float parentDay = parentWidget->settings.fogProperties[dayKey];
		const float parentNight = parentWidget->settings.fogProperties[nightKey];
		if (settings.fogProperties[dayKey] == parentDay && settings.fogProperties[nightKey] == parentNight)
			return;

		settings.fogProperties[dayKey] = parentDay;
		settings.fogProperties[nightKey] = parentNight;
		changed = true;
	};

	const float scale = Util::GetUIScale();
	if (ImGui::BeginTable("FogTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 80.0f * scale);
		ImGui::TableSetupColumn("Day", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Night", ImGuiTableColumnFlags_WidthStretch, 1.0f);

		// Header row
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TableSetColumnIndex(1);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Day");
		ImGui::TableSetColumnIndex(2);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Night");

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(2);
		ImGui::Separator();

		// Near
		if (nearMatches) {
			const char* highlightId = pushFogRowHighlight(WeatherSetting::kDayNear, WeatherSetting::kNightNear);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			if (hasParent) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * scale, 2.0f * scale));
				ImGui::Checkbox("##FogNear", &settings.inheritFlags[WeatherInherit::kFogNear]);
				inheritFogPair(WeatherInherit::kFogNear, WeatherSetting::kDayNear, WeatherSetting::kNightNear);
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(2);
				ImGui::SameLine();
			}
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Near");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1);
			if (WeatherUtils::DrawSliderFloat("##Day Near", settings.fogProperties[WeatherSetting::kDayNear], 0.0f, 1000000.0f, nullptr, "%.0f"))
				changed = true;
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1);
			if (WeatherUtils::DrawSliderFloat("##Night Near", settings.fogProperties[WeatherSetting::kNightNear], 0.0f, 1000000.0f, nullptr, "%.0f"))
				changed = true;
			popFogRowHighlight(highlightId);
		}

		// Far
		if (farMatches) {
			const char* highlightId = pushFogRowHighlight(WeatherSetting::kDayFar, WeatherSetting::kNightFar);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			if (hasParent) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * scale, 2.0f * scale));
				ImGui::Checkbox("##FogFar", &settings.inheritFlags[WeatherInherit::kFogFar]);
				inheritFogPair(WeatherInherit::kFogFar, WeatherSetting::kDayFar, WeatherSetting::kNightFar);
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(2);
				ImGui::SameLine();
			}
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Far");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1);
			if (WeatherUtils::DrawSliderFloat("##Day Far", settings.fogProperties[WeatherSetting::kDayFar], 0.0f, 1000000.0f, nullptr, "%.0f"))
				changed = true;
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1);
			if (WeatherUtils::DrawSliderFloat("##Night Far", settings.fogProperties[WeatherSetting::kNightFar], 0.0f, 1000000.0f, nullptr, "%.0f"))
				changed = true;
			popFogRowHighlight(highlightId);
		}

		// Power
		if (powerMatches) {
			const char* highlightId = pushFogRowHighlight(WeatherSetting::kDayPower, WeatherSetting::kNightPower);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			if (hasParent) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * scale, 2.0f * scale));
				ImGui::Checkbox("##FogPower", &settings.inheritFlags[WeatherInherit::kFogPower]);
				inheritFogPair(WeatherInherit::kFogPower, WeatherSetting::kDayPower, WeatherSetting::kNightPower);
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(2);
				ImGui::SameLine();
			}
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Power");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1);
			if (WeatherUtils::DrawSliderFloat("##Day Power", settings.fogProperties[WeatherSetting::kDayPower], 0.0f, 10.0f, nullptr, "%.3f"))
				changed = true;
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1);
			if (WeatherUtils::DrawSliderFloat("##Night Power", settings.fogProperties[WeatherSetting::kNightPower], 0.0f, 10.0f, nullptr, "%.3f"))
				changed = true;
			popFogRowHighlight(highlightId);
		}

		// Max
		if (maxMatches) {
			const char* highlightId = pushFogRowHighlight(WeatherSetting::kDayMax, WeatherSetting::kNightMax);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			if (hasParent) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * scale, 2.0f * scale));
				ImGui::Checkbox("##FogMax", &settings.inheritFlags[WeatherInherit::kFogMax]);
				inheritFogPair(WeatherInherit::kFogMax, WeatherSetting::kDayMax, WeatherSetting::kNightMax);
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(2);
				ImGui::SameLine();
			}
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Max");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1);
			if (WeatherUtils::DrawSliderFloat("##Day Max", settings.fogProperties[WeatherSetting::kDayMax], 0.0f, 1.0f, nullptr, "%.3f"))
				changed = true;
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1);
			if (WeatherUtils::DrawSliderFloat("##Night Max", settings.fogProperties[WeatherSetting::kNightMax], 0.0f, 1.0f, nullptr, "%.3f"))
				changed = true;
			popFogRowHighlight(highlightId);
		}

		ImGui::EndTable();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void WeatherWidget::DrawProperties(std::string category, std::map<std::string, int> properties)
{
	// Only show category if any property matches search
	if (std::ranges::none_of(properties, [&](const auto& p) { return MatchesSearch(p.first); }))
		return;

	ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", category.c_str());

	bool changed = false;
	auto* editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * WidgetDefaults::kSliderWidthRatio);

	for (auto& p : properties) {
		DrawSearchSectionIfMatches(p.first, [&](const char*) {
			// Inherit checkbox
			if (hasParent) {
				bool& inheritFlag = settings.inheritFlags[p.first];
				bool inheritChanged = DrawWithHighlight(p.first, [&]() {
					return ImGui::Checkbox(("##inherit_" + p.first).c_str(), &inheritFlag);
				});
				if (inheritChanged && inheritFlag) {
					InheritFromParent(p.first);
					changed = true;
				}
				Util::AddTooltip(inheritFlag ? "Inheriting from parent" : "Inherit from parent");
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
		});
	}

	ImGui::PopItemWidth();

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

	// Copy records (form references)
	for (size_t i = 0; i < ColorTimes::kTotal; i++) {
		settings.imageSpaceRefs[i] = parentWidget->settings.imageSpaceRefs[i];
		settings.volumetricLightingRefs[i] = parentWidget->settings.volumetricLightingRefs[i];
	}
	settings.precipitationData = parentWidget->settings.precipitationData;
	settings.referenceEffect = parentWidget->settings.referenceEffect;

	// Set all inherit flags to true
	settings.inheritFlags[WeatherInherit::kDalcSpecular] = true;
	settings.inheritFlags[WeatherInherit::kDalcFresnel] = true;
	settings.inheritFlags[WeatherInherit::kDalcDirXMax] = true;
	settings.inheritFlags[WeatherInherit::kDalcDirXMin] = true;
	settings.inheritFlags[WeatherInherit::kDalcDirYMax] = true;
	settings.inheritFlags[WeatherInherit::kDalcDirYMin] = true;
	settings.inheritFlags[WeatherInherit::kDalcDirZMax] = true;
	settings.inheritFlags[WeatherInherit::kDalcDirZMin] = true;

	settings.inheritFlags[WeatherInherit::kFogNear] = true;
	settings.inheritFlags[WeatherInherit::kFogFar] = true;
	settings.inheritFlags[WeatherInherit::kFogPower] = true;
	settings.inheritFlags[WeatherInherit::kFogMax] = true;

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

	// Records
	for (size_t i = 0; i < ColorTimes::kTotal; i++) {
		settings.inheritFlags["ImageSpace_" + std::to_string(i)] = true;
		settings.inheritFlags["VolumetricLighting_" + std::to_string(i)] = true;
	}
	settings.inheritFlags["Precipitation"] = true;
	settings.inheritFlags["ReferenceEffect"] = true;

	// Apply the changes — form references require a weather reinit to propagate
	pendingReinit = true;
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

	// Collect all feature names from both current and original settings to detect deletions
	std::set<std::string> allFeatureNames;
	for (const auto& [featureName, _] : settings.featureSettings) {
		allFeatureNames.insert(featureName);
	}
	for (const auto& [featureName, _] : originalSettings.featureSettings) {
		allFeatureNames.insert(featureName);
	}

	// Save current settings or clear deleted features
	for (const auto& featureName : allFeatureNames) {
		auto it = settings.featureSettings.find(featureName);
		weatherManager->SaveSettingsToWeather(
			weather,
			featureName,
			it != settings.featureSettings.end() ? it->second : json::object());
	}
}

void WeatherWidget::LoadFeatureSettings()
{
	auto* weatherManager = WeatherManager::GetSingleton();
	auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();

	// First, validate that all feature settings in the JSON exist as loaded features.
	// Prevents loading a .json that references features that aren't installed. (Will load only settings for installed features.)
	// Should make it very obvious to users when they have not followed the correct installation instructions.
	if (js.contains("featureSettings") && js["featureSettings"].is_object()) {
		std::vector<std::string> missingFeatures;

		for (const auto& [featureName, featureJson] : js["featureSettings"].items()) {
			if (featureJson.empty()) {
				continue;
			}

			// Check if this feature exists and is loaded
			bool featureExists = false;
			for (auto* feature : Feature::GetFeatureList()) {
				if (feature && feature->loaded && feature->GetShortName() == featureName) {
					featureExists = true;
					break;
				}
			}

			if (!featureExists) {
				missingFeatures.push_back(featureName);
			}
		}

		// If we found missing features, warn the user
		if (!missingFeatures.empty()) {
			std::string missingList;
			for (size_t i = 0; i < missingFeatures.size(); ++i) {
				if (i > 0)
					missingList += ", ";
				missingList += missingFeatures[i];
			}

			// Show notification
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Warning: {} references missing feature(s): {}", GetEditorID(), missingList),
				ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
				5.0f);

			// Add to Feature Issues system for each missing feature
			for (const auto& featureName : missingFeatures) {
				FeatureIssues::FeatureFileInfo fileInfo;
				fileInfo.featureName = featureName;

				FeatureIssues::AddFeatureIssue(
					featureName,
					"",
					std::format("Weather '{}' contains settings for this feature, but the feature is not loaded. "
								"The weather-specific parameters will be ignored until the feature is installed and loaded.",
						GetEditorID()),
					FeatureIssues::FeatureIssueInfo::IssueType::UNKNOWN,
					fileInfo,
					"");
			}

			logger::warn("{}: JSON contains feature settings for features that are not loaded: {}", GetEditorID(), missingList);
		}
	}

	// Now load settings for features that ARE loaded
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
	if (pendingReinit) {
		Widget::ForceWeatherReinit(weather);
		pendingReinit = false;
	}
}

void WeatherWidget::RevertChanges()
{
	auto* weatherManager = WeatherManager::GetSingleton();

	// If this weather is currently active, reset enabled feature overrides to user defaults
	if (weather == weatherManager->GetCurrentWeathers().currentWeather) {
		auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();

		for (const auto& [featureName, featureSettings] : settings.featureSettings) {
			if (!featureSettings.value("__enabled", false) || !globalRegistry->HasWeatherSupport(featureName)) {
				continue;
			}

			globalRegistry->EndFeatureTransition(featureName);

			if (auto* featureRegistry = globalRegistry->GetFeatureRegistry(featureName)) {
				for (const auto& var : featureRegistry->GetVariables()) {
					var->SetToUserSettings();
				}
			}
		}
	}

	weatherManager->ClearAllFeatureSettingsForWeather(weather);
	settings = vanillaSettings;
	pendingReinit = true;
	ApplyChanges();
}

void WeatherWidget::Delete()
{
	// Clear cache and local settings before base Delete() to prevent reloading stale data
	auto* weatherManager = WeatherManager::GetSingleton();
	weatherManager->ClearAllFeatureSettingsForWeather(weather);
	settings.featureSettings.clear();

	Widget::Delete();
}

bool WeatherWidget::Settings::operator==(const Settings& o) const
{
	return parent == o.parent &&
	       inheritFlags == o.inheritFlags &&
	       weatherProperties == o.weatherProperties &&
	       weatherColors == o.weatherColors &&
	       fogProperties == o.fogProperties &&
	       std::equal(std::begin(atmosphereColors), std::end(atmosphereColors), std::begin(o.atmosphereColors)) &&
	       std::equal(std::begin(dalc), std::end(dalc), std::begin(o.dalc)) &&
	       std::equal(std::begin(clouds), std::end(clouds), std::begin(o.clouds)) &&
	       std::equal(std::begin(imageSpaceRefs), std::end(imageSpaceRefs), std::begin(o.imageSpaceRefs)) &&
	       std::equal(std::begin(volumetricLightingRefs), std::end(volumetricLightingRefs), std::begin(o.volumetricLightingRefs)) &&
	       precipitationData == o.precipitationData &&
	       referenceEffect == o.referenceEffect &&
	       featureSettings == o.featureSettings;
}

bool WeatherWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}

void WeatherWidget::DrawFeatureSettings()
{
	ImGui::TextWrapped(
		"Configure feature-specific settings that will be applied when this weather is active. "
		"These override the feature's global settings for this weather only.");
	ImGui::Spacing();

	auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();

	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature || !feature->loaded) {
			continue;
		}

		std::string featureName = feature->GetShortName();
		auto* featureRegistry = globalRegistry->GetFeatureRegistry(featureName);

		// Check if feature has registered weather variables
		if (!featureRegistry) {
			continue;
		}

		std::string displayName = feature->GetName();
		auto featureIt = settings.featureSettings.find(featureName);
		const json* featureJsonView = (featureIt != settings.featureSettings.end()) ? &featureIt->second : nullptr;
		auto getFeatureJson = [&]() -> json& {
			return settings.featureSettings.try_emplace(featureName, json::object()).first->second;
		};

		// Handle pending navigation - auto-expand this feature if it matches
		bool shouldAutoExpand = (pendingFeatureNavigation == featureName);
		if (shouldAutoExpand) {
			ImGui::SetNextItemOpen(true);
		}

		if (ImGui::TreeNodeEx(displayName.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
			// Check if weather-specific overrides are enabled (using special key)
			bool overridesEnabled = featureJsonView ? featureJsonView->value("__enabled", false) : false;

			// Weather-specific override toggle
			ImGui::PushStyleColor(ImGuiCol_Button, overridesEnabled ? ImVec4(0.2f, 0.7f, 0.2f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, overridesEnabled ? ImVec4(0.3f, 0.8f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, overridesEnabled ? ImVec4(0.1f, 0.6f, 0.1f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

			bool toggleClicked = ImGui::Button(overridesEnabled ? "Using Weather-Specific Settings" : "Using Global Settings", ImVec2(-1, 0));

			ImGui::PopStyleColor(3);

			if (auto _tt = Util::HoverTooltipWrapper()) {
				if (overridesEnabled) {
					ImGui::Text("This weather has custom overrides for this feature.");
					ImGui::Text("Click to disable overrides and use global settings instead.");
					ImGui::Text("(Settings will be preserved but not applied)");
				} else {
					ImGui::Text("This weather uses global feature settings.");
					ImGui::Text("Click to enable weather-specific overrides.");
				}
			}

			if (toggleClicked) {
				auto& featureJson = getFeatureJson();
				if (overridesEnabled) {
					// Disable overrides - mark as disabled but keep the settings
					featureJson["__enabled"] = false;
				} else {
					// Enable overrides - mark as enabled
					featureJson["__enabled"] = true;
					// If no settings exist yet, copy current global values as starting point
					bool hasActualSettings = false;
					for (auto it = featureJson.begin(); it != featureJson.end(); ++it) {
						if (it.key() != "__enabled") {
							hasActualSettings = true;
							break;
						}
					}
					if (!hasActualSettings) {
						const auto& variables = featureRegistry->GetVariables();
						for (const auto& var : variables) {
							json tempJson;
							var->SaveToJson(tempJson);
							std::string varName = var->GetName();
							if (tempJson.contains(varName)) {
								featureJson[varName] = tempJson[varName];
							}
						}
					}
				}
				EditorWindow::GetSingleton()->PushUndoState(this);
				if (EditorWindow::GetSingleton()->settings.autoApplyChanges) {
					ApplyChanges();
				}
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Only show controls if weather-specific overrides are enabled
			if (overridesEnabled) {
				auto& featureJson = getFeatureJson();
				// Draw UI for each registered variable
				const auto& variables = featureRegistry->GetVariables();
				bool modified = false;

				for (const auto& var : variables) {
					std::string varName = var->GetName();
					std::string varDisplayName = var->GetDisplayName();
					std::string tooltip = var->GetTooltip();

					ImGui::PushID(varName.c_str());

					// Check if this variable has a weather-specific value
					bool hasOverride = featureJson.contains(varName);

					json currentValue;
					if (hasOverride) {
						currentValue = featureJson.at(varName);
					} else {
						json tempJson;
						var->SaveToJson(tempJson);
						auto it = tempJson.find(varName);
						if (it == tempJson.end()) {
							ImGui::PopID();
							continue;
						}
						currentValue = *it;
					}

					// Try to detect variable type and render appropriate control
					// Check if it's a bool variable first
					if (auto* boolVar = dynamic_cast<WeatherVariables::WeatherVariable<bool>*>(var.get())) {
						bool value = currentValue.get<bool>();

						if (ImGui::Checkbox(varDisplayName.c_str(), &value)) {
							featureJson[varName] = value;
							modified = true;
						}

						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text("%s", tooltip.c_str());
						}

						// Right-click context menu to reset individual values
						if (ImGui::BeginPopupContextItem()) {
							if (ImGui::MenuItem("Reset to Global")) {
								featureJson.erase(varName);
								modified = true;
							}
							ImGui::EndPopup();
						}

					} else if (auto* floatVar = dynamic_cast<WeatherVariables::FloatVariable*>(var.get())) {
						float value = currentValue.get<float>();
						float minVal = floatVar->GetMin();
						float maxVal = floatVar->GetMax();

						if (ImGui::SliderFloat(varDisplayName.c_str(), &value, minVal, maxVal, "%.3f")) {
							featureJson[varName] = value;
							modified = true;
						}

						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text("%s", tooltip.c_str());
						}

						// Right-click context menu to reset individual values
						if (ImGui::BeginPopupContextItem()) {
							if (ImGui::MenuItem("Reset to Global")) {
								featureJson.erase(varName);
								modified = true;
							}
							ImGui::EndPopup();
						}

					} else if (auto* float3Var = dynamic_cast<WeatherVariables::Float3Variable*>(var.get())) {
						// Handle float3 (color) variables
						float3 value = currentValue.get<float3>();
						float colorArray[3] = { value.x, value.y, value.z };

						if (ImGui::ColorEdit3(varDisplayName.c_str(), colorArray)) {
							featureJson[varName] = json{ colorArray[0], colorArray[1], colorArray[2] };
							modified = true;
						}

						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text("%s", tooltip.c_str());
						}

						if (ImGui::BeginPopupContextItem()) {
							if (ImGui::MenuItem("Reset to Global")) {
								featureJson.erase(varName);
								modified = true;
							}
							ImGui::EndPopup();
						}

					} else if (auto* float4Var = dynamic_cast<WeatherVariables::Float4Variable*>(var.get())) {
						// Handle float4 (color with alpha) variables
						float4 value = currentValue.get<float4>();
						float colorArray[4] = { value.x, value.y, value.z, value.w };

						if (ImGui::ColorEdit4(varDisplayName.c_str(), colorArray)) {
							featureJson[varName] = json{ colorArray[0], colorArray[1], colorArray[2], colorArray[3] };
							modified = true;
						}

						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text("%s", tooltip.c_str());
						}

						if (ImGui::BeginPopupContextItem()) {
							if (ImGui::MenuItem("Reset to Global")) {
								featureJson.erase(varName);
								modified = true;
							}
							ImGui::EndPopup();
						}

					} else {
						// Generic handling for other types
						ImGui::TextDisabled("%s: %s", varDisplayName.c_str(), currentValue.dump().c_str());
						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Unsupported Variable Type");
							ImGui::Text("%s", tooltip.c_str());
							ImGui::Separator();
							ImGui::TextWrapped("This variable type doesn't have a custom UI implementation yet. The raw JSON value is shown above.");
						}
					}

					ImGui::PopID();
				}

				if (modified) {
					EditorWindow::GetSingleton()->PushUndoState(this);
					if (EditorWindow::GetSingleton()->settings.autoApplyChanges) {
						ApplyChanges();
					}
				}

			} else {
				ImGui::TextColored({ 0.7f, 0.7f, 0.7f, 1.0f }, "Enable weather-specific overrides above to customize settings for this weather.");
			}

			ImGui::TreePop();
		}
	}

	// Clear navigation state after processing
	if (!pendingFeatureNavigation.empty()) {
		pendingFeatureNavigation.clear();
		pendingSettingHighlight.clear();
	}
}

void WeatherWidget::NavigateToFeatureSetting(const std::string& featureName, const std::string& settingName)
{
	// Store the navigation request
	pendingFeatureNavigation = featureName;
	pendingSettingHighlight = settingName;

	// Switch to Features tab
	activeTabOverride = "Features";
}

std::vector<Widget::SearchResult> WeatherWidget::CollectSearchableSettings() const
{
	std::vector<SearchResult> results;

	const std::vector<std::pair<std::string, std::vector<std::string>>> tabEntries = {
		{ WeatherTab::kBasic, { "Sun Damage", "Wind Speed", "Wind Direction", "Wind Direction Range",
					   "Precipitation Begin Fade In", "Precipitation End Fade Out",
					   "Thunder Lightning Begin Fade In", "Thunder Lightning End Fade Out",
					   "Thunder Lightning Frequency", "Lightning Color",
					   "Visual Effect Begin", "Visual Effect End", "Trans Delta" } },
		{ WeatherTab::kFog, { WeatherSetting::kDayNear, WeatherSetting::kDayFar, WeatherSetting::kDayPower, WeatherSetting::kDayMax,
								 WeatherSetting::kNightNear, WeatherSetting::kNightFar, WeatherSetting::kNightPower, WeatherSetting::kNightMax } },
	};

	for (const auto& [tab, names] : tabEntries) {
		for (const auto& name : names) {
			results.push_back({ name, tab, name });
		}
	}

	results.push_back({ WeatherSetting::kFresnelPower, WeatherTab::kDalc, WeatherSetting::kFresnelPower });
	results.push_back({ WeatherSetting::kSpecular, WeatherTab::kDalc, WeatherSetting::kSpecular });
	results.push_back({ WeatherDisplay::kDirectionalXMax, WeatherTab::kDalc, WeatherSetting::kDirectionalXMax });
	results.push_back({ WeatherDisplay::kDirectionalXMin, WeatherTab::kDalc, WeatherSetting::kDirectionalXMin });
	results.push_back({ WeatherDisplay::kDirectionalYMax, WeatherTab::kDalc, WeatherSetting::kDirectionalYMax });
	results.push_back({ WeatherDisplay::kDirectionalYMin, WeatherTab::kDalc, WeatherSetting::kDirectionalYMin });
	results.push_back({ WeatherDisplay::kDirectionalZMax, WeatherTab::kDalc, WeatherSetting::kDirectionalZMax });
	results.push_back({ WeatherDisplay::kDirectionalZMin, WeatherTab::kDalc, WeatherSetting::kDirectionalZMin });

	for (int i = 0; i < ColorTypes::kTotal; i++) {
		std::string colorType = ColorTypeLabel(i);
		results.push_back({ colorType, WeatherTab::kAtmosphere, colorType });
	}

	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		std::string layerId = std::format("Cloud Layer {}", i);
		results.push_back({ layerId, WeatherTab::kClouds, layerId });
	}

	// Records tab: one entry per time-of-day slot for each form-picker section
	for (int i = 0; i < ColorTimes::kTotal; i++) {
		std::string label = std::format("{} {}", WeatherRecord::kImageSpace, ColorTimeLabel(i));
		results.push_back({ label, WeatherTab::kRecords, label });
	}
	for (int i = 0; i < ColorTimes::kTotal; i++) {
		std::string label = std::format("{} {}", WeatherRecord::kVolumetricLighting, ColorTimeLabel(i));
		results.push_back({ label, WeatherTab::kRecords, label });
	}
	results.push_back({ WeatherRecord::kPrecipitation, WeatherTab::kRecords, WeatherRecord::kPrecipitation });
	results.push_back({ WeatherRecord::kVisualEffect, WeatherTab::kRecords, WeatherRecord::kVisualEffect });

	return results;
}

ID3D11ShaderResourceView* WeatherWidget::GetCloudTexture(int layerIndex)
{
	if (cloudTextureCache.contains(layerIndex)) {
		return cloudTextureCache[layerIndex];
	}

	const auto& texturePath = settings.clouds[layerIndex].texturePath;
	if (texturePath.empty()) {
		return nullptr;
	}

	std::string resourcePath = WeatherUtils::TexturePath::BuildResourcePath(texturePath);

	ID3D11ShaderResourceView* srv = nullptr;
	ImVec2 textureSize;

	if (Util::LoadDDSTextureFromFile(globals::d3d::device, resourcePath.c_str(), &srv, textureSize)) {
		cloudTextureCache[layerIndex] = srv;
		return srv;
	}

	return nullptr;
}
