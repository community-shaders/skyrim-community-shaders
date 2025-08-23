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

	RenderWeatherControl();
	RenderAllSettings();
}

void MenuManager::RenderWeatherControl()
{
	auto& effectManager = EffectManager::GetSingleton();
	auto& weatherManager = WeatherManager::GetSingleton();

	if (ImGui::TreeNodeEx("WEATHER CONTROL", ImGuiTreeNodeFlags_DefaultOpen)) {
		// Current weather status
		uint32_t currentWeatherID = static_cast<uint32_t>(effectManager.commonData.weather[0]);
		uint32_t lastWeatherID = static_cast<uint32_t>(effectManager.commonData.weather[1]);
		float blendFactor = effectManager.commonData.weather[2];

		ImGui::Text("Current: 0x%X, Last: 0x%X", currentWeatherID, lastWeatherID);
		ImGui::Text("Blend Factor: %.2f", blendFactor);

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

		ImGui::TreePop();
	}
}

void MenuManager::RenderAllSettings()
{
	auto& settingManager = SettingManager::GetSingleton();

	auto categories = settingManager.GetAllCategories();
	for (const auto& category : categories) {
		if (ImGui::TreeNodeEx(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			auto settings = settingManager.GetSettingsByCategory(category);

			if (ImGui::BeginTable((category + "_table").c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				// Add weather ignore controls for categories with weather support
				if (settingManager.CategoryHasWeatherSupport(category)) {
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
							const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };
							auto v = settingManager.GetValue<TimeOfDayValue>(settingKey, category, true);
							bool changed = false;

							for (int i = 0; i < TimeOfDayValue::Total; ++i) {
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::Text("%s%s", settingKey.c_str(), timeOfDayNames[i].c_str());
								ImGui::TableSetColumnIndex(1);
								std::string id = "##" + settingKey + timeOfDayNames[i];
								if (ImGui::SliderFloat(id.c_str(), &v.values[i], settingInfo->minValue, settingInfo->maxValue, "%.1f")) {
									changed = true;
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
			ImGui::TreePop();
		}
	}
}