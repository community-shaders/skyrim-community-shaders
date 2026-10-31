#include "WeatherPickerWindow.h"

#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "Features/CSEditor.h"
#include "Globals.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/MenuIcons.h"
#include "Menu/ThemeManager.h"
#include "PaletteWindow.h"
#include "Utils/UI.h"
#include "imgui_internal.h"

#include <format>

#define I18N_KEY_PREFIX "cs_editor."

void WeatherPickerWindow::Draw()
{
	if (!open)
		return;

	const auto* editor = EditorWindow::GetSingleton();
	const float scale = Util::GetUIScale();
	const float pad = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	const auto& displaySize = ImGui::GetIO().DisplaySize;

	// Default spot: right-aligned, directly above the palette window. The palette only reports a
	// real position once it has been drawn, so keep re-anchoring until then (or until it is
	// known to be closed) and leave the window free to be dragged afterwards.
	float anchorX = displaySize.x - pad;
	float anchorY = displaySize.y - pad;
	bool anchorResolved = !PaletteWindow::GetSingleton()->open;
	if (ImGuiWindow* palette = ImGui::FindWindowByName("##CSEditorPalette"); palette && palette->WasActive && palette->Size.y > 0.0f) {
		anchorX = palette->Pos.x + palette->Size.x;
		anchorY = palette->Pos.y - pad;
		anchorResolved = true;
	}

	static bool positioned = false;
	if (editor->resetLayout || !positioned)
		ImGui::SetNextWindowPos(ImVec2(anchorX, anchorY), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
	if (anchorResolved)
		positioned = true;

	if (ImGui::Begin("##WeatherPickerWindow", nullptr, CSEditorWindows::kFloatingPanelFlags))
		DrawContents();
	ImGui::End();
}

void WeatherPickerWindow::DrawContents()
{
	const float scale = Util::GetUIScale();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ThemeManager::Constants::CSE_PICKER_ITEM_SPACING * scale, ThemeManager::Constants::CSE_PICKER_ITEM_SPACING * scale));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ThemeManager::Constants::CSE_PICKER_ITEM_SPACING * scale, ThemeManager::Constants::CSE_PICKER_FRAME_PADDING_Y * scale));

	auto* sky = globals::game::sky;
	if (!sky) {
		ImGui::TextDisabled("%s", T(TKEY("sky_not_available"), "Sky not available"));
		ImGui::PopStyleVar(2);
		return;
	}

	auto weatherShortName = [](RE::TESWeather* weather) -> std::string {
		if (!weather)
			return {};
		if (auto* editorId = weather->GetFormEditorID(); editorId && editorId[0] != '\0')
			return editorId;
		return std::format("{:08X}", weather->GetFormID());
	};

	std::string comboPreviewStorage;
	const char* comboPreview = T(TKEY("select_weather"), "Select Weather");
	if (sky->currentWeather) {
		comboPreviewStorage = weatherShortName(sky->currentWeather);
		comboPreview = comboPreviewStorage.c_str();
	}

	static constexpr const char* kWeatherSearchId = "WeatherPickerWindow";
	CSEditor::SyncWeatherFilter();
	auto& filteredWeathers = CSEditor::GetFilteredWeathers();
	auto& selectedWeatherIdx = CSEditor::GetSelectedWeatherIdx();

	const float comboWidth = ThemeManager::Constants::CSE_PICKER_COMBO_WIDTH * scale;
	const float listWidth = comboWidth;
	const float listHeight = ImGui::GetTextLineHeightWithSpacing() * ThemeManager::Constants::CSE_PICKER_LIST_ROWS;
	// Popup is the scroll list with the filter chip grid joined to its right.
	const float chipsWidth = 2.0f * (ImGui::CalcTextSize("Aurora Sun").x + ImGui::GetStyle().FramePadding.x * 2.0f + ThemeManager::Constants::CSE_PICKER_CHIP_PAD * scale) + ThemeManager::Constants::CSE_PICKER_CHIP_GAP * scale;
	const float popupWidth = listWidth + chipsWidth + ImGui::GetStyle().WindowPadding.x * 2.0f + ThemeManager::Constants::CSE_PICKER_GROUP_GAP * scale;
	ImGui::SetNextWindowSizeConstraints(ImVec2(popupWidth, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

	ImGui::SetNextItemWidth(comboWidth);
	if (ImGui::BeginCombo("##WeatherPickerWindowCombo", comboPreview)) {
		ImGui::BeginGroup();
		ImGui::SetNextItemWidth(listWidth);
		auto searchText = Util::DrawComboSearchInput(kWeatherSearchId);
		if (ImGui::BeginChild("##WeatherPickerList", ImVec2(listWidth, listHeight), ImGuiChildFlags_None)) {
			for (int i = 0; i < static_cast<int>(filteredWeathers.size()); ++i) {
				auto* weather = filteredWeathers[i];
				if (!searchText.empty()) {
					auto editorId = weather->GetFormEditorID() ? std::string(weather->GetFormEditorID()) : "";
					auto name = weather->GetName() ? std::string(weather->GetName()) : "";
					auto formId = std::format("{:08X}", weather->GetFormID());
					if (!Util::StringMatchesSearch(editorId, searchText) &&
						!Util::StringMatchesSearch(name, searchText) &&
						!Util::StringMatchesSearch(formId, searchText))
						continue;
				}

				const bool isSelected = (selectedWeatherIdx == i);
				ImGui::PushID(static_cast<int>(weather->GetFormID()));  // Editor IDs can repeat across plugins
				ImGui::PushStyleColor(ImGuiCol_Text, CSEditor::GetWeatherTypeColor(weather));
				const bool didSelect = Util::Selectable(weatherShortName(weather).c_str(), isSelected);
				ImGui::PopStyleColor();
				ImGui::PopID();

				if (didSelect) {
					CSEditor::SelectWeather(sky, i);
					Util::ClearComboSearch(kWeatherSearchId);
					break;
				}

				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::Text(T(TKEY("tooltip_weather_name"), "Weather: %s"), weather->GetName() ? weather->GetName() : "Unnamed");
					ImGui::Text(T(TKEY("tooltip_editor_id"), "Editor ID: %s"), weather->GetFormEditorID() ? weather->GetFormEditorID() : "None");
					ImGui::Text(T(TKEY("tooltip_form_id"), "Form ID: 0x%08X"), weather->GetFormID());
					ImGui::EndTooltip();
				}
				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndChild();
		ImGui::EndGroup();

		ImGui::SameLine(0.0f, ThemeManager::Constants::CSE_PICKER_GROUP_GAP * scale);
		ImGui::BeginGroup();
		CSEditor::DrawWeatherFilterChips();
		ImGui::EndGroup();

		ImGui::EndCombo();
	} else {
		Util::ClearComboSearch(kWeatherSearchId);
	}

	ImGui::SameLine(0.0f, ThemeManager::Constants::CSE_PICKER_ITEM_SPACING * scale);
	const float iconSize = ImGui::GetFrameHeight();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
	{
		auto _style = Util::TransparentIconButtonStyle();
		if (Icons::Button("##WeatherPickerWindowReset", Icons::TI(MenuIcons::kRestore), ImVec2(iconSize, iconSize)))
			CSEditor::ResetWeatherToDefault(sky);
	}
	ImGui::PopStyleVar();
	Util::AddTooltip(T(TKEY("reset_weather_tooltip"), "Resets weather to default"));

	CSEditor::DrawWeatherTransitionBar(sky, comboWidth + ThemeManager::Constants::CSE_PICKER_ITEM_SPACING * scale + iconSize, false);

	ImGui::PopStyleVar(2);
}

void WeatherPickerWindow::Save() {}
void WeatherPickerWindow::Load() {}

#undef I18N_KEY_PREFIX
