#include "Widget.h"
#include "EditorWindow.h"
#include "State.h"
#include "Util.h"
#include "WeatherUtils.h"

bool Widget::MatchesSearch(const std::string& text) const
{
	// If search is empty or inactive, match everything
	if (searchBuffer[0] == '\0') {
		return true;
	}
	return ContainsStringIgnoreCase(text, searchBuffer);
}

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

	if (!std::filesystem::exists(filePath)) {
		// No saved file exists, reset to vanilla/default values
		logger::info("{}: No settings file found, resetting to vanilla values", GetEditorID());
		js = json();
		LoadSettings();
		
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("No saved file - reset {} to vanilla values", GetEditorID()),
			ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
			3.0f);
		return;
	}

	// File exists, load from it
	std::ifstream settingsFile(filePath);

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to open settings file: {}", filePath);
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Failed to open file for {}", GetEditorID()),
			ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
			3.0f);
		return;
	}

	try {
		settingsFile >> js;
		settingsFile.close();
		
		// Validate that we loaded valid JSON
		if (js.is_null()) {
			logger::warn("{}: Loaded JSON is null, file may be empty or invalid", filePath);
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Invalid file for {} - resetting to vanilla", GetEditorID()),
				ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
				3.0f);
			js = json();
			LoadSettings();
			return;
		}
		
		logger::info("{}: Successfully loaded settings from file", GetEditorID());
		LoadSettings();
		
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Loaded saved settings for {}", GetEditorID()),
			ImVec4(0.0f, 1.0f, 0.5f, 1.0f),
			3.0f);
			
	} catch (const nlohmann::json::parse_error& e) {
		logger::error("Error parsing settings for file ({}) : {}\n", filePath, e.what());
		logger::error("Parse error at byte {}: {}", e.byte, e.what());
		settingsFile.close();
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Parse error for {} - resetting to vanilla", GetEditorID()),
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
			3.0f);
		js = json();
		LoadSettings();
		return;
	} catch (const std::exception& e) {
		logger::error("Unexpected error loading settings file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Error loading {} - resetting to vanilla", GetEditorID()),
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
			3.0f);
		js = json();
		LoadSettings();
		return;
	}
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
		
		// Apply the vanilla values to the game
		ApplyChanges();
		
		EditorWindow::GetSingleton()->ShowNotification(
			std::format("Deleted {} - reverted to vanilla values", GetEditorID()),
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
			3.0f);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error deleting settings file ({}) : {}\n", filePath, e.what());
	}
}

bool Widget::HasSavedFile() const
{
	std::string filePath = std::format("{}\\{}\\{}.json", Util::PathHelpers::GetCommunityShaderPath().string(), const_cast<Widget*>(this)->GetFolderName(), GetEditorID());
	return std::filesystem::exists(filePath);
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

void Widget::DrawWidgetHeader(const char* searchId, bool showApplyRevert, bool showSaveLoad, bool showForceWeather, RE::TESWeather* weather)
{
	auto editorWindow = EditorWindow::GetSingleton();
	auto menu = globals::menu;
	bool useIcons = !editorWindow->settings.useTextButtons && menu && menu->GetSettings().Theme.ShowActionIcons;

	if (useIcons) {
		const float iconSize = ImGui::GetFrameHeight();
		const ImVec2 buttonSize(iconSize, iconSize);

		// Search bar first
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::InputTextWithHint(searchId, "Search settings (Ctrl+F)", searchBuffer, sizeof(searchBuffer))) {
			searchActive = searchBuffer[0] != '\0';
		}
		
		// Handle Ctrl+F to focus search
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			ImGui::SetKeyboardFocusHere(-1);
		}

		// Force Weather button (Weather widget only)
		if (showForceWeather && weather) {
			ImGui::SameLine();
			
			bool isLocked = editorWindow->IsWeatherLocked() && editorWindow->GetLockedWeather() == weather;
			const char* lockLabel = isLocked ? "Unlock" : "Force Weather";
			ImVec2 lockTextSize = ImGui::CalcTextSize(lockLabel);
			float lockButtonWidth = lockTextSize.x + ImGui::GetStyle().FramePadding.x * 2.0f;
			
			if (isLocked) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
			}
			if (ImGui::Button(lockLabel, ImVec2(lockButtonWidth, iconSize))) {
				if (isLocked) {
					editorWindow->UnlockWeather();
				} else {
					editorWindow->LockWeather(weather);
				}
			}
			if (isLocked) {
				ImGui::PopStyleColor(2);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(isLocked ? "Unlock Weather" : "Force This Weather");
			}
		}

		// Icon buttons
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));

		// Apply/Revert buttons
		if (showApplyRevert && !editorWindow->settings.autoApplyChanges && menu->uiIcons.applyToGame.texture) {
			ImGui::SameLine();
			if (ImGui::ImageButton((std::string(searchId) + "_Apply").c_str(), menu->uiIcons.applyToGame.texture, buttonSize)) {
				ApplyChanges();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Apply changes to the game");
			}

			if (menu->uiIcons.featureSettingRevert.texture) {
				ImGui::SameLine();
				if (ImGui::ImageButton((std::string(searchId) + "_Revert").c_str(), menu->uiIcons.featureSettingRevert.texture, buttonSize)) {
					RevertChanges();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Revert to saved values");
				}
			}
		}

		// Save/Load/Delete buttons
		if (showSaveLoad && menu->uiIcons.saveSettings.texture && menu->uiIcons.loadSettings.texture) {
			ImGui::SameLine();
			if (ImGui::ImageButton((std::string(searchId) + "_Save").c_str(), menu->uiIcons.saveSettings.texture, buttonSize)) {
				Save();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Save to file");
			}

			ImGui::SameLine();
			if (ImGui::ImageButton((std::string(searchId) + "_Load").c_str(), menu->uiIcons.loadSettings.texture, buttonSize)) {
				Load();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Load saved file (or reset to vanilla if no file)");
			}

			if (HasSavedFile() && menu->uiIcons.deleteSettings.texture) {
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.3f, 1.0f));
				if (ImGui::ImageButton((std::string(searchId) + "_Delete").c_str(), menu->uiIcons.deleteSettings.texture, buttonSize)) {
					if (editorWindow->settings.suppressDeleteWarning) {
						Delete();
					} else {
						ImGui::OpenPopup("ConfirmDelete");
					}
				}
				ImGui::PopStyleColor(2);
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Delete saved file and revert to defaults");
				}
			}
		}

		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
	} else {
		// Text button mode
		const float buttonHeight = ImGui::GetFrameHeight();

		// Search bar first
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::InputTextWithHint(searchId, "Search settings (Ctrl+F)", searchBuffer, sizeof(searchBuffer))) {
			searchActive = searchBuffer[0] != '\0';
		}
		
		// Handle Ctrl+F to focus search
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			ImGui::SetKeyboardFocusHere(-1);
		}

		// Force Weather button (Weather widget only)
		if (showForceWeather && weather) {
			ImGui::SameLine();
			
			bool isLocked = editorWindow->IsWeatherLocked() && editorWindow->GetLockedWeather() == weather;
			const char* lockLabel = isLocked ? "Unlock" : "Force Weather";
			ImVec2 lockSize = ImGui::CalcTextSize(lockLabel);
			lockSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
			lockSize.y = buttonHeight;
			
			if (isLocked) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
			}
			if (ImGui::Button(lockLabel, lockSize)) {
				if (isLocked) {
					editorWindow->UnlockWeather();
				} else {
					editorWindow->LockWeather(weather);
				}
			}
			if (isLocked) {
				ImGui::PopStyleColor(2);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(isLocked ? "Unlock Weather" : "Force This Weather");
			}
		}

		// Apply/Revert buttons
		if (showApplyRevert && !editorWindow->settings.autoApplyChanges) {
			ImGui::SameLine();
			
			ImVec2 applySize = ImGui::CalcTextSize("Apply");
			applySize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
			applySize.y = buttonHeight;
			
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
			if (ImGui::Button("Apply", applySize)) {
				ApplyChanges();
			}
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Apply changes to the game");
			}
			
			ImGui::SameLine();
			
			ImVec2 revertSize = ImGui::CalcTextSize("Revert");
			revertSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
			revertSize.y = buttonHeight;
			
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
			if (ImGui::Button("Revert", revertSize)) {
				RevertChanges();
			}
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Revert to saved values");
			}
		}

		// Save/Load/Delete buttons
		if (showSaveLoad) {
			ImGui::SameLine();
			
			ImVec2 saveSize = ImGui::CalcTextSize("Save");
			saveSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
			saveSize.y = buttonHeight;
			
			if (ImGui::Button("Save", saveSize)) {
				Save();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Save to file");
			}
			
			ImGui::SameLine();
			
			ImVec2 loadSize = ImGui::CalcTextSize("Load");
			loadSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
			loadSize.y = buttonHeight;
			
			if (ImGui::Button("Load", loadSize)) {
				Load();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Load saved file (or reset to vanilla if no file)");
			}
			
			if (HasSavedFile()) {
				ImGui::SameLine();
				
				ImVec2 deleteSize = ImGui::CalcTextSize("Delete");
				deleteSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
				deleteSize.y = buttonHeight;
				
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.3f, 1.0f));
				if (ImGui::Button("Delete", deleteSize)) {
					if (editorWindow->settings.suppressDeleteWarning) {
						Delete();
					} else {
						ImGui::OpenPopup("ConfirmDelete");
					}
				}
				ImGui::PopStyleColor(2);
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Delete saved file and revert to defaults");
				}
			}
		}
	}

	// Confirmation popup for delete (shared by all widgets)
	if (showSaveLoad && ImGui::BeginPopupModal("ConfirmDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Are you sure you want to delete this file?");
		ImGui::Text("This will revert to vanilla/mod provided values.");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		
		if (ImGui::Checkbox("Don't show this warning again", &editorWindow->settings.suppressDeleteWarning)) {
			editorWindow->Save();
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
	
	ImGui::Separator();
}
