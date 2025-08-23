#include "MenuManager.h"

#include "EffectManager.h"
#include "SettingManager.h"

MenuManager& MenuManager::GetSingleton()
{
	static MenuManager instance;
	return instance;
}

void MenuManager::RenderImGui()
{
	if (!ImGui::BeginTable("ENBPostProcessing", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV, ImVec2(0, 0))) {
		return;
	}

	ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthFixed, 400.0f);
	ImGui::TableSetupColumn("Effects", ImGuiTableColumnFlags_WidthStretch);

	ImGui::TableNextRow();

	// Left side - Settings
	ImGui::TableSetColumnIndex(0);
	if (ImGui::BeginChild("Settings", ImVec2(0, 0), false)) {
		RenderSettingsPanel();
	}
	ImGui::EndChild();

	// Right side - Effects
	ImGui::TableSetColumnIndex(1);
	if (ImGui::BeginChild("Effects", ImVec2(0, 0), false)) {
		EffectManager::GetSingleton().RenderEffectsList();
	}
	ImGui::EndChild();

	ImGui::EndTable();
}

void MenuManager::RenderSettingsPanel()
{
	auto& settingManager = SettingManager::GetSingleton();
	auto& effectManager = EffectManager::GetSingleton();

	if (ImGui::Button("Apply")) {
		settingManager.Load();
		effectManager.Apply();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Load all settings from enbseries.ini, weather files, and effect configurations, reload shaders");
	}

	ImGui::SameLine();

	if (ImGui::Button("Load")) {
		settingManager.Load();
		effectManager.Load();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Load all settings from enbseries.ini, weather files, and effect configurations");
	}

	ImGui::SameLine();

	if (ImGui::Button("Save")) {
		settingManager.Save();
		effectManager.Save();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Save all settings to enbseries.ini, weather files, and effect configurations");
	}

	ImGui::Separator();

	RenderAllSettings();
}

void MenuManager::RenderWeatherControl()
{
	auto& effectManager = EffectManager::GetSingleton();
	auto& weatherManager = WeatherManager::GetSingleton();

	// Current weather status
	uint32_t currentWeatherID = static_cast<uint32_t>(effectManager.commonData.weather[0]);
	uint32_t lastWeatherID = static_cast<uint32_t>(effectManager.commonData.weather[1]);
	float blendFactor = effectManager.commonData.weather[2];

	ImGui::Text("Current: 0x%X, Last: 0x%X", currentWeatherID, lastWeatherID);
	ImGui::Text("Blend Factor: %.2f", blendFactor);

	// Time of day status
	const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };
	auto activeIndices = GetActiveTimeOfDayIndices();

	if (!activeIndices.empty()) {
		std::string activeTimesText = "Active Times";
		for (size_t i = 0; i < activeIndices.size(); ++i) {
			if (i > 0)
				activeTimesText += ", ";
			activeTimesText += timeOfDayNames[activeIndices[i]];
		}
		ImGui::Text("%s", activeTimesText.c_str());
	}

	// Weather file list
	if (ImGui::TreeNodeEx("Weather Files", ImGuiTreeNodeFlags_DefaultOpen)) {
		const auto& weatherEntries = weatherManager.GetWeatherEntries();

		if (!weatherEntries.empty()) {
			if (ImGui::BeginChild("WeatherList", ImVec2(0, 200), true)) {
				// Sort weather entries by name for consistent display
				std::vector<std::pair<std::string, const WeatherManager::WeatherEntry*>> sortedWeathers;
				for (const auto& [key, entry] : weatherEntries) {
					sortedWeathers.emplace_back(key, &entry);
				}
				std::sort(sortedWeathers.begin(), sortedWeathers.end());

				for (const auto& [key, entry] : sortedWeathers) {
					ImGui::PushID(key.c_str());

					// Show weather file name and IDs
					ImGui::Text("%s", entry->fileName.c_str());
					ImGui::SameLine();
					ImGui::Text("(%s)", key.c_str());

					// Show weather IDs on same line
					ImGui::SameLine();
					std::string idsText = "IDs: ";
					for (size_t i = 0; i < entry->weatherIDs.size() && i < 3; ++i) {
						if (i > 0)
							idsText += ", ";
						idsText += std::format("0x{:X}", entry->weatherIDs[i]);
					}
					if (entry->weatherIDs.size() > 3) {
						idsText += "...";
					}
					ImGui::Text("%s", idsText.c_str());

					ImGui::PopID();
				}
			}
			ImGui::EndChild();
		} else {
			ImGui::Text("No weather files loaded");
			ImGui::Text("Make sure _weatherlist.ini exists in enbseries folder");
		}

		ImGui::TreePop();
	}
}

std::map<std::string, std::vector<std::string>> MenuManager::GetCategorizedSettings() const
{
	std::map<std::string, std::vector<std::string>> categorizedSettings;

	// Global Settings - Master controls and basic adjustments
	categorizedSettings["Main"] = {
		"GLOBAL",
		"COLORCORRECTION",
		"EFFECT",
		"DEPTHOFFIELD"
	};

	// Weather-Based Settings - Categories that change with weather/time
	categorizedSettings["Weather"] = { "BLOOM", "LENS", "SKY", "ENVIRONMENT", "IMAGEBASEDLIGHTING", "VOLUMETRICFOG", "GAMEVOLUMETRICRAYS" };

	return categorizedSettings;
}

std::vector<int> MenuManager::GetActiveTimeOfDayIndices() const
{
	auto& effectManager = EffectManager::GetSingleton();
	std::vector<int> activeIndices;

	// Access time of day data from EffectManager (this data is updated every frame)
	const auto& commonData = effectManager.commonData;

	// Check if we're in interior (> 0.5) or exterior
	bool isInterior = commonData.eInteriorFactor > 0.5f;

	if (isInterior) {
		// For interiors, show both interior time periods
		activeIndices.push_back(6);  // InteriorDay
		activeIndices.push_back(7);  // InteriorNight
	} else {
		// For exteriors, show all exterior time periods
		activeIndices.push_back(0);  // Dawn
		activeIndices.push_back(1);  // Sunrise
		activeIndices.push_back(2);  // Day
		activeIndices.push_back(3);  // Sunset
		activeIndices.push_back(4);  // Dusk
		activeIndices.push_back(5);  // Night
	}

	return activeIndices;
}

float MenuManager::GetTimeOfDayBlendFactor(int timeIndex) const
{
	auto& effectManager = EffectManager::GetSingleton();
	const auto& commonData = effectManager.commonData;

	// Return the actual blend factor for each time period
	switch (timeIndex) {
	case 0:
		return commonData.timeOfDay1[0];  // Dawn
	case 1:
		return commonData.timeOfDay1[1];  // Sunrise
	case 2:
		return commonData.timeOfDay1[2];  // Day
	case 3:
		return commonData.timeOfDay1[3];  // Sunset
	case 4:
		return commonData.timeOfDay2[0];  // Dusk
	case 5:
		return commonData.timeOfDay2[1];  // Night
	case 6:                               // InteriorDay - calculate from interior blend
	case 7:                               // InteriorNight - calculate from interior blend
		{
			if (commonData.eInteriorFactor > 0.5f) {
				float dayNightFactor = (commonData.timeOfDay1[2] + commonData.timeOfDay1[1] +
										commonData.timeOfDay1[0] * 0.5f + commonData.timeOfDay1[3] * 0.5f);
				return (timeIndex == 6) ? dayNightFactor : (1.0f - dayNightFactor);
			}
			return 0.0f;
		}
	default:
		return 0.0f;
	}
}

void MenuManager::RenderAllSettings()
{
	auto& settingManager = SettingManager::GetSingleton();
	auto categorizedSettings = GetCategorizedSettings();

	if (ImGui::BeginTabBar("SettingsTabBar", ImGuiTabBarFlags_None)) {
		for (const auto& [tabName, categories] : categorizedSettings) {
			if (ImGui::BeginTabItem(tabName.c_str())) {
				// Add weather control to the Weather tab
				if (tabName == "Weather") {
					RenderWeatherControl();
					ImGui::Separator();
					
					// Show TimeOfDay header for Weather tab only
					const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };
					auto activeIndices = GetActiveTimeOfDayIndices();
					
					if (!activeIndices.empty()) {
						if (ImGui::BeginTable("WeatherTimeOfDayHeader", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
							ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
							ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::Text("Time Periods");
							ImGui::TableSetColumnIndex(1);
							
							float totalWidth = ImGui::GetContentRegionAvail().x;
							float sliderWidth = (totalWidth - (activeIndices.size() - 1) * 8.0f) / activeIndices.size();
							
							for (size_t idx = 0; idx < activeIndices.size(); ++idx) {
								int i = activeIndices[idx];
								
								if (idx > 0) {
									ImGui::SameLine();
								}
								
								// Use a child region to control the exact width and center the text
								ImGui::BeginChild(("##weatherheader_" + std::to_string(i)).c_str(), ImVec2(sliderWidth, ImGui::GetTextLineHeight()), false, ImGuiWindowFlags_NoScrollbar);
								
								float labelWidth = ImGui::CalcTextSize(timeOfDayNames[i].c_str()).x;
								float centerOffset = (sliderWidth - labelWidth) * 0.5f;
								if (centerOffset > 0) {
									ImGui::SetCursorPosX(centerOffset);
								}
								
								// Style the label based on activity
								float blendFactor = GetTimeOfDayBlendFactor(i);
								bool isActive = blendFactor > 0.01f;
								
								if (!isActive) {
									// Inactive periods: dim the text
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));
								}
								// Active periods: use default theme color (no style override)
								
								ImGui::Text("%s", timeOfDayNames[i].c_str());
								
								if (!isActive) {
									ImGui::PopStyleColor();
								}
								
								ImGui::EndChild();
							}
							
							ImGui::EndTable();
						}
						
						ImGui::Separator();
					}
				}

				for (const auto& category : categories) {
					if (ImGui::CollapsingHeader(category.c_str())) {
						auto settings = settingManager.GetSettingsByCategory(category);

						if (ImGui::BeginTable((category + "_table").c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
							ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
							ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

							// Add weather ignore controls for categories with weather support
							if (settingManager.CategoryHasWeatherSupport(category)) {
								auto& effectManager = EffectManager::GetSingleton();
								bool isInterior = effectManager.commonData.eInteriorFactor > 0.5f;

								if (!isInterior) {
									// Show exterior ignore setting when outside
									ImGui::TableNextRow();
									ImGui::TableSetColumnIndex(0);
									ImGui::Text("IgnoreWeatherSystem");
									ImGui::TableSetColumnIndex(1);
									bool ignoreWeather = settingManager.GetIgnoreWeatherSystem(category);
									if (ImGui::Checkbox(("##IgnoreWeatherSystem_" + category).c_str(), &ignoreWeather)) {
										settingManager.SetIgnoreWeatherSystem(category, ignoreWeather);
									}
									if (ImGui::IsItemHovered()) {
										ImGui::SetTooltip("When enabled, uses enbseries.ini values instead of weather-specific values for exterior areas");
									}
								} else {
									// Show interior ignore setting when inside
									ImGui::TableNextRow();
									ImGui::TableSetColumnIndex(0);
									ImGui::Text("IgnoreWeatherSystemInterior");
									ImGui::TableSetColumnIndex(1);
									bool ignoreWeatherInterior = settingManager.GetIgnoreWeatherSystemInterior(category);
									if (ImGui::Checkbox(("##IgnoreWeatherSystemInterior_" + category).c_str(), &ignoreWeatherInterior)) {
										settingManager.SetIgnoreWeatherSystemInterior(category, ignoreWeatherInterior);
									}
									if (ImGui::IsItemHovered()) {
										ImGui::SetTooltip("When enabled, uses enbseries.ini values instead of weather-specific values for interior areas");
									}
								}

								// Add separator
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::Separator();
								ImGui::TableSetColumnIndex(1);
								ImGui::Separator();
							}


							for (const auto& settingKey : settings) {
								auto settingInfo = settingManager.GetSettingInfo(settingKey, category);
								if (!settingInfo)
									continue;

								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								if (settingInfo->type != SettingType::TimeOfDay)
									ImGui::Text("%s", settingKey.c_str());
								ImGui::TableSetColumnIndex(1);

								switch (settingInfo->type) {
								case SettingType::Bool:
									{
										bool v = settingManager.GetValue<bool>(settingKey, category, true);
										if (ImGui::Checkbox(("##" + settingKey).c_str(), &v)) {
											settingManager.SetValue<bool>(settingKey, category, v);
										}
										break;
									}
								case SettingType::Float:
									{
										float v = settingManager.GetValue<float>(settingKey, category, true);
										if (ImGui::SliderFloat(("##" + settingKey).c_str(), &v, settingInfo->minValue, settingInfo->maxValue, "%.2f")) {
											settingManager.SetValue<float>(settingKey, category, v);
										}
										break;
									}
								case SettingType::TimeOfDay:
									{
										auto v = settingManager.GetValue<TimeOfDayValue>(settingKey, category, true);
										auto activeIndices = GetActiveTimeOfDayIndices();
										bool changed = false;

										// Show active time-of-day periods on one row
										ImGui::TableNextRow();
										ImGui::TableSetColumnIndex(0);
										ImGui::Text("%s", settingKey.c_str());
										ImGui::TableSetColumnIndex(1);

										// Render all active sliders horizontally
										float totalWidth = ImGui::GetContentRegionAvail().x;
										float sliderWidth = (totalWidth - (activeIndices.size() - 1) * 8.0f) / activeIndices.size();  // 8px spacing between sliders

										for (size_t idx = 0; idx < activeIndices.size(); ++idx) {
											int i = activeIndices[idx];

											if (idx > 0) {
												ImGui::SameLine();
											}

											// Style the slider based on activity
											float blendFactor = GetTimeOfDayBlendFactor(i);
											bool isActive = blendFactor > 0.0f;

											if (!isActive) {
												// Inactive sliders: dim the appearance
												ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
											}

											ImGui::PushItemWidth(sliderWidth);
											std::string id = "##" + settingKey + std::to_string(i);
											if (ImGui::SliderFloat(id.c_str(), &v.values[i], settingInfo->minValue, settingInfo->maxValue, "%.1f")) {
												changed = true;
											}

											// Add tooltip showing blend factor
											if (ImGui::IsItemHovered()) {
												ImGui::SetTooltip("%.0f%%", blendFactor * 100.0f);
											}

											ImGui::PopItemWidth();

											if (!isActive) {
												ImGui::PopStyleVar();  // Alpha
											}
										}

										if (changed) {
											settingManager.SetValue<TimeOfDayValue>(settingKey, category, v);
										}
										break;
									}
								}
							}

							ImGui::EndTable();
						}
					}
				}
				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}
}