#include "Widget.h"
#include "EditorWindow.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"
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

	if (!std::filesystem::exists(filePath) || !std::filesystem::is_directory(filePath)) {
		try {
			std::filesystem::create_directories(filePath);
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error creating directory during Save ({}) : {}\n", filePath, e.what());
			return;
		}
	}

	std::ofstream settingsFile(file);
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

		settingsFile << js.dump(2);
		settingsFile.flush();

		if (settingsFile.fail()) {
			logger::error("{}: Failed to write settings to file", GetEditorID());
			settingsFile.close();
			return;
		}

		settingsFile.close();
		EditorWindow::GetSingleton()->OnWidgetJsonAttachmentChanged(this);

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
		return;
	}

	try {
		std::filesystem::remove(filePath);

		js = json();

		// Reload settings from vanilla/mod defaults
		LoadSettings();

		// Apply the vanilla values to the game
		ApplyChanges();

		EditorWindow::GetSingleton()->OnWidgetJsonAttachmentChanged(this);

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
			if (ImGui::MenuItem("Delete Saved File")) {
				ImGui::OpenPopup("DeleteConfirmation");
			}
			if (ImGui::MenuItem("Revert to Game Values")) {
				RevertChanges();
			}

			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	DrawDeleteConfirmationModal();
}

void Widget::DrawDeleteConfirmationModal(const char* popupId)
{
	if (!ImGui::IsPopupOpen(popupId))
		return;
	if (deleteConfirmationFrame == ImGui::GetFrameCount())
		return;

	if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		deleteConfirmationFrame = ImGui::GetFrameCount();
		ImGui::Text("Are you sure you want to delete the saved settings file?");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const float buttonWidth = 120.0f;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float totalWidth = (buttonWidth * 2) + spacing;
		const float cursorX = (ImGui::GetWindowWidth() - totalWidth) / 2.0f;

		ImGui::SetCursorPosX(cursorX);

		if (ImGui::Button("Yes, Delete", ImVec2(buttonWidth, 0))) {
			Delete();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();

		ImGui::EndPopup();
	}
}

std::string Widget::GetFolderName()
{
	switch (form->GetFormType()) {
	case RE::FormType::Weather:
		return "Weathers";
	case RE::FormType::LightingMaster:
		return "Lighting Templates";
	case RE::FormType::ImageSpace:
		return "ImageSpaces";
	case RE::FormType::VolumetricLighting:
		return "Volumetric Lighting";
	case RE::FormType::ShaderParticleGeometryData:
		return "Precipitation";
	case RE::FormType::ReferenceEffect:
		return "Visual Effects";
	case RE::FormType::Cell:
		return "Cell Lighting";
	default:
		return "Other Editor Widgets";
	}
}

void Widget::DrawWidgetHeader(const char* searchId, bool showApply, bool showSaveLoadRevert, bool showForceWeather, RE::TESWeather* weather)
{
	auto editorWindow = EditorWindow::GetSingleton();
	auto menu = globals::menu;
	bool useIcons = !editorWindow->settings.useTextButtons && menu && menu->GetSettings().Theme.ShowActionIcons;

	auto drawSearchBar = [&]() {
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::InputTextWithHint(searchId, "Search settings (Ctrl+F)", searchBuffer, sizeof(searchBuffer)))
			searchActive = searchBuffer[0] != '\0';
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
			ImGui::SetKeyboardFocusHere(-1);
	};

	auto drawForceWeatherButton = [&](float height) {
		if (!showForceWeather || !weather)
			return;
		ImGui::SameLine();
		bool isLocked = editorWindow->IsWeatherLocked() && editorWindow->GetLockedWeather() == weather;
		const char* lockLabel = isLocked ? "Unlock" : "Force Weather";
		ImVec2 lockSize = ImGui::CalcTextSize(lockLabel);
		lockSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
		lockSize.y = height;

		if (isLocked) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
		}
		if (ImGui::Button(lockLabel, lockSize)) {
			if (isLocked)
				editorWindow->UnlockWeather();
			else
				editorWindow->LockWeather(weather);
		}
		if (isLocked)
			ImGui::PopStyleColor(2);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(isLocked ? "Unlock Weather" : "Force This Weather");
	};

	auto drawUnsavedIndicator = [&]() {
		if (!HasUnsavedChanges() || !menu)
			return;
		ImGui::SameLine();
		ImGui::TextColored(menu->GetTheme().StatusPalette.Warning, "(UNSAVED CHANGES)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Unsaved changes - click save to keep");
	};

	if (useIcons) {
		const float iconSize = ImGui::GetFrameHeight() * 0.85f;
		const ImVec2 buttonSize(iconSize, iconSize);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, ImGui::GetStyle().ItemSpacing.y));

		drawSearchBar();
		drawForceWeatherButton(iconSize);

		// Transparent icon button style
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));

		auto iconButton = [&](const char* suffix, void* texture, const char* tooltip, auto callback) {
			if (!texture)
				return;
			ImGui::SameLine();
			if (ImGui::ImageButton((std::string(searchId) + suffix).c_str(), texture, buttonSize))
				callback();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
		};

		// Apply button
		if (showApply && !editorWindow->settings.autoApplyChanges)
			iconButton("_Apply", menu->uiIcons.applyToGame.texture, "Apply changes to the game", [&]() { ApplyChanges(); });

		// Save/Load/Revert/Delete group
		if (showSaveLoadRevert) {
			iconButton("_Save", menu->uiIcons.saveSettings.texture, "Save to file", [&]() { Save(); });
			iconButton("_Load", menu->uiIcons.loadSettings.texture, "Load saved file (or reset to vanilla if no file)", [&]() { Load(); });
			iconButton("_Revert", menu->uiIcons.featureSettingRevert.texture, "Revert to original game values", [&]() { RevertChanges(); });

			if (HasSavedFile() && menu->uiIcons.deleteSettings.texture) {
				ImGui::SameLine();
				{
					auto _style = Util::ErrorButtonStyle();
					if (ImGui::ImageButton((std::string(searchId) + "_Delete").c_str(), menu->uiIcons.deleteSettings.texture, buttonSize))
						ImGui::OpenPopup("DeleteConfirmation");
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Delete saved file");
			}
		}

		drawUnsavedIndicator();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	} else {
		const float buttonHeight = ImGui::GetFrameHeight();
		if (!menu) {
			drawSearchBar();
			drawForceWeatherButton(buttonHeight);
			ImGui::Separator();
			return;
		}
		const auto& palette = menu->GetTheme().StatusPalette;

		drawSearchBar();
		drawForceWeatherButton(buttonHeight);

		auto styledTextButton = [&](const char* label, const ImVec4& color, const char* tooltip, auto callback) {
			ImGui::SameLine();
			ImVec2 size = ImGui::CalcTextSize(label);
			size.x += ImGui::GetStyle().FramePadding.x * 2.0f;
			size.y = buttonHeight;
			auto hover = color;
			hover.w = 0.8f;
			auto active = color;
			active.w = 1.0f;
			{
				auto styledButton = Util::StyledButtonWrapper(color, hover, active);
				if (ImGui::Button(label, size))
					callback();
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
		};

		auto textButton = [&](const char* label, const char* tooltip, auto callback) {
			ImGui::SameLine();
			ImVec2 size = ImGui::CalcTextSize(label);
			size.x += ImGui::GetStyle().FramePadding.x * 2.0f;
			size.y = buttonHeight;
			if (Util::ButtonWithFlash(label, size))
				callback();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
		};

		// Apply button
		if (showApply && !editorWindow->settings.autoApplyChanges)
			styledTextButton("Apply", palette.SuccessColor, "Apply changes to the game", [&]() { ApplyChanges(); });

		// Save/Load/Revert/Delete group
		if (showSaveLoadRevert) {
			textButton("Save", "Save to file", [&]() { Save(); });
			textButton("Load", "Load saved file (or reset to vanilla if no file)", [&]() { Load(); });
			styledTextButton("Revert", palette.Warning, "Revert to original game values", [&]() { RevertChanges(); });

			if (HasSavedFile())
				styledTextButton("Delete", palette.Error, "Delete saved file", [&]() { ImGui::OpenPopup("DeleteConfirmation"); });
		}

		drawUnsavedIndicator();
	}

	DrawDeleteConfirmationModal();

	ImGui::Separator();
}
