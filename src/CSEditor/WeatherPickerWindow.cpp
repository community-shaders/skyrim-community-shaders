#include "WeatherPickerWindow.h"

#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "Features/CSEditor.h"
#include "Globals.h"
#include "IconsTabler.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/ThemeManager.h"
#include "PaletteWindow.h"
#include "Utils/Game.h"
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

	constexpr ImGuiWindowFlags kFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse;

	if (ImGui::Begin("##WeatherPickerWindow", nullptr, kFlags))
		DrawContents();
	ImGui::End();
}

void WeatherPickerWindow::DrawContents()
{
	const float scale = Util::GetUIScale();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * scale, 4.0f * scale));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * scale, 2.0f * scale));

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

	const float comboWidth = 180.0f * scale;
	const float listWidth = comboWidth;
	const float listHeight = ImGui::GetTextLineHeightWithSpacing() * 12.0f;
	// Popup is the scroll list with the filter chip grid joined to its right.
	const float chipsWidth = 2.0f * (ImGui::CalcTextSize("Aurora Sun").x + ImGui::GetStyle().FramePadding.x * 2.0f + 6.0f * scale) + 3.0f * scale;
	const float popupWidth = listWidth + chipsWidth + ImGui::GetStyle().WindowPadding.x * 2.0f + 2.0f * scale;
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
				const bool didSelect = ImGui::Selectable(weatherShortName(weather).c_str(), isSelected);
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

		ImGui::SameLine(0.0f, 2.0f * scale);
		ImGui::BeginGroup();
		CSEditor::DrawWeatherFilterChips();
		ImGui::EndGroup();

		ImGui::EndCombo();
	} else {
		Util::ClearComboSearch(kWeatherSearchId);
	}

	ImGui::SameLine(0.0f, 4.0f * scale);
	const float iconSize = ImGui::GetFrameHeight();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
	{
		auto _style = Util::TransparentIconButtonStyle();
		if (Icons::Button("##WeatherPickerWindowReset", Icons::TI(ICON_TI_RESTORE), ImVec2(iconSize, iconSize)))
			CSEditor::ResetWeatherToDefault(sky);
	}
	ImGui::PopStyleVar();
	Util::AddTooltip(T(TKEY("reset_weather_tooltip"), "Resets weather to default"));

	CSEditor::DrawWeatherTransitionBar(sky, comboWidth + 4.0f * scale + iconSize, false);

	ImGui::PopStyleVar(2);
}

void WeatherPickerWindow::Save() {}
void WeatherPickerWindow::Load() {}

#undef I18N_KEY_PREFIX
