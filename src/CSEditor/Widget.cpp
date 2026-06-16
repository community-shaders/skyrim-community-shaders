#include "Widget.h"

#include <algorithm>
#include <format>

#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"
#include "WeatherUtils.h"
#include "imgui_internal.h"

#define I18N_KEY_PREFIX "cs_editor."

void Widget::Save()
{
	SaveSettings();
	const auto file = GetSaveFilePath();
	const auto filePath = std::filesystem::path(file).parent_path();

	if (!std::filesystem::exists(filePath) || !std::filesystem::is_directory(filePath)) {
		try {
			std::filesystem::create_directories(filePath);
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error creating directory during Save ({}) : {}\n", filePath.string(), e.what());
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

void Widget::Load(bool showNotification)
{
	std::string filePath = GetSaveFilePath();

	if (!std::filesystem::exists(filePath)) {
		js = json();
		LoadSettings();

		if (showNotification) {
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("No saved file - reset {} to vanilla values", GetEditorID()),
				Util::Colors::GetInfo(),
				3.0f);
		}
		return;
	}

	// File exists, load from it
	std::ifstream settingsFile(filePath);

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to open settings file: {}", filePath);
		if (showNotification) {
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Failed to open file for {}", GetEditorID()),
				Util::Colors::GetWarning(),
				3.0f);
		}
		return;
	}

	try {
		settingsFile >> js;
		settingsFile.close();

		// Validate that we loaded valid JSON
		if (js.is_null()) {
			logger::warn("{}: Loaded JSON is null, file may be empty or invalid", filePath);
			if (showNotification) {
				EditorWindow::GetSingleton()->ShowNotification(
					std::format("Invalid file for {} - resetting to vanilla", GetEditorID()),
					Util::Colors::GetWarning(),
					3.0f);
			}
			js = json();
			LoadSettings();
			return;
		}

		LoadSettings();

		if (showNotification) {
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Loaded saved settings for {}", GetEditorID()),
				Util::Colors::GetSuccess(),
				3.0f);
		}

	} catch (const nlohmann::json::parse_error& e) {
		logger::error("Error parsing settings for file ({}) : {}\n", filePath, e.what());
		logger::error("Parse error at byte {}: {}", e.byte, e.what());
		settingsFile.close();
		if (showNotification) {
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Parse error for {} - resetting to vanilla", GetEditorID()),
				Util::Colors::GetError(),
				3.0f);
		}
		js = json();
		LoadSettings();
		return;
	} catch (const std::exception& e) {
		logger::error("Unexpected error loading settings file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
		if (showNotification) {
			EditorWindow::GetSingleton()->ShowNotification(
				std::format("Error loading {} - resetting to vanilla", GetEditorID()),
				Util::Colors::GetError(),
				3.0f);
		}
		js = json();
		LoadSettings();
		return;
	}
}

void Widget::Delete()
{
	std::string filePath = GetSaveFilePath();

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
			Util::Colors::GetSuccess(),
			3.0f);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error deleting settings file ({}) : {}\n", filePath, e.what());
	}
}

bool Widget::HasSavedFile() const
{
	return std::filesystem::exists(GetSaveFilePath());
}

void Widget::DrawMenu()
{
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu(T(TKEY("menu"), "Menu"))) {
			if (ImGui::MenuItem(T(TKEY("save"), "Save"))) {
				Save();
			}
			if (ImGui::MenuItem(T(TKEY("load"), "Load"))) {
				Load();
			}
			if (ImGui::MenuItem(T(TKEY("delete_saved_file"), "Delete Saved File"))) {
				ImGui::OpenPopup("DeleteConfirmation");
			}
			if (ImGui::MenuItem(T(TKEY("revert_to_game_values"), "Revert to Game Values"))) {
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

	if (auto popup = Util::CenteredPopupModal(popupId)) {
		deleteConfirmationFrame = ImGui::GetFrameCount();
		ImGui::Text("%s", T(TKEY("confirm_delete_saved_file"), "Are you sure you want to delete the saved settings file?"));
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const float scale = Util::GetUIScale();
		const float buttonWidth = 120.0f * scale;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float totalWidth = (buttonWidth * 2) + spacing;
		const float cursorX = (ImGui::GetWindowWidth() - totalWidth) / 2.0f;

		ImGui::SetCursorPosX(cursorX);

		if (ImGui::Button(T(TKEY("yes_delete"), "Yes, Delete"), ImVec2(buttonWidth, 0))) {
			Delete();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();

		if (ImGui::Button(T(TKEY("cancel"), "Cancel"), ImVec2(buttonWidth, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
	}
}

std::string Widget::GetSaveFilePath() const
{
	return std::format("{}\\{}\\{}.json", Util::PathHelpers::GetCommunityShaderPath().string(), GetFolderName(), GetSaveKey());
}

std::string Widget::GetFolderName() const
{
	switch (form->GetFormType()) {
	case RE::FormType::Weather:
		return std::string(kWeatherFolderName);
	case RE::FormType::LightingMaster:
		return std::string(kLightingTemplateFolderName);
	case RE::FormType::ImageSpace:
		return std::string(kImageSpaceFolderName);
	case RE::FormType::VolumetricLighting:
		return std::string(kVolumetricLightingFolderName);
	case RE::FormType::ShaderParticleGeometryData:
		return std::string(kPrecipitationFolderName);
	case RE::FormType::ReferenceEffect:
		return std::string(kVisualEffectsFolderName);
	case RE::FormType::Cell:
		return std::string(kCellLightingFolderName);
	default:
		return std::string(kOtherEditorWidgetsFolderName);
	}
}

bool Widget::BeginWidgetWindow()
{
	SetupWidgetWindowDefaults(GetWidgetTypeName());
	if (m_pendingFocus) {
		ImGui::SetNextWindowFocus();
		m_pendingFocus = false;
	}
	bool result = Util::BeginWithRoundedClose(GetWindowTitle().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags);
	UpdateWidgetTypeSize(GetWidgetTypeName());
	return result;
}

void Widget::ForceWeatherReinit(RE::TESWeather* weather)
{
	auto* sky = globals::game::sky;
	if (weather && sky && sky->currentWeather == weather) {
		sky->ForceWeather(weather, true);
		sky->ReleaseWeatherOverride();
	}
}

void Widget::ForceCurrentWeatherReinit()
{
	auto* sky = globals::game::sky;
	if (sky && sky->currentWeather) {
		sky->ForceWeather(sky->currentWeather, true);
		sky->ReleaseWeatherOverride();
	}
}

void Widget::DrawWidgetHeader(const char* searchId, bool showApply, bool showSaveLoadRevert, bool showForceWeather, RE::TESWeather* weather)
{
	auto editorWindow = EditorWindow::GetSingleton();
	auto menu = globals::menu;
	bool useIcons = !editorWindow->settings.useTextButtons && menu && menu->GetSettings().Theme.ShowActionIcons;
	const float scale = Util::GetUIScale();
	if (navigatedFromSearch) {
		ClearSearchState(true);
		navigatedFromSearch = false;
	}

	auto drawSearchBar = [&]() {
		ImGui::SetNextItemWidth(WidgetUI::kSearchBarWidth * scale);
		bool ctrlF = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		             ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false);
		if (ctrlF) {
			ClearSearchState(true);
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::InputTextWithHint(searchId, T(TKEY("search_settings_hint"), "Search settings (Ctrl+F)"), searchBuffer, sizeof(searchBuffer));
		searchInputMin = ImGui::GetItemRectMin();
		searchInputMax = ImGui::GetItemRectMax();
		if (ImGui::IsItemEdited())
			dropdownVisible = true;
	};

	auto drawForceWeatherButton = [&]() {
		if (!showForceWeather || !weather)
			return;
		ImGui::SameLine();
		bool isLocked = editorWindow->IsWeatherLocked() && editorWindow->GetLockedWeather() == weather;
		const char* lockLabel = isLocked ? T(TKEY("unlock"), "Unlock") : T(TKEY("force_weather"), "Force Weather");

		if (isLocked) {
			ImGui::PushStyleColor(ImGuiCol_Button, WidgetUI::kLockButtonColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WidgetUI::kLockButtonHoverColor);
		}
		if (ImGui::Button(lockLabel)) {
			if (isLocked)
				editorWindow->UnlockWeather();
			else
				editorWindow->LockWeather(weather);
		}
		if (isLocked)
			ImGui::PopStyleColor(2);
		Util::AddTooltip(isLocked ? T(TKEY("unlock_weather"), "Unlock Weather") : T(TKEY("force_this_weather"), "Force This Weather"));
	};

	auto drawUnsavedIndicator = [&]() {
		if (!HasUnsavedChanges() || !menu)
			return;
		ImGui::SameLine();
		ImGui::TextColored(menu->GetTheme().StatusPalette.Warning, "%s", T(TKEY("unsaved_changes"), "(UNSAVED CHANGES)"));
		Util::AddTooltip(T(TKEY("unsaved_changes_tooltip"), "Unsaved changes - click save to keep"));
	};

	if (useIcons) {
		const float iconSize = ImGui::GetFrameHeight() * WidgetUI::kIconButtonSizeRatio;
		const ImVec2 buttonSize(iconSize, iconSize);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(WidgetUI::kIconButtonSpacing * scale, ImGui::GetStyle().ItemSpacing.y));

		drawSearchBar();
		drawForceWeatherButton();

		// Transparent icon button style
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, WidgetUI::kIconButtonTransparent);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WidgetUI::kIconButtonHover);

		auto iconButton = [&](const char* suffix, void* texture, const char* tooltip, auto callback) {
			if (!texture)
				return;
			ImGui::SameLine();
			if (ImGui::ImageButton((std::string(searchId) + suffix).c_str(), texture, buttonSize))
				callback();
			Util::AddTooltip(tooltip);
		};

		// Apply button
		if (showApply && (!editorWindow->settings.autoApplyChanges || RequiresManualApply())) {
			if (menu->uiIcons.applyToGame.texture) {
				iconButton("_Apply", menu->uiIcons.applyToGame.texture, T(TKEY("apply_changes"), "Apply changes to the game"), [&]() { ApplyChanges(); });
			} else {
				ImGui::SameLine();
				if (ImGui::Button(T(TKEY("apply"), "Apply")))
					ApplyChanges();
				Util::AddTooltip(T(TKEY("apply_changes"), "Apply changes to the game"));
			}
		}

		// Save/Load/Revert/Delete group
		if (showSaveLoadRevert) {
			iconButton("_Save", menu->uiIcons.saveSettings.texture, T(TKEY("save_to_file"), "Save to file"), [&]() { Save(); });
			iconButton("_Load", menu->uiIcons.loadSettings.texture, T(TKEY("load_saved_file"), "Load saved file (or reset to vanilla if no file)"), [&]() { Load(); });
			iconButton("_Revert", menu->uiIcons.featureSettingRevert.texture, T(TKEY("revert_to_original"), "Revert to original game values"), [&]() { RevertChanges(); });

			if (HasSavedFile() && menu->uiIcons.deleteSettings.texture) {
				ImGui::SameLine();
				if (Util::ErrorImageButton((std::string(searchId) + "_Delete").c_str(), menu->uiIcons.deleteSettings.texture, buttonSize))
					ImGui::OpenPopup("DeleteConfirmation");
				Util::AddTooltip(T(TKEY("delete_saved_file_tooltip"), "Delete saved file"));
			}
		}

		drawUnsavedIndicator();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	} else {
		if (!menu) {
			drawSearchBar();
			drawForceWeatherButton();
		} else {
			drawSearchBar();
			drawForceWeatherButton();

			auto textButton = [&](const char* label, const char* tooltip, auto callback) {
				ImGui::SameLine();
				if (Util::ButtonWithFlash(label))
					callback();
				Util::AddTooltip(tooltip);
			};

			// Apply button
			if (showApply && (!editorWindow->settings.autoApplyChanges || RequiresManualApply())) {
				ImGui::SameLine();
				if (Util::SuccessButton(T(TKEY("apply"), "Apply")))
					ApplyChanges();
				Util::AddTooltip(T(TKEY("apply_changes"), "Apply changes to the game"));
			}

			// Save/Load/Revert/Delete group
			if (showSaveLoadRevert) {
				textButton(T(TKEY("save"), "Save"), T(TKEY("save_to_file"), "Save to file"), [&]() { Save(); });
				textButton(T(TKEY("load"), "Load"), T(TKEY("load_saved_file"), "Load saved file (or reset to vanilla if no file)"), [&]() { Load(); });
				ImGui::SameLine();
				if (Util::WarningButton(T(TKEY("revert"), "Revert")))
					RevertChanges();
				Util::AddTooltip(T(TKEY("revert_to_original"), "Revert to original game values"));

				if (HasSavedFile()) {
					ImGui::SameLine();
					if (Util::ErrorTextButton(T(TKEY("delete"), "Delete")))
						ImGui::OpenPopup("DeleteConfirmation");
					Util::AddTooltip(T(TKEY("delete_saved_file_tooltip"), "Delete saved file"));
				}
			}

			drawUnsavedIndicator();
		}
	}

	DrawDeleteConfirmationModal();

	if (showApply && RequiresManualApply() && editorWindow->settings.autoApplyChanges && menu) {
		ImGui::SameLine();
		ImGui::TextColored(menu->GetTheme().StatusPalette.Warning, "%s", T(TKEY("changes_require_manual_apply"), "(Changes require manual apply)"));
		Util::AddTooltip(T(TKEY("manual_apply_required_tooltip"), "This form type is only re-read by the engine on weather reinit.\nAuto-apply is disabled - use the Apply button."));
	}

	ImGui::Separator();

	// Remember where the dropdown should appear so DrawSearchDropdown()
	// (called after this function) can anchor itself below the search bar.
	searchDropdownAnchor = ImGui::GetCursorScreenPos();

	// Rebuild match list only when the query changed; this gates per-frame
	// CollectSearchableSettings() walks plus three case-insensitive searches per entry.
	if (searchBuffer[0] != '\0') {
		if (searchResultsForQuery != searchBuffer) {
			searchResults = CollectSearchableSettings();
			std::erase_if(searchResults, [&](const SearchResult& r) {
				return !ContainsStringIgnoreCase(r.displayName, searchBuffer) &&
				       !ContainsStringIgnoreCase(r.tabName, searchBuffer) &&
				       !ContainsStringIgnoreCase(r.settingId, searchBuffer);
			});
			searchResultsForQuery = searchBuffer;
		}
	} else {
		ClearSearchState(false);
	}
}

void Widget::DrawSearchDropdown()
{
	if (!dropdownVisible || searchResults.empty())
		return;

	const float scale = Util::GetUIScale();
	ImGui::SetNextWindowPos(searchDropdownAnchor, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(WidgetUI::kSearchDropdownWidth * scale, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
	const ImVec4 dropdownBg(WidgetUI::kSearchDropdownBgGray, WidgetUI::kSearchDropdownBgGray, WidgetUI::kSearchDropdownBgGray, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, dropdownBg);
	const std::string dropdownWindowId = std::format("##SearchDropdown_{}", static_cast<const void*>(this));
	if (ImGui::Begin(dropdownWindowId.c_str(), nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoFocusOnAppearing)) {
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		// Treat clicks on the search input itself as "inside" so typing/cursor
		// positioning in the input doesn't dismiss the dropdown.
		const ImRect searchInputRect(searchInputMin, searchInputMax);
		const bool clickedOutside = ImGui::GetIO().MouseClicked[0] &&
		                            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
		                            !searchInputRect.Contains(ImGui::GetIO().MousePos);
		if (clickedOutside || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			dropdownVisible = false;
		} else {
			const size_t shown = std::min(WidgetUI::kSearchDropdownMaxResults, searchResults.size());
			for (size_t i = 0; i < shown; ++i) {
				const auto& result = searchResults[i];
				std::string label = result.tabName.empty() ? result.displayName : std::format("{} ({})", result.displayName, result.tabName);

				ImGui::PushID(static_cast<int>(i));
				if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups)) {
					NavigateToSearchResult(result);
					navigatedFromSearch = true;
				}
				ImGui::PopID();
			}

			if (searchResults.size() > WidgetUI::kSearchDropdownMaxResults) {
				ImGui::Separator();
				auto count = searchResults.size() - WidgetUI::kSearchDropdownMaxResults;
				auto formatted = std::vformat(T(TKEY("more_results"), "... {} more results"), std::make_format_args(count));
				ImGui::TextDisabled("%s", formatted.c_str());
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void Widget::ClearSearchState(bool clearBuffer)
{
	if (clearBuffer)
		searchBuffer[0] = '\0';
	searchResults.clear();
	searchResultsForQuery.clear();
	dropdownVisible = false;
}

void Widget::NavigateToSearchResult(const SearchResult& result)
{
	activeTabOverride = result.tabName;
	highlightedSetting = result.settingId;
	highlightedDisplaySetting = result.displayName;
	highlightStartTime = static_cast<float>(ImGui::GetTime());
	scrollToHighlighted = true;
}

int Widget::GetTabFlagsForOverride(const std::string& tabName)
{
	if (activeTabOverride.empty() || activeTabOverride != tabName)
		return 0;
	activeTabOverride.clear();
	return ImGuiTabItemFlags_SetSelected;
}

bool Widget::MatchesSearch(const std::string& settingId) const
{
	if (searchBuffer[0] == '\0')
		return true;
	return std::any_of(searchResults.begin(), searchResults.end(),
		[&](const SearchResult& r) { return r.settingId == settingId; });
}

bool Widget::MatchesAnySearch(std::initializer_list<const char*> settingIds) const
{
	return std::any_of(settingIds.begin(), settingIds.end(), [&](const char* settingId) {
		return MatchesSearch(settingId);
	});
}

bool Widget::IsHighlighted(const std::string& settingId) const
{
	if (highlightedSetting != settingId && highlightedDisplaySetting != settingId)
		return false;
	const float elapsed = static_cast<float>(ImGui::GetTime()) - highlightStartTime;
	return elapsed < WidgetUI::kHighlightDurationSeconds;
}

void Widget::PushHighlightStyle(const std::string& settingId)
{
	if (!IsHighlighted(settingId))
		return;
	const float elapsed = static_cast<float>(ImGui::GetTime()) - highlightStartTime;
	const float normalized = std::clamp(elapsed / WidgetUI::kHighlightDurationSeconds, 0.0f, 1.0f);
	const float triangularFade = 1.0f - std::abs(normalized * 2.0f - 1.0f);
	const float alpha = std::clamp(WidgetUI::kHighlightMaxAlpha * triangularFade, 0.0f, WidgetUI::kHighlightMaxAlpha);
	ImVec4 frameBg = WidgetUI::kHighlightFrameBg;
	ImVec4 frameBgHovered = WidgetUI::kHighlightFrameBgHovered;
	frameBg.w = alpha;
	frameBgHovered.w = alpha;
	ImGui::PushStyleColor(ImGuiCol_FrameBg, frameBg);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frameBgHovered);
}

void Widget::PopHighlightStyle(const std::string& settingId)
{
	if (!IsHighlighted(settingId))
		return;
	ImGui::PopStyleColor(2);
	if (scrollToHighlighted) {
		ImGui::SetScrollHereY(0.5f);
		scrollToHighlighted = false;
	}
}

#undef I18N_KEY_PREFIX
