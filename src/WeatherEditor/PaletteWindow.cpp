#include "PaletteWindow.h"
#include "EditorWindow.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"

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
	const float paletteWidth = std::min(600.0f * scale, displaySize.x - pad * 2.0f);
	const float bottomY = displaySize.y - pad;
	const float spaceBelow = bottomY - editor->viewportBottomY - pad;  // room between viewport bottom and screen bottom
	const float paletteHeight = std::min(400.0f * scale, spaceBelow);
	const auto layoutCond = editor->resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	ImGui::SetNextWindowSize(ImVec2(paletteWidth, paletteHeight), layoutCond);
	ImGui::SetNextWindowPos(
		ImVec2(displaySize.x - paletteWidth - pad, bottomY - paletteHeight),
		layoutCond);
	if (Util::BeginWithRoundedClose("Palette", &open, ImGuiWindowFlags_NoFocusOnAppearing)) {
		if (ImGui::BeginTabBar("PaletteTabs")) {
			if (ImGui::BeginTabItem("Colours")) {
				DrawColorsTab();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Values")) {
				DrawValuesTab();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void PaletteWindow::DrawColorsTab()
{
	const float scale = Util::GetUIScale();
	const float buttonSize = 32.0f * scale;
	const float spacing = 8.0f * scale;

	// Favorites section at top
	ImGui::SeparatorText("Favourites");
	ImGui::TextWrapped("Drag colours here to save as favourites.");
	ImGui::Spacing();

	for (int i = 0; i < maxFavoriteSlots; i++) {
		if (i > 0)
			ImGui::SameLine(0.0f, spacing);

		std::string id = "##favorite_" + std::to_string(i);

		if (favoriteColors[i].has_value()) {
			// Show filled favorite slot
			auto& color = favoriteColors[i].value();
			ImVec4 colorVec(color.x, color.y, color.z, 1.0f);

			if (ImGui::ColorButton(id.c_str(), colorVec, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				copiedColor = color;
				hasColorInClipboard = true;
				ImGui::SetClipboardText(std::format("{:.3f}, {:.3f}, {:.3f}", color.x, color.y, color.z).c_str());
			}

			// Drag source
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
				ImGui::SetDragDropPayload("COLOR_DND", &color, sizeof(float3));
				ImGui::ColorButton("##preview", colorVec, ImGuiColorEditFlags_NoAlpha);
				ImGui::EndDragDropSource();
			}

			// Right-click to clear
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::Selectable("Clear favourite")) {
					favoriteColors[i].reset();
					Save();
				}
				ImGui::EndPopup();
			}

			Util::AddTooltip(std::format("RGB: {:.3f}, {:.3f}, {:.3f}\nClick to copy\nRight-click to clear", color.x, color.y, color.z).c_str());
		} else {
			// Show empty favorite slot with star
			ImVec4 emptyColor(0.2f, 0.2f, 0.2f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, emptyColor);
			ImGui::Button(id.c_str(), ImVec2(buttonSize, buttonSize));
			ImGui::PopStyleColor();

			// Draw star icon in center
			ImVec2 buttonMin = ImGui::GetItemRectMin();
			ImVec2 buttonMax = ImGui::GetItemRectMax();
			ImVec2 center = ImVec2((buttonMin.x + buttonMax.x) * 0.5f, (buttonMin.y + buttonMax.y) * 0.5f);
			float starSize = buttonSize * 0.4f;
			ImU32 starColor = IM_COL32(160, 160, 160, 255);

			DrawIconStar(center, starSize, starColor, false);

			Util::AddTooltip("Drag a colour here to add to favourites");
		}

		// Drag-and-drop target
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
	ImGui::Spacing();
	ImGui::Spacing();

	// Recently Used section
	ImGui::SeparatorText("Recently Used");
	auto recentColors = GetRecentColors(5);

	if (recentColors.empty()) {
		ImGui::TextDisabled("No recent colors");
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

			// Drag source
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
				ImGui::SetDragDropPayload("COLOR_DND", &entry->color, sizeof(float3));
				ImGui::ColorButton("##preview", color, ImGuiColorEditFlags_NoAlpha);
				ImGui::EndDragDropSource();
			}

			Util::AddTooltip(std::format("RGB: {:.3f}, {:.3f}, {:.3f}\nUsed {} times\nClick to copy",
				entry->color.x, entry->color.y, entry->color.z, entry->useCount)
					.c_str());
		}
	}
	ImGui::Spacing();

	// Most Used section
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::TextUnformatted("Most Used");
	ImGui::Spacing();
	ImGui::TextWrapped("Favourite/most commonly used colours here.");
	ImGui::Spacing();

	auto mostUsedColors = GetMostUsedColors(20);

	if (mostUsedColors.empty()) {
		ImGui::TextDisabled("No frequently used colors yet");
		ImGui::TextDisabled("(Colors used 3+ times will appear here)");
	} else {
		int colorIndex = 0;
		for (auto* entry : mostUsedColors) {
			if (colorIndex > 0 && colorIndex % 10 != 0)
				ImGui::SameLine(0.0f, spacing);

			ImVec4 color(entry->color.x, entry->color.y, entry->color.z, 1.0f);
			std::string id = "##mostused_color_" + std::to_string(colorIndex);

			if (ImGui::ColorButton(id.c_str(), color, ImGuiColorEditFlags_NoAlpha, ImVec2(buttonSize, buttonSize))) {
				copiedColor = entry->color;
				hasColorInClipboard = true;
				ImGui::SetClipboardText(std::format("{:.3f}, {:.3f}, {:.3f}", entry->color.x, entry->color.y, entry->color.z).c_str());
			}

			// Drag source
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
				ImGui::SetDragDropPayload("COLOR_DND", &entry->color, sizeof(float3));
				ImGui::ColorButton("##preview", color, ImGuiColorEditFlags_NoAlpha);
				ImGui::EndDragDropSource();
			}

			// Right-click to remove
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::Selectable("Remove from palette")) {
					auto it = std::find_if(colorEntries.begin(), colorEntries.end(),
						[entry](const ColorEntry& e) { return &e == entry; });
					if (it != colorEntries.end()) {
						colorEntries.erase(it);
						Save();
					}
				}
				ImGui::EndPopup();
			}

			Util::AddTooltip(std::format("RGB: {:.3f}, {:.3f}, {:.3f}\nUsed {} times\nClick to copy\nRight-click to remove",
				entry->color.x, entry->color.y, entry->color.z, entry->useCount)
					.c_str());

			colorIndex++;
		}
	}
}

void PaletteWindow::DrawValuesTab()
{
	// Recently Used section
	ImGui::SeparatorText("Recently Used");
	auto recentValues = GetRecentValues(3);
	if (recentValues.empty()) {
		ImGui::TextDisabled("No recent values");
	} else {
		for (auto* entry : recentValues) {
			std::string label = std::format("{}: {:.3f}", entry->name, entry->value);
			if (ImGui::Selectable(label.c_str())) {
				copiedValue = entry->value;
				copiedValueName = entry->name;
				hasValueInClipboard = true;
				ImGui::SetClipboardText(std::to_string(entry->value).c_str());
			}

			Util::AddTooltip(std::format("Used {} times\nClick to copy", entry->useCount).c_str());
		}
	}
	ImGui::Spacing();
	// Most Used section
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::TextUnformatted("Most Used");
	ImGui::Spacing();
	ImGui::TextWrapped("Favourite/most commonly used values here.");
	ImGui::Spacing();

	auto mostUsedValues = GetMostUsedValues(20);

	if (mostUsedValues.empty()) {
		ImGui::TextDisabled("No frequently used values yet");
		ImGui::TextDisabled("(Values used 3+ times will appear here)");
	} else {
		for (auto* entry : mostUsedValues) {
			std::string label = std::format("{}: {:.3f}##{}", entry->name, entry->value, (void*)entry);
			if (ImGui::Selectable(label.c_str())) {
				copiedValue = entry->value;
				copiedValueName = entry->name;
				hasValueInClipboard = true;
				ImGui::SetClipboardText(std::to_string(entry->value).c_str());
			}

			// Right-click to remove
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::Selectable("Remove from palette")) {
					auto it = std::find_if(valueEntries.begin(), valueEntries.end(),
						[entry](const ValueEntry& e) { return &e == entry; });
					if (it != valueEntries.end()) {
						valueEntries.erase(it);
						Save();
					}
				}
				ImGui::EndPopup();
			}

			Util::AddTooltip(std::format("Used {} times\nClick to copy\nRight-click to remove", entry->useCount).c_str());
		}
	}
}

std::vector<PaletteWindow::ColorEntry*> PaletteWindow::GetRecentColors(int count)
{
	std::vector<ColorEntry*> result;
	std::vector<ColorEntry*> allColors;

	for (auto& entry : colorEntries) {
		allColors.push_back(&entry);
	}

	std::sort(allColors.begin(), allColors.end(), [](ColorEntry* a, ColorEntry* b) {
		return a->lastUsedTime > b->lastUsedTime;
	});

	for (int i = 0; i < std::min(count, (int)allColors.size()); i++) {
		result.push_back(allColors[i]);
	}

	return result;
}

std::vector<PaletteWindow::ColorEntry*> PaletteWindow::GetMostUsedColors(int count)
{
	std::vector<ColorEntry*> result;

	for (auto& entry : colorEntries) {
		if (entry.useCount >= 3 || entry.isFavorite) {
			result.push_back(&entry);
		}
	}

	std::sort(result.begin(), result.end(), [](ColorEntry* a, ColorEntry* b) {
		// Favorites always come first
		if (a->isFavorite != b->isFavorite)
			return a->isFavorite;
		// Then sort by use count
		return a->useCount > b->useCount;
	});

	if ((int)result.size() > count) {
		result.resize(count);
	}

	return result;
}

std::vector<PaletteWindow::ValueEntry*> PaletteWindow::GetRecentValues(int count)
{
	std::vector<ValueEntry*> result;
	std::vector<ValueEntry*> allValues;

	for (auto& entry : valueEntries) {
		allValues.push_back(&entry);
	}

	std::sort(allValues.begin(), allValues.end(), [](ValueEntry* a, ValueEntry* b) {
		return a->lastUsedTime > b->lastUsedTime;
	});

	for (int i = 0; i < std::min(count, (int)allValues.size()); i++) {
		result.push_back(allValues[i]);
	}

	return result;
}

std::vector<PaletteWindow::ValueEntry*> PaletteWindow::GetMostUsedValues(int count)
{
	std::vector<ValueEntry*> result;

	for (auto& entry : valueEntries) {
		if (entry.useCount >= 3 || entry.isFavorite) {
			result.push_back(&entry);
		}
	}

	std::sort(result.begin(), result.end(), [](ValueEntry* a, ValueEntry* b) {
		// Favorites always come first
		if (a->isFavorite != b->isFavorite)
			return a->isFavorite;
		// Then sort by use count
		return a->useCount > b->useCount;
	});

	if ((int)result.size() > count) {
		result.resize(count);
	}

	return result;
}

void PaletteWindow::TrackColorUsage(const float3& color)
{
	float currentTime = static_cast<float>(ImGui::GetTime());

	// Find existing entry (with small epsilon for float comparison)
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

	// Add new entry
	ColorEntry newEntry;
	newEntry.color = color;
	newEntry.useCount = 1;
	newEntry.lastUsedTime = currentTime;
	colorEntries.push_back(newEntry);
	Save();
}

void PaletteWindow::TrackValueUsage(const std::string& name, float value)
{
	float currentTime = static_cast<float>(ImGui::GetTime());

	// Find existing entry
	const float epsilon = 0.001f;
	for (auto& entry : valueEntries) {
		if (entry.name == name && std::abs(entry.value - value) < epsilon) {
			entry.useCount++;
			entry.lastUsedTime = currentTime;
			Save();
			return;
		}
	}

	// Add new entry
	ValueEntry newEntry;
	newEntry.name = name;
	newEntry.value = value;
	newEntry.useCount = 1;
	newEntry.lastUsedTime = currentTime;
	valueEntries.push_back(newEntry);
	Save();
}

void PaletteWindow::Save()
{
	auto editorWindow = EditorWindow::GetSingleton();

	// Save favorites
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

	// Save color entries
	editorWindow->settings.paletteColors.clear();
	for (const auto& entry : colorEntries) {
		EditorWindow::Settings::PaletteColorEntry e;
		e.r = entry.color.x;
		e.g = entry.color.y;
		e.b = entry.color.z;
		e.useCount = entry.useCount;
		e.lastUsedTime = entry.lastUsedTime;
		e.isFavorite = entry.isFavorite;
		editorWindow->settings.paletteColors.push_back(e);
	}

	// Save value entries
	editorWindow->settings.paletteValues.clear();
	for (const auto& entry : valueEntries) {
		EditorWindow::Settings::PaletteValueEntry e;
		e.name = entry.name;
		e.value = entry.value;
		e.useCount = entry.useCount;
		e.lastUsedTime = entry.lastUsedTime;
		e.isFavorite = entry.isFavorite;
		editorWindow->settings.paletteValues.push_back(e);
	}

	editorWindow->Save();
}

void PaletteWindow::Load()
{
	auto editorWindow = EditorWindow::GetSingleton();

	// Load favorites
	for (size_t i = 0; i < editorWindow->settings.paletteFavorites.size(); i++) {
		const auto& fav = editorWindow->settings.paletteFavorites[i];
		if (fav.hasValue) {
			favoriteColors[i] = float3{ fav.r, fav.g, fav.b };
		} else {
			favoriteColors[i].reset();
		}
	}

	// Load color entries
	colorEntries.clear();
	for (const auto& e : editorWindow->settings.paletteColors) {
		ColorEntry entry;
		entry.color = { e.r, e.g, e.b };
		entry.useCount = e.useCount;
		entry.lastUsedTime = e.lastUsedTime;
		entry.isFavorite = e.isFavorite;
		colorEntries.push_back(entry);
	}

	// Load value entries
	valueEntries.clear();
	for (const auto& e : editorWindow->settings.paletteValues) {
		ValueEntry entry;
		entry.name = e.name;
		entry.value = e.value;
		entry.useCount = e.useCount;
		entry.lastUsedTime = e.lastUsedTime;
		entry.isFavorite = e.isFavorite;
		valueEntries.push_back(entry);
	}
}
