#include "WeatherWidget.h"

#include <format>
#include <unordered_set>

#include "imgui_internal.h"

#include "../../I18n/I18n.h"
#include "../EditorWindow.h"
#include "FeatureIssues.h"
#include "State.h"
#include "Utils/UI.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#define I18N_KEY_PREFIX "cs_editor."

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

	const char* TranslateWeatherPropertyLabel(std::string_view label)
	{
		if (label == "Sun Damage")
			return T(TKEY("sun_damage"), "Sun Damage");
		if (label == "Wind Speed")
			return T(TKEY("wind_speed"), "Wind Speed");
		if (label == "Wind Direction")
			return T(TKEY("wind_direction_label"), "Wind Direction");
		if (label == "Wind Direction Range")
			return T(TKEY("wind_direction_range_label"), "Wind Direction Range");
		if (label == "Precipitation Begin Fade In")
			return T(TKEY("precipitation_begin_fade_in_label"), "Precipitation Begin Fade In");
		if (label == "Precipitation End Fade Out")
			return T(TKEY("precipitation_end_fade_out_label"), "Precipitation End Fade Out");
		if (label == "Thunder Lightning Begin Fade In")
			return T(TKEY("thunder_lightning_begin_fade_in"), "Thunder Lightning Begin Fade In");
		if (label == "Thunder Lightning End Fade Out")
			return T(TKEY("thunder_lightning_end_fade_out"), "Thunder Lightning End Fade Out");
		if (label == "Thunder Lightning Frequency")
			return T(TKEY("thunder_lightning_frequency"), "Thunder Lightning Frequency");
		if (label == "Lightning Color")
			return T(TKEY("lightning_color_label"), "Lightning Color");
		if (label == "Visual Effect Begin")
			return T(TKEY("visual_effect_begin"), "Visual Effect Begin");
		if (label == "Visual Effect End")
			return T(TKEY("visual_effect_end"), "Visual Effect End");
		if (label == "Trans Delta")
			return T(TKEY("trans_delta"), "Trans Delta");

		// Fallback: return the original label via T() which caches a stable null-terminated copy
		return T(std::string(label).c_str(), std::string(label).c_str());
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
			if (ImGui::BeginCombo(T(TKEY("parent"), "Parent"), settings.parent.c_str())) {
				// Option for "None"
				if (ImGui::Selectable(T(TKEY("none"), "None"), parent == nullptr)) {
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
				ImGui::TextUnformatted(T(TKEY("parent_cs_editor_feature"), "Editor-only feature: Set a parent weather to copy settings from."));
				ImGui::TextUnformatted(T(TKEY("use_inherit_checkboxes"), "Use 'Inherit From Parent' checkboxes to copy specific values."));
				Util::Text::Warning("%s", T(TKEY("not_same_as_cell_lighting"), "Note: This is NOT the same as cell lighting template inheritance."));
				ImGui::EndTooltip();
			}

			if (parent) {
				ImGui::SameLine();
				if (Util::ButtonWithFlash(T(TKEY("inherit_all"), "Inherit All"))) {
					InheritAllFromParent();
				}
				Util::AddTooltip(T(TKEY("copy_all_from_parent"), "Copy all parameter values from parent weather"));

				if (!parent->IsOpen()) {
					ImGui::SameLine();
					if (Util::ButtonWithFlash(T(TKEY("open"), "Open")))
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

		if (ImGui::BeginTabItem(T(TKEY("basic"), WeatherTab::kBasic), nullptr, basicFlags)) {
			BeginScrollableContent("##BasicScroll");
			DrawProperties(T(TKEY("category_sun"), "Sun"), { { "Sun Damage", UINT8_SLIDER } });
			DrawProperties(T(TKEY("category_wind"), "Wind"), { { "Wind Speed", UINT8_SLIDER }, { "Wind Direction", UINT8_SLIDER }, { "Wind Direction Range", UINT8_SLIDER } });
			DrawProperties(T(TKEY("category_precipitation"), "Precipitation"), { { "Precipitation Begin Fade In", UINT8_SLIDER }, { "Precipitation End Fade Out", UINT8_SLIDER } });
			DrawProperties(T(TKEY("category_lightning"), "Lightning"), { { "Thunder Lightning Begin Fade In", UINT8_SLIDER }, { "Thunder Lightning End Fade Out", UINT8_SLIDER },
																		   { "Thunder Lightning Frequency", UINT8_SLIDER }, { "Lightning Color", COLOR3_PICKER } });
			DrawProperties(T(TKEY("category_visual_effects"), "Visual Effects"), { { "Visual Effect Begin", UINT8_SLIDER }, { "Visual Effect End", UINT8_SLIDER } });
			DrawProperties(T(TKEY("category_weather_transition"), "Weather Transition"), { { "Trans Delta", UINT8_SLIDER } });
			EndScrollableContent();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(T(TKEY("lighting_dalc"), WeatherTab::kDalc), nullptr, dalcFlags)) {
			BeginScrollableContent("##DALCScroll");
			DrawDALCSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("atmosphere_colors"), WeatherTab::kAtmosphere), nullptr, atmosphereFlags)) {
			BeginScrollableContent("##AtmosphereScroll");
			DrawWeatherColorSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("clouds"), WeatherTab::kClouds), nullptr, cloudsFlags)) {
			BeginScrollableContent("##CloudsScroll");
			DrawCloudSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("fog"), WeatherTab::kFog), nullptr, fogFlags)) {
			BeginScrollableContent("##FogScroll");
			DrawFogSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("features"), "Features"), nullptr, featuresFlags)) {
			BeginScrollableContent("##FeaturesScroll");
			DrawFeatureSettings();
			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("records"), WeatherTab::kRecords), nullptr, recordsFlags)) {
			BeginScrollableContent("##RecordsScroll");
			ImGui::Spacing();
			ImGui::TextWrapped("%s", T(TKEY("form_record_references"), "Form record references used by this weather."));
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
			auto drawInheritCheckbox = [&](const std::string& inheritKey, auto& recordRef, auto& parentRef) -> bool {
				if (!hasParent)
					return false;
				bool& inheritFlag = settings.inheritFlags[inheritKey];
				ImGui::Checkbox(("##inherit_" + inheritKey).c_str(), &inheritFlag);
				if (inheritFlag && parentWidget && recordRef != parentRef) {
					recordRef = parentRef;
					pendingReinit = true;
				}
				Util::AddTooltip(inheritFlag ? T(TKEY("inheriting_from_parent"), "Inheriting from parent") : T(TKEY("inherit_from_parent"), "Inherit from parent"));
				ImGui::SameLine();
				return inheritFlag;
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

					const bool isInherited = drawInheritCheckbox(inheritKey, recordRefs[i], parentRefs[i]);
					ImGui::Text("%s:", label.c_str());
					ImGui::SameLine(todLabelOffset);
					if (isInherited)
						PushInheritedStyle();
					if (WeatherUtils::DrawFormPickerCached(pickerId, recordRefs[i], widgets, false, true, pickerWidth)) {
						pendingReinit = true;
						if (isInherited)
							settings.inheritFlags[inheritKey] = false;
					}
					if (isInherited) {
						Util::AddTooltip(T(TKEY("inherited_from_parent_weather"), "Inherited from parent weather"));
						PopInheritedStyle();
					}
					drawOpenButton(recordRefs[i], widgets, std::format("{}##{}", T(TKEY("open"), "Open"), i), openTooltip);

					if (recordHighlighted)
						PopHighlightIfNeeded(rowId, recordHighlighted);
					ImGui::PopID();
				}
				ImGui::Spacing();
			};
			auto drawSingleRecordSection = [&](const char* sectionLabel, const char* recordId, const char* inheritKey, const char* valueLabel, const char* pickerId, auto& recordRef, auto& parentRef, auto& widgets, const std::string& buttonId, const char* openTooltip) {
				if (!MatchesSearch(recordId))
					return;
				if (ShouldOpenSearchSection())
					ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				if (!ImGui::CollapsingHeader(sectionLabel, ImGuiTreeNodeFlags_DefaultOpen))
					return;
				const bool recordHighlighted = pushRecordHighlight(recordId);

				const bool isInherited = drawInheritCheckbox(inheritKey, recordRef, parentRef);
				ImGui::Text("%s:", valueLabel);
				ImGui::SameLine(formLabelOffset);
				if (isInherited)
					PushInheritedStyle();
				if (WeatherUtils::DrawFormPickerCached(pickerId, recordRef, widgets, false, true, pickerWidth)) {
					pendingReinit = true;
					if (isInherited)
						settings.inheritFlags[inheritKey] = false;
				}
				if (isInherited) {
					Util::AddTooltip(T(TKEY("inherited_from_parent_weather"), "Inherited from parent weather"));
					PopInheritedStyle();
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
			drawTimeRecordSection(T(TKEY("record_imagespace"), "ImageSpace"), WeatherRecord::kImageSpaceIdOffset, WeatherRecord::kImageSpace, settings.imageSpaceRefs, parentImageSpaceRefs, editorWindow->imageSpaceWidgets, "##ImageSpace", T(TKEY("open_imagespace_edit"), "Open this ImageSpace for editing"));
			drawTimeRecordSection(T(TKEY("record_volumetric_lighting"), "Volumetric Lighting"), WeatherRecord::kVolumetricLightingIdOffset, "VolumetricLighting", settings.volumetricLightingRefs, parentVolumetricLightingRefs, editorWindow->volumetricLightingWidgets, "##VolumetricLighting", T(TKEY("open_volumetric_edit"), "Open this Volumetric Lighting for editing"));
			drawSingleRecordSection(T(TKEY("record_precipitation"), "Precipitation"), WeatherRecord::kPrecipitation, WeatherRecord::kPrecipitation, T(TKEY("particle_shader"), "Particle Shader"), "##Precipitation", settings.precipitationData, parentPrecipitationData, editorWindow->precipitationWidgets, std::format("{}##Precip", T(TKEY("open"), "Open")), T(TKEY("open_precipitation_edit"), "Open this Precipitation for editing"));
			drawSingleRecordSection(T(TKEY("record_visual_effect"), "Visual Effect"), WeatherRecord::kVisualEffect, "ReferenceEffect", T(TKEY("record_visual_effect"), "Visual Effect"), "##ReferenceEffect", settings.referenceEffect, parentReferenceEffect, editorWindow->referenceEffectWidgets, std::format("{}##RefEffect", T(TKEY("open"), "Open")), T(TKEY("open_visual_effect_edit"), "Open this Visual Effect for editing"));

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
					Util::Colors::GetError(),
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
				Util::Colors::GetError(),
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
		auto drawDalcColor = [&](const char* settingId, const char* label, float3(&values)[4], bool* inheritFlag = nullptr, float3* parentValues = nullptr) {
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
			if (drawDalcColor(WeatherSetting::kSpecular, T(TKEY("dalc_specular"), "Specular"), specularColors, &settings.inheritFlags[WeatherInherit::kDalcSpecular], parentSpecular)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].specular = specularColors[i];
				changed = true;
			}

			if (drawDalcFloat(WeatherSetting::kFresnelPower, T(TKEY("dalc_fresnel_power"), "Fresnel Power"), fresnelPowers, &settings.inheritFlags[WeatherInherit::kDalcFresnel], parentFresnel)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].fresnelPower = fresnelPowers[i];
				changed = true;
			}
		} else {
			if (drawDalcColor(WeatherSetting::kSpecular, T(TKEY("dalc_specular"), "Specular"), specularColors)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].specular = specularColors[i];
				changed = true;
			}

			if (drawDalcFloat(WeatherSetting::kFresnelPower, T(TKEY("dalc_fresnel_power"), "Fresnel Power"), fresnelPowers)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].fresnelPower = fresnelPowers[i];
				changed = true;
			}
		}

		TOD::DrawTODSeparator();

		// Directional colors with per-parameter inheritance
		if (hasParent && parentWidget) {
			if (drawDalcColor(WeatherSetting::kDirectionalXMax, T(TKEY("dalc_directional_x_max"), "Directional +X"), directionalXMax, &settings.inheritFlags[WeatherInherit::kDalcDirXMax], parentDirXMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].max = directionalXMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalXMin, T(TKEY("dalc_directional_x_min"), "Directional -X"), directionalXMin, &settings.inheritFlags[WeatherInherit::kDalcDirXMin], parentDirXMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].min = directionalXMin[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalYMax, T(TKEY("dalc_directional_y_max"), "Directional +Y"), directionalYMax, &settings.inheritFlags[WeatherInherit::kDalcDirYMax], parentDirYMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].max = directionalYMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalYMin, T(TKEY("dalc_directional_y_min"), "Directional -Y"), directionalYMin, &settings.inheritFlags[WeatherInherit::kDalcDirYMin], parentDirYMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].min = directionalYMin[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalZMax, T(TKEY("dalc_directional_z_max"), "Directional +Z"), directionalZMax, &settings.inheritFlags[WeatherInherit::kDalcDirZMax], parentDirZMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].max = directionalZMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalZMin, T(TKEY("dalc_directional_z_min"), "Directional -Z"), directionalZMin, &settings.inheritFlags[WeatherInherit::kDalcDirZMin], parentDirZMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].min = directionalZMin[i];
				changed = true;
			}
		} else {
			if (drawDalcColor(WeatherSetting::kDirectionalXMax, T(TKEY("dalc_directional_x_max"), "Directional +X"), directionalXMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].max = directionalXMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalXMin, T(TKEY("dalc_directional_x_min"), "Directional -X"), directionalXMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[0].min = directionalXMin[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalYMax, T(TKEY("dalc_directional_y_max"), "Directional +Y"), directionalYMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].max = directionalYMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalYMin, T(TKEY("dalc_directional_y_min"), "Directional -Y"), directionalYMin)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[1].min = directionalYMin[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalZMax, T(TKEY("dalc_directional_z_max"), "Directional +Z"), directionalZMax)) {
				for (int i = 0; i < ColorTimes::kTotal; i++)
					settings.dalc[i].directional[2].max = directionalZMax[i];
				changed = true;
			}

			if (drawDalcColor(WeatherSetting::kDirectionalZMin, T(TKEY("dalc_directional_z_min"), "Directional -Z"), directionalZMin)) {
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
	const char* kEnabledBadge = T(TKEY("enabled_badge"), "[Enabled]");

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

			if (ImGui::Checkbox(std::format("{}##{}", T(TKEY("enable"), "Enable"), layer).c_str(), &layerEnabled)) {
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
						if (TOD::DrawTODColorRow(T(TKEY("cloud_color"), "Cloud Color"), settings.clouds[i].color, settings.inheritFlags[colorKey], parentColors)) {
							changed = true;
						}

						if (TOD::DrawTODFloatRow(T(TKEY("cloud_alpha"), "Cloud Alpha"), settings.clouds[i].cloudAlpha, settings.inheritFlags[alphaKey], parentAlphas, 0.0f, 1.0f)) {
							changed = true;
						}
					});
				} else {
					drawCloudTODRows([&]() {
						if (TOD::DrawTODColorRow(T(TKEY("cloud_color"), "Cloud Color"), settings.clouds[i].color)) {
							changed = true;
						}

						if (TOD::DrawTODFloatRow(T(TKEY("cloud_alpha"), "Cloud Alpha"), settings.clouds[i].cloudAlpha, 0.0f, 1.0f)) {
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

	const float scale = Util::GetUIScale();
	if (ImGui::BeginTable("FogTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableSetupColumn(T(TKEY("parameter"), "Parameter"), ImGuiTableColumnFlags_WidthFixed, 80.0f * scale);
		ImGui::TableSetupColumn(T(TKEY("day"), "Day"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn(T(TKEY("night"), "Night"), ImGuiTableColumnFlags_WidthStretch, 1.0f);

		// Header row
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TableSetColumnIndex(1);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s", T(TKEY("day"), "Day"));
		ImGui::TableSetColumnIndex(2);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s", T(TKEY("night"), "Night"));

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(1);
		ImGui::Separator();
		ImGui::TableSetColumnIndex(2);
		ImGui::Separator();

		DrawFogRow(nearMatches, WeatherInherit::kFogNear, T(TKEY("fog_near"), "Near"), WeatherSetting::kDayNear, WeatherSetting::kNightNear, 0.0f, 1000000.0f, "%.0f", hasParent, parentWidget, changed);
		DrawFogRow(farMatches, WeatherInherit::kFogFar, T(TKEY("fog_far"), "Far"), WeatherSetting::kDayFar, WeatherSetting::kNightFar, 0.0f, 1000000.0f, "%.0f", hasParent, parentWidget, changed);
		DrawFogRow(powerMatches, WeatherInherit::kFogPower, T(TKEY("fog_power_short"), "Power"), WeatherSetting::kDayPower, WeatherSetting::kNightPower, 0.0f, 10.0f, "%.3f", hasParent, parentWidget, changed);
		DrawFogRow(maxMatches, WeatherInherit::kFogMax, T(TKEY("fog_max"), "Max"), WeatherSetting::kDayMax, WeatherSetting::kNightMax, 0.0f, 1.0f, "%.3f", hasParent, parentWidget, changed);

		ImGui::EndTable();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
}

void WeatherWidget::DrawFogSlider(const char* id, float& prop, float min, float max, const char* fmt, bool& inheritRef, bool isInherited, bool& changed)
{
	ImGui::SetNextItemWidth(-1);
	if (isInherited)
		PushInheritedStyle();
	if (WeatherUtils::DrawSliderFloat(id, prop, min, max, nullptr, fmt)) {
		changed = true;
		inheritRef = false;
	}
	if (isInherited) {
		Util::AddTooltip(T(TKEY("inherited_from_parent_weather"), "Inherited from parent weather"));
		PopInheritedStyle();
	}
}

void WeatherWidget::DrawFogRow(bool matches, const char* inheritKey, const char* label,
	const char* dayPropKey, const char* nightPropKey, float min, float max, const char* fmt,
	bool hasParent, WeatherWidget* parentWidget, bool& changed)
{
	if (!matches)
		return;

	const float scale = Util::GetUIScale();

	const char* highlightId = nullptr;
	if (PushHighlightIfNeeded(dayPropKey))
		highlightId = dayPropKey;
	else if (PushHighlightIfNeeded(nightPropKey))
		highlightId = nightPropKey;

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	if (hasParent) {
		ImGui::PushStyleColor(ImGuiCol_FrameBg, WidgetUI::kInheritCheckboxFrameBg);
		ImGui::PushStyleColor(ImGuiCol_CheckMark, WidgetUI::kInheritCheckboxMark);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * scale, 2.0f * scale));
		ImGui::Checkbox(std::format("##Fog{}", label).c_str(), &settings.inheritFlags[inheritKey]);
		if (parentWidget && settings.inheritFlags[inheritKey]) {
			const float parentDay = parentWidget->settings.fogProperties[dayPropKey];
			const float parentNight = parentWidget->settings.fogProperties[nightPropKey];
			if (settings.fogProperties[dayPropKey] != parentDay || settings.fogProperties[nightPropKey] != parentNight) {
				settings.fogProperties[dayPropKey] = parentDay;
				settings.fogProperties[nightPropKey] = parentNight;
				changed = true;
			}
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(2);
		ImGui::SameLine();
	}
	ImGui::AlignTextToFramePadding();
	ImGui::Text("%s", label);
	bool& inheritRef = settings.inheritFlags[inheritKey];
	const bool isInherited = hasParent && inheritRef;
	ImGui::TableSetColumnIndex(1);
	DrawFogSlider(std::format("##Day {}", label).c_str(), settings.fogProperties[dayPropKey], min, max, fmt, inheritRef, isInherited, changed);
	ImGui::TableSetColumnIndex(2);
	DrawFogSlider(std::format("##Night {}", label).c_str(), settings.fogProperties[nightPropKey], min, max, fmt, inheritRef, isInherited, changed);

	if (highlightId)
		PopHighlightIfNeeded(highlightId, true);
}

void WeatherWidget::DrawProperties(std::string category, std::map<std::string, int> properties)
{
	// Only show category if any property matches search
	if (std::ranges::none_of(properties, [&](const auto& p) { return MatchesSearch(p.first); }))
		return;

	ImGui::TextColored(WidgetUI::kCategoryHeaderColor, "%s", category.c_str());

	bool changed = false;
	auto* editorWindow = EditorWindow::GetSingleton();
	bool hasParent = editorWindow->settings.enableInheritFromParent && HasParent();

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * WidgetDefaults::kSliderWidthRatio);

	for (auto& p : properties) {
		DrawSearchSectionIfMatches(p.first, [&](const char*) {
			const std::string controlLabel = std::format("{}##{}", TranslateWeatherPropertyLabel(p.first), p.first);
			bool isInherited = false;
			if (hasParent) {
				bool& inheritFlag = settings.inheritFlags[p.first];
				isInherited = inheritFlag;
				bool inheritChanged = DrawWithHighlight(p.first, [&]() {
					return ImGui::Checkbox(("##inherit_" + p.first).c_str(), &inheritFlag);
				});
				if (inheritChanged && inheritFlag) {
					InheritFromParent(p.first);
					changed = true;
				}
				Util::AddTooltip(inheritFlag ? T(TKEY("inheriting_from_parent"), "Inheriting from parent") : T(TKEY("inherit_from_parent"), "Inherit from parent"));
				ImGui::SameLine();
			}

			if (isInherited)
				PushInheritedStyle();
			bool controlChanged = false;
			switch (p.second) {
			case 0:
				controlChanged = WeatherUtils::DrawSliderInt8(controlLabel, settings.weatherProperties[p.first]);
				break;
			case 1:
				controlChanged = WeatherUtils::DrawColorEdit(controlLabel, settings.weatherColors[p.first]);
				break;
			case 2:
				controlChanged = WeatherUtils::DrawSliderUint8(controlLabel, settings.weatherProperties[p.first]);
				break;
			case 3:
				controlChanged = WeatherUtils::DrawSliderFloat(controlLabel, settings.fogProperties[p.first]);
				break;
			default:
				break;
			}
			if (controlChanged) {
				changed = true;
				if (isInherited)
					settings.inheritFlags[p.first] = false;
			}
			if (isInherited) {
				Util::AddTooltip(T(TKEY("inherited_from_parent_weather"), "Inherited from parent weather"));
				PopInheritedStyle();
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

void WeatherWidget::SyncInheritedValuesFromParent()
{
	WeatherWidget* parentWidget = GetParent();
	if (!parentWidget)
		return;

	const auto& flags = settings.inheritFlags;
	auto inherited = [&](const std::string& key) {
		auto it = flags.find(key);
		return it != flags.end() && it->second;
	};
	auto syncFogPair = [&](const char* flagKey, const char* dayKey, const char* nightKey) {
		if (inherited(flagKey)) {
			settings.fogProperties[dayKey] = parentWidget->settings.fogProperties[dayKey];
			settings.fogProperties[nightKey] = parentWidget->settings.fogProperties[nightKey];
		}
	};
	auto syncDalcTOD = [&](const char* flagKey, auto accessor) {
		if (inherited(flagKey))
			for (int i = 0; i < ColorTimes::kTotal; i++)
				accessor(settings.dalc[i]) = accessor(parentWidget->settings.dalc[i]);
	};
	auto syncRecord = [&](const std::string& flagKey, auto& ref, const auto& parentRef) {
		if (inherited(flagKey) && ref != parentRef) {
			ref = parentRef;
			pendingReinit = true;
		}
	};

	for (auto& [key, value] : settings.weatherProperties)
		if (inherited(key))
			value = parentWidget->settings.weatherProperties[key];
	for (auto& [key, value] : settings.weatherColors)
		if (inherited(key))
			value = parentWidget->settings.weatherColors[key];

	syncFogPair(WeatherInherit::kFogNear, WeatherSetting::kDayNear, WeatherSetting::kNightNear);
	syncFogPair(WeatherInherit::kFogFar, WeatherSetting::kDayFar, WeatherSetting::kNightFar);
	syncFogPair(WeatherInherit::kFogPower, WeatherSetting::kDayPower, WeatherSetting::kNightPower);
	syncFogPair(WeatherInherit::kFogMax, WeatherSetting::kDayMax, WeatherSetting::kNightMax);

	for (int i = 0; i < ColorTypes::kTotal; i++)
		if (inherited("Atmosphere_" + ColorTypeLabel(i)))
			settings.atmosphereColors[i] = parentWidget->settings.atmosphereColors[i];

	syncDalcTOD(WeatherInherit::kDalcSpecular, [](DALC& d) -> auto& { return d.specular; });
	syncDalcTOD(WeatherInherit::kDalcFresnel, [](DALC& d) -> auto& { return d.fresnelPower; });
	syncDalcTOD(WeatherInherit::kDalcDirXMax, [](DALC& d) -> auto& { return d.directional[0].max; });
	syncDalcTOD(WeatherInherit::kDalcDirXMin, [](DALC& d) -> auto& { return d.directional[0].min; });
	syncDalcTOD(WeatherInherit::kDalcDirYMax, [](DALC& d) -> auto& { return d.directional[1].max; });
	syncDalcTOD(WeatherInherit::kDalcDirYMin, [](DALC& d) -> auto& { return d.directional[1].min; });
	syncDalcTOD(WeatherInherit::kDalcDirZMax, [](DALC& d) -> auto& { return d.directional[2].max; });
	syncDalcTOD(WeatherInherit::kDalcDirZMin, [](DALC& d) -> auto& { return d.directional[2].min; });

	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		if (inherited(std::format("Cloud{}_Color", i)))
			for (int j = 0; j < ColorTimes::kTotal; j++)
				settings.clouds[i].color[j] = parentWidget->settings.clouds[i].color[j];
		if (inherited(std::format("Cloud{}_Alpha", i)))
			for (int j = 0; j < ColorTimes::kTotal; j++)
				settings.clouds[i].cloudAlpha[j] = parentWidget->settings.clouds[i].cloudAlpha[j];
	}

	for (size_t i = 0; i < ColorTimes::kTotal; i++) {
		syncRecord("ImageSpace_" + std::to_string(i), settings.imageSpaceRefs[i], parentWidget->settings.imageSpaceRefs[i]);
		syncRecord("VolumetricLighting_" + std::to_string(i), settings.volumetricLightingRefs[i], parentWidget->settings.volumetricLightingRefs[i]);
	}
	syncRecord("Precipitation", settings.precipitationData, parentWidget->settings.precipitationData);
	syncRecord("ReferenceEffect", settings.referenceEffect, parentWidget->settings.referenceEffect);

	ApplyChanges();
}

void WeatherWidget::PropagateToChildren()
{
	if (!EditorWindow::GetSingleton()->settings.enableInheritFromParent)
		return;

	static thread_local std::unordered_set<const WeatherWidget*> visiting;
	if (!visiting.insert(this).second)
		return;

	const std::string myId = GetEditorID();
	for (auto& widget : EditorWindow::GetSingleton()->weatherWidgets) {
		auto* child = static_cast<WeatherWidget*>(widget.get());
		if (child != this && child->settings.parent == myId) {
			bool hasAnyInherit = false;
			for (const auto& [key, val] : child->settings.inheritFlags)
				if (val) {
					hasAnyInherit = true;
					break;
				}
			if (hasAnyInherit && !visiting.contains(child))
				child->SyncInheritedValuesFromParent();
		}
	}

	visiting.erase(this);
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

	// Basic tab — weatherProperties and weatherColors (e.g. Lightning Color)
	for (const auto& [key, value] : parentWidget->settings.weatherProperties) {
		settings.weatherProperties[key] = value;
		settings.inheritFlags[key] = true;
	}
	for (const auto& [key, value] : parentWidget->settings.weatherColors) {
		settings.weatherColors[key] = value;
		settings.inheritFlags[key] = true;
	}

	// DALC tab
	static constexpr const char* kDalcFlags[] = {
		WeatherInherit::kDalcSpecular,
		WeatherInherit::kDalcFresnel,
		WeatherInherit::kDalcDirXMax,
		WeatherInherit::kDalcDirXMin,
		WeatherInherit::kDalcDirYMax,
		WeatherInherit::kDalcDirYMin,
		WeatherInherit::kDalcDirZMax,
		WeatherInherit::kDalcDirZMin,
	};
	for (int i = 0; i < ColorTimes::kTotal; i++)
		settings.dalc[i] = parentWidget->settings.dalc[i];
	for (const auto* key : kDalcFlags)
		settings.inheritFlags[key] = true;

	// Atmosphere tab
	for (int i = 0; i < ColorTypes::kTotal; i++) {
		settings.atmosphereColors[i] = parentWidget->settings.atmosphereColors[i];
		settings.inheritFlags["Atmosphere_" + ColorTypeLabel(i)] = true;
	}

	// Clouds tab
	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		settings.clouds[i] = parentWidget->settings.clouds[i];
		settings.inheritFlags[std::format("Cloud{}_Color", i)] = true;
		settings.inheritFlags[std::format("Cloud{}_Alpha", i)] = true;
	}

	// Fog tab
	settings.fogProperties = parentWidget->settings.fogProperties;
	static constexpr const char* kFogFlags[] = {
		WeatherInherit::kFogNear,
		WeatherInherit::kFogFar,
		WeatherInherit::kFogPower,
		WeatherInherit::kFogMax,
	};
	for (const auto* key : kFogFlags)
		settings.inheritFlags[key] = true;

	// Records tab
	for (size_t i = 0; i < ColorTimes::kTotal; i++) {
		settings.imageSpaceRefs[i] = parentWidget->settings.imageSpaceRefs[i];
		settings.volumetricLightingRefs[i] = parentWidget->settings.volumetricLightingRefs[i];
		settings.inheritFlags["ImageSpace_" + std::to_string(i)] = true;
		settings.inheritFlags["VolumetricLighting_" + std::to_string(i)] = true;
	}
	settings.precipitationData = parentWidget->settings.precipitationData;
	settings.referenceEffect = parentWidget->settings.referenceEffect;
	settings.inheritFlags["Precipitation"] = true;
	settings.inheritFlags["ReferenceEffect"] = true;

	// Form references require a weather reinit to propagate
	pendingReinit = true;
	if (EditorWindow::GetSingleton()->settings.autoApplyChanges)
		ApplyChanges();

	EditorWindow::GetSingleton()->ShowNotification(
		std::format("Inherited all settings from {}", parentWidget->GetEditorID()),
		Util::Colors::GetSuccess(),
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
				Util::Colors::GetWarning(),
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
	PropagateToChildren();
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
	ImGui::TextWrapped("%s",
		T(TKEY("feature_specific_settings"),
			"Configure feature-specific settings that will be applied when this weather is active. "
			"These override the feature's global settings for this weather only."));
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

		std::string displayName = feature->GetDisplayName();
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

		if (ImGui::TreeNodeEx(std::format("{}##{}", displayName, featureName).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
			// Check if weather-specific overrides are enabled (using special key)
			bool overridesEnabled = featureJsonView ? featureJsonView->value("__enabled", false) : false;

			// Weather-specific override toggle
			ImGui::PushStyleColor(ImGuiCol_Button, overridesEnabled ? WidgetUI::kOverrideEnabledButton : Util::Colors::GetDisabled());
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, overridesEnabled ? WidgetUI::kOverrideEnabledButtonHovered : WidgetUI::kOverrideDisabledButtonHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, overridesEnabled ? WidgetUI::kOverrideEnabledButtonActive : WidgetUI::kOverrideDisabledButtonActive);

			bool toggleClicked = ImGui::Button(overridesEnabled ? T(TKEY("using_weather_specific_settings"), "Using Weather-Specific Settings") : T(TKEY("using_global_settings"), "Using Global Settings"), ImVec2(-1, 0));

			ImGui::PopStyleColor(3);

			if (auto _tt = Util::HoverTooltipWrapper()) {
				if (overridesEnabled) {
					ImGui::Text("%s", T(TKEY("custom_overrides_tooltip_0"), "This weather has custom overrides for this feature."));
					ImGui::Text("%s", T(TKEY("custom_overrides_tooltip_1"), "Click to disable overrides and use global settings instead."));
					ImGui::Text("%s", T(TKEY("custom_overrides_tooltip_2"), "(Settings will be preserved but not applied)"));
				} else {
					ImGui::Text("%s", T(TKEY("global_settings_tooltip_0"), "This weather uses global feature settings."));
					ImGui::Text("%s", T(TKEY("global_settings_tooltip_1"), "Click to enable weather-specific overrides."));
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
							if (ImGui::MenuItem(T(TKEY("reset_to_global"), "Reset to Global"))) {
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
							if (ImGui::MenuItem(T(TKEY("reset_to_global"), "Reset to Global"))) {
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
							if (ImGui::MenuItem(T(TKEY("reset_to_global"), "Reset to Global"))) {
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
							if (ImGui::MenuItem(T(TKEY("reset_to_global"), "Reset to Global"))) {
								featureJson.erase(varName);
								modified = true;
							}
							ImGui::EndPopup();
						}

					} else {
						// Generic handling for other types
						ImGui::TextDisabled("%s: %s", varDisplayName.c_str(), currentValue.dump().c_str());
						if (auto _tt = Util::HoverTooltipWrapper()) {
							Util::Text::Warning("%s", T(TKEY("unsupported_variable_type"), "Unsupported Variable Type"));
							ImGui::Text("%s", tooltip.c_str());
							ImGui::Separator();
							ImGui::TextWrapped("%s", T(TKEY("unsupported_variable_type_tooltip"), "This variable type doesn't have a custom UI implementation yet. The raw JSON value is shown above."));
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
				ImGui::TextColored(WidgetUI::kHelpTextColor, "%s", T(TKEY("enable_weather_overrides_hint"), "Enable weather-specific overrides above to customize settings for this weather."));
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

	const char* basicEntries[] = {
		"Sun Damage", "Wind Speed", "Wind Direction", "Wind Direction Range",
		"Precipitation Begin Fade In", "Precipitation End Fade Out",
		"Thunder Lightning Begin Fade In", "Thunder Lightning End Fade Out",
		"Thunder Lightning Frequency", "Lightning Color",
		"Visual Effect Begin", "Visual Effect End", "Trans Delta"
	};
	for (const auto* name : basicEntries) {
		results.push_back({ TranslateWeatherPropertyLabel(name), WeatherTab::kBasic, name });
	}

	results.push_back({ T(TKEY("day_near"), "Day Near"), WeatherTab::kFog, WeatherSetting::kDayNear });
	results.push_back({ T(TKEY("day_far"), "Day Far"), WeatherTab::kFog, WeatherSetting::kDayFar });
	results.push_back({ T(TKEY("day_power"), "Day Power"), WeatherTab::kFog, WeatherSetting::kDayPower });
	results.push_back({ T(TKEY("day_max"), "Day Max"), WeatherTab::kFog, WeatherSetting::kDayMax });
	results.push_back({ T(TKEY("night_near"), "Night Near"), WeatherTab::kFog, WeatherSetting::kNightNear });
	results.push_back({ T(TKEY("night_far"), "Night Far"), WeatherTab::kFog, WeatherSetting::kNightFar });
	results.push_back({ T(TKEY("night_power"), "Night Power"), WeatherTab::kFog, WeatherSetting::kNightPower });
	results.push_back({ T(TKEY("night_max"), "Night Max"), WeatherTab::kFog, WeatherSetting::kNightMax });

	results.push_back({ T(TKEY("dalc_fresnel_power"), "Fresnel Power"), WeatherTab::kDalc, WeatherSetting::kFresnelPower });
	results.push_back({ T(TKEY("dalc_specular"), "Specular"), WeatherTab::kDalc, WeatherSetting::kSpecular });
	results.push_back({ T(TKEY("dalc_directional_x_max"), "Directional +X"), WeatherTab::kDalc, WeatherSetting::kDirectionalXMax });
	results.push_back({ T(TKEY("dalc_directional_x_min"), "Directional -X"), WeatherTab::kDalc, WeatherSetting::kDirectionalXMin });
	results.push_back({ T(TKEY("dalc_directional_y_max"), "Directional +Y"), WeatherTab::kDalc, WeatherSetting::kDirectionalYMax });
	results.push_back({ T(TKEY("dalc_directional_y_min"), "Directional -Y"), WeatherTab::kDalc, WeatherSetting::kDirectionalYMin });
	results.push_back({ T(TKEY("dalc_directional_z_max"), "Directional +Z"), WeatherTab::kDalc, WeatherSetting::kDirectionalZMax });
	results.push_back({ T(TKEY("dalc_directional_z_min"), "Directional -Z"), WeatherTab::kDalc, WeatherSetting::kDirectionalZMin });

	for (int i = 0; i < ColorTypes::kTotal; i++) {
		std::string colorType = ColorTypeLabel(i);
		results.push_back({ colorType, WeatherTab::kAtmosphere, colorType });
	}

	for (int i = 0; i < TESWeather::kTotalLayers; i++) {
		std::string layerId = std::vformat(T(TKEY("cloud_layer"), "Cloud Layer {}"), std::make_format_args(i));
		results.push_back({ layerId, WeatherTab::kClouds, layerId });
	}

	// Records tab: one entry per time-of-day slot for each form-picker section
	for (int i = 0; i < ColorTimes::kTotal; i++) {
		std::string label = std::format("{} {}", T(TKEY("record_imagespace"), "ImageSpace"), ColorTimeLabel(i));
		results.push_back({ label, WeatherTab::kRecords, label });
	}
	for (int i = 0; i < ColorTimes::kTotal; i++) {
		std::string label = std::format("{} {}", T(TKEY("record_volumetric_lighting"), "Volumetric Lighting"), ColorTimeLabel(i));
		results.push_back({ label, WeatherTab::kRecords, label });
	}
	results.push_back({ T(TKEY("record_precipitation"), "Precipitation"), WeatherTab::kRecords, WeatherRecord::kPrecipitation });
	results.push_back({ T(TKEY("record_visual_effect"), "Visual Effect"), WeatherTab::kRecords, WeatherRecord::kVisualEffect });

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

#undef I18N_KEY_PREFIX

	std::string resourcePath = WeatherUtils::TexturePath::BuildResourcePath(texturePath);

	ID3D11ShaderResourceView* srv = nullptr;
	ImVec2 textureSize;

	if (Util::LoadDDSTextureFromFile(globals::d3d::device, resourcePath.c_str(), &srv, textureSize)) {
		cloudTextureCache[layerIndex] = srv;
		return srv;
	}

	return nullptr;
}
