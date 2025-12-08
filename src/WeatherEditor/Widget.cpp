#include "Widget.h"
#include "EditorWindow.h"
#include "State.h"
#include "Util.h"

void Widget::Save()
{
	SaveSettings();
	const std::string filePath = std::format("{}\\{}", Util::PathHelpers::GetCommunityShaderPath().string(), GetFolderName());
	const std::string file = std::format("{}\\{}.json", filePath, GetEditorID());

	std::ofstream settingsFile(file);
	if (!std::filesystem::exists(filePath) || !std::filesystem::is_directory(filePath)) {
		try {
			std::filesystem::create_directories(filePath);
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error creating directory during Save ({}) : {}\n", filePath, e.what());
			return;
		}
	}

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to open settings file: {}", file);
		return;
	}

	if (settingsFile.fail()) {
		logger::warn("Unable to create settings file: {}", file);
		settingsFile.close();
		return;
	}

	try {
		// Validate that we have valid JSON to write
		if (js.is_null()) {
			logger::warn("{}: Cannot save - JSON data is null", GetEditorID());
			settingsFile.close();
			return;
		}

		logger::info("{}: Saving settings file: {}", GetEditorID(), file);

		// Write with indentation for readability
		settingsFile << js.dump(2);
		settingsFile.flush();

		if (settingsFile.fail()) {
			logger::error("{}: Failed to write settings to file", GetEditorID());
			settingsFile.close();
			return;
		}

		settingsFile.close();
		logger::info("{}: Successfully saved settings", GetEditorID());

	} catch (const nlohmann::json::exception& e) {
		logger::error("{}: JSON error while saving settings: {}", GetEditorID(), e.what());
		settingsFile.close();
	} catch (const std::exception& e) {
		logger::error("{}: Unexpected error saving settings file: {}", GetEditorID(), e.what());
		settingsFile.close();
	}
}

void Widget::Load()
{
	std::string filePath = std::format("{}\\{}\\{}.json", Util::PathHelpers::GetCommunityShaderPath().string(), GetFolderName(), GetEditorID());

	std::ifstream settingsFile(filePath);

	if (!std::filesystem::exists(filePath)) {
		// No saved file exists, reload vanilla/default values
		logger::info("{}: No settings file found, reloading vanilla values", GetEditorID());
		js = json();
		LoadSettings();
		return;
	}

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to load settings file: {}", filePath);
		return;
	}

	try {
		settingsFile >> js;
		settingsFile.close();

		// Validate that we loaded valid JSON
		if (js.is_null()) {
			logger::warn("{}: Loaded JSON is null, file may be empty or invalid", filePath);
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Failed to load settings for {}", GetEditorID()),
				ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
				3.0f);
			return;
		}

		logger::info("{}: Successfully loaded settings file", GetEditorID());

	} catch (const nlohmann::json::parse_error& e) {
		logger::error("Error parsing settings for file ({}) : {}\n", filePath, e.what());
		logger::error("Parse error at byte {}: {}", e.byte, e.what());
		settingsFile.close();
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Failed to parse settings for {}", GetEditorID()),
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
			3.0f);
		return;
	} catch (const std::exception& e) {
		logger::error("Unexpected error loading settings file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Failed to load settings for {}", GetEditorID()),
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
			3.0f);
		return;
	}
	LoadSettings();
}

void Widget::Delete()
{
	std::string filePath = std::format("{}\\{}\\{}.json", Util::PathHelpers::GetCommunityShaderPath().string(), GetFolderName(), GetEditorID());

	if (!std::filesystem::exists(filePath)) {
		logger::info("Settings file does not exist, nothing to delete: {}", filePath);
		return;
	}

	try {
		std::filesystem::remove(filePath);
		logger::info("Deleted settings file: {}", filePath);

		// Clear the in-memory JSON data
		js = json();

		// Reload settings from vanilla/mod defaults
		LoadSettings();

		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Deleted {} - reverted to vanilla values", GetEditorID()),
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
			3.0f);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error deleting settings file ({}) : {}\n", filePath, e.what());
	}
}

void Widget::DrawMenu()
{
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("Menu")) {
			if (ImGui::MenuItem("Save")) {
				Save();
			}
			if (ImGui::MenuItem("Load")) {
				Load();
			}
			if (ImGui::MenuItem("Revert to Defaults")) {
				auto& settings = EditorWindow::GetSingleton()->settings;
				if (settings.suppressDeleteWarning) {
					// Delete directly if warning is suppressed
					Delete();
				} else {
					// Open confirmation popup
					ImGui::OpenPopup("ConfirmDelete");
				}
			}

			// Confirmation popup for delete
			if (ImGui::BeginPopupModal("ConfirmDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Are you sure you want to delete this file?");
				ImGui::Text("This will revert to vanilla/mod provided values.");
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				auto& settings = EditorWindow::GetSingleton()->settings;
				if (ImGui::Checkbox("Don't show this warning again", &settings.suppressDeleteWarning)) {
					// Save the preference immediately
					EditorWindow::GetSingleton()->Save();
				}

				ImGui::Spacing();

				if (ImGui::Button("Yes", ImVec2(120, 0))) {
					Delete();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SetItemDefaultFocus();
				ImGui::SameLine();
				if (ImGui::Button("No", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}

std::string Widget::GetFolderName()
{
	switch (form->GetFormType()) {
	case RE::FormType::Weather:
		return "Weathers";
	case RE::FormType::LightingMaster:
		return "LightingTemplates";
	case RE::FormType::WorldSpace:
		return "WorldSpaces";
	default:
		return "Unknown";
	}
}
