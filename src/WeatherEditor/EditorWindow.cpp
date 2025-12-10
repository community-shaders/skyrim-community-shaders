#include "EditorWindow.h"

#include "Features/WeatherEditor.h"
#include "PaletteWindow.h"
#include "State.h"
#include "Weather/LightingTemplateWidget.h"
#include "WeatherUtils.h"
#include "imgui_internal.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings, recordMarkers, markedRecords, autoApplyChanges, suppressDeleteWarning, useTextButtons, enableInheritFromParent, editorUIScale, favoriteWidgets, recentWidgets, maxRecentWidgets, rememberOpenWidgets, lastOpenWidgets)

void TextUnformattedDisabled(const char* a_text, const char* a_textEnd = nullptr)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextUnformatted(a_text, a_textEnd);
	ImGui::PopStyleColor();
}

void AddTooltip(const char* a_desc, ImGuiHoveredFlags a_flags = ImGuiHoveredFlags_DelayNormal)
{
	if (ImGui::IsItemHovered(a_flags)) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8, 8 });
		if (ImGui::BeginTooltip()) {
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 50.0f);
			ImGui::TextUnformatted(a_desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::PopStyleVar();
	}
}

inline void HelpMarker(const char* a_desc)
{
	ImGui::AlignTextToFramePadding();
	TextUnformattedDisabled("(?)");
	AddTooltip(a_desc, ImGuiHoveredFlags_DelayShort);
}

void DrawIconStar(ImVec2 center, float radius, ImU32 color, bool /*filled*/)
{
	auto* drawList = ImGui::GetWindowDrawList();
	const int numPoints = 5;
	const float angleStep = 3.14159f / numPoints;
	ImVec2 points[10];

	for (int i = 0; i < numPoints * 2; i++) {
		float angle = -1.57079f + i * angleStep;
		float r = (i % 2 == 0) ? radius : radius * 0.38f;
		points[i] = ImVec2(center.x + cosf(angle) * r, center.y + sinf(angle) * r);
	}

	for (int i = 0; i < 10; i++) {
		drawList->AddLine(points[i], points[(i + 1) % 10], color, 1.5f);
	}
}

void DrawIconCircle(ImVec2 center, float radius, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	if (filled) {
		drawList->AddCircleFilled(center, radius, color, 16);
	} else {
		drawList->AddCircle(center, radius, color, 16, 1.5f);
	}
}

void DrawIconWave(ImVec2 center, float width, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	const int segments = 8;
	const float amplitude = width * 0.15f;
	const float waveWidth = width * 0.8f;
	const float segmentWidth = waveWidth / segments;

	ImVec2 start(center.x - waveWidth * 0.5f, center.y);

	if (filled) {
		// Draw filled wave using multiple horizontal lines
		for (int i = 0; i < segments; i++) {
			float x1 = start.x + i * segmentWidth;
			float x2 = start.x + (i + 1) * segmentWidth;
			float y1 = start.y + sinf(i * 3.14159f / 2.0f) * amplitude;
			float y2 = start.y + sinf((i + 1) * 3.14159f / 2.0f) * amplitude;
			drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color, 3.0f);
		}
	} else {
		// Draw outline wave
		for (int i = 0; i < segments; i++) {
			float x1 = start.x + i * segmentWidth;
			float x2 = start.x + (i + 1) * segmentWidth;
			float y1 = start.y + sinf(i * 3.14159f / 2.0f) * amplitude;
			float y2 = start.y + sinf((i + 1) * 3.14159f / 2.0f) * amplitude;
			drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color, 1.5f);
		}
	}
}

bool IconButton(const char* label, bool filled, const char* iconType)
{
	ImVec2 buttonSize(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
	ImVec2 cursorPos = ImGui::GetCursorScreenPos();

	bool result = ImGui::InvisibleButton(label, buttonSize);

	bool hovered = ImGui::IsItemHovered();
	bool active = ImGui::IsItemActive();

	ImU32 bgColor = active  ? ImGui::GetColorU32(ImGuiCol_ButtonActive) :
	                hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) :
	                          ImGui::GetColorU32(ImGuiCol_Button);
	ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);

	auto* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + buttonSize.x, cursorPos.y + buttonSize.y), bgColor, ImGui::GetStyle().FrameRounding);

	ImVec2 center(cursorPos.x + buttonSize.x * 0.5f, cursorPos.y + buttonSize.y * 0.5f);
	float iconSize = buttonSize.x * 0.35f;

	if (strcmp(iconType, "star") == 0) {
		DrawIconStar(center, iconSize, iconColor, filled);
	} else if (strcmp(iconType, "circle") == 0) {
		DrawIconCircle(center, iconSize, iconColor, filled);
	} else if (strcmp(iconType, "wave") == 0) {
		DrawIconWave(center, buttonSize.x * 0.7f, iconColor, filled);
	}

	return result;
}

void EditorWindow::ShowObjectsWindow()
{
	ImGui::Begin("Weather and Lighting Browser");

	// Static variable to track the selected category
	static std::string selectedCategory = "Lighting Template";

	// Static variable for filtering objects
	static char filterBuffer[256] = "";
	static bool showOnlyFlagged = false;
	static bool showOnlyFavorites = false;

	// Create a table with two columns
	if (ImGui::BeginTable("ObjectTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoHostExtendX)) {
		// Set up column widths
		ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthStretch, 0.3f);  // 30% width
		ImGui::TableSetupColumn("Objects", ImGuiTableColumnFlags_WidthStretch, 0.7f);     // 70% width

		ImGui::TableNextRow();

		// Left column: Categories
		ImGui::TableSetColumnIndex(0);

		ImGui::Text("Categories");
		ImGui::Spacing();

		// List of categories
		const char* categories[] = { "Lighting Template", "Weather", "WorldSpace", "Cell Lighting", "ImageSpace", "Volumetric Lighting", "Shader Particle Geometry", "Lens Flare", "Visual Effect" };
		for (int i = 0; i < IM_ARRAYSIZE(categories); ++i) {
			// Highlight the selected category
			if (ImGui::Selectable(categories[i], selectedCategory == categories[i])) {
				selectedCategory = categories[i];  // Update selected category
			}
		}  // Right column: Objects
		ImGui::TableSetColumnIndex(1);

		// Display current active weather
		auto sky = globals::game::sky;
		if (sky && sky->currentWeather) {
			auto currentWeather = sky->currentWeather;
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
			ImGui::Text("Current Active Weather:");
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", currentWeather->GetFormEditorID());
			ImGui::SameLine();
			ImGui::TextDisabled("(0x%08X)", currentWeather->GetFormID());

			// Add button to open the current weather
			ImGui::SameLine();
			if (ImGui::SmallButton("Open##CurrentWeather")) {
				for (auto& widget : weatherWidgets) {
					if (widget->form == currentWeather) {
						widget->SetOpen(true);
						break;
					}
				}
			}
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// Handle Ctrl+F to focus search bar
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
				ImGui::SetKeyboardFocusHere();
			}
		}
		ImGui::InputTextWithHint("##ObjectFilter", "Filter... (Ctrl+F)", filterBuffer, sizeof(filterBuffer));

		ImGui::SameLine();
		HelpMarker("Type a part of an object name to filter the list.\nCtrl+F: Focus search\nEnter: Open selected");

		// Quick filter buttons on same row
		ImGui::SameLine();
		ImGui::Dummy(ImVec2(10.0f, 0.0f));  // Spacer
		ImGui::SameLine();
		if (IconButton("##filterFavorites", showOnlyFavorites, "star")) {
			showOnlyFavorites = !showOnlyFavorites;
		}
		ImGui::SameLine();
		ImGui::Text("Favorites");

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(10.0f, 0.0f));  // Spacer
		ImGui::SameLine();
		if (IconButton("##filterFlagged", showOnlyFlagged, "circle")) {
			showOnlyFlagged = !showOnlyFlagged;
		}
		ImGui::SameLine();
		ImGui::Text("Flagged");

		// Show recent widgets section for current category
		auto recentIt = settings.recentWidgets.find(selectedCategory);
		if (recentIt != settings.recentWidgets.end() && !recentIt->second.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Recent:");
			ImGui::SameLine();
			for (size_t i = 0; i < std::min(size_t(5), recentIt->second.size()); ++i) {
				if (i > 0)
					ImGui::SameLine();
				if (ImGui::SmallButton(recentIt->second[i].c_str())) {
					// Find and open widget in current category's collection
					auto& widgets = selectedCategory == "Weather"                  ? weatherWidgets :
					                selectedCategory == "WorldSpace"               ? worldSpaceWidgets :
					                selectedCategory == "Lighting Template"        ? lightingTemplateWidgets :
					                selectedCategory == "ImageSpace"               ? imageSpaceWidgets :
					                selectedCategory == "Volumetric Lighting"      ? volumetricLightingWidgets :
					                selectedCategory == "Shader Particle Geometry" ? precipitationWidgets :
					                selectedCategory == "Lens Flare"               ? lensFlareWidgets :
					                selectedCategory == "Visual Effect"            ? referenceEffectWidgets :
					                                                                 weatherWidgets;

					for (auto& widget : widgets) {
						if (widget->GetEditorID() == recentIt->second[i]) {
							widget->SetOpen(true);
							break;
						}
					}
				}
			}
		}

		// Create a table for the right column with "Name" and "ID" headers. Different weights to prevent truncation.
		if (ImGui::BeginTable("DetailsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable)) {
			ImGui::TableSetupColumn("Fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 25.0f);  // Favorite indicator
			ImGui::TableSetupColumn("Editor ID", ImGuiTableColumnFlags_WidthStretch, 3.5f);                          // Largest - weather/template names
			ImGui::TableSetupColumn("Form ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);                             // Fixed - 8 hex chars
			ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 2.0f);                               // Medium - plugin names
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.5f);                             // Smaller - status text

			ImGui::TableHeadersRow();

			// Handle column sorting
			if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
				if (sortSpecs->SpecsDirty) {
					if (sortSpecs->SpecsCount > 0) {
						const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
						currentSortColumn = static_cast<SortColumn>(spec.ColumnIndex);
						sortAscending = (spec.SortDirection == ImGuiSortDirection_Ascending);
					} else {
						currentSortColumn = SortColumn::None;
					}
					sortSpecs->SpecsDirty = false;
				}
			}

			// Display objects based on the selected category
			std::vector<std::unique_ptr<Widget>> emptyWidgets;
			const auto& widgets = selectedCategory == "Weather"                  ? weatherWidgets :
			                      selectedCategory == "WorldSpace"               ? worldSpaceWidgets :
			                      selectedCategory == "Cell Lighting"            ? emptyWidgets :
			                      selectedCategory == "ImageSpace"               ? imageSpaceWidgets :
			                      selectedCategory == "Volumetric Lighting"      ? volumetricLightingWidgets :
			                      selectedCategory == "Shader Particle Geometry" ? precipitationWidgets :
			                      selectedCategory == "Lens Flare"               ? lensFlareWidgets :
			                      selectedCategory == "Visual Effect"            ? referenceEffectWidgets :
			                                                                       lightingTemplateWidgets;
			// Sort widgets based on current sort column
			std::vector<Widget*> sortedWidgets;
			sortedWidgets.reserve(widgets.size());
			for (const auto& w : widgets) {
				sortedWidgets.push_back(w.get());
			}
			if (currentSortColumn != SortColumn::None) {
				std::sort(sortedWidgets.begin(), sortedWidgets.end(), [this](Widget* a, Widget* b) {
					int comparison = 0;
					switch (currentSortColumn) {
					case SortColumn::EditorID:
						comparison = _stricmp(a->GetEditorID().c_str(), b->GetEditorID().c_str());
						break;
					case SortColumn::FormID:
						comparison = _stricmp(a->GetFormID().c_str(), b->GetFormID().c_str());
						break;
					case SortColumn::File:
						comparison = _stricmp(a->GetFilename().c_str(), b->GetFilename().c_str());
						break;
					case SortColumn::Status:
						{
							auto markerA = settings.markedRecords.find(a->GetEditorID());
							auto markerB = settings.markedRecords.find(b->GetEditorID());
							std::string statusA = (markerA != settings.markedRecords.end()) ? markerA->second : "";
							std::string statusB = (markerB != settings.markedRecords.end()) ? markerB->second : "";
							comparison = _stricmp(statusA.c_str(), statusB.c_str());
							break;
						}
					default:
						break;
					}
					return sortAscending ? (comparison < 0) : (comparison > 0);
				});
			}

			// Special handling for Cell Lighting category
			if (selectedCategory == "Cell Lighting") {
				auto player = RE::PlayerCharacter::GetSingleton();
				if (player && player->parentCell) {
					auto cell = player->parentCell;
					bool isInterior = cell->IsInteriorCell();

					if (isInterior) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);

						// No favorite star for cell lighting (it's always the current cell)
						ImGui::Dummy(ImVec2(24, 24));

						ImGui::TableNextColumn();

						// Display current cell name
						const char* cellName = cell->GetName();
						std::string displayName = cellName && cellName[0] ? cellName : "[Unnamed Cell]";
						std::string label = std::format("[CURRENT CELL] {}", displayName);

						bool isOpen = currentCellLightingWidget && currentCellLightingWidget->IsOpen();
						if (ImGui::Selectable(label.c_str(), isOpen, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
							if (ImGui::IsMouseDoubleClicked(0)) {
								// Open or reuse the cell lighting widget
								if (currentCellLightingWidget && currentCellLightingWidget->cell == cell) {
									currentCellLightingWidget->SetOpen(true);
								} else {
									currentCellLightingWidget = std::make_unique<CellLightingWidget>(cell);
									currentCellLightingWidget->CacheFormData();
									currentCellLightingWidget->Load();
									currentCellLightingWidget->SetOpen(true);
								}
							}
						}

						// Highlight current cell
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.4f, 0.6f, 0.3f)));
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.4f, 0.6f, 0.3f)));

						// Enter key to open
						if (isOpen && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
							if (currentCellLightingWidget && currentCellLightingWidget->cell == cell) {
								currentCellLightingWidget->SetOpen(true);
							}
						}

						// Form ID column
						ImGui::TableNextColumn();
						ImGui::Text("0x%08X", cell->GetFormID());

						// File column
						ImGui::TableNextColumn();
						auto file = cell->GetFile(0);
						if (file) {
							ImGui::Text("%s", file->fileName);
						}

						// Status column
						ImGui::TableNextColumn();
						ImGui::Text("Interior Cell");
					} else {
						// Show message that cell lighting is only for interior cells
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(1);
						ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Cell Lighting is only available for interior cells.");
						ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "You are currently in an exterior cell.");
					}
				} else {
					// No player or cell
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(1);
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Player cell not available.");
				}
			}

			// Get current cell's lighting template for prioritization
			RE::BGSLightingTemplate* currentCellLightingTemplate = nullptr;
			if (selectedCategory == "Lighting Template") {
				auto player = RE::PlayerCharacter::GetSingleton();
				if (player && player->parentCell) {
					auto& cellData = player->parentCell->GetRuntimeData();
					currentCellLightingTemplate = cellData.lightingTemplate;
				}
			}

			// Filtered display of widgets - show current cell's lighting template first
			if (currentCellLightingTemplate && selectedCategory == "Lighting Template") {
				for (int i = 0; i < sortedWidgets.size(); ++i) {
					auto* ltWidget = dynamic_cast<LightingTemplateWidget*>(sortedWidgets[i]);
					if (!ltWidget || ltWidget->lightingTemplate != currentCellLightingTemplate)
						continue;
					if (!ContainsStringIgnoreCase(sortedWidgets[i]->GetEditorID(), filterBuffer))
						continue;

					// Apply quick filters
					if (showOnlyFavorites && !IsFavorite(sortedWidgets[i]->GetEditorID()))
						continue;
					if (showOnlyFlagged && settings.markedRecords.find(sortedWidgets[i]->GetEditorID()) == settings.markedRecords.end())
						continue;

					auto editorLabel = std::format("[CURRENT] {}", sortedWidgets[i]->GetEditorID());
					auto markedRecord = settings.markedRecords.find(sortedWidgets[i]->GetEditorID());
					ImGui::TableNextRow();

					// Highlight current cell's lighting template
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.4f, 0.6f, 0.3f)));
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.4f, 0.6f, 0.3f)));

					ImGui::TableSetColumnIndex(0);

					// Favorite star
					if (IconButton("##fav_current", IsFavorite(sortedWidgets[i]->GetEditorID()), "star")) {
						ToggleFavorite(sortedWidgets[i]->GetEditorID());
					}

					ImGui::TableNextColumn();

					// Editor ID column with [CURRENT] prefix
					bool isSelected = sortedWidgets[i]->IsOpen();
					if (ImGui::Selectable(editorLabel.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
						if (ImGui::IsMouseDoubleClicked(0)) {
							sortedWidgets[i]->SetOpen(true);
							AddToRecent(sortedWidgets[i]->GetEditorID(), selectedCategory);
						}
					}
					// Enter key to open
					if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
						sortedWidgets[i]->SetOpen(true);
						AddToRecent(sortedWidgets[i]->GetEditorID(), selectedCategory);
					}  // Context menu
					if (ImGui::BeginPopupContextItem(std::format("widget_context_menu##{}", sortedWidgets[i]->GetFormID()).c_str(), ImGuiPopupFlags_MouseButtonRight)) {
						auto& markedRecords = settings.markedRecords;

						for (auto& recordMarker : settings.recordMarkers) {
							if (ImGui::MenuItem(recordMarker.first.c_str())) {
								settings.markedRecords[sortedWidgets[i]->GetEditorID()] = recordMarker.first;
								Save();
							}
						}

						if (ImGui::MenuItem("Remove")) {
							markedRecords.erase(sortedWidgets[i]->GetEditorID());
							Save();
						}

						ImGui::EndPopup();
					}

					// Form ID column
					ImGui::TableNextColumn();
					ImGui::Text(sortedWidgets[i]->GetFormID().c_str());

					// File column
					ImGui::TableNextColumn();
					ImGui::Text(sortedWidgets[i]->GetFilename().c_str());

					// Status column
					ImGui::TableNextColumn();
					if (markedRecord != settings.markedRecords.end()) {
						ImGui::Text("%s", markedRecord->second.c_str());
					}
				}
			}

			// Filtered display of widgets - regular list
			for (int i = 0; i < sortedWidgets.size(); ++i) {
				// Skip current cell's lighting template if already shown
				if (currentCellLightingTemplate && selectedCategory == "Lighting Template") {
					auto* ltWidget = dynamic_cast<LightingTemplateWidget*>(sortedWidgets[i]);
					if (ltWidget && ltWidget->lightingTemplate == currentCellLightingTemplate)
						continue;
				}

				if (!ContainsStringIgnoreCase(sortedWidgets[i]->GetEditorID(), filterBuffer))
					continue;

				// Apply quick filters
				if (showOnlyFavorites && !IsFavorite(sortedWidgets[i]->GetEditorID()))
					continue;
				if (showOnlyFlagged && settings.markedRecords.find(sortedWidgets[i]->GetEditorID()) == settings.markedRecords.end())
					continue;

				auto editorLabel = sortedWidgets[i]->GetEditorID();
				auto markedRecord = settings.markedRecords.find(editorLabel);
				ImGui::TableNextRow();

				// Set background colour
				if (markedRecord != settings.markedRecords.end()) {
					auto& color = settings.recordMarkers[markedRecord->second];
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(color));
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(color));
				}

				ImGui::TableSetColumnIndex(0);

				// Favorite star
				if (IconButton(std::format("##fav_{}", i).c_str(), IsFavorite(sortedWidgets[i]->GetEditorID()), "star")) {
					ToggleFavorite(sortedWidgets[i]->GetEditorID());
				}

				ImGui::TableNextColumn();

				// Editor ID column
				bool isSelected = sortedWidgets[i]->IsOpen();
				if (ImGui::Selectable(editorLabel.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
					if (ImGui::IsMouseDoubleClicked(0)) {
						sortedWidgets[i]->SetOpen(true);
						AddToRecent(sortedWidgets[i]->GetEditorID(), selectedCategory);
					}
				}

				// Show ImageSpace and VolumetricLighting info for weather widgets
				if (selectedCategory == "Weather" && ImGui::IsItemHovered()) {
					auto* weatherWidget = dynamic_cast<WeatherWidget*>(sortedWidgets[i]);
					if (weatherWidget && weatherWidget->weather) {
						ImGui::BeginTooltip();

						// ImageSpace info
						ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "ImageSpace:");
						for (int tod = 0; tod < 4; tod++) {
							auto imgSpace = weatherWidget->weather->imageSpaces[tod];
							ImGui::Text("  %s: %s",
								TOD::GetPeriodName(tod),
								imgSpace ? imgSpace->GetFormEditorID() : "None");
						}

						ImGui::Spacing();

						// VolumetricLighting info
						ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Volumetric Lighting:");
						for (int tod = 0; tod < 4; tod++) {
							auto volLight = weatherWidget->weather->volumetricLighting[tod];
							ImGui::Text("  %s: %s",
								TOD::GetPeriodName(tod),
								volLight ? volLight->GetFormEditorID() : "None");
						}

						ImGui::EndTooltip();
					}
				}

				// Enter key to open
				if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
					sortedWidgets[i]->SetOpen(true);
					AddToRecent(sortedWidgets[i]->GetEditorID(), selectedCategory);
				}

				// Opens a context menu on right click to mark records by color
				if (ImGui::BeginPopupContextItem(std::format("widget_context_menu##{}", sortedWidgets[i]->GetFormID()).c_str(), ImGuiPopupFlags_MouseButtonRight)) {
					auto& markedRecords = settings.markedRecords;

					for (auto& recordMarker : settings.recordMarkers) {
						if (ImGui::MenuItem(recordMarker.first.c_str())) {
							settings.markedRecords[editorLabel] = recordMarker.first;
							Save();
						}
					}

					if (ImGui::MenuItem("Remove")) {
						markedRecords.erase(editorLabel);
						Save();
					}

					ImGui::EndPopup();
				}

				// Form ID column
				ImGui::TableNextColumn();
				ImGui::Text(sortedWidgets[i]->GetFormID().c_str());

				// File column
				ImGui::TableNextColumn();
				ImGui::Text(sortedWidgets[i]->GetFilename().c_str());

				// Status column
				ImGui::TableNextColumn();

				// Re-check if the record exists after potential removal
				markedRecord = settings.markedRecords.find(editorLabel);
				if (markedRecord != settings.markedRecords.end()) {
					ImGui::Text("%s", markedRecord->second.c_str());
				}
			}
		}

		// Filtered display of widgets - regular list
		for (int i = 0; i < sortedWidgets.size(); ++i) {
			// Skip current cell's lighting template if already shown
			if (currentCellLightingTemplate && selectedCategory == "Lighting Template") {
				auto* ltWidget = dynamic_cast<LightingTemplateWidget*>(sortedWidgets[i]);
				if (ltWidget && ltWidget->lightingTemplate == currentCellLightingTemplate)
					continue;
			}

			if (!ContainsStringIgnoreCase(sortedWidgets[i]->GetEditorID(), filterBuffer))
				continue;

			// Apply quick filters
			if (showOnlyFavorites && !IsFavorite(sortedWidgets[i]->GetEditorID()))
				continue;
			if (showOnlyFlagged && settings.markedRecords.find(sortedWidgets[i]->GetEditorID()) == settings.markedRecords.end())
				continue;

			auto editorLabel = sortedWidgets[i]->GetEditorID();
			auto markedRecord = settings.markedRecords.find(editorLabel);
			ImGui::TableNextRow();

			// Set background colour
			if (markedRecord != settings.markedRecords.end()) {
				auto& color = settings.recordMarkers[markedRecord->second];
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(color));
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(color));
			}

			ImGui::TableSetColumnIndex(0);

			// Favorite star
			if (IconButton(std::format("##fav_{}", i).c_str(), IsFavorite(sortedWidgets[i]->GetEditorID()), "star")) {
				ToggleFavorite(sortedWidgets[i]->GetEditorID());
			}

			ImGui::TableNextColumn();

			// Editor ID column
			bool isSelected = sortedWidgets[i]->IsOpen();
			if (ImGui::Selectable(editorLabel.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				if (ImGui::IsMouseDoubleClicked(0)) {
					sortedWidgets[i]->SetOpen(true);
					AddToRecent(sortedWidgets[i]->GetEditorID(), selectedCategory);
				}
			}
			// Enter key to open
			if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
				sortedWidgets[i]->SetOpen(true);
				AddToRecent(sortedWidgets[i]->GetEditorID(), selectedCategory);
			}

			// Opens a context menu on right click to mark records by color
			if (ImGui::BeginPopupContextItem(std::format("widget_context_menu##{}", sortedWidgets[i]->GetFormID()).c_str(), ImGuiPopupFlags_MouseButtonRight)) {
				auto& markedRecords = settings.markedRecords;

				for (auto& recordMarker : settings.recordMarkers) {
					if (ImGui::MenuItem(recordMarker.first.c_str())) {
						settings.markedRecords[editorLabel] = recordMarker.first;
						Save();
					}
				}

				if (ImGui::MenuItem("Remove")) {
					markedRecords.erase(editorLabel);
					Save();
				}

				ImGui::EndPopup();
			}

			// Form ID column
			ImGui::TableNextColumn();
			ImGui::Text(sortedWidgets[i]->GetFormID().c_str());

			// File column
			ImGui::TableNextColumn();
			ImGui::Text(sortedWidgets[i]->GetFilename().c_str());

			// Status column
			ImGui::TableNextColumn();

			// Re-check if the record exists after potential removal
			markedRecord = settings.markedRecords.find(editorLabel);
			if (markedRecord != settings.markedRecords.end()) {
				ImGui::Text("%s", markedRecord->second.c_str());
			}
		}
		ImGui::EndTable();
	}

	ImGui::EndTable();
}

// End the window
ImGui::End();
}

void EditorWindow::ShowViewportWindow()
{
	ImGui::Begin("Viewport");

	// Top bar
	auto calendar = RE::Calendar::GetSingleton();
	if (calendar && calendar->gameHour) {
		ImGui::SliderFloat("##ViewportSlider", &calendar->gameHour->value, 0.0f, 23.99f, "Time: %.2f");
		ImGui::SameLine();
		int activePeriod = TOD::GetActivePeriod();
		ImGui::Text("(%s)", TOD::GetPeriodName(activePeriod));
	}

	// The size of the image in ImGui																														   // Get the available space in the current window
	ImVec2 availableSpace = ImGui::GetContentRegionAvail();

	// Calculate aspect ratio of the image
	float aspectRatio = ImGui::GetIO().DisplaySize.x / ImGui::GetIO().DisplaySize.y;

	// Determine the size to fit while preserving the aspect ratio
	ImVec2 imageSize;
	if (availableSpace.x / availableSpace.y < aspectRatio) {
		// Fit width
		imageSize.x = availableSpace.x;
		imageSize.y = availableSpace.x / aspectRatio;
	} else {
		// Fit height
		imageSize.y = availableSpace.y;
		imageSize.x = availableSpace.y * aspectRatio;
	}

	ImGui::Image((void*)tempTexture->srv.get(), imageSize);

	ImGui::End();
}

void EditorWindow::ShowWidgetWindow()
{
	// Global shortcut for closing focused widget (Ctrl+W)
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
		// Close the most recently focused widget
		for (int i = 0; i < (int)weatherWidgets.size(); i++) {
			auto& widget = weatherWidgets[i];
			if (widget->IsOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
				widget->SetOpen(false);
				break;
			}
		}
		for (int i = 0; i < (int)worldSpaceWidgets.size(); i++) {
			auto& widget = worldSpaceWidgets[i];
			if (widget->IsOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
				widget->SetOpen(false);
				break;
			}
		}
		for (int i = 0; i < (int)lightingTemplateWidgets.size(); i++) {
			auto& widget = lightingTemplateWidgets[i];
			if (widget->IsOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
				widget->SetOpen(false);
				break;
			}
		}
		for (int i = 0; i < (int)imageSpaceWidgets.size(); i++) {
			auto& widget = imageSpaceWidgets[i];
			if (widget->IsOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
				widget->SetOpen(false);
				break;
			}
		}
		for (int i = 0; i < (int)volumetricLightingWidgets.size(); i++) {
			auto& widget = volumetricLightingWidgets[i];
			if (widget->IsOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
				widget->SetOpen(false);
				break;
			}
		}
		for (int i = 0; i < (int)precipitationWidgets.size(); i++) {
			auto& widget = precipitationWidgets[i];
			if (widget->IsOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
				widget->SetOpen(false);
				break;
			}
		}
		for (int i = 0; i < (int)lensFlareWidgets.size(); i++) {
			auto& widget = lensFlareWidgets[i];
			if (widget->IsOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
				widget->SetOpen(false);
				break;
			}
		}
		for (int i = 0; i < (int)referenceEffectWidgets.size(); i++) {
			auto& widget = referenceEffectWidgets[i];
			if (widget->IsOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
				widget->SetOpen(false);
				break;
			}
		}
	}

	for (int i = 0; i < (int)weatherWidgets.size(); i++) {
		auto& widget = weatherWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)worldSpaceWidgets.size(); i++) {
		auto& widget = worldSpaceWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)lightingTemplateWidgets.size(); i++) {
		auto& widget = lightingTemplateWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)imageSpaceWidgets.size(); i++) {
		auto& widget = imageSpaceWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)volumetricLightingWidgets.size(); i++) {
		auto& widget = volumetricLightingWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)precipitationWidgets.size(); i++) {
		auto& widget = precipitationWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)lensFlareWidgets.size(); i++) {
		auto& widget = lensFlareWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	for (int i = 0; i < (int)referenceEffectWidgets.size(); i++) {
		auto& widget = referenceEffectWidgets[i];
		if (widget->IsOpen())
			widget->DrawWidget();
	}

	// Draw current cell lighting widget if open
	if (currentCellLightingWidget && currentCellLightingWidget->IsOpen()) {
		currentCellLightingWidget->DrawWidget();
	}
}

void EditorWindow::RenderUI()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& framebuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
	auto& context = globals::d3d::context;

	context->ClearRenderTargetView(framebuffer.RTV, (float*)&ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);

	// Apply editor UI scale
	ImGuiIO& io = ImGui::GetIO();
	float previousScale = io.FontGlobalScale;
	io.FontGlobalScale = settings.editorUIScale;

	// Increase background opacity for all editor windows
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);

	// Check for Escape key to close editor (but not if a popup is open)
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
		open = false;
	}

	// Check for Ctrl+Z to undo
	if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
		if (CanUndo()) {
			PerformUndo();
		}
	}

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Save All Open Widgets", "Ctrl+S")) {
				SaveAll();
			}

			// Save individual widgets submenu
			if (ImGui::BeginMenu("Save")) {
				bool hasOpenWidgets = false;

				// Weather widgets
				for (auto& widget : weatherWidgets) {
					if (widget->IsOpen()) {
						hasOpenWidgets = true;
						if (ImGui::MenuItem(std::format("Save {}", widget->GetEditorID()).c_str())) {
							widget->Save();
						}
					}
				}

				// WorldSpace widgets
				for (auto& widget : worldSpaceWidgets) {
					if (widget->IsOpen()) {
						hasOpenWidgets = true;
						if (ImGui::MenuItem(std::format("Save {}", widget->GetEditorID()).c_str())) {
							widget->Save();
						}
					}
				}

				// Lighting Template widgets
				for (auto& widget : lightingTemplateWidgets) {
					if (widget->IsOpen()) {
						hasOpenWidgets = true;
						if (ImGui::MenuItem(std::format("Save {}", widget->GetEditorID()).c_str())) {
							widget->Save();
						}
					}
				}

				// ImageSpace widgets
				for (auto& widget : imageSpaceWidgets) {
					if (widget->IsOpen()) {
						hasOpenWidgets = true;
						if (ImGui::MenuItem(std::format("Save {}", widget->GetEditorID()).c_str())) {
							widget->Save();
						}
					}
				}

				if (!hasOpenWidgets) {
					ImGui::TextDisabled("No open widgets");
				}

				ImGui::EndMenu();
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Close All Weather Widgets")) {
				for (auto& widget : weatherWidgets) widget->SetOpen(false);
			}
			if (ImGui::MenuItem("Close All WorldSpace Widgets")) {
				for (auto& widget : worldSpaceWidgets) widget->SetOpen(false);
			}
			if (ImGui::MenuItem("Close All Lighting Widgets")) {
				for (auto& widget : lightingTemplateWidgets) widget->SetOpen(false);
			}
			if (ImGui::MenuItem("Close All ImageSpace Widgets")) {
				for (auto& widget : imageSpaceWidgets) widget->SetOpen(false);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Settings")) {
			if (ImGui::MenuItem("General Settings")) {
				showSettingsWindow = true;
				settingsSelectedCategory = "General";
			}
			if (ImGui::MenuItem("Editor Flags")) {
				showSettingsWindow = true;
				settingsSelectedCategory = "Flags";
			}
			ImGui::Separator();

			// Current cell lighting
			auto player = RE::PlayerCharacter::GetSingleton();
			if (player && player->parentCell && player->parentCell->IsInteriorCell()) {
				if (ImGui::MenuItem("Edit Current Cell Lighting")) {
					// Check if widget already exists
					bool found = false;
					if (currentCellLightingWidget && currentCellLightingWidget->cell == player->parentCell) {
						currentCellLightingWidget->SetOpen(true);
						found = true;
					}

					if (!found) {
						// Create new widget for current cell
						currentCellLightingWidget = std::make_unique<CellLightingWidget>(player->parentCell);
						currentCellLightingWidget->CacheFormData();
						currentCellLightingWidget->Load();
						currentCellLightingWidget->SetOpen(true);
					}
				}
			} else {
				ImGui::BeginDisabled();
				ImGui::MenuItem("Edit Current Cell Lighting");
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("Only available in interior cells");
				}
			}

			ImGui::Separator();

			if (ImGui::Checkbox("Auto-Apply Changes", &settings.autoApplyChanges)) {
				Save();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Automatically apply weather changes to the game as you edit");
			}
			if (ImGui::Checkbox("Remember Open Widgets", &settings.rememberOpenWidgets)) {
				Save();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Restore previously open widgets when editor reopens");
			}
			if (ImGui::Checkbox("Enable Inherit From Parent", &settings.enableInheritFromParent)) {
				Save();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Show inherit from parent options in weather widgets");
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window")) {
			if (ImGui::MenuItem("Palette", nullptr, PaletteWindow::GetSingleton()->open)) {
				PaletteWindow::GetSingleton()->open = !PaletteWindow::GetSingleton()->open;
			}

			ImGui::Separator();
			ImGui::Text("Open Widgets:");
			ImGui::Separator();

			int openCount = 0;
			for (auto& widget : weatherWidgets) {
				if (widget->IsOpen()) {
					openCount++;
					if (ImGui::MenuItem(std::format("Weather: {}", widget->GetEditorID()).c_str())) {
						// Focus window (ImGui will bring to front when clicked)
					}
				}
			}
			for (auto& widget : worldSpaceWidgets) {
				if (widget->IsOpen()) {
					openCount++;
					if (ImGui::MenuItem(std::format("WorldSpace: {}", widget->GetEditorID()).c_str())) {
						// Focus window
					}
				}
			}
			for (auto& widget : lightingTemplateWidgets) {
				if (widget->IsOpen()) {
					openCount++;
					if (ImGui::MenuItem(std::format("Lighting: {}", widget->GetEditorID()).c_str())) {
						// Focus window
					}
				}
			}
			for (auto& widget : imageSpaceWidgets) {
				if (widget->IsOpen()) {
					openCount++;
					if (ImGui::MenuItem(std::format("ImageSpace: {}", widget->GetEditorID()).c_str())) {
						// Focus window
					}
				}
			}

			if (openCount == 0) {
				ImGui::TextDisabled("No widgets open");
			}

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help")) {
			ImGui::Text("Weather Editor");
			ImGui::Separator();
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Keyboard Shortcuts:");
			ImGui::BulletText("Ctrl+F: Focus search");
			ImGui::BulletText("Ctrl+S: Save all open widgets");
			ImGui::BulletText("Ctrl+W: Close focused widget");
			ImGui::BulletText("Enter: Open selected widget");
			ImGui::BulletText("Esc: Close editor");
			ImGui::Separator();
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Quick Tips:");
			ImGui::BulletText("Double-click to edit");
			ImGui::BulletText("Right-click to mark status");
			ImGui::BulletText("Click star icon to favorite");
			ImGui::BulletText("Use quick filters for fast sorting");
			ImGui::BulletText("Auto-Apply updates game live");
			ImGui::BulletText("Lock weather to prevent changes");
			ImGui::BulletText("Undo button reverts recent changes (Ctrl+Z)");
			ImGui::Separator();
			ImGui::Text("Total Objects:");
			ImGui::BulletText("Weathers: %d", (int)weatherWidgets.size());
			ImGui::BulletText("WorldSpaces: %d", (int)worldSpaceWidgets.size());
			ImGui::BulletText("Lighting: %d", (int)lightingTemplateWidgets.size());
			ImGui::BulletText("ImageSpaces: %d", (int)imageSpaceWidgets.size());
			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Favorites: %d", (int)settings.favoriteWidgets.size());

			// Count total recent widgets across all categories
			int totalRecent = 0;
			for (const auto& [category, widgets] : settings.recentWidgets) {
				totalRecent += static_cast<int>(widgets.size());
			}
			ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Recent: %d", totalRecent);
			ImGui::EndMenu();
		}

		// Pause Time button
		auto menu = globals::menu;
		if (menu && menu->uiIcons.pauseTime.texture) {
			bool isPaused = IsTimePaused();

			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			if (isPaused) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
			} else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));
			}

			const float menuBarHeight = ImGui::GetFrameHeight();
			const float buttonDim = menuBarHeight * 0.85f;  // 85% of menu bar height
			const ImVec2 buttonSize(buttonDim, buttonDim);

			if (ImGui::ImageButton("##GlobalPauseTime", menu->uiIcons.pauseTime.texture, buttonSize)) {
				if (isPaused) {
					ResumeTime();
				} else {
					PauseTime();
				}
			}

			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar();

			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(isPaused ? "Resume Time" : "Pause Time");
			}
		}

		// Undo button
		if (menu && menu->uiIcons.undo.texture) {
			bool canUndo = CanUndo();

			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			if (!canUndo) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.25f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
			} else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			}

			const float menuBarHeight = ImGui::GetFrameHeight();
			const float buttonDim = menuBarHeight * 0.85f;
			const ImVec2 buttonSize(buttonDim, buttonDim);

			if (ImGui::ImageButton("##GlobalUndo", menu->uiIcons.undo.texture, buttonSize) && canUndo) {
				PerformUndo();
			}

			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar();

			if (ImGui::IsItemHovered()) {
				if (canUndo) {
					ImGui::SetTooltip("Undo (Ctrl+Z) - %d states", (int)undoStack.size());
				} else {
					ImGui::SetTooltip("Undo (Ctrl+Z) - No changes to undo");
				}
			}
		}  // Weather lock indicator
		if (weatherLockActive && lockedWeather) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
			const char* weatherName = lockedWeather->GetFormEditorID();
			ImGui::Text(" [LOCKED: %s]", weatherName ? weatherName : "Unknown");
			ImGui::PopStyleColor();
		}

		// Time pause indicator
		if (timePaused) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
			ImGui::Text(" [TIME PAUSED]");
			ImGui::PopStyleColor();
		}

		// Close button on the right side
		float menuBarHeight = ImGui::GetFrameHeight();
		float closeButtonSize = menuBarHeight * 0.9f;  // 10% smaller than menu bar
		ImGui::SameLine(ImGui::GetWindowWidth() - closeButtonSize - 10.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
		if (ImGui::Button("X", ImVec2(closeButtonSize, closeButtonSize))) {
			open = false;
		}
		ImGui::PopStyleColor(3);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Close Weather Editor (Esc)");
		}
		ImGui::EndMainMenuBar();
	}

	auto width = ImGui::GetIO().DisplaySize.x;
	auto height = ImGui::GetIO().DisplaySize.y;
	auto viewportWidth = width * 0.5f;                // Make the viewport take up 50% of the width
	auto sideWidth = (width - viewportWidth) / 2.0f;  // Divide the remaining width equally between the side windows
	ImGui::SetNextWindowSize(ImVec2(sideWidth, ImGui::GetIO().DisplaySize.y * 0.75f), ImGuiCond_FirstUseEver);
	ShowObjectsWindow();

	ImGui::SetNextWindowSize(ImVec2(viewportWidth, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver);
	ShowViewportWindow();

	auto settingsWindowHeight = height * 0.25f;
	auto settingsWindowWidth = width * 0.25f;
	ImGui::SetNextWindowSizeConstraints(ImVec2(settingsWindowWidth, settingsWindowHeight), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::SetNextWindowPos({ (width / 2.0f) - (settingsWindowWidth / 2.0f), (height / 2.0f) - (settingsWindowHeight / 2.0f) }, ImGuiCond_Appearing);
	if (showSettingsWindow) {
		ShowSettingsWindow();
	}

	ShowWidgetWindow();

	// Show palette window
	PaletteWindow::GetSingleton()->Draw();

	// Render notifications on top of everything
	RenderNotifications();

	// Pop the alpha style var
	ImGui::PopStyleVar();

	// Restore previous font scale
	io.FontGlobalScale = previousScale;
}

EditorWindow::~EditorWindow()
{
	delete tempTexture;
	weatherWidgets.clear();
	worldSpaceWidgets.clear();
	lightingTemplateWidgets.clear();
	imageSpaceWidgets.clear();
	volumetricLightingWidgets.clear();
	precipitationWidgets.clear();
	referenceEffectWidgets.clear();
	artObjectWidgets.clear();
	effectShaderWidgets.clear();
	currentCellLightingWidget.reset();
}

void EditorWindow::SetupResources()
{
	auto dataHandler = RE::TESDataHandler::GetSingleton();
	auto& weatherArray = dataHandler->GetFormArray<RE::TESWeather>();

	Load();
	PaletteWindow::GetSingleton()->Load();

	for (auto weather : weatherArray) {
		auto widget = std::make_unique<WeatherWidget>(weather);
		widget->CacheFormData();
		widget->Load();
		weatherWidgets.push_back(std::move(widget));
	}

	auto& worldSpaceArray = dataHandler->GetFormArray<RE::TESWorldSpace>();

	for (auto worldSpace : worldSpaceArray) {
		auto widget = std::make_unique<WorldSpaceWidget>(worldSpace);
		widget->CacheFormData();
		widget->Load();
		worldSpaceWidgets.push_back(std::move(widget));
	}

	auto& lightingTemplateArray = dataHandler->GetFormArray<RE::BGSLightingTemplate>();

	for (auto lightingTemplate : lightingTemplateArray) {
		auto widget = std::make_unique<LightingTemplateWidget>(lightingTemplate);
		widget->CacheFormData();
		widget->Load();
		lightingTemplateWidgets.push_back(std::move(widget));
	}

	auto& imageSpaceArray = dataHandler->GetFormArray<RE::TESImageSpace>();

	for (auto imageSpace : imageSpaceArray) {
		auto widget = std::make_unique<ImageSpaceWidget>(imageSpace);
		widget->CacheFormData();
		widget->Load();
		imageSpaceWidgets.push_back(std::move(widget));
	}

	auto& volumetricLightingArray = dataHandler->GetFormArray<RE::BGSVolumetricLighting>();

	for (auto volumetricLighting : volumetricLightingArray) {
		auto widget = std::make_unique<VolumetricLightingWidget>(volumetricLighting);
		widget->CacheFormData();
		widget->Load();
		volumetricLightingWidgets.push_back(std::move(widget));
	}

	auto& precipitationArray = dataHandler->GetFormArray<RE::BGSShaderParticleGeometryData>();

	for (auto precipitation : precipitationArray) {
		auto widget = std::make_unique<PrecipitationWidget>(precipitation);
		widget->CacheFormData();
		widget->Load();
		precipitationWidgets.push_back(std::move(widget));
	}

	auto& lensFlareArray = dataHandler->GetFormArray<RE::BGSLensFlare>();

	for (auto lensFlare : lensFlareArray) {
		auto widget = std::make_unique<LensFlareWidget>(lensFlare);
		widget->CacheFormData();
		widget->Load();
		lensFlareWidgets.push_back(std::move(widget));
	}

	auto& referenceEffectArray = dataHandler->GetFormArray<RE::BGSReferenceEffect>();

	for (auto referenceEffect : referenceEffectArray) {
		auto widget = std::make_unique<ReferenceEffectWidget>(referenceEffect);
		widget->CacheFormData();
		widget->Load();
		referenceEffectWidgets.push_back(std::move(widget));
	}

	// Cache art objects for form picker performance
	auto& artObjectArray = dataHandler->GetFormArray<RE::BGSArtObject>();
	for (auto artObject : artObjectArray) {
		auto widget = std::make_unique<SimpleFormWidget>();
		widget->form = artObject;
		widget->CacheFormData();
		artObjectWidgets.push_back(std::move(widget));
	}

	// Cache effect shaders for form picker performance
	auto& effectShaderArray = dataHandler->GetFormArray<RE::TESEffectShader>();
	for (auto effectShader : effectShaderArray) {
		auto widget = std::make_unique<SimpleFormWidget>();
		widget->form = effectShader;
		widget->CacheFormData();
		effectShaderWidgets.push_back(std::move(widget));
	}
}

void EditorWindow::Draw()
{
	// Track editor open state for vanity camera management
	static bool wasOpen = false;

	if (open && !wasOpen) {
		// Editor just opened - disable vanity camera and restore session
		DisableVanityCamera();
		RestoreSessionWidgets();
	} else if (!open && wasOpen) {
		// Editor just closed - restore vanity camera and save session
		RestoreVanityCamera();
		SaveSessionWidgets();
	}

	wasOpen = open;

	// Re-enforce weather lock if active (handles time changes)
	if (weatherLockActive && lockedWeather) {
		auto sky = RE::Sky::GetSingleton();
		if (sky && sky->currentWeather != lockedWeather) {
			sky->ForceWeather(lockedWeather, false);
		}
	}

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& framebuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];

	ID3D11Resource* resource = nullptr;
	framebuffer.SRV->GetResource(&resource);

	if (!tempTexture) {
		D3D11_TEXTURE2D_DESC texDesc{};
		((ID3D11Texture2D*)resource)->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		framebuffer.SRV->GetDesc(&srvDesc);

		tempTexture = new Texture2D(texDesc);
		tempTexture->CreateSRV(srvDesc);
	}

	auto& context = globals::d3d::context;

	context->CopyResource(tempTexture->resource.get(), resource);

	if (resource) {
		resource->Release();
	}

	RenderUI();
}

void EditorWindow::SaveAll()
{
	for (auto& weather : weatherWidgets) {
		if (weather->IsOpen())
			weather->Save();
	}

	for (auto& worldspace : worldSpaceWidgets) {
		if (worldspace->IsOpen())
			worldspace->Save();
	}

	for (auto& lightingTemplate : lightingTemplateWidgets) {
		if (lightingTemplate->IsOpen())
			lightingTemplate->Save();
	}

	for (auto& imageSpace : imageSpaceWidgets) {
		if (imageSpace->IsOpen())
			imageSpace->Save();
	}

	Save();
}

void EditorWindow::SaveSettings()
{
	j = settings;
}

void EditorWindow::LoadSettings()
{
	if (!j.empty())
		settings = j;
}

void EditorWindow::ShowSettingsWindow()
{
	ImGui::Begin("Settings", &showSettingsWindow);

	if (ImGui::BeginTable("SettingsTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoHostExtendX)) {
		ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch, 0.3f);
		ImGui::TableSetupColumn("##Settings", ImGuiTableColumnFlags_WidthStretch, 0.7f);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		const char* options[] = { "General", "Flags" };
		for (int i = 0; i < IM_ARRAYSIZE(options); ++i) {
			if (ImGui::Selectable(options[i], settingsSelectedCategory == options[i])) {
				settingsSelectedCategory = options[i];
			}
		}

		ImGui::TableSetColumnIndex(1);

		if (settingsSelectedCategory == "General") {
			ImGui::Checkbox("Auto-apply changes", &settings.autoApplyChanges);
			AddTooltip("Automatically apply changes to weather/lighting when editing");

			ImGui::Checkbox("Suppress delete warnings", &settings.suppressDeleteWarning);
			AddTooltip("Don't show confirmation dialog when deleting saved files");

			ImGui::Checkbox("Use text buttons instead of icons", &settings.useTextButtons);
			AddTooltip("Display action buttons as text labels instead of icons");

			ImGui::Checkbox("Enable 'Inherit From Parent' feature", &settings.enableInheritFromParent);
			AddTooltip("Show checkboxes to copy settings from parent weather (editor-only feature)");

			ImGui::Separator();
			ImGui::TextUnformatted("UI Scale");
			ImGui::Spacing();

			if (ImGui::SliderFloat("Editor UI Scale", &settings.editorUIScale, 0.5f, 2.0f, "%.2f")) {
				Save();
			}
			AddTooltip("Scale the size of all editor UI elements (0.5 = 50%, 2.0 = 200%)");

			if (ImGui::Button("Reset to 1.0")) {
				settings.editorUIScale = 1.0f;
				Save();
			}
			ImGui::SameLine();
			AddTooltip("Reset UI scale to default (100%)");

			ImGui::Separator();
			ImGui::TextUnformatted("Session & History");
			ImGui::Spacing();

			ImGui::Checkbox("Remember open widgets", &settings.rememberOpenWidgets);
			AddTooltip("Automatically reopen widgets that were open when you last closed the editor");

			ImGui::SliderInt("Max recent widgets", &settings.maxRecentWidgets, 5, 20);
			AddTooltip("Maximum number of recent widgets to remember");

			if (ImGui::Button("Clear Recent History")) {
				settings.recentWidgets.clear();
				Save();
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear Favorites")) {
				settings.favoriteWidgets.clear();
				Save();
			}

		} else if (settingsSelectedCategory == "Flags") {
			if (ImGui::BeginTable("FlagsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Colour", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);

				auto& recordMarkers = settings.recordMarkers;

				// Store markers to delete (can't delete while iterating)
				static std::string markerToDelete;
				markerToDelete.clear();

				// Store rename info (old name -> new name)
				static std::pair<std::string, std::string> renameInfo;
				static bool needsRename = false;

				// Store separate buffers for each marker
				static std::unordered_map<std::string, std::array<char, 256>> labelBuffers;

				for (auto& recordMarker : recordMarkers) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);

					// Editable label - use separate buffer for each marker
					auto& labelBuffer = labelBuffers[recordMarker.first];
					if (labelBuffer[0] == '\0' || labelBuffers.find(recordMarker.first) == labelBuffers.end()) {
						strncpy_s(labelBuffer.data(), labelBuffer.size(), recordMarker.first.c_str(), labelBuffer.size() - 1);
						labelBuffer[labelBuffer.size() - 1] = '\0';
					}

					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputText(std::format("##Label{}", recordMarker.first).c_str(), labelBuffer.data(), labelBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
						// Mark for rename only on Enter
						renameInfo = { recordMarker.first, std::string(labelBuffer.data()) };
						needsRename = true;
					}

					ImGui::TableSetColumnIndex(1);
					if (ImGui::ColorEdit3(std::format("Color##{}", recordMarker.first).c_str(), (float*)&recordMarker.second)) {
						Save();
					}

					ImGui::TableSetColumnIndex(2);
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
					if (ImGui::Button(std::format("Delete##{}", recordMarker.first).c_str(), ImVec2(-1, 0))) {
						markerToDelete = recordMarker.first;
					}
					ImGui::PopStyleColor();
				}

				// Process rename
				if (needsRename && renameInfo.first != renameInfo.second && !renameInfo.second.empty()) {
					// Check if new name doesn't already exist
					if (recordMarkers.find(renameInfo.second) == recordMarkers.end()) {
						auto color = recordMarkers[renameInfo.first];
						recordMarkers.erase(renameInfo.first);
						recordMarkers[renameInfo.second] = color;

						// Update any records that were using the old marker name
						for (auto& [recordId, markerName] : settings.markedRecords) {
							if (markerName == renameInfo.first) {
								markerName = renameInfo.second;
							}
						}

						Save();
					}
					needsRename = false;
				}

				// Process deletion
				if (!markerToDelete.empty()) {
					recordMarkers.erase(markerToDelete);

					// Remove any records that were using this marker
					for (auto it = settings.markedRecords.begin(); it != settings.markedRecords.end();) {
						if (it->second == markerToDelete) {
							it = settings.markedRecords.erase(it);
						} else {
							++it;
						}
					}

					Save();
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				if (recordMarkers.size() < maxRecordMarkers && ImGui::Selectable("Add new marker")) {
					recordMarkers.insert({ std::format("New marker {}", recordMarkers.size()), { 0.5f, 0.5f, 0.5f, 1.0f } });
					Save();
				}

				ImGui::EndTable();
			}
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

void EditorWindow::Save()
{
	SaveSettings();
	const std::string filePath = Util::PathHelpers::GetCommunityShaderPath().string();
	const std::string file = std::format("{}\\{}.json", filePath, settingsFilename);

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

	logger::info("Saving settings file: {}", file);

	try {
		settingsFile << j.dump(1);

		settingsFile.close();
	} catch (const nlohmann::json::parse_error& e) {
		logger::warn("Error parsing settings for settings file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
	}
}

void EditorWindow::Load()
{
	std::string filePath = std::format("{}\\{}.json", Util::PathHelpers::GetCommunityShaderPath().string(), settingsFilename);

	std::ifstream settingsFile(filePath);

	if (!std::filesystem::exists(filePath)) {
		// Does not have any settings so just return.
		return;
	}

	if (!settingsFile.good() || !settingsFile.is_open()) {
		logger::warn("Failed to load settings file: {}", filePath);
		return;
	}

	try {
		j << settingsFile;
		settingsFile.close();
	} catch (const nlohmann::json::parse_error& e) {
		logger::warn("Error parsing settings for file ({}) : {}\n", filePath, e.what());
		settingsFile.close();
	}
	LoadSettings();
}

void EditorWindow::LockWeather(RE::TESWeather* weather)
{
	if (!weather)
		return;

	auto sky = RE::Sky::GetSingleton();
	if (!sky)
		return;

	// Force the weather to be active
	sky->ForceWeather(weather, false);

	lockedWeather = weather;
	weatherLockActive = true;

	logger::info("Weather locked: {}", weather->GetFormEditorID() ? weather->GetFormEditorID() : "Unknown");
}

void EditorWindow::UnlockWeather()
{
	if (!weatherLockActive)
		return;

	auto sky = RE::Sky::GetSingleton();
	if (sky) {
		// Release weather override to allow natural progression
		sky->ReleaseWeatherOverride();
	}

	logger::info("Weather unlocked: {}", lockedWeather && lockedWeather->GetFormEditorID() ? lockedWeather->GetFormEditorID() : "Unknown");

	lockedWeather = nullptr;
	weatherLockActive = false;
}

void EditorWindow::PauseTime()
{
	if (timePaused)
		return;

	auto calendar = RE::Calendar::GetSingleton();
	if (calendar && calendar->timeScale) {
		savedTimeScale = calendar->timeScale->value;
		calendar->timeScale->value = 0.0f;
		timePaused = true;
		logger::info("Time paused (saved timescale: {})", savedTimeScale);
	}
}

void EditorWindow::ResumeTime()
{
	if (!timePaused)
		return;

	auto calendar = RE::Calendar::GetSingleton();
	if (calendar && calendar->timeScale) {
		calendar->timeScale->value = savedTimeScale;
		timePaused = false;
		logger::info("Time resumed (timescale: {})", savedTimeScale);
	}
}

void EditorWindow::DisableVanityCamera()
{
	if (vanityCameraDisabled)
		return;

	auto setting = RE::GetINISetting("fAutoVanityModeDelay:Camera");
	if (setting) {
		savedVanityCameraDelay = setting->GetFloat();
		setting->data.f = 10000.0f;
		vanityCameraDisabled = true;
		logger::info("Vanity camera disabled (saved delay: {})", savedVanityCameraDelay);
	}
}

void EditorWindow::RestoreVanityCamera()
{
	if (!vanityCameraDisabled)
		return;

	auto setting = RE::GetINISetting("fAutoVanityModeDelay:Camera");
	if (setting) {
		setting->data.f = savedVanityCameraDelay;
		vanityCameraDisabled = false;
		logger::info("Vanity camera restored (delay: {})", savedVanityCameraDelay);
	}
}

void EditorWindow::PushUndoState(Widget* widget)
{
	if (!widget)
		return;

	UndoState state;
	state.widget = widget;
	state.widgetId = widget->GetEditorID();
	state.settings = widget->js;

	undoStack.push_back(state);

	if (undoStack.size() > maxUndoStates) {
		undoStack.erase(undoStack.begin());
	}
}

void EditorWindow::PerformUndo()
{
	if (undoStack.empty())
		return;

	UndoState state = undoStack.back();
	undoStack.pop_back();

	if (!state.widget) {
		for (auto& w : weatherWidgets) {
			if (w->GetEditorID() == state.widgetId) {
				state.widget = w.get();
				break;
			}
		}
		if (!state.widget) {
			for (auto& w : imageSpaceWidgets) {
				if (w->GetEditorID() == state.widgetId) {
					state.widget = w.get();
					break;
				}
			}
		}
		if (!state.widget) {
			for (auto& w : lightingTemplateWidgets) {
				if (w->GetEditorID() == state.widgetId) {
					state.widget = w.get();
					break;
				}
			}
		}
	}

	if (state.widget) {
		state.widget->js = state.settings;
		state.widget->LoadSettings();
		state.widget->ApplyChanges();
		ShowNotification(
			std::format("Undone changes to {}", state.widgetId),
			ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
			2.0f);
	}
}

void EditorWindow::ShowNotification(const std::string& message, const ImVec4& color, float duration)
{
	Notification notif;
	notif.message = message;
	notif.color = color;
	notif.startTime = static_cast<float>(ImGui::GetTime());
	notif.duration = duration;
	notifications.push_back(notif);
}

void EditorWindow::RenderNotifications()
{
	float currentTime = static_cast<float>(ImGui::GetTime());
	float yOffset = 10.0f;

	// Remove expired notifications
	notifications.erase(
		std::remove_if(notifications.begin(), notifications.end(),
			[currentTime](const Notification& n) { return currentTime - n.startTime > n.duration; }),
		notifications.end());

	// Render active notifications
	for (auto& notif : notifications) {
		float elapsed = currentTime - notif.startTime;
		float fadeStart = notif.duration - 0.5f;  // Start fading 0.5s before end
		float alpha = 1.0f;

		// Fade out in the last 0.5 seconds
		if (elapsed > fadeStart) {
			alpha = 1.0f - ((elapsed - fadeStart) / 0.5f);
		}

		// Position in top-left corner
		ImGui::SetNextWindowPos(ImVec2(10.0f, yOffset), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.8f * alpha);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f, 10.0f));

		if (ImGui::Begin(std::format("##Notification{}", (void*)&notif).c_str(),
				nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
			ImVec4 colorWithAlpha = notif.color;
			colorWithAlpha.w *= alpha;
			ImGui::PushStyleColor(ImGuiCol_Text, colorWithAlpha);
			ImGui::TextUnformatted(notif.message.c_str());
			ImGui::PopStyleColor();

			yOffset += ImGui::GetWindowSize().y + 5.0f;
		}
		ImGui::End();

		ImGui::PopStyleVar(2);
	}
}

void EditorWindow::AddToRecent(const std::string& widgetId, const std::string& category)
{
	auto& categoryRecent = settings.recentWidgets[category];

	// Remove if already exists
	auto it = std::find(categoryRecent.begin(), categoryRecent.end(), widgetId);
	if (it != categoryRecent.end()) {
		categoryRecent.erase(it);
	}

	// Add to front
	categoryRecent.insert(categoryRecent.begin(), widgetId);

	// Limit size
	if (categoryRecent.size() > static_cast<size_t>(settings.maxRecentWidgets)) {
		categoryRecent.resize(settings.maxRecentWidgets);
	}

	SaveSettings();
}

void EditorWindow::ToggleFavorite(const std::string& widgetId)
{
	auto it = std::find(settings.favoriteWidgets.begin(), settings.favoriteWidgets.end(), widgetId);
	if (it != settings.favoriteWidgets.end()) {
		settings.favoriteWidgets.erase(it);
	} else {
		settings.favoriteWidgets.push_back(widgetId);
	}
	SaveSettings();
}

bool EditorWindow::IsFavorite(const std::string& widgetId) const
{
	return std::find(settings.favoriteWidgets.begin(), settings.favoriteWidgets.end(), widgetId) != settings.favoriteWidgets.end();
}

void EditorWindow::SaveSessionWidgets()
{
	settings.lastOpenWidgets.clear();

	// Save all currently open widgets
	for (auto& widget : weatherWidgets) {
		if (widget->IsOpen()) {
			settings.lastOpenWidgets.push_back(widget->GetEditorID());
		}
	}
	for (auto& widget : worldSpaceWidgets) {
		if (widget->IsOpen()) {
			settings.lastOpenWidgets.push_back(widget->GetEditorID());
		}
	}
	for (auto& widget : lightingTemplateWidgets) {
		if (widget->IsOpen()) {
			settings.lastOpenWidgets.push_back(widget->GetEditorID());
		}
	}

	SaveSettings();
}

void EditorWindow::RestoreSessionWidgets()
{
	if (!settings.rememberOpenWidgets || settings.lastOpenWidgets.empty()) {
		return;
	}

	// Open widgets that were open in last session
	for (const auto& widgetId : settings.lastOpenWidgets) {
		// Search in all widget collections
		for (auto& widget : weatherWidgets) {
			if (widget->GetEditorID() == widgetId) {
				widget->SetOpen(true);
				break;
			}
		}
		for (auto& widget : worldSpaceWidgets) {
			if (widget->GetEditorID() == widgetId) {
				widget->SetOpen(true);
				break;
			}
		}
		for (auto& widget : lightingTemplateWidgets) {
			if (widget->GetEditorID() == widgetId) {
				widget->SetOpen(true);
				break;
			}
		}
	}
}
