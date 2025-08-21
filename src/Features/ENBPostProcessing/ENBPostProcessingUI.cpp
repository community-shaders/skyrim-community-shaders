#include "ENBPostProcessingUI.h"
#include "PCH.h"
#include "SettingsRegistry.h"

ENBPostProcessingUI& ENBPostProcessingUI::GetSingleton()
{
	static ENBPostProcessingUI instance;
	return instance;
}

void ENBPostProcessingUI::RenderImGui()
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
		RenderEffectsList();
	}
	ImGui::EndChild();

	ImGui::EndTable();
}

void ENBPostProcessingUI::RenderSettingsPanel()
{
	auto& effectManager = EffectManager::GetSingleton();

	if (ImGui::Button("Load")) {
		effectManager.LoadEffects();
	}

	ImGui::SameLine();

	if (ImGui::Button("Save")) {
		effectManager.SaveEffects();
		effectManager.SaveENBSettings();
	}

	RenderWeatherControl();
	RenderAllSettings();
}

void ENBPostProcessingUI::RenderEffectsList()
{
	auto& effectManager = EffectManager::GetSingleton();

	for (auto& [name, effect] : effectManager.effects) {
		const auto& errors = effect->GetErrors();

		if (ImGui::CollapsingHeader(name.c_str())) {
			effect->RenderImGui();

			// Show compilation errors if any
			if (!errors.empty()) {
				for (const auto& error : errors) {
					ImGui::TextWrapped("%s", error.c_str());
				}
			}
		}
	}
}

void ENBPostProcessingUI::RenderWeatherControl()
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

void ENBPostProcessingUI::RenderAllSettings()
{
	auto& settingsRegistry = SettingsRegistry::GetSingleton();

	auto categories = settingsRegistry.GetAllCategories();
	for (const auto& category : categories) {
		if (ImGui::TreeNodeEx(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			auto settings = settingsRegistry.GetSettingsByCategory(category);

			if (ImGui::BeginTable((category + "_table").c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				for (const auto& settingKey : settings) {
					auto settingInfo = settingsRegistry.GetSettingInfo(settingKey);
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
							bool v = settingsRegistry.GetValue<bool>(settingKey);
							if (ImGui::Checkbox(("##" + settingKey).c_str(), &v)) {
								settingsRegistry.SetValue<bool>(settingKey, v);
							}
							break;
						}
					case SettingType::Float:
						{
							float v = settingsRegistry.GetValue<float>(settingKey);
							if (ImGui::SliderFloat(("##" + settingKey).c_str(), &v, settingInfo->minValue, settingInfo->maxValue, "%.2f")) {
								settingsRegistry.SetValue<float>(settingKey, v);
							}
							break;
						}
					case SettingType::TimeOfDay:
						{
							const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night" };
							auto v = settingsRegistry.GetValue<TimeOfDayValue>(settingKey);
							bool changed = false;

							for (const auto& timeOfDay : timeOfDayNames) {
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::Text("%s%s", settingKey.c_str(), timeOfDay.c_str());
								ImGui::TableSetColumnIndex(1);
								std::string id = "##" + settingKey + timeOfDay;
								if (ImGui::SliderFloat(id.c_str(), &v[timeOfDay], settingInfo->minValue, settingInfo->maxValue, "%.1f")) {
									changed = true;
								}
							}

							if (changed) {
								settingsRegistry.SetValue<TimeOfDayValue>(settingKey, v);
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