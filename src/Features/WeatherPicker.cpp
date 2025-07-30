#include "WeatherPicker.h"
#include "Feature.h"
#include "Menu.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <nlohmann/json.hpp>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WeatherPicker::WeatherDetailsWindowSettings,
	Enabled,
	ShowInOverlay,
	Position,
	PositionSet)

void WeatherPicker::DataLoaded()
{
	LoadAllWeathers();
}

std::pair<std::string, std::vector<std::string>> WeatherPicker::GetFeatureSummary()
{
	std::string description = "Interactive weather control system that lets you instantly change and analyze weather conditions in-game.";

	std::vector<std::string> keyFeatures = {
		"Instantly switch between any weather with immediate or gradual transitions",
		"Filter weather by type (Pleasant, Cloudy, Rainy, Snow, Aurora) for easy browsing",
		"View detailed weather information including wind, precipitation, and lightning data",
		"Color-coded weather names show all weather properties at a glance",
		"Persistent overlay window for continuous weather monitoring while playing"
	};

	return { description, keyFeatures };
}

void WeatherPicker::DrawSettings()
{
	if (ImGui::TreeNodeEx("Weather Details", ImGuiTreeNodeFlags_DefaultOpen)) {
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
			ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", Util::Input::KeyIdToString(menuSettings.OverlayToggleKey));
		}
		ImGui::Spacing();

		// Render core weather details
		RenderCoreWeatherDetails(true);  // true = show interactive elements in main settings panel

		// Render weather analysis from features with collapsible headers
		RenderFeatureWeatherAnalysis();

		ImGui::TreePop();
	}
}

void WeatherPicker::RenderWeatherDetailsWindow(bool* open)
{
	if (!*open)
		return;

	// Set initial position if not already set
	if (!WeatherDetailsWindow.PositionSet) {
		ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f));
		WeatherDetailsWindow.Position = ImVec2(50.0f, 50.0f);
		WeatherDetailsWindow.PositionSet = true;
	} else {
		ImGui::SetNextWindowPos(WeatherDetailsWindow.Position, ImGuiCond_FirstUseEver);
	}

	ImGui::SetNextWindowSize(ImVec2(600, 800), ImGuiCond_FirstUseEver);
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

ImVec4 WeatherPicker::GetWeatherTypeColor(RE::TESWeather* weather)
{
	if (!weather) {
		return Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor;
	}

	const auto& theme = Menu::GetSingleton()->GetTheme();

	// Priority order for weather classification colors (highest priority first)
	static const std::vector<std::pair<RE::TESWeather::WeatherDataFlag, ImVec4>> priorityColors = {
		{ RE::TESWeather::WeatherDataFlag::kPleasant, ImVec4(0.0f, 1.0f, 0.0f, 1.0f) },         // Placeholder, will use theme
		{ RE::TESWeather::WeatherDataFlag::kCloudy, ImVec4(0.7f, 0.7f, 0.7f, 1.0f) },           // Gray for cloudy
		{ RE::TESWeather::WeatherDataFlag::kRainy, ImVec4(0.4f, 0.7f, 1.0f, 1.0f) },            // Light blue for rain
		{ RE::TESWeather::WeatherDataFlag::kSnow, ImVec4(0.9f, 0.9f, 1.0f, 1.0f) },             // Light blue-white for snow
		{ RE::TESWeather::WeatherDataFlag::kPermAurora, ImVec4(0.8f, 0.4f, 1.0f, 1.0f) },       // Purple for aurora
		{ RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun, ImVec4(0.9f, 0.6f, 1.0f, 1.0f) }  // Light purple for aurora follows sun
	};

	// Check flags in priority order
	for (const auto& [flag, color] : priorityColors) {
		if (weather->data.flags.any(flag)) {
			// Handle theme-dependent colors
			if (flag == RE::TESWeather::WeatherDataFlag::kPleasant) {
				return theme.StatusPalette.SuccessColor;
			}
			return color;
		}
	}

	// Check for unclassified/unflagged weather
	if (weather->data.flags.underlying() == 0) {
		return ImVec4(0.9f, 0.85f, 0.7f, 1.0f);  // Light tan/beige for unclassified/unflagged
	}

	return theme.StatusPalette.InfoColor;  // Default blue
}

// --- Helper: Display basic weather info (name, flags, percentage) ---
void WeatherPicker::DisplayWeatherBasicInfo(RE::TESWeather* weather, float weatherPct)
{
	if (!weather) {
		ImGui::BulletText("No Weather Found");
		return;
	}
	std::string weatherText = Util::FormatWeather(weather);
	ImGui::Bullet();
	ImGui::SameLine();
	bool showTooltip = WeatherPicker::RenderMultiColorWeatherName(weather, weatherText);
	if (showTooltip) {
		ImGui::BeginTooltip();
		ImGui::Text("Name: %s", weather->GetName() ? weather->GetName() : "Unnamed");
		ImGui::Text("Editor ID: %s", weather->GetFormEditorID() ? weather->GetFormEditorID() : "None");
		ImGui::Text("Form ID: 0x%08X", weather->GetFormID());
		auto flagNames = WeatherPicker::GetWeatherFlagNames(weather);
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

void WeatherPicker::DisplayPrecipitationInfo(RE::TESWeather* weather)
{
	if (!weather || !weather->precipitationData) {
		ImGui::BulletText("Particle Density: No precipitation data");
		return;
	}
	auto particleDensity = weather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	ImGui::BulletText("Particle Density: %.3f", particleDensity);
	GET_INSTANCE_MEMBER(particleTexture, weather->precipitationData)
	if (particleTexture.textureName.c_str()) {
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

void WeatherPicker::DisplayLightningInfo(RE::TESWeather* weather, bool showInteractiveElements)
{
	if (!weather || weather->data.thunderLightningFrequency <= 0)
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
		weather->data.lightningColor.red = static_cast<std::uint8_t>(lightningColor[0] * 255.0f);
		weather->data.lightningColor.green = static_cast<std::uint8_t>(lightningColor[1] * 255.0f);
		weather->data.lightningColor.blue = static_cast<std::uint8_t>(lightningColor[2] * 255.0f);
	}
	int8_t thunderFreqRaw = weather->data.thunderLightningFrequency;
	ImGui::BulletText("Thunder Frequency: %d (signed 8-bit)", static_cast<int>(thunderFreqRaw));
	ImGui::Indent();
	if (thunderFreqRaw >= 76) {
		if (thunderFreqRaw == 76) {
			ImGui::BulletText("This matches ~75%% frequency in Creation Kit");
		} else if (thunderFreqRaw > 76) {
			ImGui::BulletText("High frequency range: Above 75%% (raw > 76)");
		}
	} else if (thunderFreqRaw >= 15) {
		if (thunderFreqRaw == 15) {
			ImGui::BulletText("This matches maximum observed frequency in Creation Kit");
		} else {
			ImGui::BulletText("High-medium frequency range: 75-100%% (raw 15-76)");
		}
	} else if (thunderFreqRaw >= 0) {
		ImGui::BulletText("Medium frequency range: 25-75%% (raw 0-15)");
	} else if (thunderFreqRaw >= -10) {
		if (thunderFreqRaw == -1) {
			ImGui::BulletText("This matches minimum frequency in Creation Kit (255 unsigned)");
		} else if (thunderFreqRaw == -10) {
			ImGui::BulletText("This matches ~5%% frequency in Creation Kit (246 unsigned)");
		} else {
			ImGui::BulletText("Low frequency range: 0-25%% (raw -10 to 0)");
		}
	} else if (thunderFreqRaw >= -53) {
		if (thunderFreqRaw == -53) {
			ImGui::BulletText("This matches ~20%% frequency in Creation Kit (203 unsigned)");
		} else {
			ImGui::BulletText("Low-medium frequency range: 5-20%% (raw -53 to -10)");
		}
	} else if (thunderFreqRaw >= -100) {
		ImGui::BulletText("Very low frequency range: Near 0%% (raw -100 to -53)");
	} else {
		ImGui::BulletText("Extreme low frequency: Likely no thunder (raw < -100)");
	}
	ImGui::Unindent();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		Util::DrawMultiLineTooltip({ "Thunder frequency raw value with observed Creation Kit behavior:",
			"",
			"Known data points from Creation Kit slider:",
			"- Raw 15 = ~100% frequency (highest thunder)",
			"- Raw 76 = ~75% frequency",
			"- Raw -10 (246 unsigned) = ~5% frequency",
			"- Raw -53 (203 unsigned) = ~20% frequency",
			"- Raw -1 (255 unsigned) = ~0% frequency (lowest thunder)",
			"",
			"Pattern: Higher positive values = more frequent thunder",
			"Lower/negative values = less frequent thunder",
			"",
			"Range: -128 to +127 (signed 8-bit integer)",
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

void WeatherPicker::DisplayWindInfo(RE::TESWeather* weather)
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
void WeatherPicker::DisplayWeatherInfo(RE::TESWeather* weather, float weatherPct, bool showInteractiveElements)
{
	WeatherPicker::DisplayWeatherBasicInfo(weather, weatherPct);
	WeatherPicker::DisplayPrecipitationInfo(weather);
	WeatherPicker::DisplayLightningInfo(weather, showInteractiveElements);
	WeatherPicker::DisplayWindInfo(weather);
}

void WeatherPicker::RenderWeatherControls(RE::Sky* sky)
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
	float checkboxWidth = 80.0f;  // Adjusted for "None"
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
			filterColor = ImVec4(0.9f, 0.85f, 0.7f, 1.0f);  // Light tan/beige for none/unclassified
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
		Util::DrawMultiLineTooltip({ "When enabled, weather changes are immediate.",
			"When disabled, uses normal transition speed." });
	}  // Reset Weather button
	std::string resetButtonLabel = "Reset Weather";
	if (sky->defaultWeather) {
		resetButtonLabel += " to " + Util::FormatWeather(sky->defaultWeather);
	}

	// Color the reset button to match the default weather
	if (sky->defaultWeather) {
		ImVec4 weatherColor = GetWeatherTypeColor(sky->defaultWeather);
		ImGui::PushStyleColor(ImGuiCol_Text, weatherColor);
	}

	if (ImGui::Button(resetButtonLabel.c_str())) {
		sky->ResetWeather();
		// Update the selection box to reflect the reset weather without double-applying
		s_selectedWeatherIdx = FindWeatherIndex(sky->defaultWeather);
		logger::info("[WeatherPicker] Reset weather to default");
	}

	if (sky->defaultWeather) {
		ImGui::PopStyleColor();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (sky->defaultWeather) {
			Util::DrawMultiLineTooltip({ "Resets to default weather:",
				Util::FormatWeather(sky->defaultWeather).c_str() });
		} else {
			ImGui::Text("Resets weather to default (no default weather set)");
		}
	}  // Weather Selection - now with colored text
	std::vector<std::string> weatherLabels;
	weatherLabels.reserve(s_filteredWeathers.size());
	for (const auto& weather : s_filteredWeathers) {
		weatherLabels.push_back(Util::FormatWeather(weather));
	}

	// Custom combo with colored text
	const char* comboPreview = (s_selectedWeatherIdx >= 0 && s_selectedWeatherIdx < weatherLabels.size()) ?
	                               weatherLabels[s_selectedWeatherIdx].c_str() :
	                               "Select Weather";

	if (ImGui::BeginCombo("Weather", comboPreview)) {
		for (int i = 0; i < s_filteredWeathers.size(); ++i) {
			const bool isSelected = (s_selectedWeatherIdx == i);
			auto weather = s_filteredWeathers[i];
			ImVec4 textColor = GetWeatherTypeColor(weather);

			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
			if (ImGui::Selectable(weatherLabels[i].c_str(), isSelected)) {
				s_selectedWeatherIdx = i;
				// Weather changed, apply it
				auto selectedWeather = s_filteredWeathers[s_selectedWeatherIdx];
				sky->SetWeather(selectedWeather, true, s_accelerateWeatherChange);
				logger::info("[WeatherPicker] Changed weather to: {}", Util::FormatWeather(selectedWeather));
			}
			ImGui::PopStyleColor();
			// Add hover tooltip to show full weather information
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::Text("Weather: %s", weather->GetName() ? weather->GetName() : "Unnamed");
				ImGui::Text("Editor ID: %s", weather->GetFormEditorID() ? weather->GetFormEditorID() : "None");
				ImGui::Text("Form ID: 0x%08X", weather->GetFormID());
				ImGui::EndTooltip();
			}

			// Set the initial focus when opening the combo (scrolls to it)
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::Spacing();
}

void WeatherPicker::RenderWeatherInformationDisplay(RE::Sky* sky, bool showInteractiveElements)
{
	static bool weatherInfoExpanded = true;
	Util::DrawSectionHeader("Weather Information", false, true, &weatherInfoExpanded);

	if (!weatherInfoExpanded)
		return;

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

void WeatherPicker::RenderCoreWeatherDetails(bool showInteractiveElements)
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

void WeatherPicker::LoadAllWeathers()
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

void WeatherPicker::UpdateFilteredWeathers()
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

	// Sort filtered weathers using the same comparator
	std::sort(s_filteredWeathers.begin(), s_filteredWeathers.end(), WeatherNameComparator{});
}

int WeatherPicker::FindWeatherIndex(RE::TESWeather* targetWeather)
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

void WeatherPicker::RenderFeatureWeatherAnalysis()
{
	// Iterate through all loaded features to show their weather analysis
	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->loaded) {
			// Skip the WeatherPicker itself to avoid recursion
			if (feature == &globals::features::weatherPicker) {
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

std::vector<std::string> WeatherPicker::GetWeatherFlagNames(RE::TESWeather* weather)
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

bool WeatherPicker::RenderMultiColorWeatherName(RE::TESWeather* weather, const std::string& weatherName)
{
	if (!weather) {
		ImGui::Text("%s", weatherName.c_str());
		return false;
	}

	// Get all flags present in this weather
	std::vector<std::string> flagNames = GetWeatherFlagNames(weather);

	// If no flags or only one flag, use simple single-color display
	if (flagNames.empty() || flagNames.size() == 1 || (flagNames.size() == 1 && flagNames[0] == "None")) {
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
ImVec4 WeatherPicker::GetWeatherFlagColor(RE::TESWeather::WeatherDataFlag flag)
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
ImVec4 WeatherPicker::GetWeatherFlagColorByName(const std::string& flagName)
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
	return ImVec4(0.9f, 0.85f, 0.7f, 1.0f);  // Light tan/beige for none/unclassified
}

std::string WeatherPicker::GetDisplayName(const RE::TESWeather* weather)
{
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

void WeatherPicker::DrawOverlay()
{
	bool overlayVisible = Menu::GetSingleton()->overlayVisible;
	// If ShowInOverlay is true and overlay is visible, auto-enable the window if not already enabled
	if (WeatherDetailsWindow.ShowInOverlay && overlayVisible) {
		if (!WeatherDetailsWindow.Enabled) {
			WeatherDetailsWindow.Enabled = true;
		}
		bool* p_open = &WeatherDetailsWindow.Enabled;
		RenderWeatherDetailsWindow(p_open);
	}
}

bool WeatherPicker::IsOverlayVisible() const
{
	return WeatherDetailsWindow.ShowInOverlay;
}