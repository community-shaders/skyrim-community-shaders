#include "CSEditor.h"
#include "I18n/I18n.h"

#define I18N_KEY_PREFIX "feature.cs_editor."

#include "CSEditor/SceneManager/SceneSettingsManager.h"
#include "Deferred.h"
#include "Feature.h"
#include "Menu.h"
#include "State.h"
#include "Util.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

#include "CSEditor/EditorWindow.h"
#include "CSEditor/FormEditSources.h"
#include "CSEditor/WeatherPickerWindow.h"
#include "Features/PerformanceOverlay.h"
#include "Menu/ThemeManager.h"
#include "imgui_internal.h"
#include <cstring>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>

namespace
{
	constexpr const char* kJsonExtension = ".json";
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CSEditor::WeatherDetailsWindowSettings,
	Enabled)

void CSEditor::PostPostLoad()
{
	// Before the game loop starts, so no call site can be executing while it is rewritten.
	EditorWindow::InstallWeatherLockHooks();
}

void CSEditor::DataLoaded()
{
	s_dataAvailable = true;
	EditorWindow::MenuOpenCloseEventHandler::Register();
}

bool CSEditor::HasWidgetJsonFiles()
{
	if (s_checkedWidgetJsonFiles)
		return s_hasWidgetJsonFiles;

	const auto communityShaderPath = Util::PathHelpers::GetCommunityShaderPath();
	for (const auto folderName : Widget::kSaveFolderNames) {
		const auto widgetSettingsPath = communityShaderPath / std::filesystem::path(folderName);
		std::error_code ec;
		const bool isDirectory = std::filesystem::is_directory(widgetSettingsPath, ec);
		if (ec) {
			// A missing folder is the normal case (the user simply has no saved
			// widgets for this category), so don't treat it as a warning.
			if (ec != std::errc::no_such_file_or_directory)
				logger::warn("[CSEditor] Failed to inspect widget settings path '{}': {}", widgetSettingsPath.string(), ec.message());
			continue;
		}
		if (!isDirectory)
			continue;

		for (std::filesystem::directory_iterator it(widgetSettingsPath, ec), end; !ec && it != end; it.increment(ec)) {
			std::error_code entryEc;
			const bool isRegularFile = it->is_regular_file(entryEc);
			if (entryEc) {
				logger::warn("[CSEditor] Failed to inspect widget settings file '{}': {}", it->path().string(), entryEc.message());
				continue;
			}
			if (isRegularFile && _stricmp(it->path().extension().string().c_str(), kJsonExtension) == 0) {
				logger::info("[CSEditor] Detected widget settings in '{}'", widgetSettingsPath.string());
				s_hasWidgetJsonFiles = true;
				s_checkedWidgetJsonFiles = true;
				return true;
			}
		}
		if (ec) {
			logger::warn("[CSEditor] Failed to scan widget settings path '{}': {}", widgetSettingsPath.string(), ec.message());
			continue;
		}
	}

	s_hasWidgetJsonFiles = FormEditSources::HasPackFormEdits();
	s_checkedWidgetJsonFiles = true;
	return s_hasWidgetJsonFiles;
}

bool CSEditor::ShouldPreloadEditorResources()
{
	return s_dataAvailable && !s_resourcesInitialized && EditorWindow::CanBeOpen() && HasWidgetJsonFiles();
}

void CSEditor::ReloadFormEdits()
{
	const auto changedKeys = FormEditSources::Refresh();
	// Lets the next Prepass build the widgets when the new pack is the first source of form edits.
	s_checkedWidgetJsonFiles = false;
	if (s_resourcesInitialized)
		EditorWindow::GetSingleton()->ReloadFormEdits(changedKeys);
}

void CSEditor::EnsureWeatherListLoaded()
{
	if (!s_dataAvailable)
		return;

	LoadAllWeathers();
}

void CSEditor::EnsureDataLoaded()
{
	if (!s_dataAvailable)
		return;

	if (!s_resourcesInitialized) {
		EditorWindow::GetSingleton()->SetupResources();
		s_resourcesInitialized = true;
	}
	LoadAllWeathers();
}

void CSEditor::OpenEditorWindow()
{
	auto* editorWindow = EditorWindow::GetSingleton();
	if (!editorWindow->open && EditorWindow::IsOpeningBlocked()) {
		editorWindow->WarnOpeningBlocked();
		return;
	}
	if (!EditorWindow::CanBeOpen())
		return;

	EnsureDataLoaded();
	editorWindow->open = true;
}

void CSEditor::ToggleEditorWindow()
{
	auto* editorWindow = EditorWindow::GetSingleton();
	if (!editorWindow)
		return;

	if (!editorWindow->open && EditorWindow::IsOpeningBlocked()) {
		editorWindow->WarnOpeningBlocked();
		return;
	}
	if (!editorWindow->open && !EditorWindow::CanBeOpen())
		return;
	if (!editorWindow->open)
		EnsureDataLoaded();
	editorWindow->open = !editorWindow->open;
}

int8_t LerpInt8_t(const int8_t oldValue, const int8_t newVal, const float lerpValue)
{
	int lerpedValue = (int)std::lerp(oldValue, newVal, lerpValue);
	return (int8_t)std::clamp(lerpedValue, -128, 127);
}

uint8_t LerpUint8_t(const uint8_t oldValue, const uint8_t newVal, const float lerpValue)
{
	int lerpedValue = (int)std::lerp(oldValue, newVal, lerpValue);
	return (uint8_t)std::clamp(lerpedValue, 0, 255);
}

void LerpColor(const RE::TESWeather::Data::Color3& oldColor, RE::TESWeather::Data::Color3& newColor, const float changePct)
{
	newColor.red = LerpInt8_t(oldColor.red, newColor.red, changePct);
	newColor.green = LerpInt8_t(oldColor.green, newColor.green, changePct);
	newColor.blue = LerpInt8_t(oldColor.blue, newColor.blue, changePct);
}

void LerpColor(const RE::Color& oldColor, RE::Color& newColor, const float changePct)
{
	newColor.red = LerpUint8_t(oldColor.red, newColor.red, changePct);
	newColor.green = LerpUint8_t(oldColor.green, newColor.green, changePct);
	newColor.blue = LerpUint8_t(oldColor.blue, newColor.blue, changePct);
}

void LerpDirectional(RE::BGSDirectionalAmbientLightingColors::Directional& oldColor, RE::BGSDirectionalAmbientLightingColors::Directional& newColor, const float changePct)
{
	LerpColor(oldColor.x.max, newColor.x.max, changePct);
	LerpColor(oldColor.x.min, newColor.x.min, changePct);
	LerpColor(oldColor.y.max, newColor.y.max, changePct);
	LerpColor(oldColor.y.min, newColor.y.min, changePct);
	LerpColor(oldColor.z.max, newColor.z.max, changePct);
	LerpColor(oldColor.z.min, newColor.z.min, changePct);
}

void CSEditor::DrawSettings()
{
	EnsureWeatherListLoaded();

	ImGui::BeginDisabled(!EditorWindow::CanBeOpen());
	if (ImGui::Button(T(TKEY("open_editor"), "Open CS Editor"), { -1, 0 }))
		OpenEditorWindow();
	ImGui::EndDisabled();
}

void CSEditor::Prepass()
{
	if (ShouldPreloadEditorResources()) {
		EnsureDataLoaded();
	}

	EditorWindow::MaintainWeatherLock();
}

void CSEditor::LerpWeather(RE::TESWeather* oldWeather, RE::TESWeather* newWeather, float currentWeatherPct)
{
	if (!oldWeather || !newWeather) {
		// Avoid dereferencing null pointers; nothing to lerp.
		return;
	}

	//// Precipitation
	newWeather->data.precipitationBeginFadeIn = LerpUint8_t(oldWeather->data.precipitationBeginFadeIn, newWeather->data.precipitationBeginFadeIn, currentWeatherPct);
	newWeather->data.precipitationEndFadeOut = LerpUint8_t(oldWeather->data.precipitationEndFadeOut, newWeather->data.precipitationEndFadeOut, currentWeatherPct);

	//// Sun
	newWeather->data.sunDamage = LerpUint8_t(oldWeather->data.sunDamage, newWeather->data.sunDamage, currentWeatherPct);
	newWeather->data.sunGlare = LerpUint8_t(oldWeather->data.sunGlare, newWeather->data.sunGlare, currentWeatherPct);

	//// Lightning
	newWeather->data.thunderLightningBeginFadeIn = LerpUint8_t(oldWeather->data.thunderLightningBeginFadeIn, newWeather->data.thunderLightningBeginFadeIn, currentWeatherPct);
	newWeather->data.thunderLightningEndFadeOut = LerpUint8_t(oldWeather->data.thunderLightningEndFadeOut, newWeather->data.thunderLightningEndFadeOut, currentWeatherPct);
	newWeather->data.thunderLightningFrequency = (int8_t)LerpUint8_t((uint8_t)oldWeather->data.thunderLightningFrequency, (uint8_t)newWeather->data.thunderLightningFrequency, currentWeatherPct);
	LerpColor(oldWeather->data.lightningColor, newWeather->data.lightningColor, currentWeatherPct);

	//// Trans delta
	newWeather->data.transDelta = LerpUint8_t(oldWeather->data.transDelta, newWeather->data.transDelta, currentWeatherPct);

	//// Visual Effects
	newWeather->data.visualEffectBegin = LerpUint8_t(oldWeather->data.visualEffectBegin, newWeather->data.visualEffectBegin, currentWeatherPct);
	newWeather->data.visualEffectEnd = LerpUint8_t(oldWeather->data.visualEffectEnd, newWeather->data.visualEffectEnd, currentWeatherPct);

	//// Wind
	newWeather->data.windDirection = LerpUint8_t(oldWeather->data.windDirection, newWeather->data.windDirection, currentWeatherPct);
	newWeather->data.windDirectionRange = LerpUint8_t(oldWeather->data.windDirectionRange, newWeather->data.windDirectionRange, currentWeatherPct);
	newWeather->data.windSpeed = LerpUint8_t(oldWeather->data.windSpeed, newWeather->data.windSpeed, currentWeatherPct);

	//// Fog
	newWeather->fogData.dayFar = std::lerp(oldWeather->fogData.dayFar, newWeather->fogData.dayFar, currentWeatherPct);
	newWeather->fogData.dayMax = std::lerp(oldWeather->fogData.dayMax, newWeather->fogData.dayMax, currentWeatherPct);
	newWeather->fogData.dayNear = std::lerp(oldWeather->fogData.dayNear, newWeather->fogData.dayNear, currentWeatherPct);
	newWeather->fogData.dayPower = std::lerp(oldWeather->fogData.dayPower, newWeather->fogData.dayPower, currentWeatherPct);

	newWeather->fogData.nightFar = std::lerp(oldWeather->fogData.nightFar, newWeather->fogData.nightFar, currentWeatherPct);
	newWeather->fogData.nightMax = std::lerp(oldWeather->fogData.nightMax, newWeather->fogData.nightMax, currentWeatherPct);
	newWeather->fogData.nightNear = std::lerp(oldWeather->fogData.nightNear, newWeather->fogData.nightNear, currentWeatherPct);
	newWeather->fogData.nightPower = std::lerp(oldWeather->fogData.nightPower, newWeather->fogData.nightPower, currentWeatherPct);

	//// Weather colors
	for (size_t i = 0; i < RE::TESWeather::ColorTypes::kTotal; i++) {
		for (size_t j = 0; j < RE::TESWeather::ColorTime::kTotal; j++) {
			LerpColor(oldWeather->colorData[i][j], newWeather->colorData[i][j], currentWeatherPct);
		}
	}

	//// DALC
	for (size_t i = 0; i < RE::TESWeather::ColorTime::kTotal; i++) {
		auto& newDALC = newWeather->directionalAmbientLightingColors[i];
		auto& oldDALC = oldWeather->directionalAmbientLightingColors[i];

		LerpColor(oldDALC.specular, newDALC.specular, currentWeatherPct);
		newWeather->directionalAmbientLightingColors[i].fresnelPower = std::lerp(oldDALC.fresnelPower, newDALC.fresnelPower, currentWeatherPct);
		LerpDirectional(oldDALC.directional, newDALC.directional, currentWeatherPct);
	}

	//// Clouds
	for (size_t i = 0; i < RE::TESWeather::kTotalLayers; i++) {
		for (size_t j = 0; j < RE::TESWeather::ColorTime::kTotal; j++) {
			LerpColor(oldWeather->cloudColorData[i][j], newWeather->cloudColorData[i][j], currentWeatherPct);
			newWeather->cloudAlpha[i][j] = std::lerp(oldWeather->cloudAlpha[i][j], newWeather->cloudAlpha[i][j], currentWeatherPct);
		}

		newWeather->cloudLayerSpeedY[i] = LerpInt8_t(oldWeather->cloudLayerSpeedY[i], newWeather->cloudLayerSpeedY[i], currentWeatherPct);
		newWeather->cloudLayerSpeedX[i] = LerpInt8_t(oldWeather->cloudLayerSpeedX[i], newWeather->cloudLayerSpeedX[i], currentWeatherPct);
	}
}

void CSEditor::DrawTimeControls()
{
	ImGui::Spacing();
	EditorWindow::GetSingleton()->DrawTimeControls();
	ImGui::Spacing();
}

// ================================================================================
// Weather Picker functionality (integrated from WeatherPicker feature)
// ================================================================================

void CSEditor::RenderWeatherDetailsWindow(bool* open, bool showSectionHeaders)
{
	if (!open || !*open)
		return;

	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->parentCell)
		return;

	// Start in the bottom-left corner so it doesn't open over the Performance Overlay (top-left).
	// The window has no size until it has been drawn once, so keep anchoring until it reports
	// one, then leave it where the user drags it.
	const float scale = Util::GetUIScale();
	static bool positioned = false;
	if (!positioned) {
		const float pad = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
		ImGui::SetNextWindowPos(ImVec2(pad, ImGui::GetIO().DisplaySize.y - pad), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
		if (ImGuiWindow* window = ImGui::FindWindowByName("Weather Details##Popup"); window && window->WasActive && window->Size.y > 0.0f)
			positioned = true;
	}

	ImGui::SetNextWindowSize(ImVec2(600 * scale, 800 * scale), ImGuiCond_FirstUseEver);
	if (Util::BeginWithRoundedClose("Weather Details##Popup", open, ImGuiWindowFlags_None)) {
		// Enable interactive elements when a menu is open
		auto shouldEnableInteractiveElements = []() -> bool {
			return (Menu::GetSingleton()->ShouldSwallowInput() ||
					(globals::game::ui && globals::game::ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)));
		};

		RenderCoreWeatherDetails(shouldEnableInteractiveElements(), showSectionHeaders);

		// Render weather analysis from features with collapsible headers
		RenderFeatureWeatherAnalysis();
	}
	ImGui::End();
}

void CSEditor::DrawWeatherDebugWindow(bool* open)
{
	if (!open || !*open)
		return;

	const float scale = Util::GetUIScale();
	ImGui::SetNextWindowPos(ImVec2(50.0f * scale, 100.0f * scale), ImGuiCond_FirstUseEver);
	// Width 0 auto-fits to the text on first use; the window stays resizable afterwards.
	ImGui::SetNextWindowSize(ImVec2(0.0f, 520.0f * scale), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f * scale, 160.0f * scale), ImVec2(FLT_MAX, FLT_MAX));

	// "Compact" in the ID drops sizes saved by the old 600x800 default.
	const std::string title = std::string(T(TKEY("weather_debug"), "Weather Debug")) + "##CSEditorWeatherDebugCompact";
	if (Util::BeginWithRoundedClose(title.c_str(), open, ImGuiWindowFlags_None)) {
		const auto& theme = Menu::GetSingleton()->GetTheme();
		auto* sky = globals::game::sky;

		if (sky && sky->currentWeather) {
			if (globals::sceneSettingsManager->HasWeatherConfig(sky->currentWeather->GetFormID()))
				ImGui::TextColored(theme.StatusPalette.SuccessColor, "%s", T(TKEY("has_custom_settings"), "Has Custom Settings"));
		} else {
			ImGui::TextColored(theme.StatusPalette.Warning, "%s", T(TKEY("no_active_weather"), "No Active Weather"));
		}

		RenderCoreWeatherDetails(false, true);
		RenderFeatureWeatherAnalysis();
	}
	ImGui::End();
}

ImVec4 CSEditor::GetWeatherTypeColor(RE::TESWeather* weather)
{
	if (!weather) {
		return Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor;
	}

	const auto& theme = Menu::GetSingleton()->GetTheme();

	// Priority order for weather classification colors (highest priority first)
	static const std::vector<RE::TESWeather::WeatherDataFlag> priorityOrder = {
		RE::TESWeather::WeatherDataFlag::kRainy,
		RE::TESWeather::WeatherDataFlag::kSnow,
		RE::TESWeather::WeatherDataFlag::kPermAurora,
		RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun,
		RE::TESWeather::WeatherDataFlag::kCloudy,
		RE::TESWeather::WeatherDataFlag::kPleasant
	};

	// Check flags in priority order
	for (const auto& flag : priorityOrder) {
		if (weather->data.flags.any(flag)) {
			return GetWeatherFlagColor(flag);
		}
	}

	// Check for unclassified/unflagged weather
	if (weather->data.flags.underlying() == 0) {
		return Menu::GetSingleton()->GetTheme().StatusPalette.Warning;
	}

	return theme.StatusPalette.InfoColor;  // Default blue
}

// --- Helper: Display basic weather info (name, flags, percentage) ---
void CSEditor::DisplayWeatherBasicInfo(RE::TESWeather* weather, float weatherPct)
{
	if (!weather) {
		ImGui::BulletText("%s", T(TKEY("no_weather_found"), "No Weather Found"));
		return;
	}
	std::string weatherText = Util::FormatWeather(weather);
	ImGui::Bullet();
	ImGui::SameLine();
	bool showTooltip = CSEditor::RenderMultiColorWeatherName(weather, weatherText);
	if (showTooltip) {
		ImGui::BeginTooltip();
		ImGui::Text(T(TKEY("tooltip_name"), "Name: %s"), weather->GetName() ? weather->GetName() : "Unnamed");
		ImGui::Text(T(TKEY("tooltip_editor_id_2"), "Editor ID: %s"), weather->GetFormEditorID() ? weather->GetFormEditorID() : "None");
		ImGui::Text(T(TKEY("tooltip_form_id_2"), "Form ID: 0x%08X"), weather->GetFormID());
		auto flagNames = CSEditor::GetWeatherFlagNames(weather);
		if (!flagNames.empty()) {
			std::string joinedFlags = flagNames[0];
			for (size_t j = 1; j < flagNames.size(); ++j) {
				joinedFlags += ", " + flagNames[j];
			}
			ImGui::Text(T(TKEY("tooltip_flags"), "Flags: %s"), joinedFlags.c_str());
		} else {
			ImGui::Text("%s", T(TKEY("tooltip_flags_none"), "Flags: None"));
		}
		ImGui::EndTooltip();
	}
	if (weatherPct >= 0.0f) {
		ImGui::BulletText(T(TKEY("weather_percentage"), "Weather Percentage: %.1f%%"), weatherPct * 100.0f);
	}
}

void CSEditor::DisplayPrecipitationInfo(RE::TESWeather* weather)
{
	if (!weather || !weather->precipitationData) {
		ImGui::BulletText("%s", T(TKEY("no_precipitation_data"), "Particle Density: No precipitation data"));
		return;
	}
	auto particleDensity = weather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	ImGui::BulletText(T(TKEY("particle_density"), "Particle Density: %.3f"), particleDensity);
	auto& particleTexture = weather->precipitationData->GetRuntimeData().particleTexture;
	if (!particleTexture.textureName.empty()) {
		ImGui::BulletText(T(TKEY("particle_texture"), "Particle Texture: %s"), particleTexture.textureName.c_str());
	} else {
		ImGui::BulletText("%s", T(TKEY("particle_texture_none"), "Particle Texture: None"));
	}
	uint8_t precipBeginFadeIn = weather->data.precipitationBeginFadeIn;
	uint8_t precipEndFadeOut = weather->data.precipitationEndFadeOut;
	float precipBeginNormalized = precipBeginFadeIn / 255.0f;
	float precipEndNormalized = precipEndFadeOut / 255.0f;
	ImGui::BulletText(T(TKEY("precip_begin_fade_in"), "Precip Begin Fade-In: %.3f (raw %u)"), precipBeginNormalized, precipBeginFadeIn);
	ImGui::BulletText(T(TKEY("precip_end_fade_out"), "Precip End Fade-Out: %.3f (raw %u)"), precipEndNormalized, precipEndFadeOut);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		Util::DrawMultiLineTooltip({ T(TKEY("precip_fade_info_0"), "Precipitation fade transition parameters:"),
			T(TKEY("precip_fade_info_1"), "Begin Fade-In: Point where precipitation starts appearing"),
			T(TKEY("precip_fade_info_2"), "End Fade-Out: Point where precipitation fully disappears"),
			T(TKEY("precip_fade_info_3"), "Raw values: 0-255 (uint8), Normalized: 0.0-1.0") });
	}
}

void CSEditor::DisplayLightningInfo(RE::TESWeather* weather, bool showInteractiveElements)
{
	if (!weather || (uint8_t)weather->data.thunderLightningFrequency == 0)
		return;
	const auto& theme = Menu::GetSingleton()->GetTheme();
	uint8_t lightningR = weather->data.lightningColor.red;
	uint8_t lightningG = weather->data.lightningColor.green;
	uint8_t lightningB = weather->data.lightningColor.blue;
	ImGui::Text("%s", T(TKEY("lightning_color"), "Lightning Color:"));
	ImGui::SameLine();
	float lightningColor[3] = { lightningR / 255.0f, lightningG / 255.0f, lightningB / 255.0f };
	ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
	if (!showInteractiveElements) {
		flags |= ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip;
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, theme.StatusPalette.Disable.w);
	}
	bool colorChanged = ImGui::ColorEdit3("##LightningColor", lightningColor, flags);
	if (!showInteractiveElements) {
		ImGui::PopStyleVar();
	}
	if (colorChanged && showInteractiveElements) {
		weather->data.lightningColor.red = static_cast<std::uint8_t>(lightningColor[0] * 255.0f + 0.5f);
		weather->data.lightningColor.green = static_cast<std::uint8_t>(lightningColor[1] * 255.0f + 0.5f);
		weather->data.lightningColor.blue = static_cast<std::uint8_t>(lightningColor[2] * 255.0f + 0.5f);
	}
	uint8_t thunderFreqRaw = (uint8_t)weather->data.thunderLightningFrequency;
	ImGui::BulletText(T(TKEY("thunder_frequency"), "Thunder Frequency: %u"), static_cast<unsigned>(thunderFreqRaw));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		Util::DrawMultiLineTooltip({ T(TKEY("thunder_freq_info_0"), "Thunder frequency raw value (0-255):"),
			"",
			T(TKEY("thunder_freq_info_1"), "Known data points from Creation Kit slider:"),
			T(TKEY("thunder_freq_info_2"), "- Raw 15 = ~100% frequency (highest thunder)"),
			T(TKEY("thunder_freq_info_3"), "- Raw 76 = ~75% frequency"),
			T(TKEY("thunder_freq_info_4"), "- Raw 203 = ~20% frequency"),
			T(TKEY("thunder_freq_info_5"), "- Raw 246 = ~5% frequency"),
			T(TKEY("thunder_freq_info_6"), "- Raw 255 = ~0% frequency (lowest thunder)"),
			"",
			T(TKEY("thunder_freq_info_7"), "Range: 0-255 (unsigned 8-bit integer)"),
			T(TKEY("thunder_freq_info_8"), "Note: Creation Kit interprets this value non-linearly") });
	}
	uint8_t lightningBeginFadeIn = weather->data.thunderLightningBeginFadeIn;
	uint8_t lightningEndFadeOut = weather->data.thunderLightningEndFadeOut;
	float lightningBeginNormalized = lightningBeginFadeIn / 255.0f;
	float lightningEndNormalized = lightningEndFadeOut / 255.0f;
	ImGui::BulletText(T(TKEY("lightning_begin_fade_in"), "Lightning Begin Fade-In: %.3f (raw %u)"), lightningBeginNormalized, lightningBeginFadeIn);
	ImGui::BulletText(T(TKEY("lightning_end_fade_out"), "Lightning End Fade-Out: %.3f (raw %u)"), lightningEndNormalized, lightningEndFadeOut);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		Util::DrawMultiLineTooltip({ T(TKEY("lightning_fade_info_0"), "Lightning fade transition parameters:"),
			T(TKEY("lightning_fade_info_1"), "Begin Fade-In: Point where lightning starts appearing"),
			T(TKEY("lightning_fade_info_2"), "End Fade-Out: Point where lightning fully disappears"),
			T(TKEY("lightning_fade_info_3"), "Raw values: 0-255 (uint8), Normalized: 0.0-1.0") });
	}
}

void CSEditor::DisplayWindInfo(RE::TESWeather* weather)
{
	auto sky = globals::game::sky;
	if (!weather || (weather->data.windSpeed <= 0 && (!sky || sky->windSpeed <= 0.0f)))
		return;
	const auto& theme = Menu::GetSingleton()->GetTheme();
	float windSpeedDisplay = weather->data.windSpeed / 255.0f;
	ImGui::BulletText(T(TKEY("weather_wind_speed"), "Weather Wind Speed: %.2f (raw %d)"), windSpeedDisplay, weather->data.windSpeed);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::string windStr = Util::Units::FormatWindSpeed(weather->data.windSpeed);
		Util::DrawMultiLineTooltip({ T(TKEY("wind_speed_tooltip_0"), "Wind speed from weather definition"),
			windStr.c_str() });
	}
	if (sky) {
		ImGui::BulletText(T(TKEY("sky_wind_speed"), "Sky Wind Speed: %.2f"), sky->windSpeed);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			Util::DrawMultiLineTooltip({ T(TKEY("sky_wind_tooltip_0"), "Current active wind speed from the sky system"),
				T(TKEY("sky_wind_tooltip_1"), "This affects particle behavior and wind-based effects") });
		}
	}
	float weatherWindDirDegrees = Util::Units::DirectionRawToDegrees(weather->data.windDirection);
	ImGui::BulletText(T(TKEY("wind_direction"), "Wind Direction: %.1f\xc2\xb0 (raw %d)"), weatherWindDirDegrees, weather->data.windDirection);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::string dirStr = Util::Units::FormatDirection(weather->data.windDirection);
		Util::DrawMultiLineTooltip({ T(TKEY("wind_direction_tooltip_0"), "Wind direction from weather definition"),
			dirStr.c_str() });
	}
	float weatherWindRangeDegrees = Util::Units::DirectionRangeToDegrees(weather->data.windDirectionRange);
	ImGui::BulletText(T(TKEY("wind_direction_range"), "Wind Direction Range: %.1f\xc2\xb0 (raw %d)"), weatherWindRangeDegrees, weather->data.windDirectionRange);

	if (auto player = RE::PlayerCharacter::GetSingleton()) {
		float playerAngleZ = player->GetAngleZ();
		float playerAngleDegrees = Util::Units::NormalizeDegrees0To360(Util::Units::RadiansToDegrees(playerAngleZ));
		ImGui::BulletText(T(TKEY("player_direction"), "Player Direction: %.1f\xc2\xb0"), playerAngleDegrees);
		float effectiveWindDirection = Util::Units::NormalizeDegrees0To360(weatherWindDirDegrees - WIND_DIRECTION_OFFSET);
		float rawDifference = Util::Units::NormalizeDegreesToSignedRange(effectiveWindDirection - playerAngleDegrees);
		ImGui::BulletText(T(TKEY("effective_wind_dir"), "Effective Wind Dir: %.1f\xc2\xb0 (raw - %.1f\xc2\xb0)"), effectiveWindDirection, WIND_DIRECTION_OFFSET);
		ImGui::BulletText(T(TKEY("wind_vs_player"), "Wind vs Player: %.1f\xc2\xb0"), rawDifference);
		const char* windRelation;
		if (std::abs(rawDifference) < 30.0f) {
			windRelation = T(TKEY("tailwind"), "Tailwind (wind behind player)");
		} else if (std::abs(rawDifference) > 150.0f) {
			windRelation = T(TKEY("headwind"), "Headwind (wind coming toward player)");
		} else if (rawDifference > 0) {
			windRelation = T(TKEY("right_crosswind"), "Right crosswind");
		} else {
			windRelation = T(TKEY("left_crosswind"), "Left crosswind");
		}
		ImGui::SameLine();
		ImGui::TextColored(theme.StatusPalette.RestartNeeded, "(%s)", windRelation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			Util::DrawMultiLineTooltip({
				T(TKEY("wind_vs_player_tooltip_0"), "Wind relative to player direction:"),
				T(TKEY("wind_vs_player_tooltip_1"), "- ~0\xc2\xb0 = Tailwind (wind behind player)"),
				T(TKEY("wind_vs_player_tooltip_2"),
					"- ~\xc2\xb1"
					"90\xc2\xb0 = Crosswind (left/right)"),
				T(TKEY("wind_vs_player_tooltip_3"),
					"- ~\xc2\xb1"
					"180\xc2\xb0 = Headwind (wind coming toward player)"),
			});
		}
	}
}

// --- Main function: now just delegates to helpers ---
void CSEditor::DisplayWeatherInfo(RE::TESWeather* weather, float weatherPct, bool showInteractiveElements)
{
	CSEditor::DisplayWeatherBasicInfo(weather, weatherPct);
	CSEditor::DisplayPrecipitationInfo(weather);
	CSEditor::DisplayLightningInfo(weather, showInteractiveElements);
	CSEditor::DisplayWindInfo(weather);
}

void CSEditor::SyncWeatherFilter()
{
	if (s_lastWeatherFlagFilter != s_weatherFlagFilter) {
		UpdateFilteredWeathers();
		s_selectedWeatherIdx = -1;
		s_lastWeatherFlagFilter = s_weatherFlagFilter;
	}
}

void CSEditor::SelectWeather(RE::Sky* sky, int filteredIdx)
{
	if (!sky || filteredIdx < 0 || filteredIdx >= static_cast<int>(s_filteredWeathers.size()))
		return;

	s_selectedWeatherIdx = filteredIdx;
	auto selectedWeather = s_filteredWeathers[filteredIdx];

	if (s_accelerateWeatherChange)
		Util::ForceWeather(sky, selectedWeather, false);
	else
		sky->SetWeather(selectedWeather, true, false);

	auto editorWindow = EditorWindow::GetSingleton();
	if (editorWindow->IsWeatherLocked())
		editorWindow->LockWeather(selectedWeather);

	logger::info("[CSEditor] Changed weather to: {}", Util::FormatWeather(selectedWeather));
}

void CSEditor::ResetWeatherToDefault(RE::Sky* sky)
{
	if (!sky)
		return;

	// A held lock redirects every engine SetWeather/ForceWeather back to the locked weather,
	// which would swallow the reset.
	auto editorWindow = EditorWindow::GetSingleton();
	if (editorWindow->IsWeatherLocked())
		editorWindow->UnlockWeather();

	sky->ResetWeather();
	s_selectedWeatherIdx = FindWeatherIndex(sky->defaultWeather);
	logger::info("[CSEditor] Reset weather to default");
}

void CSEditor::DrawWeatherTransitionBar(RE::Sky* sky, float width, bool verboseOverlay)
{
	if (!sky)
		return;

	if (sky->lastWeather)
		s_cachedLastWeather = sky->lastWeather;

	auto* lastWeather = sky->lastWeather;
	const float lerpFactor = sky->currentWeatherPct;
	const bool isTransitioning = lastWeather && lerpFactor < 1.0f;
	const float displayPct = isTransitioning ? lerpFactor : 1.0f;

	if (!isTransitioning) {
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	}

	std::string transitionOverlay;
	if (isTransitioning) {
		const float transitionPct = lerpFactor * 100.0f;
		if (verboseOverlay) {
			std::string fromName = "?";
			if (auto* editorId = lastWeather->GetFormEditorID(); editorId && editorId[0] != '\0')
				fromName = editorId;
			else
				fromName = std::format("{:08X}", lastWeather->GetFormID());
			transitionOverlay = I18n::GetSingleton()->Format("feature.cs_editor.transition_overlay",
				{ { "percent", std::format("{:.1f}", transitionPct) }, { "weather", fromName } },
				"{percent}% | Transitioning from {weather}");
		} else {
			transitionOverlay = std::format("{:.0f}%", transitionPct);
		}
	} else {
		transitionOverlay = "100%";
	}
	ImGui::ProgressBar(displayPct, ImVec2(width, 0), transitionOverlay.c_str());

	if (!isTransitioning) {
		ImGui::PopStyleColor();
	}
}

void CSEditor::DrawWeatherFilterChips()
{
	struct Chip
	{
		const char* label;
		uint32_t bit;
		ImVec4 color;
		const char* tooltip;
	};

	const auto& theme = Menu::GetSingleton()->GetTheme();
	const Chip chips[] = {
		{ T(TKEY("pleasant"), "Pleasant"), static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kPleasant), GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag::kPleasant), nullptr },
		{ T(TKEY("cloudy"), "Cloudy"), static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kCloudy), GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag::kCloudy), nullptr },
		{ T(TKEY("rainy"), "Rainy"), static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kRainy), GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag::kRainy), nullptr },
		{ T(TKEY("snow"), "Snow"), static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kSnow), GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag::kSnow), nullptr },
		{ T(TKEY("aurora"), "Aurora"), static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kPermAurora), GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag::kPermAurora), nullptr },
		{ T(TKEY("aurora_sun"), "Aurora Sun"), static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun), GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun), nullptr },
		{ T(TKEY("none_filter"), "None"), UNCLASSIFIED_FLAG, theme.StatusPalette.Warning, T(TKEY("none_filter_tooltip_0"), "Shows weathers that are not classified under any specific category.") },
	};

	const float scale = Util::GetUIScale();
	const ImGuiStyle& style = ImGui::GetStyle();
	const char* accelerateLabel = T(TKEY("accelerate"), "Accelerate");

	// Uniform cell size from the widest label so the grid lines up.
	float cellW = ImGui::CalcTextSize(accelerateLabel).x;
	for (const auto& chip : chips)
		cellW = std::max(cellW, ImGui::CalcTextSize(chip.label).x);
	cellW += style.FramePadding.x * 2.0f + 6.0f * scale;
	const float cellH = ImGui::GetFrameHeight();

	// One toggle: dim tint = off, bright tint + solid border = on.
	auto drawChip = [&](const char* label, const ImVec4& color, bool active, const char* tooltip) -> bool {
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton(label, ImVec2(cellW, cellH));
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 max(min.x + cellW, min.y + cellH);

		const float bgAlpha = (active ? 0.50f : 0.10f) + (hovered ? 0.12f : 0.0f);
		const float textAlpha = active ? 1.0f : 0.45f;
		const float borderAlpha = active ? 0.95f : 0.30f;

		auto* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, bgAlpha)), style.FrameRounding);
		dl->AddRect(min, max, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, borderAlpha)), style.FrameRounding);

		const ImVec2 textSize = ImGui::CalcTextSize(label);
		dl->AddText(ImVec2(min.x + (cellW - textSize.x) * 0.5f, min.y + (cellH - textSize.y) * 0.5f),
			ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, textAlpha)), label);

		if (tooltip) {
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(tooltip);
		}
		return clicked;
	};

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f * scale, 3.0f * scale));
	ImGui::PushID("WeatherFilterChips");

	int cell = 0;
	auto nextCell = [&]() {
		if (cell % 2 != 0)
			ImGui::SameLine();
		++cell;
	};

	for (const auto& chip : chips) {
		nextCell();
		if (drawChip(chip.label, chip.color, (s_weatherFlagFilter & chip.bit) != 0, chip.tooltip))
			s_weatherFlagFilter ^= chip.bit;
	}

	nextCell();
	if (drawChip(accelerateLabel, theme.StatusPalette.Disable, s_accelerateWeatherChange,
			T(TKEY("accelerate_weather_change_tooltip"), "When enabled, weather changes instantly")))
		s_accelerateWeatherChange = !s_accelerateWeatherChange;

	ImGui::PopID();
	ImGui::PopStyleVar();
}

void CSEditor::RenderWeatherInformationDisplay(RE::Sky* sky, bool showInteractiveElements, bool showSectionHeader)
{
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();
	if (showSectionHeader) {
		Util::DrawSectionHeader(T(TKEY("weather_information"), "Weather Information"), false, true);
	}

	// Update cache: store current lastWeather if it exists, otherwise keep the cached one
	if (sky->lastWeather) {
		s_cachedLastWeather = sky->lastWeather;
	}

	// Use cached last weather for display if sky->lastWeather is null
	RE::TESWeather* displayLastWeather = sky->lastWeather ? sky->lastWeather : s_cachedLastWeather;

	ImGui::TextUnformatted(T(TKEY("current_weather_column"), "Current Weather"));
	DisplayWeatherInfo(sky->currentWeather, sky->currentWeatherPct, showInteractiveElements);

	ImGui::Spacing();
	ImGui::TextUnformatted(T(TKEY("last_weather_column"), "Last Weather"));
	DisplayWeatherInfo(displayLastWeather, std::abs(sky->currentWeatherPct - 1.0f), showInteractiveElements);
}

void CSEditor::RenderCoreWeatherDetails(bool showInteractiveElements, bool showSectionHeaders)
{
	const auto showError = [](const char* msg) {
		auto menu = Menu::GetSingleton();
		const auto& theme = menu->GetTheme();
		ImGui::TextColored(theme.StatusPalette.Error, "%s", msg);
	};

	if (auto sky = globals::game::sky) {
		if (sky->mode.get() == RE::Sky::Mode::kFull) {
			if (showInteractiveElements) {
				// Same picker as the editor's Weather Picker window (combo, filter chips, reset, transition).
				WeatherPickerWindow::GetSingleton()->DrawContents();
			}
			RenderWeatherInformationDisplay(sky, showInteractiveElements, showSectionHeaders);
			ImGui::Spacing();
		} else {
			showError(T(TKEY("sky_not_full"), "Sky not in full mode"));
		}
	} else {
		showError(T(TKEY("sky_not_available"), "Sky not available"));
	}
}

void CSEditor::LoadAllWeathers()
{
	if (s_weathersLoaded)
		return;

	auto dataHandler = RE::TESDataHandler::GetSingleton();
	if (dataHandler) {
		auto& weatherArray = dataHandler->GetFormArray<RE::TESWeather>();
		s_allWeathers.clear();
		s_allWeathers.reserve(weatherArray.size());
		for (auto weather : weatherArray) {
			if (weather) {
				s_allWeathers.push_back(weather);
			}
		}

		// Sort by name, then editorID, then formID for consistent ordering
		std::sort(s_allWeathers.begin(), s_allWeathers.end(), WeatherNameComparator{});
		s_weathersLoaded = true;
		// Initial population of filtered weathers
		UpdateFilteredWeathers();
	}
}

void CSEditor::UpdateFilteredWeathers()
{
	s_filteredWeathers.clear();
	for (auto weather : s_allWeathers) {
		bool shouldInclude = false;

		// Check if all filters are selected (0x7F = all 7 bits)
		if (s_weatherFlagFilter == ALL_WEATHER_FLAGS) {
			shouldInclude = true;
		} else {
			// Check regular weather flags
			uint32_t weatherFlags = weather->data.flags.underlying();
			if ((weatherFlags & (s_weatherFlagFilter & 0x3F)) != 0) {
				shouldInclude = true;
			}

			// Check for None filter (bit 6) - includes weathers that don't match any of our tracked flags
			if (s_weatherFlagFilter & UNCLASSIFIED_FLAG) {
				// Define the mask for all the specific weather flags we track
				uint32_t trackedFlags = static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kPleasant) |
				                        static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kCloudy) |
				                        static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kRainy) |
				                        static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kSnow) |
				                        static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kPermAurora) |
				                        static_cast<uint32_t>(RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun);

				// Include if weather has no flags or only has flags we don't track
				if ((weatherFlags & trackedFlags) == 0) {
					shouldInclude = true;
				}
			}
		}

		if (shouldInclude) {
			s_filteredWeathers.push_back(weather);
		}
	}
}

int CSEditor::FindWeatherIndex(RE::TESWeather* targetWeather)
{
	if (!targetWeather)
		return -1;
	for (size_t i = 0; i < s_filteredWeathers.size(); ++i) {
		if (s_filteredWeathers[i] == targetWeather) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void CSEditor::RenderFeatureWeatherAnalysis()
{
	// Iterate through all loaded features to show their weather analysis
	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->loaded) {
			// Skip the CSEditor itself to avoid recursion
			if (feature == &globals::features::csEditor) {
				continue;
			}

			// Check if this feature provides weather analysis
			auto weatherConfig = feature->GetWeatherAnalysisConfig();
			if (weatherConfig.sectionName.empty()) {
				continue;  // Skip features that don't provide weather analysis
			}

			auto featureName = feature->GetShortName();
			ImGui::PushID(featureName.c_str());

			const ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
			bool isExpanded = ImGui::TreeNodeEx(weatherConfig.sectionName.c_str(), treeFlags);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("feature_weather_analysis_tooltip_0"), "Weather analysis provided by: "));
				ImGui::Text("%s", feature->GetDisplayName().c_str());
				ImGui::Text("%s", T(TKEY("feature_weather_analysis_tooltip_1"), "Feature category: "));
				ImGui::Text("%s", feature->GetDisplayCategory().c_str());
				ImGui::Text(T(TKEY("feature_weather_analysis_tooltip_2"), "Click to %s this feature's weather data"),
					isExpanded ? T(TKEY("collapse"), "collapse") : T(TKEY("expand"), "expand"));
			}

			if (isExpanded) {
				if (weatherConfig.drawFunction) {
					// Call the feature's weather analysis draw function
					weatherConfig.drawFunction();
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}
	}
}

std::vector<std::string> CSEditor::GetWeatherFlagNames(RE::TESWeather* weather)
{
	std::vector<std::string> flagNames;
	if (!weather) {
		return flagNames;
	}

	uint32_t flags = weather->data.flags.underlying();
	if (flags == 0) {
		flagNames.push_back("None");
		return flagNames;
	}

	// Use magic_enum to iterate through all weather flags
	for (auto flagValue : magic_enum::enum_values<RE::TESWeather::WeatherDataFlag>()) {
		if (flagValue != RE::TESWeather::WeatherDataFlag::kNone &&
			weather->data.flags.any(flagValue)) {
			// Convert enum name to canonical format (strip 'k' prefix)
			std::string flagName = std::string(magic_enum::enum_name(flagValue));
			if (flagName.starts_with("k")) {
				flagName = flagName.substr(1);
			}

			// Use canonical English names for logic (PermAurora → Aurora, AuroraFollowsSun → Aurora Sun)
			if (flagName == "PermAurora") {
				flagName = "Aurora";
			} else if (flagName == "AuroraFollowsSun") {
				flagName = "Aurora Sun";
			}

			flagNames.push_back(flagName);
		}
	}

	// Check for any unknown flags (flags not covered by the enum)
	uint32_t knownFlags = 0;
	for (auto flagValue : magic_enum::enum_values<RE::TESWeather::WeatherDataFlag>()) {
		if (flagValue != RE::TESWeather::WeatherDataFlag::kNone) {
			knownFlags |= static_cast<uint32_t>(flagValue);
		}
	}

	uint32_t unknownFlags = flags & ~knownFlags;
	if (unknownFlags != 0) {
		flagNames.push_back(std::format("{}({})", T(TKEY("unknown"), "Unknown"), unknownFlags));
	}

	return flagNames;
}

bool CSEditor::RenderMultiColorWeatherName(RE::TESWeather* weather, const std::string& weatherName)
{
	if (!weather) {
		ImGui::Text("%s", weatherName.c_str());
		return false;
	}

	// Get all flags present in this weather
	std::vector<std::string> flagNames = GetWeatherFlagNames(weather);

	// If no flags or only one flag, use simple single-color display
	if (flagNames.size() <= 1) {
		ImVec4 weatherColor = GetWeatherTypeColor(weather);
		ImGui::PushStyleColor(ImGuiCol_Text, weatherColor);
		ImGui::Text("%s", weatherName.c_str());
		ImGui::PopStyleColor();
		return ImGui::IsItemHovered();
	}
	// For multiple flags, create a color-coded display
	// We'll show the weather name in segments, each with its own color

	// Create a visual representation with colored segments
	// Format: "WeatherName [Flag1][Flag2][Flag3]"

	// Display the main weather name in the primary color (highest priority flag)
	ImVec4 primaryColor = GetWeatherTypeColor(weather);
	ImGui::PushStyleColor(ImGuiCol_Text, primaryColor);

	// Extract base weather name (without the flag suffix)
	std::string baseName = weatherName;
	size_t bracketPos = baseName.find(" [");
	if (bracketPos != std::string::npos) {
		baseName = baseName.substr(0, bracketPos);
	}

	ImGui::Text("%s", baseName.c_str());
	ImGui::PopStyleColor();

	// Check if the main weather name (the most important part) was hovered
	bool baseNameHovered = ImGui::IsItemHovered();

	// Display flags as colored chips on the same line
	ImGui::SameLine();
	ImGui::Text(" ");

	for (size_t i = 0; i < flagNames.size(); ++i) {
		if (flagNames[i] == "None" || flagNames[i].find("Unknown") == 0) {
			continue;  // Skip "None" and "Unknown" flags for cleaner display
		}

		ImGui::SameLine();
		ImVec4 flagColor = GetWeatherFlagColorByName(flagNames[i]);
		ImGui::PushStyleColor(ImGuiCol_Text, flagColor);
		// Translate canonical flag name for display
		std::string flagKey = std::string(TKEY("flag_")) + flagNames[i];
		std::transform(flagKey.begin(), flagKey.end(), flagKey.begin(), ::tolower);
		const char* displayFlag = T(flagKey.c_str(), flagNames[i].c_str());
		ImGui::Text("[%s]", displayFlag);
		ImGui::PopStyleColor();
	}

	// Return true if the base name (largest, most visible part) was hovered
	return baseNameHovered;
}

// Helper function to get color for a specific weather flag
ImVec4 CSEditor::GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag flag)
{
	const auto& theme = Menu::GetSingleton()->GetTheme();

	switch (flag) {
	case RE::TESWeather::WeatherDataFlag::kRainy:
		return ImVec4(0.4f, 0.7f, 1.0f, 1.0f);  // Light blue for rain
	case RE::TESWeather::WeatherDataFlag::kSnow:
		return ImVec4(0.9f, 0.9f, 1.0f, 1.0f);  // Light blue-white for snow
	case RE::TESWeather::WeatherDataFlag::kPermAurora:
		return ImVec4(0.8f, 0.4f, 1.0f, 1.0f);  // Purple for aurora
	case RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun:
		return ImVec4(0.9f, 0.6f, 1.0f, 1.0f);  // Light purple for aurora follows sun
	case RE::TESWeather::WeatherDataFlag::kCloudy:
		return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray for cloudy
	case RE::TESWeather::WeatherDataFlag::kPleasant:
		return theme.StatusPalette.SuccessColor;  // Green for pleasant
	default:
		return theme.StatusPalette.InfoColor;  // Default blue
	}
}

// Helper function to get color for a specific flag name
ImVec4 CSEditor::GetWeatherFlagColorByName(const std::string& flagName)
{
	// Map display flag names back to enum values
	// Note: We use manual mapping here because the display names (from GetWeatherFlagNames)
	// are transformed from the original enum names (e.g., "kRainy" -> "Rainy")
	static const std::unordered_map<std::string, RE::TESWeather::WeatherDataFlag> flagNameMap = {
		{ "Rainy", RE::TESWeather::WeatherDataFlag::kRainy },
		{ "Snow", RE::TESWeather::WeatherDataFlag::kSnow },
		{ "Aurora", RE::TESWeather::WeatherDataFlag::kPermAurora },
		{ "Aurora Sun", RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun },
		{ "Cloudy", RE::TESWeather::WeatherDataFlag::kCloudy },
		{ "Pleasant", RE::TESWeather::WeatherDataFlag::kPleasant }
	};

	auto it = flagNameMap.find(flagName);
	if (it != flagNameMap.end()) {
		return GetWeatherFlagColor(it->second);
	}

	// Default for unclassified or unknown flags
	return Menu::GetSingleton()->GetTheme().StatusPalette.Warning;
}

std::string CSEditor::GetDisplayName(const RE::TESWeather* weather)
{
	if (!weather) {
		return "Unknown";
	}
	const char* name = weather->GetName();
	if (name && strlen(name) > 0) {
		return std::string(name);
	}
	const char* editorID = weather->GetFormEditorID();
	if (editorID && strlen(editorID) > 0) {
		return std::string(editorID);
	}
	return std::to_string(weather->GetFormID());
}

#undef I18N_KEY_PREFIX

void CSEditor::DrawOverlay()
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->parentCell)
		return;

	bool overlayVisible = Menu::GetSingleton()->overlayVisible;
	static bool s_prevOverlayVisible = false;
	// Driven by the Performance Overlay's weather picker toggle; auto-enable the window when the overlay appears
	if (IsOverlayVisible() && overlayVisible) {
		if (!s_prevOverlayVisible && !WeatherDetailsWindow.Enabled) {
			WeatherDetailsWindow.Enabled = true;
		}
		bool* p_open = &WeatherDetailsWindow.Enabled;
		RenderWeatherDetailsWindow(p_open, false);
	}
	s_prevOverlayVisible = overlayVisible;
}

bool CSEditor::IsOverlayVisible() const
{
	return globals::features::performanceOverlay.settings.ShowWeatherPicker;
}
