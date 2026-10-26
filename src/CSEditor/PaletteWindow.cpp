#include "PaletteWindow.h"
#include "../I18n/I18n.h"
#include "EditorWindow.h"
#include "Menu/Fonts.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"

#include <format>

#define I18N_KEY_PREFIX "cs_editor."

// Forward declaration from EditorWindow.cpp
void DrawIconStar(ImVec2 center, float radius, ImU32 color, bool filled);

void PaletteWindow::Draw()
{
	if (!open)
		return;

	const auto* editor = EditorWindow::GetSingleton();
	const float scale = Util::GetUIScale();
	const float pad = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	const auto& displaySize = ImGui::GetIO().DisplaySize;
	const float bottomY = displaySize.y - pad;
	const auto layoutCond = editor->resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;

	// Bottom-right anchor. AlwaysAutoResize sizes to this frame's scaled content; no fixed
	// pixel size, so res / UI scale changes stay proportional automatically.
	ImGui::SetNextWindowPos(ImVec2(displaySize.x - pad, bottomY), layoutCond, ImVec2(1.0f, 1.0f));

	constexpr ImGuiWindowFlags kFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse;

	// No title/close chrome; toggle via the action-bar Palette checkbox.
	if (ImGui::Begin("##CSEditorPalette", nullptr, kFlags)) {
		DrawContents();
	}
	ImGui::End();
}

void PaletteWindow::DrawContents()
{
	const float scale = Util::GetUIScale();
	const float buttonSize = 24.0f * scale;
	const float spacing = 4.0f * scale;
	// Favourites row is the widest fixed content; wrap / auto-size both key off this.
	const float favouritesRowWidth = buttonSize * static_cast<float>(maxFavoriteSlots) +
	                                 spacing * static_cast<float>(maxFavoriteSlots - 1);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * scale, 2.0f * scale));

	ImGui::TextUnformatted(T(TKEY("favourites"), "Favourites"));

	for (int i = 0; i < maxFavoriteSlots; i++) {
		if (i > 0)
			ImGui::SameLine(0.0f, spacing);

		std::string id = "##favorite_" + std::to_string(i);

		if (favoriteColors[i].has_value()) {
			auto& color = favoriteColors[i].value();
			ImVec4 colorVec(color.x, color.y, color.z, 1.0f);

			if (ImGui::ColorButton(id.c_str(), colorVec, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				copiedColor = color;
				hasColorInClipboard = true;
				ImGui::SetClipboardText(std::format("{:.3f}, {:.3f}, {:.3f}", color.x, color.y, color.z).c_str());
			}

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
				ImGui::SetDragDropPayload("COLOR_DND", &color, sizeof(float3));
				ImGui::ColorButton("##preview", colorVec, ImGuiColorEditFlags_NoAlpha);
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::Selectable(T(TKEY("clear_favourite"), "Clear favourite"))) {
					favoriteColors[i].reset();
					Save();
				}
				ImGui::EndPopup();
			}

			Util::AddTooltip(std::format("RGB: {:.3f}, {:.3f}, {:.3f}\n{}\n{}",
				color.x, color.y, color.z,
				T(TKEY("click_to_copy"), "Click to copy"),
				T(TKEY("right_click_to_clear"), "Right-click to clear"))
					.c_str());
		} else {
			ImVec4 emptyColor(0.2f, 0.2f, 0.2f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, emptyColor);
			ImGui::Button(id.c_str(), ImVec2(buttonSize, buttonSize));
			ImGui::PopStyleColor();

			ImVec2 buttonMin = ImGui::GetItemRectMin();
			ImVec2 buttonMax = ImGui::GetItemRectMax();
			ImVec2 center = ImVec2((buttonMin.x + buttonMax.x) * 0.5f, (buttonMin.y + buttonMax.y) * 0.5f);
			float starSize = buttonSize * 0.4f;
			ImU32 starColor = IM_COL32(160, 160, 160, 255);
			DrawIconStar(center, starSize, starColor, false);

			Util::AddTooltip(T(TKEY("drag_to_favourites"), "Drag a colour here to add to favourites"));
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COLOR_DND")) {
				float3 droppedColor;
				memcpy(&droppedColor, payload->Data, sizeof(float3));
				favoriteColors[i] = droppedColor;
				Save();
			}
			ImGui::EndDragDropTarget();
		}
	}

	{
		MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
		// Wrap to the favourites row width so AlwaysAutoResize doesn't depend on a prior frame size.
		ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + favouritesRowWidth);
		Util::Text::WrappedSecondary("%s", T(TKEY("drag_colours_here"), "Drag colours here to save as favourites."));
		ImGui::PopTextWrapPos();
	}

	ImGui::Spacing();
	ImGui::TextUnformatted(T(TKEY("recently_used"), "Recently Used"));
	auto recentColors = GetRecentColors(5);

	if (recentColors.empty()) {
		MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
		Util::Text::Secondary("%s", T(TKEY("no_recent_colors"), "No recent colors"));
	} else {
		for (size_t i = 0; i < recentColors.size(); i++) {
			if (i > 0)
				ImGui::SameLine(0.0f, spacing);

			auto* entry = recentColors[i];
			ImVec4 color(entry->color.x, entry->color.y, entry->color.z, 1.0f);
			std::string id = "##recent_color_" + std::to_string(i);

			if (ImGui::ColorButton(id.c_str(), color, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				copiedColor = entry->color;
				hasColorInClipboard = true;
				ImGui::SetClipboardText(std::format("{:.3f}, {:.3f}, {:.3f}", entry->color.x, entry->color.y, entry->color.z).c_str());
			}

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
				ImGui::SetDragDropPayload("COLOR_DND", &entry->color, sizeof(float3));
				ImGui::ColorButton("##preview", color, ImGuiColorEditFlags_NoAlpha);
				ImGui::EndDragDropSource();
			}

			Util::AddTooltip(std::format("RGB: {:.3f}, {:.3f}, {:.3f}\n{}\n{}",
				entry->color.x, entry->color.y, entry->color.z,
				std::vformat(T(TKEY("used_times"), "Used {} times"), std::make_format_args(entry->useCount)),
				T(TKEY("click_to_copy"), "Click to copy"))
					.c_str());
		}
	}

	ImGui::PopStyleVar(2);
}

std::vector<PaletteWindow::ColorEntry*> PaletteWindow::GetRecentColors(int count)
{
	std::vector<ColorEntry*> result;
	std::vector<ColorEntry*> allColors;
	allColors.reserve(colorEntries.size());

	for (auto& entry : colorEntries)
		allColors.push_back(&entry);

	std::sort(allColors.begin(), allColors.end(), [](ColorEntry* a, ColorEntry* b) {
		return a->lastUsedTime > b->lastUsedTime;
	});

	for (int i = 0; i < std::min(count, (int)allColors.size()); i++)
		result.push_back(allColors[i]);

	return result;
}

void PaletteWindow::TrackColorUsage(const float3& color)
{
	float currentTime = static_cast<float>(ImGui::GetTime());

	const float epsilon = 0.001f;
	for (auto& entry : colorEntries) {
		if (std::abs(entry.color.x - color.x) < epsilon &&
			std::abs(entry.color.y - color.y) < epsilon &&
			std::abs(entry.color.z - color.z) < epsilon) {
			entry.useCount++;
			entry.lastUsedTime = currentTime;
			Save();
			return;
		}
	}

	ColorEntry newEntry;
	newEntry.color = color;
	newEntry.useCount = 1;
	newEntry.lastUsedTime = currentTime;
	colorEntries.push_back(newEntry);
	Save();
}

void PaletteWindow::Save()
{
	auto editorWindow = EditorWindow::GetSingleton();

	editorWindow->settings.paletteFavorites = {};
	for (size_t i = 0; i < favoriteColors.size(); i++) {
		if (favoriteColors[i].has_value()) {
			editorWindow->settings.paletteFavorites[i].hasValue = true;
			editorWindow->settings.paletteFavorites[i].r = favoriteColors[i].value().x;
			editorWindow->settings.paletteFavorites[i].g = favoriteColors[i].value().y;
			editorWindow->settings.paletteFavorites[i].b = favoriteColors[i].value().z;
		} else {
			editorWindow->settings.paletteFavorites[i].hasValue = false;
		}
	}

	editorWindow->settings.paletteColors.clear();
	for (const auto& entry : colorEntries) {
		EditorWindow::Settings::PaletteColorEntry e;
		e.r = entry.color.x;
		e.g = entry.color.y;
		e.b = entry.color.z;
		e.useCount = entry.useCount;
		e.lastUsedTime = entry.lastUsedTime;
		e.isFavorite = false;
		editorWindow->settings.paletteColors.push_back(e);
	}

	// Values tab was removed; drop any previously saved value entries.
	editorWindow->settings.paletteValues.clear();

	editorWindow->Save();
}

void PaletteWindow::Load()
{
	auto editorWindow = EditorWindow::GetSingleton();

	for (size_t i = 0; i < editorWindow->settings.paletteFavorites.size(); i++) {
		const auto& fav = editorWindow->settings.paletteFavorites[i];
		if (fav.hasValue) {
			favoriteColors[i] = float3{ fav.r, fav.g, fav.b };
		} else {
			favoriteColors[i].reset();
		}
	}

	colorEntries.clear();
	for (const auto& e : editorWindow->settings.paletteColors) {
		ColorEntry entry;
		entry.color = { e.r, e.g, e.b };
		entry.useCount = e.useCount;
		entry.lastUsedTime = e.lastUsedTime;
		colorEntries.push_back(entry);
	}
}

#undef I18N_KEY_PREFIX
