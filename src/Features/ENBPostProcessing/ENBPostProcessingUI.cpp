#include "ENBPostProcessingUI.h"
#include "PCH.h"

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

	RenderColorCorrectionSettings();
	RenderAdaptationSettings();
	RenderDepthOfFieldSettings();
	RenderWeatherControl();
	RenderBloomSettings();
	RenderLensSettings();
}

void ENBPostProcessingUI::RenderEffectsList()
{
	auto& effectManager = EffectManager::GetSingleton();

	for (auto& [name, effect] : effectManager.effects) {
		bool isCompiled = effect->IsCompiled();
		const auto& errors = effect->GetErrors();

		// Set text color based on compilation status
		if (!isCompiled && !errors.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));  // Red for errors
		} else if (!isCompiled) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));  // Yellow for uncompiled
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));  // Green for successful
		}

		if (ImGui::CollapsingHeader(name.c_str())) {
			effect->RenderImGui();

			// Show compilation errors if any
			if (!errors.empty()) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Compilation Errors:");
				for (const auto& error : errors) {
					ImGui::TextWrapped("%s", error.c_str());
				}
			}
		}

		ImGui::PopStyleColor();
	}
}

void ENBPostProcessingUI::RenderColorCorrectionSettings()
{
	auto& effectManager = EffectManager::GetSingleton();

	if (ImGui::TreeNodeEx("COLORCORRECTION", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::BeginTable("colorcorrection_table", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Brightness");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##Brightness", &effectManager.enbSettings.COLORCORRECTION.Brightness, 0.0f, 3.0f, "%.2f");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("GammaCurve");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##GammaCurve", &effectManager.enbSettings.COLORCORRECTION.GammaCurve, 0.1f, 3.0f, "%.2f");

			ImGui::EndTable();
		}
		ImGui::TreePop();
	}
}

void ENBPostProcessingUI::RenderAdaptationSettings()
{
	auto& effectManager = EffectManager::GetSingleton();

	if (ImGui::TreeNodeEx("ADAPTATION", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::BeginTable("adaptation_table", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("AdaptationSensitivity");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##AdaptationSensitivity", &effectManager.enbSettings.ADAPTATION.AdaptationSensitivity, 0.0f, 2.0f, "%.2f");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("ForceMinMaxValues");
			ImGui::TableSetColumnIndex(1);
			ImGui::Checkbox("##ForceMinMaxValues", &effectManager.enbSettings.ADAPTATION.ForceMinMaxValues);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("AdaptationMin");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##AdaptationMin", &effectManager.enbSettings.ADAPTATION.AdaptationMin, 0.0f, 1.0f, "%.2f");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("AdaptationMax");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##AdaptationMax", &effectManager.enbSettings.ADAPTATION.AdaptationMax, 0.0f, 5.0f, "%.2f");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("AdaptationTime");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##AdaptationTime", &effectManager.enbSettings.ADAPTATION.AdaptationTime, 0.0f, 10.0f, "%.1f");

			ImGui::EndTable();
		}
		ImGui::TreePop();
	}
}

void ENBPostProcessingUI::RenderDepthOfFieldSettings()
{
	auto& effectManager = EffectManager::GetSingleton();

	if (ImGui::TreeNodeEx("DEPTHOFFIELD", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::BeginTable("depthoffield_table", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("FocusingTime");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##FocusingTime", &effectManager.enbSettings.DEPTHOFFIELD.FocusingTime, 0.0f, 10.0f, "%.1f");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("ApertureTime");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##ApertureTime", &effectManager.enbSettings.DEPTHOFFIELD.ApertureTime, 0.0f, 10.0f, "%.1f");

			ImGui::EndTable();
		}
		ImGui::TreePop();
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

void ENBPostProcessingUI::RenderBloomSettings()
{
	auto& effectManager = EffectManager::GetSingleton();
	auto& weatherManager = WeatherManager::GetSingleton();

	if (ImGui::TreeNodeEx("BLOOM", ImGuiTreeNodeFlags_DefaultOpen)) {
		// Check if weather settings are active
		uint32_t currentWeatherID = static_cast<uint32_t>(effectManager.commonData.weather[0]);
		uint32_t lastWeatherID = static_cast<uint32_t>(effectManager.commonData.weather[1]);
		bool hasWeatherSettings = weatherManager.FindWeatherEntry(currentWeatherID) != nullptr ||
		                          weatherManager.FindWeatherEntry(lastWeatherID) != nullptr;

		if (hasWeatherSettings) {
			auto effectiveSettings = effectManager.GetEffectiveBloomAmount();
			ImGui::BeginDisabled(true);
			RenderTimeOfDaySettings("Amount", effectiveSettings);
			ImGui::EndDisabled();
		} else {
			ImGui::Text("Using ENB Settings (No Weather Data)");
			RenderTimeOfDaySettings("Amount", effectManager.enbSettings.BLOOM.Amount);
		}

		ImGui::TreePop();
	}
}

void ENBPostProcessingUI::RenderLensSettings()
{
	auto& effectManager = EffectManager::GetSingleton();
	auto& weatherManager = WeatherManager::GetSingleton();

	if (ImGui::TreeNodeEx("LENS", ImGuiTreeNodeFlags_DefaultOpen)) {
		// Check if weather settings are active
		uint32_t currentWeatherID = static_cast<uint32_t>(effectManager.commonData.weather[0]);
		uint32_t lastWeatherID = static_cast<uint32_t>(effectManager.commonData.weather[1]);
		bool hasWeatherSettings = weatherManager.FindWeatherEntry(currentWeatherID) != nullptr ||
		                          weatherManager.FindWeatherEntry(lastWeatherID) != nullptr;

		if (hasWeatherSettings) {
			auto effectiveSettings = effectManager.GetEffectiveLensAmount();
			ImGui::BeginDisabled(true);
			RenderTimeOfDaySettings("Amount", effectiveSettings);
			ImGui::EndDisabled();
		} else {
			ImGui::Text("Using ENB Settings (No Weather Data)");
			RenderTimeOfDaySettings("Amount", effectManager.enbSettings.LENS.Amount);
		}

		ImGui::TreePop();
	}
}

void ENBPostProcessingUI::RenderTimeOfDaySettings(const std::string& prefix, EffectManager::TimeOfDaySettings& settings)
{
	const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night" };

	if (ImGui::BeginTable((prefix + "_timeofday_table").c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		for (const auto& timeOfDay : timeOfDayNames) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%s", timeOfDay.c_str());
			ImGui::TableSetColumnIndex(1);
			std::string id = "##" + prefix + timeOfDay;
			ImGui::SliderFloat(id.c_str(), &settings[timeOfDay], 0.0f, 10.0f, "%.1f");
		}

		ImGui::EndTable();
	}
}