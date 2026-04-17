#include "WeatherEditor.h"

#include "Deferred.h"
#include "Feature.h"
#include "Menu.h"
#include "State.h"
#include "Util.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include "WeatherManager.h"

#include "WeatherEditor/EditorWindow.h"
#include <nlohmann/json.hpp>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WeatherEditor::WeatherDetailsWindowSettings,
	Enabled,
	ShowInOverlay,
	Position,
	PositionSet)

void WeatherEditor::DataLoaded()
{
	EditorWindow::GetSingleton()->SetupResources();
	LoadAllWeathers();
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

void WeatherEditor::DrawSettings()
{
	bool canOpen = EditorWindow::CanBeOpen();
	ImGui::BeginDisabled(!canOpen);
	if (ImGui::Button("Open Editor", { -1, 0 }))
		EditorWindow::GetSingleton()->open = true;
	ImGui::EndDisabled();

	// Time controls
	DrawTimeControls();

	// Basic weather editor info
	DrawWeatherStatusPanel();

	// Integrated Weather Picker UI
	DrawWeatherPickerSection();
}

void WeatherEditor::Prepass()
{
	// Re-enforce weather lock if active (handles time changes)
	auto editorWindow = EditorWindow::GetSingleton();
	if (editorWindow->IsWeatherLocked()) {
		auto lockedWeather = editorWindow->GetLockedWeather();
		auto sky = globals::game::sky;
		if (sky && lockedWeather && sky->currentWeather != lockedWeather) {
			sky->ForceWeather(lockedWeather, false);
		}
	}

	// Update time controls (handles sleep/wait and external state sync)
	editorWindow->UpdateTimeState();
}

void WeatherEditor::DrawWeatherPickerSection()
{
	ImGui::Spacing();
	Util::DrawSectionHeader("Weather Details");

	const auto& themeSettings = Menu::GetSingleton()->GetTheme();
	const auto& menuSettings = Menu::GetSingleton()->GetSettings();

	// Show as Overlay checkbox
	bool showInOverlay = WeatherDetailsWindow.ShowInOverlay;
	if (ImGui::Checkbox("Show in Overlay", &showInOverlay)) {
		WeatherDetailsWindow.ShowInOverlay = showInOverlay;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Opens weather details in a separate window that stays open\neven when the main menu is closed. ");
		ImGui::Text("Toggle with ");
		ImGui::SameLine();
		ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", Util::Input::KeyIdToString(menuSettings.OverlayToggleKey).c_str());
	}
	ImGui::Spacing();

	// Render core weather details
	RenderCoreWeatherDetails(true);  // true = show interactive elements in main settings panel

	// Render weather analysis from features with collapsible headers
	RenderFeatureWeatherAnalysis();
}

void WeatherEditor::LerpWeather(RE::TESWeather* oldWeather, RE::TESWeather* newWeather, float currentWeatherPct)
{
	if (!oldWeather || !newWeather) {
		// Avoid dereferencing null pointers; nothing to lerp.
		return;
	}

	//// Precipitation
	newWeather->data.precipitationBeginFadeIn = LerpUint8_t(oldWeather->data.precipitationBeginFadeIn, newWeather->data.precipitationBeginFadeIn, currentWeatherPct);
	newWeather->data.precipitationEndFadeOut = LerpUint8_t(oldWeather->data.precipitationEndFadeOut, newWeather->data.precipitationEndFadeOut, currentWeatherPct);

	//// Sun
	newWeather->data.sunDamage = LerpInt8_t(oldWeather->data.sunDamage, newWeather->data.sunDamage, currentWeatherPct);
	newWeather->data.sunGlare = LerpInt8_t(oldWeather->data.sunGlare, newWeather->data.sunGlare, currentWeatherPct);

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
	newWeather->data.windDirection = LerpInt8_t(oldWeather->data.windDirection, newWeather->data.windDirection, currentWeatherPct);
	newWeather->data.windDirectionRange = LerpInt8_t(oldWeather->data.windDirectionRange, newWeather->data.windDirectionRange, currentWeatherPct);
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

void WeatherEditor::DrawTimeControls()
{
	ImGui::Spacing();
	Util::DrawSectionHeader("Time Controls");
	ImGui::Spacing();
	EditorWindow::GetSingleton()->DrawTimeControls();
	ImGui::Spacing();
}

void WeatherEditor::DrawWeatherStatusPanel()
{
	ImGui::Spacing();
	Util::DrawSectionHeader("Weather Status");
	ImGui::Spacing();

	auto weatherManager = WeatherManager::GetSingleton();
	auto currentWeathers = weatherManager->GetCurrentWeathers();
	const auto& theme = Menu::GetSingleton()->GetTheme();

	if (currentWeathers.currentWeather) {
		// Show if weather has custom settings
		if (weatherManager->HasWeatherSettings(currentWeathers.currentWeather)) {
			ImGui::TextColored(theme.StatusPalette.SuccessColor, "Has Custom Settings");
		} else {
			ImGui::TextColored(theme.StatusPalette.Disable, "Using Default Settings");
		}

		// Show what the current weather is
		ImGui::Text("Current Weather: %s",
			currentWeathers.currentWeather->GetFormEditorID() ?
				currentWeathers.currentWeather->GetFormEditorID() :
				std::format("{:08X}", currentWeathers.currentWeather->GetFormID()).c_str());

		// Always reserve space for transition info to prevent UI shifting
		if (currentWeathers.lastWeather && currentWeathers.lerpFactor < 1.0f) {
			ImGui::Text("Transitioning From: %s",
				currentWeathers.lastWeather->GetFormEditorID() ?
					currentWeathers.lastWeather->GetFormEditorID() :
					std::format("{:08X}", currentWeathers.lastWeather->GetFormID()).c_str());
		} else {
			ImGui::Text("Transitioning From: No Transition");
		}

		// Always show progress bar
		const bool isTransitioning = currentWeathers.lastWeather && currentWeathers.lerpFactor < 1.0f;
		float displayPct = isTransitioning ? currentWeathers.lerpFactor : 1.0f;

		// Show background color when transition is complete
		if (!isTransitioning) {
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		}

		ImGui::ProgressBar(displayPct, ImVec2(-1, 0),
			isTransitioning ?
				std::format("Transition: {:.1f}%", currentWeathers.lerpFactor * 100.0f).c_str() :
				"");

		if (!isTransitioning) {
			ImGui::PopStyleColor();
		}

	} else {
		ImGui::TextColored(theme.StatusPalette.Warning, "No Active Weather");
	}
}

// ================================================================================
// Weather Picker functionality (integrated from WeatherPicker feature)
// ================================================================================

void WeatherEditor::RenderWeatherDetailsWindow(bool* open)
{
	if (!open || !*open)
		return;

	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->parentCell)
		return;

	// Set initial position if not already set
	const float scale = Util::GetUIScale();
	if (!WeatherDetailsWindow.PositionSet) {
		const float pos = 50.0f * scale;
		ImGui::SetNextWindowPos(ImVec2(pos, pos));
		WeatherDetailsWindow.Position = ImVec2(pos, pos);
		WeatherDetailsWindow.PositionSet = true;
	} else {
		ImGui::SetNextWindowPos(WeatherDetailsWindow.Position, ImGuiCond_FirstUseEver);
	}

	ImGui::SetNextWindowSize(ImVec2(600 * scale, 800 * scale), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Weather Details##Popup", open, ImGuiWindowFlags_None)) {
		// Remember window position for next frame
		ImVec2 currentPos = ImGui::GetWindowPos();
		if (currentPos.x != WeatherDetailsWindow.Position.x || currentPos.y != WeatherDetailsWindow.Position.y) {
			WeatherDetailsWindow.Position = currentPos;
		}

		// Enable interactive elements when a menu is open
		auto shouldEnableInteractiveElements = []() -> bool {
			return (Menu::GetSingleton()->ShouldSwallowInput() ||
					(globals::game::ui && globals::game::ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)));
		};

		RenderCoreWeatherDetails(shouldEnableInteractiveElements());

		// Render weather analysis from features with collapsible headers
		RenderFeatureWeatherAnalysis();
	}
	ImGui::End();
}

ImVec4 WeatherEditor::GetWeatherTypeColor(RE::TESWeather* weather)
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
void WeatherEditor::DisplayWeatherBasicInfo(RE::TESWeather* weather, float weatherPct)
{
	if (!weather) {
		ImGui::BulletText("No Weather Found");
		return;
	}
	std::string weatherText = Util::FormatWeather(weather);
	ImGui::Bullet();
	ImGui::SameLine();
	bool showTooltip = WeatherEditor::RenderMultiColorWeatherName(weather, weatherText);
	if (showTooltip) {
		ImGui::BeginTooltip();
		ImGui::Text("Name: %s", weather->GetName() ? weather->GetName() : "Unnamed");
		ImGui::Text("Editor ID: %s", weather->GetFormEditorID() ? weather->GetFormEditorID() : "None");
		ImGui::Text("Form ID: 0x%08X", weather->GetFormID());
		auto flagNames = WeatherEditor::GetWeatherFlagNames(weather);
		if (!flagNames.empty()) {
			std::string joinedFlags = flagNames[0];
			for (size_t j = 1; j < flagNames.size(); ++j) {
				joinedFlags += ", " + flagNames[j];
			}
			ImGui::Text("Flags: %s", joinedFlags.c_str());
		} else {
			ImGui::Text("Flags: None");
		}
		ImGui::EndTooltip();
	}
	if (weatherPct >= 0.0f) {
		ImGui::BulletText("Weather Percentage: %.1f%%", weatherPct * 100.0f);
	}
}

void WeatherEditor::DisplayPrecipitationInfo(RE::TESWeather* weather)
{
	if (!weather || !weather->precipitationData) {
		ImGui::BulletText("Particle Density: No precipitation data");
		return;
	}
	auto particleDensity = weather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	ImGui::BulletText("Particle Density: %.3f", particleDensity);
	GET_INSTANCE_MEMBER(particleTexture, weather->precipitationData)
	if (!particleTexture.textureName.empty()) {
		ImGui::BulletText("Particle Texture: %s", particleTexture.textureName.c_str());
	} else {
		ImGui::BulletText("Particle Texture: None");
	}
	uint8_t precipBeginFadeIn = weather->data.precipitationBeginFadeIn;
	uint8_t precipEndFadeOut = weather->data.precipitationEndFadeOut;
	float precipBeginNormalized = precipBeginFadeIn / 255.0f;
	float precipEndNormalized = precipEndFadeOut / 255.0f;
	ImGui::BulletText("Precip Begin Fade-In: %.3f (raw %u)", precipBeginNormalized, precipBeginFadeIn);
	ImGui::BulletText("Precip End Fade-Out: %.3f (raw %u)", precipEndNormalized, precipEndFadeOut);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		Util::DrawMultiLineTooltip({ "Precipitation fade transition parameters:",
			"Begin Fade-In: Point where precipitation starts appearing",
			"End Fade-Out: Point where precipitation fully disappears",
			"Raw values: 0-255 (uint8), Normalized: 0.0-1.0" });
	}
}

void WeatherEditor::DisplayLightningInfo(RE::TESWeather* weather, bool showInteractiveElements)
{
	if (!weather || (uint8_t)weather->data.thunderLightningFrequency == 0)
		return;
	const auto& theme = Menu::GetSingleton()->GetTheme();
	uint8_t lightningR = weather->data.lightningColor.red;
	uint8_t lightningG = weather->data.lightningColor.green;
	uint8_t lightningB = weather->data.lightningColor.blue;
	ImGui::Text("Lightning Color:");
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
	ImGui::BulletText("Thunder Frequency: %u", static_cast<unsigned>(thunderFreqRaw));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		Util::DrawMultiLineTooltip({ "Thunder frequency raw value (0-255):",
			"",
			"Known data points from Creation Kit slider:",
			"- Raw 15 = ~100% frequency (highest thunder)",
			"- Raw 76 = ~75% frequency",
			"- Raw 203 = ~20% frequency",
			"- Raw 246 = ~5% frequency",
			"- Raw 255 = ~0% frequency (lowest thunder)",
			"",
			"Range: 0-255 (unsigned 8-bit integer)",
			"Note: Creation Kit interprets this value non-linearly" });
	}
	uint8_t lightningBeginFadeIn = weather->data.thunderLightningBeginFadeIn;
	uint8_t lightningEndFadeOut = weather->data.thunderLightningEndFadeOut;
	float lightningBeginNormalized = lightningBeginFadeIn / 255.0f;
	float lightningEndNormalized = lightningEndFadeOut / 255.0f;
	ImGui::BulletText("Lightning Begin Fade-In: %.3f (raw %u)", lightningBeginNormalized, lightningBeginFadeIn);
	ImGui::BulletText("Lightning End Fade-Out: %.3f (raw %u)", lightningEndNormalized, lightningEndFadeOut);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		Util::DrawMultiLineTooltip({ "Lightning fade transition parameters:",
			"Begin Fade-In: Point where lightning starts appearing",
			"End Fade-Out: Point where lightning fully disappears",
			"Raw values: 0-255 (uint8), Normalized: 0.0-1.0" });
	}
}

void WeatherEditor::DisplayWindInfo(RE::TESWeather* weather)
{
	auto sky = globals::game::sky;
	if (!weather || (weather->data.windSpeed <= 0 && (!sky || sky->windSpeed <= 0.0f)))
		return;
	const auto& theme = Menu::GetSingleton()->GetTheme();
	float windSpeedDisplay = weather->data.windSpeed / 255.0f;
	ImGui::BulletText("Weather Wind Speed: %.2f (raw %d)", windSpeedDisplay, weather->data.windSpeed);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::string windStr = Util::Units::FormatWindSpeed(weather->data.windSpeed);
		Util::DrawMultiLineTooltip({ "Wind speed from weather definition",
			windStr.c_str() });
	}
	if (sky) {
		ImGui::BulletText("Sky Wind Speed: %.2f", sky->windSpeed);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			Util::DrawMultiLineTooltip({ "Current active wind speed from the sky system",
				"This affects particle behavior and wind-based effects" });
		}
	}
	float weatherWindDirDegrees = Util::Units::DirectionRawToDegrees(weather->data.windDirection);
	ImGui::BulletText("Wind Direction: %.1f° (raw %d)", weatherWindDirDegrees, weather->data.windDirection);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::string dirStr = Util::Units::FormatDirection(weather->data.windDirection);
		Util::DrawMultiLineTooltip({ "Wind direction from weather definition",
			dirStr.c_str() });
	}
	float weatherWindRangeDegrees = Util::Units::DirectionRangeToDegrees(weather->data.windDirectionRange);
	ImGui::BulletText("Wind Direction Range: %.1f° (raw %d)", weatherWindRangeDegrees, weather->data.windDirectionRange);

	if (auto player = RE::PlayerCharacter::GetSingleton()) {
		float playerAngleZ = player->GetAngleZ();
		float playerAngleDegrees = Util::Units::NormalizeDegrees0To360(Util::Units::RadiansToDegrees(playerAngleZ));
		ImGui::BulletText("Player Direction: %.1f°", playerAngleDegrees);
		float effectiveWindDirection = Util::Units::NormalizeDegrees0To360(weatherWindDirDegrees - WIND_DIRECTION_OFFSET);
		float rawDifference = Util::Units::NormalizeDegreesToSignedRange(effectiveWindDirection - playerAngleDegrees);
		ImGui::BulletText("Effective Wind Dir: %.1f° (raw - %.1f°)", effectiveWindDirection, WIND_DIRECTION_OFFSET);
		ImGui::BulletText("Wind vs Player: %.1f°", rawDifference);
		const char* windRelation;
		if (std::abs(rawDifference) < 30.0f) {
			windRelation = "Tailwind (wind behind player)";
		} else if (std::abs(rawDifference) > 150.0f) {
			windRelation = "Headwind (wind coming toward player)";
		} else if (rawDifference > 0) {
			windRelation = "Right crosswind";
		} else {
			windRelation = "Left crosswind";
		}
		ImGui::SameLine();
		ImGui::TextColored(theme.StatusPalette.RestartNeeded, "(%s)", windRelation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			Util::DrawMultiLineTooltip({
				"Wind relative to player direction:",
				"- ~0° = Tailwind (wind behind player)",
				"- ~±90° = Crosswind (left/right)",
				"- ~±180° = Headwind (wind coming toward player)",
			});
		}
	}
}

// --- Main function: now just delegates to helpers ---
void WeatherEditor::DisplayWeatherInfo(RE::TESWeather* weather, float weatherPct, bool showInteractiveElements)
{
	WeatherEditor::DisplayWeatherBasicInfo(weather, weatherPct);
	WeatherEditor::DisplayPrecipitationInfo(weather);
	WeatherEditor::DisplayLightningInfo(weather, showInteractiveElements);
	WeatherEditor::DisplayWindInfo(weather);
}

void WeatherEditor::RenderWeatherControls(RE::Sky* sky)
{
	// Weather Selection Section (only show interactive elements in inline mode)
	static bool weatherControlsExpanded = true;
	Util::DrawSectionHeader("Weather Controls", false, true, &weatherControlsExpanded);

	if (!weatherControlsExpanded)
		return;

	ImGui::Text("Filter by Weather Type:");
	if (ImGui::Button("Select All")) {
		s_weatherFlagFilter = ALL_WEATHER_FLAGS;  // All weather flags (bits 0-6, including unclassified)
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear All")) {
		s_weatherFlagFilter = 0x00;  // No flags
	}
	// Dynamic checkbox layout - calculate how many fit per row
	float availableWidth = ImGui::GetContentRegionAvail().x;
	float checkboxWidth = 110.0f;  // Fits "Aurora Sun" label
	int checkboxesPerRow = std::max(1, static_cast<int>(availableWidth / checkboxWidth));

	// Colored checkboxes with dynamic layout
	struct WeatherFilter
	{
		const char* label;
		RE::TESWeather::WeatherDataFlag flag;
		bool isUnclassified;
	};

	std::vector<WeatherFilter> filters = {
		{ "Pleasant", RE::TESWeather::WeatherDataFlag::kPleasant, false },
		{ "Cloudy", RE::TESWeather::WeatherDataFlag::kCloudy, false },
		{ "Rainy", RE::TESWeather::WeatherDataFlag::kRainy, false },
		{ "Snow", RE::TESWeather::WeatherDataFlag::kSnow, false },
		{ "Aurora", RE::TESWeather::WeatherDataFlag::kPermAurora, false },
		{ "Aurora Sun", RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun, false },
		{ "None", RE::TESWeather::WeatherDataFlag::kNone, true }  // Special case for unclassified
	};
	for (size_t i = 0; i < filters.size(); ++i) {
		if (i > 0 && i % checkboxesPerRow != 0) {
			ImGui::SameLine();
		}
		// Get color - use the helper function for consistency
		ImVec4 filterColor;
		if (filters[i].isUnclassified) {
			filterColor = Menu::GetSingleton()->GetTheme().StatusPalette.Warning;
		} else {
			filterColor = GetWeatherFlagColor(filters[i].flag);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, filterColor);
		if (filters[i].isUnclassified) {
			// Special handling for None filter - use CheckboxFlags for consistency
			ImGui::CheckboxFlags(filters[i].label, &s_weatherFlagFilter, UNCLASSIFIED_FLAG);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				Util::DrawMultiLineTooltip({ "Shows weathers that are not classified under any specific category.",
					"Includes weathers with no flags or only untracked flags.",
					"Categories tracked: Pleasant, Cloudy, Rainy, Snow, Aurora, Aurora Sun" });
			}
		} else {
			ImGui::CheckboxFlags(filters[i].label, &s_weatherFlagFilter, static_cast<uint32_t>(filters[i].flag));
		}
		ImGui::PopStyleColor();
	}

	// Update filtered weathers when filter changes
	if (s_lastWeatherFlagFilter != s_weatherFlagFilter) {
		UpdateFilteredWeathers();
		s_selectedWeatherIdx = -1;
		s_lastWeatherFlagFilter = s_weatherFlagFilter;
	}

	// Accelerate checkbox
	ImGui::Checkbox("Accelerate Weather Change", &s_accelerateWeatherChange);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("When enabled, weather changes instantly");
	}  // Reset Weather button
	if (ImGui::Button("Reset Weather")) {
		sky->ResetWeather();
		// Update the selection box to reflect the reset weather without double-applying
		s_selectedWeatherIdx = FindWeatherIndex(sky->defaultWeather);
		logger::info("[WeatherEditor] Reset weather to default");
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Resets weather to default");
	}

	// Lock Weather toggle
	ImGui::SameLine();
	auto editorWindow = EditorWindow::GetSingleton();
	bool isLocked = editorWindow->IsWeatherLocked();
	const char* lockLabel = isLocked ? "Unlock Weather" : "Lock Weather";

	if (isLocked) {
		const auto& theme = Menu::GetSingleton()->GetTheme();
		ImGui::PushStyleColor(ImGuiCol_Button, theme.StatusPalette.SuccessColor);
	}
	if (ImGui::Button(lockLabel)) {
		if (isLocked) {
			editorWindow->UnlockWeather();
		} else if (sky->currentWeather) {
			editorWindow->LockWeather(sky->currentWeather);
		}
	}
	if (isLocked) {
		ImGui::PopStyleColor();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(isLocked ? "Unlock weather to allow natural changes" : "Lock current weather to prevent changes");
	}

	// Weather Selection - now with colored text
	std::vector<std::string> weatherLabels;
	weatherLabels.reserve(s_filteredWeathers.size());
	for (const auto& weather : s_filteredWeathers) {
		weatherLabels.push_back(Util::FormatWeather(weather));
	}

	// Custom combo with colored text
	const char* comboPreview = (s_selectedWeatherIdx >= 0 && s_selectedWeatherIdx < static_cast<int>(weatherLabels.size())) ?
	                               weatherLabels[s_selectedWeatherIdx].c_str() :
	                               "Select Weather";

	static constexpr const char* kWeatherSearchId = "WeatherPicker";

	if (ImGui::BeginCombo("Weather", comboPreview)) {
		auto searchText = Util::DrawComboSearchInput(kWeatherSearchId);

		for (int i = 0; i < static_cast<int>(s_filteredWeathers.size()); ++i) {
			const bool isSelected = (s_selectedWeatherIdx == i);
			auto weather = s_filteredWeathers[i];

			// Filter by EditorID, Name, and FormID only (not classification tags)
			if (!searchText.empty()) {
				auto editorId = weather->GetFormEditorID() ? std::string(weather->GetFormEditorID()) : "";
				auto name = weather->GetName() ? std::string(weather->GetName()) : "";
				auto formId = std::format("{:08X}", weather->GetFormID());

				if (!Util::StringMatchesSearch(editorId, searchText) &&
					!Util::StringMatchesSearch(name, searchText) &&
					!Util::StringMatchesSearch(formId, searchText))
					continue;
			}

			ImGui::PushStyleColor(ImGuiCol_Text, GetWeatherTypeColor(weather));
			bool didSelect = ImGui::Selectable(weatherLabels[i].c_str(), isSelected);
			ImGui::PopStyleColor();

			if (didSelect) {
				s_selectedWeatherIdx = i;
				auto selectedWeather = s_filteredWeathers[i];

				if (s_accelerateWeatherChange)
					sky->ForceWeather(selectedWeather, false);
				else
					sky->SetWeather(selectedWeather, true, false);

				// If the lock is active, retarget it to the newly chosen weather so
				// Prepass() enforces the new choice instead of reverting it.
				if (editorWindow->IsWeatherLocked()) {
					editorWindow->lockedWeather = selectedWeather;
				}

				Util::ClearComboSearch(kWeatherSearchId);
				logger::info("[WeatherEditor] Changed weather to: {}", Util::FormatWeather(selectedWeather));
				break;
			}

			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::Text("Weather: %s", weather->GetName() ? weather->GetName() : "Unnamed");
				ImGui::Text("Editor ID: %s", weather->GetFormEditorID() ? weather->GetFormEditorID() : "None");
				ImGui::Text("Form ID: 0x%08X", weather->GetFormID());
				ImGui::EndTooltip();
			}

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	} else {
		Util::ClearComboSearch(kWeatherSearchId);
	}
}

void WeatherEditor::RenderWeatherInformationDisplay(RE::Sky* sky, bool showInteractiveElements)
{
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();
	Util::DrawSectionHeader("Weather Information", false, true);

	// Update cache: store current lastWeather if it exists, otherwise keep the cached one
	if (sky->lastWeather) {
		s_cachedLastWeather = sky->lastWeather;
	}

	// Use cached last weather for display if sky->lastWeather is null
	RE::TESWeather* displayLastWeather = sky->lastWeather ? sky->lastWeather : s_cachedLastWeather;

	// Create resizable 2-column table for current and last weather
	if (ImGui::BeginTable("WeatherComparison", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersV)) {
		// Set up columns
		ImGui::TableSetupColumn("Current Weather", ImGuiTableColumnFlags_WidthStretch, 0.5f);
		ImGui::TableSetupColumn("Last Weather", ImGuiTableColumnFlags_WidthStretch, 0.5f);
		ImGui::TableHeadersRow();

		ImGui::TableNextRow();

		// Current Weather Column
		ImGui::TableNextColumn();
		DisplayWeatherInfo(sky->currentWeather, sky->currentWeatherPct, showInteractiveElements);

		// Last Weather Column
		ImGui::TableNextColumn();
		DisplayWeatherInfo(displayLastWeather, std::abs(sky->currentWeatherPct - 1.0f), showInteractiveElements);

		ImGui::EndTable();
	}
}

void WeatherEditor::RenderCoreWeatherDetails(bool showInteractiveElements)
{
	const auto showError = [](const char* msg) {
		auto menu = Menu::GetSingleton();
		const auto& theme = menu->GetTheme();
		ImGui::TextColored(theme.StatusPalette.Error, "%s", msg);
	};

	if (auto sky = globals::game::sky) {
		if (sky->mode.get() == RE::Sky::Mode::kFull) {
			if (showInteractiveElements) {
				RenderWeatherControls(sky);
			}
			RenderWeatherInformationDisplay(sky, showInteractiveElements);
			ImGui::Spacing();
		} else {
			showError("Sky not in full mode");
		}
	} else {
		showError("Sky not available");
	}
}

void WeatherEditor::LoadAllWeathers()
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

void WeatherEditor::UpdateFilteredWeathers()
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

int WeatherEditor::FindWeatherIndex(RE::TESWeather* targetWeather)
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

void WeatherEditor::RenderFeatureWeatherAnalysis()
{
	// Iterate through all loaded features to show their weather analysis
	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->loaded) {
			// Skip the WeatherEditor itself to avoid recursion
			if (feature == &globals::features::weatherEditor) {
				continue;
			}

			// Check if this feature provides weather analysis
			auto weatherConfig = feature->GetWeatherAnalysisConfig();
			if (weatherConfig.sectionName.empty()) {
				continue;  // Skip features that don't provide weather analysis
			}

			auto featureName = feature->GetShortName();
			ImGui::PushID(featureName.c_str());

			// Create collapsible header for feature weather analysis
			bool isExpanded = ImGui::CollapsingHeader(weatherConfig.sectionName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Weather analysis provided by: %s", feature->GetName().c_str());
				ImGui::Text("Feature category: %s", std::string(feature->GetCategory()).c_str());
				ImGui::Text("Click to %s this feature's weather data", isExpanded ? "collapse" : "expand");
			}

			if (isExpanded && weatherConfig.drawFunction) {
				// Call the feature's weather analysis draw function
				weatherConfig.drawFunction();
			}

			ImGui::PopID();
		}
	}
}

std::vector<std::string> WeatherEditor::GetWeatherFlagNames(RE::TESWeather* weather)
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
			// Convert enum name to human-readable format
			std::string flagName = std::string(magic_enum::enum_name(flagValue));

			// Remove 'k' prefix and convert to readable format
			if (flagName.starts_with("k")) {
				flagName = flagName.substr(1);
			}

			// Convert specific cases to more readable names
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
		flagNames.push_back("Unknown(" + std::to_string(unknownFlags) + ")");
	}

	return flagNames;
}

bool WeatherEditor::RenderMultiColorWeatherName(RE::TESWeather* weather, const std::string& weatherName)
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
		ImGui::Text("[%s]", flagNames[i].c_str());
		ImGui::PopStyleColor();
	}

	// Return true if the base name (largest, most visible part) was hovered
	return baseNameHovered;
}

// Helper function to get color for a specific weather flag
ImVec4 WeatherEditor::GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag flag)
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
ImVec4 WeatherEditor::GetWeatherFlagColorByName(const std::string& flagName)
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

std::string WeatherEditor::GetDisplayName(const RE::TESWeather* weather)
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

void WeatherEditor::DrawOverlay()
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->parentCell)
		return;

	bool overlayVisible = Menu::GetSingleton()->overlayVisible;
	static bool s_prevOverlayVisible = false;
	// If ShowInOverlay is true and overlay is visible, auto-enable the window if not already enabled
	if (WeatherDetailsWindow.ShowInOverlay && overlayVisible) {
		if (!s_prevOverlayVisible && !WeatherDetailsWindow.Enabled) {
			WeatherDetailsWindow.Enabled = true;
		}
		bool* p_open = &WeatherDetailsWindow.Enabled;
		RenderWeatherDetailsWindow(p_open);
	}
	s_prevOverlayVisible = overlayVisible;
}

bool WeatherEditor::IsOverlayVisible() const
{
	return WeatherDetailsWindow.ShowInOverlay;
}
