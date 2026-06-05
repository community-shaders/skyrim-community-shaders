#include "EditorWindow.h"

#include "../I18n/I18n.h"
#include "Features/CSEditor.h"
#include "Features/HDRDisplay.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "InteriorOnlyPanel.h"
#include "Menu.h"
#include "Menu/BackgroundBlur.h"
#include "PaletteWindow.h"
#include "State.h"
#include "Utils/UI.h"
#include "Weather/LightingTemplateWidget.h"
#include "WeatherUtils.h"
#include "imgui_internal.h"

#define I18N_KEY_PREFIX "cs_editor."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings::PaletteColorEntry, r, g, b, useCount, lastUsedTime, isFavorite)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings::PaletteFavoriteColor, hasValue, r, g, b)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings, recordMarkers, markedRecords, autoApplyChanges, useTextButtons, enableInheritFromParent, editorUIScale, favoriteWidgets, recentWidgets, maxRecentWidgets, showViewport, widgetTypeSizes, paletteColors, paletteFavorites)

void DrawIconStar(ImVec2 center, float radius, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	constexpr int numPoints = 5;
	const float angleStep = IM_PI / numPoints;
	ImVec2 points[10];

	for (int i = 0; i < numPoints * 2; i++) {
		float angle = -IM_PI * 0.5f + i * angleStep;
		float r = (i % 2 == 0) ? radius : radius * 0.38f;
		points[i] = ImVec2(center.x + cosf(angle) * r, center.y + sinf(angle) * r);
	}

	if (filled) {
		// Disable AA fill temporarily — ImGui adds a 1px fringe around each filled shape
		// that creates visible seams at the pentagon/triangle boundaries.
		ImDrawListFlags oldFlags = drawList->Flags;
		drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;

		ImVec2 innerPentagon[5];
		for (int i = 0; i < 5; i++) {
			innerPentagon[i] = points[i * 2 + 1];  // inner points
		}
		drawList->AddConvexPolyFilled(innerPentagon, 5, color);
		for (int i = 0; i < 5; i++) {
			drawList->AddTriangleFilled(points[i * 2], points[i * 2 + 1], points[(i * 2 + 9) % 10], color);
		}

		drawList->Flags = oldFlags;

		// Draw an AA polyline over the outer perimeter to restore smooth edges
		drawList->AddPolyline(points, 10, color, ImDrawFlags_Closed, 1.5f);
	} else {
		drawList->AddPolyline(points, 10, color, ImDrawFlags_Closed, 1.5f);
	}
}

void DrawIconCircle(ImVec2 center, float radius, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	if (filled)
		drawList->AddCircleFilled(center, radius, color, 16);
	else
		drawList->AddCircle(center, radius, color, 16, 1.5f * Util::GetUIScale());
}

void DrawIconWave(ImVec2 center, float width, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	const float thickness = (filled ? 3.0f : 1.5f) * Util::GetUIScale();
	const int segments = 8;
	const float amplitude = width * 0.15f;
	const float waveWidth = width * 0.8f;
	const float segmentWidth = waveWidth / segments;
	ImVec2 start(center.x - waveWidth * 0.5f, center.y);

	for (int i = 0; i < segments; i++) {
		float x1 = start.x + i * segmentWidth;
		float x2 = start.x + (i + 1) * segmentWidth;
		float y1 = start.y + sinf(i * 3.14159f / 2.0f) * amplitude;
		float y2 = start.y + sinf((i + 1) * 3.14159f / 2.0f) * amplitude;
		drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color, thickness);
	}
}

bool IconButton(const char* label, bool filled, const char* iconType)
{
	ImVec2 buttonSize(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
	ImVec2 cursorPos = ImGui::GetCursorScreenPos();

	bool result = ImGui::InvisibleButton(label, buttonSize);

	ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);

	auto* drawList = ImGui::GetWindowDrawList();
	const ImVec2 buttonMax(cursorPos.x + buttonSize.x, cursorPos.y + buttonSize.y);
	drawList->AddRectFilled(cursorPos, buttonMax, ImGui::GetColorU32(ImGuiCol_Button), ImGui::GetStyle().FrameRounding);
	Util::DrawCurrentItemRoundedButtonHighlight(drawList);

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

namespace
{
	const char* GetFilterColumnName(int index)
	{
		switch (index) {
		case 0:
			return T(TKEY("filter_all"), "All");
		case 1:
			return T(TKEY("filter_editor_id"), "Editor ID");
		case 2:
			return T(TKEY("filter_form_id"), "Form ID");
		case 3:
			return T(TKEY("filter_file"), "File");
		case 4:
			return T(TKEY("filter_status"), "Status");
		default:
			return "";
		}
	}

	constexpr int kFilterColumnCount = 5;
}  // namespace

void EditorWindow::ResetObjectsFilter()
{
	m_currentFilterColumn = FilterColumn::All;
	m_filterBuffer[0] = '\0';
	m_showOnlyFlagged = false;
	m_showOnlyFavorites = false;
}

bool EditorWindow::MatchesObjectFilter(Widget* w) const
{
	static_assert(static_cast<int>(FilterColumn::Count_) == kFilterColumnCount,
		"kFilterColumnCount must match FilterColumn enum");
	if (!w)
		return false;
	if (m_filterBuffer[0] == '\0')
		return true;
	switch (m_currentFilterColumn) {
	case FilterColumn::EditorID:
		return ContainsStringIgnoreCase(w->GetEditorID(), m_filterBuffer);
	case FilterColumn::FormID:
		return ContainsStringIgnoreCase(w->GetFormID(), m_filterBuffer);
	case FilterColumn::File:
		return ContainsStringIgnoreCase(w->GetFilename(), m_filterBuffer);
	case FilterColumn::Status:
		{
			auto it = settings.markedRecords.find(w->GetEditorID());
			return it != settings.markedRecords.end() && ContainsStringIgnoreCase(it->second, m_filterBuffer);
		}
	case FilterColumn::All:
	default:
		{
			const auto editorId = w->GetEditorID();
			if (ContainsStringIgnoreCase(editorId, m_filterBuffer))
				return true;
			if (ContainsStringIgnoreCase(w->GetFormID(), m_filterBuffer))
				return true;
			if (ContainsStringIgnoreCase(w->GetFilename(), m_filterBuffer))
				return true;
			auto it = settings.markedRecords.find(editorId);
			if (it != settings.markedRecords.end() && ContainsStringIgnoreCase(it->second, m_filterBuffer))
				return true;
			return false;
		}
	}
}

std::string EditorWindow::ResolveEditorId(RE::TESForm* form, const WidgetVec& widgets)
{
	if (!form)
		return "";
	for (const auto& widget : widgets) {
		if (widget->form && widget->form->GetFormID() == form->GetFormID())
			return widget->GetEditorID();
	}
	const char* editorid = form->GetFormEditorID();
	return editorid ? editorid : std::format("0x{:08X}", form->GetFormID());
}

void EditorWindow::ShowObjectsWindow()
{
	Util::BeginWithRoundedClose(T(TKEY("weather_lighting_browser"), "CS Editor Browser"), nullptr);

	// Reset filter state when the user switches categories so stale column
	// selections (e.g. Status) don't hide all items in the new category.
	if (m_selectedCategory != m_previousSelectedCategory) {
		ResetObjectsFilter();
		m_previousSelectedCategory = m_selectedCategory;
	}

	// Create a table with two columns
	if (ImGui::BeginTable("ObjectTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner)) {
		// Fixed categories column, objects column fills remaining width
		const float categoriesWidth = 180.0f * Util::GetUIScale();
		ImGui::TableSetupColumn(T(TKEY("categories"), "Categories"), ImGuiTableColumnFlags_WidthFixed, categoriesWidth);
		ImGui::TableSetupColumn(T(TKEY("objects"), "Objects"), ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();

		if (resetLayout)
			ImGui::TableSetColumnWidth(0, categoriesWidth);

		// Left column: Categories
		ImGui::TableSetColumnIndex(0);

		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
		if (ImGui::BeginListBox("##CategoriesList", { -FLT_MIN, -FLT_MIN })) {
			ImGui::Text("%s", T(TKEY("categories"), "Categories"));
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			struct CategoryOption
			{
				const char* id;
				const char* label;
			};
			const CategoryOption categories[] = {
				{ "Weather", T(TKEY("category_weather"), "Weather") },
				{ "ImageSpace", T(TKEY("category_imagespace"), "ImageSpace") },
				{ "Lighting Template", T(TKEY("category_lighting_template"), "Lighting Template") },
				{ "Cell Lighting", T(TKEY("category_cell_lighting"), "Cell Lighting") },
				{ "Volumetric Lighting", T(TKEY("category_volumetric_lighting"), "Volumetric Lighting") },
				{ "Shader Particle Geometry", T(TKEY("category_shader_particle"), "Shader Particle Geometry") },
				{ "Lens Flare", T(TKEY("category_lens_flare"), "Lens Flare") },
				{ "Visual Effect", T(TKEY("category_visual_effect"), "Visual Effect") },
				{ "Interior Only", T(TKEY("category_interior_only"), "Interior Only") },
				{ "Light Editor", T(TKEY("category_lighting_editor"), "Light Editor") }
			};
			for (int i = 0; i < IM_ARRAYSIZE(categories); ++i) {
				// Highlight the selected category
				if (ImGui::Selectable(categories[i].label, m_selectedCategory == categories[i].id)) {
					m_selectedCategory = categories[i].id;  // Keep the stable English ID internally
				}
			}
			ImGui::EndListBox();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		// Right column: Objects
		ImGui::TableSetColumnIndex(1);

		if (ImGui::BeginChild("##ObjectsContent", { 0, 0 }, ImGuiChildFlags_Borders, kStickyHeaderFlags)) {
			// Interior Only category has its own panel
			if (m_selectedCategory == "Interior Only") {
				InteriorOnlyPanel::Draw();
				ImGui::EndChild();
				ImGui::EndTable();
				ImGui::End();
				return;
			}

			if (m_selectedCategory == "Light Editor") {
				BeginScrollableContent("##LightEditorScroll");
				lightEditor.DrawSettings();
				EndScrollableContent();
				ImGui::EndChild();
				ImGui::EndTable();
				ImGui::End();
				return;
			}

			// Returns the widget collection for a given category; Cell Lighting and unknown
			// categories return an empty collection since they have no standalone widget list.
			auto getWidgetsForCategory = [&](const std::string& cat) -> const std::vector<std::unique_ptr<Widget>>& {
				static const std::vector<std::unique_ptr<Widget>> emptyWidgets;
				if (cat == "Weather")
					return weatherWidgets;
				if (cat == "Lighting Template")
					return lightingTemplateWidgets;
				if (cat == "ImageSpace")
					return imageSpaceWidgets;
				if (cat == "Volumetric Lighting")
					return volumetricLightingWidgets;
				if (cat == "Shader Particle Geometry")
					return precipitationWidgets;
				if (cat == "Lens Flare")
					return lensFlareWidgets;
				if (cat == "Visual Effect")
					return referenceEffectWidgets;
				return emptyWidgets;
			};

			// Build active records for the current category tab
			struct ActiveRecord
			{
				std::string label;
				std::string suffix;
				RE::FormID formId;
				std::function<void()> open;
			};
			std::vector<ActiveRecord> activeRecords;

			{
				auto* sky = globals::game::sky;
				auto* weather = sky ? sky->currentWeather : nullptr;

				auto openByFormId = [](RE::FormID id, const WidgetVec* widgets) -> std::function<void()> {
					return [id, widgets]() {
						for (const auto& widget : *widgets) {
							if (widget->form && widget->form->GetFormID() == id) {
								widget->SetOpen(true);
								widget->RequestFocus();
								break;
							}
						}
					};
				};

				auto addSingle = [&](RE::TESForm* form, const WidgetVec& widgets, std::string suffix = "") {
					if (!form)
						return;
					auto id = form->GetFormID();
					activeRecords.push_back({ ResolveEditorId(form, widgets), std::move(suffix), id, openByFormId(id, &widgets) });
				};

				auto addTOD = [&](auto*(&fields)[RE::TESWeather::ColorTimes::kTotal], const WidgetVec& widgets) {
					for (int tod = 0; tod < RE::TESWeather::ColorTimes::kTotal; ++tod) {
						auto* form = fields[tod];
						if (!form)
							continue;
						auto id = form->GetFormID();
						bool already = std::any_of(activeRecords.begin(), activeRecords.end(),
							[&](const ActiveRecord& r) { return r.formId == id; });
						if (!already)
							activeRecords.push_back({ ResolveEditorId(form, widgets), TOD::GetPeriodName(tod), id, openByFormId(id, &widgets) });
					}
				};

				auto addWeather = [&](RE::TESWeather* weatherRecord, std::string suffix = "") {
					if (!weatherRecord)
						return;
					auto id = weatherRecord->GetFormID();
					activeRecords.push_back({ ResolveEditorId(weatherRecord, weatherWidgets), std::move(suffix), id, openByFormId(id, &weatherWidgets) });
				};

				if (m_selectedCategory == "Weather") {
					addWeather(weather);
					if (sky && sky->lastWeather != weather)
						addWeather(sky->lastWeather, T(TKEY("transitioning"), "transitioning"));
				} else if (m_selectedCategory == "ImageSpace") {
					if (weather)
						addTOD(weather->imageSpaces, imageSpaceWidgets);
				} else if (m_selectedCategory == "Lighting Template") {
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (player && player->parentCell)
						addSingle(player->parentCell->GetRuntimeData().lightingTemplate, lightingTemplateWidgets);
				} else if (m_selectedCategory == "Cell Lighting") {
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (player && player->parentCell && player->parentCell->IsInteriorCell()) {
						auto* cell = player->parentCell;
						const char* cellName = cell->GetName();
						std::string displayName = cellName && cellName[0] ? cellName : T(TKEY("unnamed_cell"), "[Unnamed Cell]");
						activeRecords.push_back({ std::move(displayName), "", cell->GetFormID(),
							[this, cell]() {
								if (currentCellLightingWidget && currentCellLightingWidget->cell == cell) {
									currentCellLightingWidget->SetOpen(true);
									currentCellLightingWidget->RequestFocus();
								} else {
									currentCellLightingWidget = std::make_unique<CellLightingWidget>(cell);
									currentCellLightingWidget->CacheFormData();
									currentCellLightingWidget->Load(false);
									currentCellLightingWidget->SetOpen(true);
									currentCellLightingWidget->RequestFocus();
								}
							} });
					}
				} else if (m_selectedCategory == "Volumetric Lighting") {
					if (weather)
						addTOD(weather->volumetricLighting, volumetricLightingWidgets);
				} else if (m_selectedCategory == "Shader Particle Geometry") {
					if (weather)
						addSingle(weather->precipitationData, precipitationWidgets);
				} else if (m_selectedCategory == "Lens Flare") {
					if (weather)
						addSingle(weather->sunGlareLensFlare, lensFlareWidgets);
				} else if (m_selectedCategory == "Visual Effect") {
					if (weather)
						addSingle(weather->referenceEffect, referenceEffectWidgets);
				}

				// Fall back to current weather when the active category has no active record
				if (activeRecords.empty())
					addWeather(weather);
			}

			if (activeRecords.size() > 4)
				activeRecords.resize(4);

			if (!activeRecords.empty()) {
				const auto& theme = Menu::GetSingleton()->GetTheme();

				ImGui::PushStyleColor(ImGuiCol_Text, theme.StatusPalette.RestartNeeded);
				ImGui::Text("%s", T(TKEY("active"), "Active:"));
				ImGui::PopStyleColor();
				ImGui::SameLine();
				const float recordX = ImGui::GetCursorPosX();

				for (int i = 0; i < (int)activeRecords.size(); ++i) {
					if (i > 0) {
						ImGui::NewLine();
						ImGui::SameLine(recordX);
					}
					const auto& rec = activeRecords[i];
					ImGui::TextColored(theme.Palette.Text, "%s", rec.label.c_str());
					ImGui::SameLine();
					if (!rec.suffix.empty()) {
						ImGui::TextDisabled("(%s)", rec.suffix.c_str());
						ImGui::SameLine();
					}
					ImGui::TextDisabled("(0x%08X)", rec.formId);
					ImGui::SameLine();
					char btnId[32];
					snprintf(btnId, sizeof(btnId), "%s##active_%d", T(TKEY("open"), "Open"), i);
					if (ImGui::SmallButton(btnId))
						rec.open();
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
			// Compute fixed widths once; reuse for both the search bar and the following combo.
			const auto& style = ImGui::GetStyle();
			// comboW = preview text + left/right padding + arrow button
			const float comboW = ImGui::CalcTextSize("Editor ID").x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
			const float helpW = ImGui::CalcTextSize("(?)").x;
			const float iconW = ImGui::GetFrameHeight();
			const float scale = Util::GetUIScale();
			const float spacerW = 10.0f * scale;
			// Fixed width is the sum of every item that follows the search bar on the same row.
			// Each SameLine() contributes style.ItemSpacing.x; widths are listed explicitly
			// so adding or removing a widget only requires updating its own expression.
			const char* favoritesText = T(TKEY("favorites"), "Favorites");
			const char* flaggedText = T(TKEY("flagged"), "Flagged");
			const float fixedW =
				style.ItemSpacing.x + comboW +                                // combo
				style.ItemSpacing.x + helpW +                                 // help marker
				style.ItemSpacing.x + spacerW +                               // spacer before favorites
				style.ItemSpacing.x + iconW +                                 // fav icon
				style.ItemSpacing.x + ImGui::CalcTextSize(favoritesText).x +  // "Favorites" label
				style.ItemSpacing.x + spacerW +                               // spacer before flagged
				style.ItemSpacing.x + iconW +                                 // flag icon
				style.ItemSpacing.x + ImGui::CalcTextSize(flaggedText).x;     // "Flagged" label
			ImGui::SetNextItemWidth(std::max(50.0f, ImGui::GetContentRegionAvail().x - fixedW));
			ImGui::InputTextWithHint("##ObjectFilter", T(TKEY("filter_hint"), "Filter... (Ctrl+F)"), m_filterBuffer, sizeof(m_filterBuffer));

			ImGui::SameLine();
			ImGui::SetNextItemWidth(comboW);
			int col = static_cast<int>(m_currentFilterColumn);
			if (ImGui::Combo("##FilterBy", &col, [](void*, int idx, const char** out) -> bool {
					*out = GetFilterColumnName(idx);
					return true; }, nullptr, kFilterColumnCount))
				m_currentFilterColumn = static_cast<FilterColumn>(col);

			ImGui::SameLine();
			Util::HelpMarker(T(TKEY("filter_help"),
				"Filter the object list by the selected column.\n"
				"All: searches Editor ID, Form ID, File, and Status.\n"
				"Status: hides items with no status marker when the search box is non-empty.\n"
				"Ctrl+F: Focus search\n"
				"Enter: Open selected"));

			// Quick filter buttons
			const ImVec2 filterSpacer(spacerW, 0.0f);
			ImGui::SameLine();
			ImGui::Dummy(filterSpacer);
			ImGui::SameLine();
			if (IconButton("##filterFavorites", m_showOnlyFavorites, "star")) {
				m_showOnlyFavorites = !m_showOnlyFavorites;
			}
			ImGui::SameLine();
			ImGui::Text("%s", favoritesText);

			ImGui::SameLine();
			ImGui::Dummy(filterSpacer);
			ImGui::SameLine();
			if (IconButton("##filterFlagged", m_showOnlyFlagged, "circle")) {
				m_showOnlyFlagged = !m_showOnlyFlagged;
			}
			ImGui::SameLine();
			ImGui::Text("%s", flaggedText);

			// Show recent widgets section for current category
			auto recentIt = settings.recentWidgets.find(m_selectedCategory);
			if (recentIt != settings.recentWidgets.end() && !recentIt->second.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor, "%s", T(TKEY("recent"), "Recent:"));
				ImGui::SameLine();
				for (size_t i = 0; i < std::min(size_t(5), recentIt->second.size()); ++i) {
					if (i > 0)
						ImGui::SameLine();
					if (ImGui::SmallButton(recentIt->second[i].c_str())) {
						// Find and open widget in current category's collection
						const auto& widgets = getWidgetsForCategory(m_selectedCategory);
						for (auto& widget : widgets) {
							if (widget->GetEditorID() == recentIt->second[i]) {
								widget->SetOpen(true);
								break;
							}
						}
					}
				}
			}

			// Scrollable area for the object table
			BeginScrollableContent("##ObjectsScrollable");

			// Stable user IDs for sortable columns — used instead of ColumnIndex so reordering/insertion won't break sorting.
			enum ColumnID : ImGuiID
			{
				ColFav = 0,
				ColEditorID,
				ColFormID,
				ColFile,
				ColStatus,
				ColJson
			};

			// Create a table for the right column with "Name" and "ID" headers. Different weights to prevent truncation.
			if (ImGui::BeginTable("DetailsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable)) {
				ImGui::TableSetupColumn(T(TKEY("fav"), "Fav"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 38.0f * scale, ColFav);  // Favorite indicator
				ImGui::TableSetupColumn(T(TKEY("editor_id"), "Editor ID"), ImGuiTableColumnFlags_WidthStretch, 3.5f, ColEditorID);                       // Largest - weather/template names
				ImGui::TableSetupColumn(T(TKEY("form_id"), "Form ID"), ImGuiTableColumnFlags_WidthFixed, 90.0f * scale, ColFormID);                      // Fixed - 8 hex chars
				ImGui::TableSetupColumn(T(TKEY("file"), "File"), ImGuiTableColumnFlags_WidthStretch, 2.0f, ColFile);                                     // Medium - plugin names
				ImGui::TableSetupColumn(T(TKEY("status"), "Status"), ImGuiTableColumnFlags_WidthStretch, 1.5f, ColStatus);                               // Smaller - status text
				ImGui::TableSetupColumn(T(TKEY("json"), "json"), ImGuiTableColumnFlags_WidthFixed, 55.0f * scale, ColJson);                              // JSON file / delete

				ImGui::TableHeadersRow();

				// Handle column sorting
				if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
					if (sortSpecs->SpecsDirty) {
						if (sortSpecs->SpecsCount > 0) {
							const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
							switch (spec.ColumnUserID) {
							case ColEditorID:
								currentSortColumn = SortColumn::EditorID;
								break;
							case ColFormID:
								currentSortColumn = SortColumn::FormID;
								break;
							case ColFile:
								currentSortColumn = SortColumn::File;
								break;
							case ColStatus:
								currentSortColumn = SortColumn::Status;
								break;
							case ColJson:
								currentSortColumn = SortColumn::JsonAttachment;
								break;
							default:
								currentSortColumn = SortColumn::None;
								break;
							}
							sortAscending = (spec.SortDirection == ImGuiSortDirection_Ascending);
						} else {
							currentSortColumn = SortColumn::None;
						}
						sortSpecs->SpecsDirty = false;
					}
				}

				// Display objects based on the selected category
				const auto& widgets = getWidgetsForCategory(m_selectedCategory);
				// Sort widgets based on current sort column
				std::vector<Widget*> sortedWidgets;
				sortedWidgets.reserve(widgets.size());
				for (const auto& w : widgets) {
					sortedWidgets.push_back(w.get());
				}
				RefreshJsonAttachmentCache(sortedWidgets);
				bool weatherTooltipShownThisFrame = false;
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
						case SortColumn::JsonAttachment:
							{
								bool aHasJson = HasCachedJsonAttachment(a);
								bool bHasJson = HasCachedJsonAttachment(b);
								comparison = static_cast<int>(aHasJson) - static_cast<int>(bHasJson);
								break;
							}
						default:
							break;
						}
						return sortAscending ? (comparison < 0) : (comparison > 0);
					});
				}

				// Helper lambda: renders the JSON delete button column for a widget
				auto drawJsonDeleteButton = [&](Widget* widget) {
					ImGui::TableNextColumn();
					if (HasCachedJsonAttachment(widget)) {
						auto* menu = globals::menu;
						if (menu && menu->uiIcons.deleteSettings.texture) {
							const float iconSize = ImGui::GetFrameHeight() * 0.85f;
							ImGui::SetNextItemAllowOverlap();
							char idBuf[32];
							snprintf(idBuf, sizeof(idBuf), "##jsondel_%s", widget->GetFormID().c_str());
							if (Util::ErrorImageButton(idBuf, menu->uiIcons.deleteSettings.texture, { iconSize, iconSize })) {
								pendingDeleteWidget = widget;
								pendingDeletePopupRequested = true;
							}
							Util::AddTooltip(T(TKEY("delete_json_file"), "Delete JSON file"));
						}
					}
				};

				// Special handling for Cell Lighting category
				if (m_selectedCategory == "Cell Lighting") {
					auto player = RE::PlayerCharacter::GetSingleton();
					if (player && player->parentCell) {
						auto cell = player->parentCell;
						bool isInterior = cell->IsInteriorCell();

						if (isInterior) {
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);

							// No favorite star for cell lighting (it's always the current cell)
							ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
							ImGui::TableNextColumn();

							// Display current cell name
							const char* cellName = cell->GetName();
							std::string displayName = cellName && cellName[0] ? cellName : "[Unnamed Cell]";
							std::string label = displayName;

							// Highlight current cell (before TableRowSelectable so hover/active can override)
							auto highlightColor = Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor;
							highlightColor.w = 0.3f;
							ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(highlightColor));
							ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::ColorConvertFloat4ToU32(highlightColor));

							bool isOpen = currentCellLightingWidget && currentCellLightingWidget->IsOpen();
							if (Util::TableRowSelectable(label.c_str(), isOpen, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
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
							ImGui::Text("%s", T(TKEY("interior_cell"), "Interior Cell"));

							// json column (empty for cells - no standalone json)
							ImGui::TableNextColumn();
						} else {
							// Show message that cell lighting is only for interior cells
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(1);
							ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
							ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.Warning, "%s", T(TKEY("cell_lighting_interior_only"), "Cell Lighting is only available for interior cells."));
							ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.Disable, "%s", T(TKEY("currently_exterior_cell"), "You are currently in an exterior cell."));
							ImGui::PopTextWrapPos();
						}
					} else {
						// No player or cell
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(1);
						ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
						ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.Error, "%s", T(TKEY("player_cell_unavailable"), "Player cell not available."));
						ImGui::PopTextWrapPos();
					}
				}

				// Centralized filter check used by the display loop below.
				auto shouldShowWidget = [&](Widget* w) {
					if (!MatchesObjectFilter(w))
						return false;
					if (m_showOnlyFavorites && !IsFavorite(w->GetEditorID()))
						return false;
					if (m_showOnlyFlagged && settings.markedRecords.find(w->GetEditorID()) == settings.markedRecords.end())
						return false;
					return true;
				};

				// Filtered display of widgets
				for (int i = 0; i < sortedWidgets.size(); ++i) {
					if (!shouldShowWidget(sortedWidgets[i]))
						continue;

					auto editorLabel = sortedWidgets[i]->GetEditorID();
					auto markedRecord = settings.markedRecords.find(editorLabel);
					ImGui::PushID(sortedWidgets[i]->GetFormID().c_str());
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
					if (Util::TableRowSelectable(editorLabel.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap)) {
						if (ImGui::IsMouseDoubleClicked(0)) {
							sortedWidgets[i]->SetOpen(true);
							AddToRecent(sortedWidgets[i]->GetEditorID(), m_selectedCategory);
						}
					}

					// Show ImageSpace and VolumetricLighting info for weather widgets
					if (!weatherTooltipShownThisFrame && m_selectedCategory == "Weather" && ImGui::IsItemHovered()) {
						auto* weatherWidget = dynamic_cast<WeatherWidget*>(sortedWidgets[i]);
						if (weatherWidget && weatherWidget->weather) {
							const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
							const ImVec2 pad = ImGui::GetStyle().WindowPadding;
							const float spacingHeight = ImGui::GetStyle().ItemSpacing.y;
							constexpr int kSectionHeaders = 2;  // "ImageSpace:" + "Volumetric Lighting:"
							constexpr int kTodValuesPerSection = 4;
							constexpr int kSpacingSeparators = 1;  // Spacing between sections
							const float estimatedTooltipHeight = (kSectionHeaders + kTodValuesPerSection * 2) * lineHeight + kSpacingSeparators * spacingHeight + pad.y * 2.0f;
							Util::SetTooltipPositionNearMouse(estimatedTooltipHeight);
							if (ImGui::BeginTooltip()) {
								// ImageSpace info - use widget cache for proper editor IDs
								ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor, "%s", T(TKEY("imagespace_label"), "ImageSpace:"));
								for (int tod = 0; tod < 4; tod++) {
									auto name = ResolveEditorId(weatherWidget->weather->imageSpaces[tod], imageSpaceWidgets);
									ImGui::Text("  %s: %s", TOD::GetPeriodName(tod), name.empty() ? T(TKEY("none_filter"), "None") : name.c_str());
								}

								ImGui::Spacing();

								// VolumetricLighting info - show short local FormID only
								ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor, "%s", T(TKEY("volumetric_lighting_label"), "Volumetric Lighting:"));
								for (int tod = 0; tod < 4; tod++) {
									auto* f = weatherWidget->weather->volumetricLighting[tod];
									ImGui::Text("  %s: %s", TOD::GetPeriodName(tod), f ? std::format("0x{:X}", f->GetLocalFormID()).c_str() : T(TKEY("none_filter"), "None"));
								}
								ImGui::EndTooltip();
							}
							weatherTooltipShownThisFrame = true;
						}
					}

					// Enter key to open
					if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
						sortedWidgets[i]->SetOpen(true);
						AddToRecent(sortedWidgets[i]->GetEditorID(), m_selectedCategory);
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

						if (ImGui::MenuItem(T(TKEY("remove"), "Remove"))) {
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

					// json / delete column
					drawJsonDeleteButton(sortedWidgets[i]);

					ImGui::PopID();
				}

				ImGui::EndTable();  // End DetailsTable
			}  // End if BeginTable("DetailsTable")

			EndScrollableContent();  // End ObjectsScrollable

		}  // End if BeginChild("##ObjectsContent")
		ImGui::EndChild();  // End ObjectsContent child

		ImGui::EndTable();  // End ObjectTable
	}  // End if BeginTable("ObjectTable")

	// Confirmation modal for json deletion - must be outside BeginChild so the modal can block the root window
	if (pendingDeleteWidget) {
		if (pendingDeletePopupRequested) {
			ImGui::OpenPopup("ListDeleteConfirmation");
			pendingDeletePopupRequested = false;
		}
		pendingDeleteWidget->DrawDeleteConfirmationModal("ListDeleteConfirmation");
		if (!ImGui::IsPopupOpen("ListDeleteConfirmation")) {
			pendingDeleteWidget = nullptr;
		}
	}

	// End the window
	ImGui::End();
}

void EditorWindow::ShowViewportWindow()
{
	Util::BeginWithRoundedClose(T(TKEY("viewport"), "Viewport"), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

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

	if (tempTexture && tempTexture->srv) {
		ImGui::Image((void*)tempTexture->srv.get(), imageSize);
	} else {
		ImGui::TextDisabled("%s", T(TKEY("viewport_unavailable"), "Viewport unavailable"));
	}

	ImGui::End();
}

void EditorWindow::ShowWidgetWindow()
{
	// Global shortcut for closing focused widget (Ctrl+W)
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
		if (lastFocusedWidget && lastFocusedWidget->IsOpen()) {
			lastFocusedWidget->SetOpen(false);
			lastFocusedWidget = nullptr;
		}
	}

	// Draw all open widgets using WidgetFactory template
	for (auto* collection : GetWidgetCollections())
		WidgetFactory::DrawOpenWidgets(*collection, lastFocusedWidget);

	// Draw current cell lighting widget if open
	if (currentCellLightingWidget && currentCellLightingWidget->IsOpen()) {
		currentCellLightingWidget->DrawWidget();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			lastFocusedWidget = currentCellLightingWidget.get();
	}
}

void EditorWindow::RenderUI()
{
	// Apply editor UI scale
	ImGuiIO& io = ImGui::GetIO();
	float previousScale = ImGui::GetStyle().FontScaleMain;
	ImGui::GetStyle().FontScaleMain = settings.editorUIScale;

	if (IsViewportActive()) {
		ImGui::GetBackgroundDrawList()->AddRectFilled({ 0, 0 }, io.DisplaySize, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
	}

	// Check for Ctrl+Z to undo
	if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
		if (CanUndo()) {
			PerformUndo();
		}
	}

	if (ImGui::BeginMainMenuBar()) {
		// Tighten bottom clip rect to prevent content bleeding over the bottom border
		{
			auto* window = ImGui::GetCurrentWindowRead();
			float borderInset = std::ceil(window->WindowBorderSize * 0.5f);
			ImGui::PushClipRect(window->ClipRect.Min,
				ImVec2(window->ClipRect.Max.x, window->ClipRect.Max.y - borderInset), true);
		}

		if (ImGui::BeginMenu(T(TKEY("file"), "File"))) {
			if (ImGui::MenuItem(T(TKEY("save_all_open_widgets"), "Save All Open Widgets"), "Ctrl+S")) {
				SaveAll();
			}

			// Save individual widgets submenu
			if (ImGui::BeginMenu(T(TKEY("save"), "Save"))) {
				bool hasOpen = false;
				for (auto* collection : GetWidgetCollections())
					hasOpen = WidgetFactory::DrawSaveWidgetMenuItems(*collection, hasOpen);

				if (currentCellLightingWidget && currentCellLightingWidget->IsOpen()) {
					hasOpen = true;
					if (ImGui::MenuItem(currentCellLightingWidget->GetEditorID().c_str()))
						currentCellLightingWidget->Save();
				}

				if (!hasOpen)
					ImGui::TextDisabled("%s", T(TKEY("no_open_widgets"), "No open widgets"));

				ImGui::EndMenu();
			}

			ImGui::Separator();
			for (auto* collection : GetWidgetCollections())
				WidgetFactory::DrawCloseAllMenuItem(*collection);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(T(TKEY("settings"), "Settings"))) {
			if (ImGui::MenuItem(T(TKEY("general_settings"), "General Settings"))) {
				showSettingsWindow = true;
				settingsSelectedCategory = "General";
			}
			if (ImGui::MenuItem(T(TKEY("editor_flags"), "Editor Flags"))) {
				showSettingsWindow = true;
				settingsSelectedCategory = "Flags";
			}
			ImGui::Separator();

			// Current cell lighting
			auto player = RE::PlayerCharacter::GetSingleton();
			if (player && player->parentCell && player->parentCell->IsInteriorCell()) {
				if (ImGui::MenuItem(T(TKEY("edit_current_cell_lighting"), "Edit Current Cell Lighting"))) {
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
				ImGui::MenuItem(T(TKEY("edit_current_cell_lighting"), "Edit Current Cell Lighting"));
				ImGui::EndDisabled();
				Util::AddTooltip(T(TKEY("interior_only_available"), "Only available in interior cells"), ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
			}

			ImGui::Separator();

			if (ImGui::Checkbox(T(TKEY("auto_apply_changes"), "Auto-Apply Changes"), &settings.autoApplyChanges)) {
				Save();
			}
			Util::AddTooltip(T(TKEY("auto_apply_changes_tooltip"), "Automatically apply weather changes to the game as you edit"));

			if (ImGui::Checkbox(T(TKEY("enable_inherit_from_parent"), "Enable Inherit From Parent"), &settings.enableInheritFromParent)) {
				Save();
			}
			Util::AddTooltip(T(TKEY("enable_inherit_tooltip"), "Show inherit from parent options in weather widgets"));
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(T(TKEY("window"), "Window"))) {
			const bool hdrActive = globals::features::hdrDisplay.loaded && globals::features::hdrDisplay.settings.enableHDR;
			if (hdrActive)
				ImGui::BeginDisabled();
			if (ImGui::Checkbox(T(TKEY("viewport"), "Viewport"), &settings.showViewport)) {
				BackgroundBlur::SetCSEditorActive(settings.showViewport);
				Save();
			}
			if (hdrActive) {
				ImGui::EndDisabled();
				Util::AddTooltip(T(TKEY("viewport_unavailable_hdr"), "Viewport is unavailable when HDR Display is enabled"), ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
			}
			if (ImGui::Checkbox(T(TKEY("palette"), "Palette"), &PaletteWindow::GetSingleton()->open)) {
			}

			if (ImGui::MenuItem(T(TKEY("reset_window_layout"), "Reset Window Layout"))) {
				resetLayout = true;
			}

			ImGui::Separator();
			ImGui::Text("%s", T(TKEY("open_widgets"), "Open Widgets:"));

			int openCount = 0;
			for (auto* collection : GetWidgetCollections())
				openCount = WidgetFactory::DrawOpenWidgetMenuItems(*collection, openCount);

			if (currentCellLightingWidget && currentCellLightingWidget->IsOpen()) {
				++openCount;
				if (ImGui::MenuItem(std::format("{}: {}", WidgetFactory::TranslateWidgetTypeName(currentCellLightingWidget->GetWidgetTypeName()), currentCellLightingWidget->GetEditorID()).c_str()))
					ImGui::SetWindowFocus(currentCellLightingWidget->GetWindowTitle().c_str());
			}

			if (openCount == 0)
				ImGui::TextDisabled("%s", T(TKEY("no_widgets_open"), "No widgets open"));

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(T(TKEY("help"), "Help"))) {
			ImGui::Text("%s", T(TKEY("cs_editor"), "CS Editor"));
			ImGui::Separator();
			ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor, "%s", T(TKEY("keyboard_shortcuts"), "Keyboard Shortcuts:"));
			ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_f"), "Ctrl+F: Focus search"));
			ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_s"), "Ctrl+S: Save all open widgets"));
			ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_w"), "Ctrl+W: Close focused widget"));
			ImGui::BulletText("%s", T(TKEY("shortcut_enter"), "Enter: Open selected widget"));
			ImGui::BulletText("%s", T(TKEY("shortcut_esc"), "Esc: Close editor"));
			ImGui::Separator();
			ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor, "%s", T(TKEY("quick_tips"), "Quick Tips:"));
			ImGui::BulletText("%s", T(TKEY("tip_double_click"), "Double-click to edit"));
			ImGui::BulletText("%s", T(TKEY("tip_right_click"), "Right-click to mark status"));
			ImGui::BulletText("%s", T(TKEY("tip_star_favorite"), "Click star icon to favorite"));
			ImGui::BulletText("%s", T(TKEY("tip_quick_filters"), "Use quick filters for fast sorting"));
			ImGui::BulletText("%s", T(TKEY("tip_auto_apply"), "Auto-Apply updates game live"));
			ImGui::BulletText("%s", T(TKEY("tip_lock_weather"), "Lock weather to prevent changes"));
			ImGui::BulletText("%s", T(TKEY("tip_undo"), "Undo button reverts recent changes (Ctrl+Z)"));
			ImGui::Separator();
			ImGui::Text("%s", T(TKEY("total_objects"), "Total Objects:"));
			ImGui::BulletText(T(TKEY("weathers_count"), "Weathers: %d"), (int)weatherWidgets.size());
			ImGui::BulletText(T(TKEY("lighting_count"), "Lighting: %d"), (int)lightingTemplateWidgets.size());
			ImGui::BulletText(T(TKEY("imagespaces_count"), "ImageSpaces: %d"), (int)imageSpaceWidgets.size());
			ImGui::Separator();
			ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.CurrentHotkey, T(TKEY("favorites_count"), "Favorites: %d"), (int)settings.favoriteWidgets.size());

			// Count total recent widgets across all categories
			int totalRecent = 0;
			for (const auto& [category, widgets] : settings.recentWidgets) {
				totalRecent += static_cast<int>(widgets.size());
			}
			ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor, T(TKEY("recent_count"), "Recent: %d"), totalRecent);
			ImGui::EndMenu();
		}

		auto menu = globals::menu;
		constexpr float kIconButtonPadding = 1.0f;  // minimal padding so icons render larger and smoother
		const float iconButtonDim = ImGui::GetFrameHeight() - kIconButtonPadding * 2;
		const ImVec2 iconButtonSize(iconButtonDim, iconButtonDim);
		const auto iconTint = Util::GetIconTint();

		// Undo button (stays on left side)
		if (menu && menu->uiIcons.undo.texture) {
			bool canUndo = CanUndo();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kIconButtonPadding, kIconButtonPadding));
			{
				auto _style = Util::TransparentIconButtonStyle();
				auto textColor = canUndo ? menu->GetTheme().Palette.Text : menu->GetTheme().StatusPalette.Disable;
				if (!canUndo)
					textColor.w = 0.5f;
				ImGui::PushStyleColor(ImGuiCol_Text, textColor);
				if (ImGui::ImageButton("##GlobalUndo", menu->uiIcons.undo.texture, iconButtonSize, ImVec2(0, 0), ImVec2(1, 1), WidgetUI::kIconButtonTransparent, iconTint) && canUndo)
					PerformUndo();
				ImGui::PopStyleColor();
			}
			ImGui::PopStyleVar(2);
			Util::AddTooltip(canUndo ? std::format("Undo (Ctrl+Z) - {} states", (int)undoStack.size()).c_str() : T(TKEY("undo_no_changes"), "Undo (Ctrl+Z) - No changes to undo"));
		}

		// Right-aligned items — use SetCursorScreenPos to bypass menu bar GroupOffset
		const float scale = Util::GetUIScale();
		const float clipRight = ImGui::GetWindowDrawList()->GetClipRectMax().x;
		const float cursorY = ImGui::GetCursorScreenPos().y;
		const float closeButtonSize = ImGui::GetFrameHeight();
		const float& itemSpacing = ImGui::GetStyle().ItemSpacing.x;
		const float sliderWidth = kMenuBarSliderWidth * scale;

		// Measure right-side elements to compute positions right-to-left
		constexpr float kCloseButtonInset = 3.0f;
		float rightCursor = clipRight - kCloseButtonInset * scale;

		// X button
		rightCursor -= closeButtonSize;
		const float xButtonX = rightCursor;

		// Time slider
		rightCursor -= itemSpacing + sliderWidth;
		const float sliderX = rightCursor;

		// Period text
		char periodBuf[64];
		std::snprintf(periodBuf, sizeof(periodBuf), "(%s)", TOD::GetPeriodName(TOD::GetActivePeriod()));
		float periodWidth = ImGui::CalcTextSize(periodBuf).x;
		rightCursor -= itemSpacing + periodWidth;
		const float periodX = rightCursor;

		// Pause Time button
		float pauseButtonX = 0;
		bool hasPauseButton = menu && menu->uiIcons.pauseTime.texture;
		if (hasPauseButton) {
			rightCursor -= itemSpacing + iconButtonDim + kIconButtonPadding * 2;
			pauseButtonX = rightCursor;
		}

		// Preview mode buttons (free camera / play mode)
		const float previewButtonWidth = iconButtonDim + kIconButtonPadding * 2;
		float freeCameraX = 0, playModeX = 0;
		bool hasFreeCam = menu && menu->uiIcons.freeCamera.texture;
		bool hasPlayMode = menu && menu->uiIcons.playMode.texture;
		if (hasPlayMode) {
			rightCursor -= itemSpacing + previewButtonWidth;
			playModeX = rightCursor;
		}
		if (hasFreeCam) {
			rightCursor -= itemSpacing + previewButtonWidth;
			freeCameraX = rightCursor;
		}

		// Preview mode status text (mirrors TIME PAUSED pattern, with hotkey + pulsating color)
		float previewStatusX = 0;
		char previewStatusBuf[128] = {};
		bool showPreviewStatus = previewMode != PreviewMode::None;
		if (showPreviewStatus) {
			std::string hotkey = Util::Input::KeyIdToString(menu->GetSettings().CSEditorToggleKey);
			if (previewMode == PreviewMode::FreeCamera)
				std::snprintf(previewStatusBuf, sizeof(previewStatusBuf), T(TKEY("preview_free_camera"), " [ %s ] FREE CAMERA (Speed: %.0f)"), hotkey.c_str(), flySpeed);
			else if (previewMode == PreviewMode::FreeCameraLocked)
				std::snprintf(previewStatusBuf, sizeof(previewStatusBuf), T(TKEY("preview_free_camera_locked"), " [ %s ] FREE CAMERA LOCKED"), hotkey.c_str());
			else
				std::snprintf(previewStatusBuf, sizeof(previewStatusBuf), T(TKEY("preview_play_mode"), " [ %s ] PLAY MODE"), hotkey.c_str());
			rightCursor -= itemSpacing + ImGui::CalcTextSize(previewStatusBuf).x;
			previewStatusX = rightCursor;
		}

		// Time paused text
		float timePausedX = 0;
		bool showTimePaused = IsTimePaused();
		const char* timePausedText = T(TKEY("time_paused_status"), " [TIME PAUSED]");
		if (showTimePaused) {
			rightCursor -= itemSpacing + ImGui::CalcTextSize(timePausedText).x;
			timePausedX = rightCursor;
		}

		// Weather lock text
		float weatherLockX = 0;
		char weatherLockBuf[128] = {};
		bool showWeatherLock = weatherLockActive && lockedWeather;
		if (showWeatherLock) {
			const char* weatherName = lockedWeather->GetFormEditorID();
			std::snprintf(weatherLockBuf, sizeof(weatherLockBuf), T(TKEY("locked_weather_status"), " [LOCKED: %s]"), weatherName ? weatherName : T(TKEY("unknown"), "Unknown"));
			rightCursor -= itemSpacing + ImGui::CalcTextSize(weatherLockBuf).x;
			weatherLockX = rightCursor;
		}

		// Render right-aligned items left to right
		const auto& statusPalette = menu->GetTheme().StatusPalette;

		if (showWeatherLock) {
			ImGui::SetCursorScreenPos(ImVec2(weatherLockX, cursorY));
			ImGui::PushStyleColor(ImGuiCol_Text, statusPalette.SuccessColor);
			ImGui::TextUnformatted(weatherLockBuf);
			ImGui::PopStyleColor();
		}

		if (showTimePaused) {
			ImGui::SetCursorScreenPos(ImVec2(timePausedX, cursorY));
			ImGui::PushStyleColor(ImGuiCol_Text, statusPalette.CurrentHotkey);
			ImGui::TextUnformatted(timePausedText);
			ImGui::PopStyleColor();
		}

		if (showPreviewStatus) {
			ImGui::SetCursorScreenPos(ImVec2(previewStatusX, cursorY));
			ImGui::TextColored(Util::GetPulsingColor(statusPalette.CurrentHotkey), "%s", previewStatusBuf);
		}

		// Toggle-style icon button helper (active: SuccessColor bg, inactive: transparent)
		auto DrawToggleIconButton = [&](const char* id, ImTextureRef texture, bool isActive, float posX) -> bool {
			ImGui::SetCursorScreenPos(ImVec2(posX, cursorY));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kIconButtonPadding, kIconButtonPadding));
			if (isActive) {
				auto color = statusPalette.SuccessColor;
				color.w = kToggleActiveAlpha;
				auto hover = color;
				hover.w = kToggleHoverAlpha;
				ImGui::PushStyleColor(ImGuiCol_Button, color);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
			} else {
				auto hover = menu->GetTheme().Palette.Text;
				hover.w = kInactiveHoverAlpha;
				ImGui::PushStyleColor(ImGuiCol_Button, WidgetUI::kIconButtonTransparent);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
			}
			bool clicked = ImGui::ImageButton(id, texture, iconButtonSize, ImVec2(0, 0), ImVec2(1, 1), WidgetUI::kIconButtonTransparent, iconTint);
			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar(2);
			return clicked;
		};

		// Preview mode buttons
		if (hasFreeCam) {
			bool isActive = previewMode == PreviewMode::FreeCamera || previewMode == PreviewMode::FreeCameraLocked;
			if (DrawToggleIconButton("##FreeCamera", menu->uiIcons.freeCamera.texture, isActive, freeCameraX))
				EnterPreviewMode(PreviewMode::FreeCamera);
			Util::AddTooltip(isActive ? T(TKEY("exit_free_camera"), "Exit Free Camera") : T(TKEY("free_camera_scroll"), "Free Camera (scroll to adjust speed)"));
		}
		if (hasPlayMode) {
			bool isActive = previewMode == PreviewMode::PlayMode;
			if (DrawToggleIconButton("##PlayMode", menu->uiIcons.playMode.texture, isActive, playModeX))
				EnterPreviewMode(PreviewMode::PlayMode);
			Util::AddTooltip(isActive ? T(TKEY("exit_play_mode"), "Exit Play Mode") : T(TKEY("play_mode_walk"), "Play Mode - Walk around normally"));
		}

		if (hasPauseButton) {
			bool isPaused = IsTimePaused();
			if (DrawToggleIconButton("##GlobalPauseTime", menu->uiIcons.pauseTime.texture, isPaused, pauseButtonX))
				TogglePause();
			Util::AddTooltip(isPaused ? T(TKEY("resume_time"), "Resume Time") : T(TKEY("pause_time"), "Pause Time"));
		}

		// Period text and time slider
		auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
		if (calendar && calendar->gameHour) {
			ImGui::SetCursorScreenPos(ImVec2(periodX, cursorY));
			ImGui::TextUnformatted(periodBuf);
			ImGui::SetCursorScreenPos(ImVec2(sliderX, cursorY));
			ImGui::SetNextItemWidth(sliderWidth);
			DrawGameHourSlider("##MenuBarSlider", "Time: %.2f");
		}

		// Close button
		ImGui::SetCursorScreenPos(ImVec2(xButtonX, cursorY));
		if (Util::ErrorButton("X", ImVec2(closeButtonSize, closeButtonSize)))
			open = false;
		Util::AddTooltip(T(TKEY("close_cs_editor"), "Close CS Editor (Esc)"));

		ImGui::PopClipRect();  // End bottom-border clip rect

		// Redraw the menu bar border on top of all content so elements appear behind it
		{
			auto* window = ImGui::GetCurrentWindowRead();
			const float border = ImGui::GetStyle().WindowBorderSize;
			if (window && border > 0.0f) {
				ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
				window->DrawList->AddRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y), borderCol, 0.0f, 0, border);
			}
		}

		ImGui::EndMainMenuBar();
	}

	// Establish a viewport-wide DockSpace so all editor windows are snappable and dockable
	ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

	auto width = ImGui::GetIO().DisplaySize.x;
	auto height = ImGui::GetIO().DisplaySize.y;
	const float scale = Util::GetUIScale();
	const float pad = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	const float menuBarHeight = ImGui::GetFrameHeight();
	const float availableWidth = width - pad * 3.0f;  // left pad + gap + right pad
	const float availableHeight = (height - menuBarHeight - pad * 2.0f) * 0.85f;
	const auto layoutCond = resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	// Browser gets up to half the available width, capped so ultrawide gives extra to viewport
	const float maxBrowserWidth = 960.0f * scale;
	const float browserWidth = std::min(availableWidth * 0.5f, maxBrowserWidth);
	const float viewportWidth = availableWidth - browserWidth;
	ImGui::SetNextWindowSize(ImVec2(browserWidth, availableHeight), layoutCond);
	ImGui::SetNextWindowPos(ImVec2(pad, menuBarHeight + pad), layoutCond);
	ShowObjectsWindow();

	if (IsViewportActive()) {
		// Size viewport height to match game aspect ratio so the preview fits snugly
		const float aspectRatio = width / height;
		const float imageHeight = viewportWidth / aspectRatio;
		const float chromeHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
		const float viewportHeight = imageHeight + chromeHeight;
		ImGui::SetNextWindowSize(ImVec2(viewportWidth, viewportHeight), layoutCond);
		ImGui::SetNextWindowPos(ImVec2(pad + browserWidth + pad, menuBarHeight + pad), layoutCond);
		viewportBottomY = menuBarHeight + pad + viewportHeight;
		ShowViewportWindow();
	} else {
		viewportBottomY = menuBarHeight + pad;
	}

	auto settingsWindowHeight = height * 0.25f;
	auto settingsWindowWidth = width * 0.25f;
	if (showSettingsWindow) {
		ImGui::SetNextWindowSize(ImVec2(settingsWindowWidth, settingsWindowHeight), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos({ (width / 2.0f) - (settingsWindowWidth / 2.0f), (height / 2.0f) - (settingsWindowHeight / 2.0f) }, ImGuiCond_Appearing);
		ShowSettingsWindow();
	}

	ShowWidgetWindow();

	// Show palette window
	PaletteWindow::GetSingleton()->Draw();

	if (resetLayout)
		ResetWidgetTypeSizes();
	resetLayout = false;

	// Render notifications on top of everything
	RenderNotifications();

	// Restore previous font scale
	ImGui::GetStyle().FontScaleMain = previousScale;
}

void EditorWindow::OpenWeatherFeatureSetting(RE::TESWeather* weather, const std::string& featureName, const std::string& settingName)
{
	if (!weather) {
		return;
	}

	// Open the editor if it's not already open
	if (!open) {
		open = true;
	}

	// Find the weather widget
	for (auto& widget : weatherWidgets) {
		auto* weatherWidget = dynamic_cast<WeatherWidget*>(widget.get());
		if (weatherWidget && weatherWidget->weather == weather) {
			// Open the widget if it's not already open
			if (!weatherWidget->open) {
				weatherWidget->open = true;
			}

			// Set up navigation to the specific feature/setting
			weatherWidget->NavigateToFeatureSetting(featureName, settingName);

			// Focus the widget window
			weatherWidget->RequestFocus();
			break;
		}
	}
}

EditorWindow::~EditorWindow()
{
	ShowGameMenus();
	delete tempTexture;
	for (auto* collection : GetWidgetCollections())
		collection->clear();
	artObjectWidgets.clear();
	effectShaderWidgets.clear();
	currentCellLightingWidget.reset();
}

void EditorWindow::SetupResources()
{
	Load();
	PaletteWindow::GetSingleton()->Load();
	InvalidateJsonAttachmentCache();

	// Populate all widget collections using WidgetFactory templates
	WidgetFactory::PopulateWidgets<WeatherWidget, RE::TESWeather>(weatherWidgets);
	WidgetFactory::PopulateWidgets<LightingTemplateWidget, RE::BGSLightingTemplate>(lightingTemplateWidgets);
	WidgetFactory::PopulateWidgets<ImageSpaceWidget, RE::TESImageSpace>(imageSpaceWidgets);
	WidgetFactory::PopulateWidgets<VolumetricLightingWidget, RE::BGSVolumetricLighting>(volumetricLightingWidgets);
	WidgetFactory::PopulateWidgets<PrecipitationWidget, RE::BGSShaderParticleGeometryData>(precipitationWidgets);
	WidgetFactory::PopulateWidgets<LensFlareWidget, RE::BGSLensFlare>(lensFlareWidgets);
	WidgetFactory::PopulateWidgets<ReferenceEffectWidget, RE::BGSReferenceEffect>(referenceEffectWidgets);

	// Cache simple form widgets for form picker performance
	WidgetFactory::PopulateSimpleWidgets<RE::BGSArtObject>(artObjectWidgets);
	WidgetFactory::PopulateSimpleWidgets<RE::TESEffectShader>(effectShaderWidgets);
}

bool EditorWindow::IsViewportActive() const
{
	return settings.showViewport && !(globals::features::hdrDisplay.loaded && globals::features::hdrDisplay.settings.enableHDR);
}

void EditorWindow::UpdateOpenState()
{
	static bool wasOpen = false;

	if (open && !wasOpen) {
		DisableVanityCamera();
		HideGameMenus();
		BackgroundBlur::SetCSEditorActive(IsViewportActive());

	} else if (!open && wasOpen) {
		lightEditor.ResetOverrides();
		RestoreVanityCamera();
		ShowGameMenus();
		BackgroundBlur::SetCSEditorActive(false);
	}

	wasOpen = open;
}

void EditorWindow::Draw()
{
	if (open)
		lightEditor.GatherLights();

	// Keep background blur in sync when HDR toggles while the editor stays open
	{
		static bool prevViewportActive = false;
		const bool viewportActive = IsViewportActive();
		if (viewportActive != prevViewportActive) {
			BackgroundBlur::SetCSEditorActive(viewportActive);
			prevViewportActive = viewportActive;
		}
	}

	// Re-enforce weather lock if active (handles time changes)
	if (weatherLockActive && lockedWeather) {
		auto sky = RE::Sky::GetSingleton();
		if (sky && sky->currentWeather != lockedWeather) {
			sky->ForceWeather(lockedWeather, false);
		}
	}

	if (!IsViewportActive()) {
		delete tempTexture;
		tempTexture = nullptr;
	} else {
		auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (renderer) {
			auto& framebuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
			if (framebuffer.SRV) {
				ID3D11Resource* resource = nullptr;
				framebuffer.SRV->GetResource(&resource);

				if (resource) {
					auto texture = static_cast<ID3D11Texture2D*>(resource);
					D3D11_TEXTURE2D_DESC texDesc{};
					texture->GetDesc(&texDesc);

					const bool needsRecreate = !tempTexture || !tempTexture->resource || !tempTexture->srv ||
					                           tempTexture->desc.Width != texDesc.Width || tempTexture->desc.Height != texDesc.Height ||
					                           tempTexture->desc.MipLevels != texDesc.MipLevels || tempTexture->desc.ArraySize != texDesc.ArraySize ||
					                           tempTexture->desc.Format != texDesc.Format ||
					                           tempTexture->desc.SampleDesc.Count != texDesc.SampleDesc.Count ||
					                           tempTexture->desc.SampleDesc.Quality != texDesc.SampleDesc.Quality;

					if (needsRecreate) {
						delete tempTexture;
						tempTexture = nullptr;

						D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
						framebuffer.SRV->GetDesc(&srvDesc);

						tempTexture = new Texture2D(texDesc);
						tempTexture->CreateSRV(srvDesc);
					}

					if (tempTexture && tempTexture->resource) {
						globals::d3d::context->CopyResource(tempTexture->resource.get(), resource);
					}

					resource->Release();
				}
			}
		}
	}

	RenderUI();
}

void EditorWindow::SaveAll()
{
	auto saveOpen = [](auto& widgets) {
		for (auto& w : widgets)
			if (w->IsOpen())
				w->Save();
	};
	for (auto* collection : GetWidgetCollections())
		saveOpen(*collection);
	if (currentCellLightingWidget && currentCellLightingWidget->IsOpen())
		currentCellLightingWidget->Save();

	Save();
}

void EditorWindow::SaveSettings()
{
	settings.widgetTypeSizes = GetWidgetTypeSizesJson();
	j = settings;
}

void EditorWindow::LoadSettings()
{
	if (!j.empty())
		settings = j;
	SetWidgetTypeSizesFromJson(settings.widgetTypeSizes);
}

void EditorWindow::ShowSettingsWindow()
{
	Util::BeginWithRoundedClose(T(TKEY("settings"), "Settings"), &showSettingsWindow);

	if (ImGui::BeginTable("SettingsTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoHostExtendX)) {
		ImGui::TableSetupColumn(T(TKEY("options"), "Options"), ImGuiTableColumnFlags_WidthStretch, 0.3f);
		ImGui::TableSetupColumn("##Settings", ImGuiTableColumnFlags_WidthStretch, 0.7f);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		struct CategoryOption
		{
			const char* id;
			const char* label;
		};
		const CategoryOption options[] = {
			{ "General", T(TKEY("general"), "General") },
			{ "Flags", T(TKEY("flags"), "Flags") }
		};
		for (const auto& option : options) {
			if (ImGui::Selectable(option.label, settingsSelectedCategory == option.id)) {
				settingsSelectedCategory = option.id;
			}
		}

		ImGui::TableSetColumnIndex(1);

		if (settingsSelectedCategory == "General") {
			ImGui::Checkbox(T(TKEY("auto_apply_changes"), "Auto-Apply Changes"), &settings.autoApplyChanges);
			Util::AddTooltip(T(TKEY("auto_apply_changes_tooltip"), "Automatically apply weather changes to the game as you edit"));

			ImGui::Checkbox(T(TKEY("use_text_buttons"), "Use text buttons instead of icons"), &settings.useTextButtons);
			Util::AddTooltip(T(TKEY("text_buttons_tooltip"), "Display action buttons as text labels instead of icons"));

			ImGui::Checkbox(T(TKEY("enable_inherit_feature"), "Enable 'Inherit From Parent' feature"), &settings.enableInheritFromParent);
			Util::AddTooltip(T(TKEY("enable_inherit_feature_tooltip"), "Show checkboxes to copy settings from parent weather (editor-only feature)"));

			ImGui::Separator();
			ImGui::TextUnformatted(T(TKEY("ui_scale"), "UI Scale"));
			ImGui::Spacing();

			if (ImGui::SliderFloat(T(TKEY("editor_ui_scale"), "Editor UI Scale"), &settings.editorUIScale, 0.5f, 2.0f, "%.2f")) {
				Save();
			}
			Util::AddTooltip(T(TKEY("editor_ui_scale_tooltip"), "Scale the size of all editor UI elements (0.5 = 50%, 2.0 = 200%)"));

			if (Util::ButtonWithFlash(T(TKEY("reset_to_default"), "Reset to 1.0"))) {
				settings.editorUIScale = 1.0f;
				Save();
			}
			ImGui::SameLine();
			Util::AddTooltip(T(TKEY("reset_ui_scale_tooltip"), "Reset UI scale to default (100%)"));

			ImGui::Separator();
			ImGui::TextUnformatted(T(TKEY("session_history"), "Session & History"));
			ImGui::Spacing();

			ImGui::SliderInt(T(TKEY("max_recent_widgets"), "Max recent widgets"), &settings.maxRecentWidgets, 5, 20);
			Util::AddTooltip(T(TKEY("max_recent_widgets_tooltip"), "Maximum number of recent widgets to remember"));

			if (Util::ButtonWithFlash(T(TKEY("clear_recent_history"), "Clear Recent History"))) {
				settings.recentWidgets.clear();
				Save();
			}
			ImGui::SameLine();
			if (Util::ButtonWithFlash(T(TKEY("clear_favorites"), "Clear Favorites"))) {
				settings.favoriteWidgets.clear();
				Save();
			}

		} else if (settingsSelectedCategory == "Flags") {
			if (ImGui::BeginTable("FlagsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn(T(TKEY("label"), "Label"), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(T(TKEY("colour"), "Colour"), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(T(TKEY("actions"), "Actions"), ImGuiTableColumnFlags_WidthFixed, 60.0f * Util::GetUIScale());

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
					auto deleteColor = Menu::GetSingleton()->GetTheme().StatusPalette.Warning;
					deleteColor.y = deleteColor.y * 0.5f;
					auto deleteHovered = deleteColor;
					deleteHovered.w = 0.8f;
					auto deleteActive = deleteColor;
					deleteActive.w = 1.0f;
					{
						auto styledButton = Util::StyledButtonWrapper(deleteColor, deleteHovered, deleteActive);
						auto deleteLabel = std::format("{}##{}", T(TKEY("delete"), "Delete"), recordMarker.first);
						if (ImGui::Button(deleteLabel.c_str(), ImVec2(-1, 0))) {
							markerToDelete = recordMarker.first;
						}
					}
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

				if (recordMarkers.size() < maxRecordMarkers && ImGui::Selectable(T(TKEY("add_new_marker"), "Add new marker"))) {
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
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
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
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (calendar && calendar->timeScale) {
		calendar->timeScale->value = savedTimeScale;
		timePaused = false;
		logger::info("Time resumed (timescale: {})", savedTimeScale);
	}
}

void EditorWindow::ResetTimeScale()
{
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->timeScale)
		return;
	if (timePaused)
		savedTimeScale = kVanillaTimeScale;
	else
		calendar->timeScale->value = kVanillaTimeScale;
	timeScaleSlider = kVanillaTimeScale;
}

void EditorWindow::UpdateTimeState()
{
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	auto ui = globals::game::ui ? globals::game::ui : RE::UI::GetSingleton();
	if (!calendar || !calendar->timeScale)
		return;

	bool sleepWaitOpen = ui && ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME);

	// External state sync (skip during sleep/wait)
	if (!sleepWaitOpen) {
		if (calendar->timeScale->value == 0.0f && !timePaused)
			savedTimeScale = kVanillaTimeScale;
		else if (calendar->timeScale->value > 0.0f && timePaused)
			timePaused = false;
	}

	// Sleep/wait handling — temporarily restore time so the wait can proceed
	if (sleepWaitOpen && calendar->timeScale->value == 0.0f) {
		if (!wasRestoredForWait) {
			wasPausedBeforeWait = true;
			if (timePaused)
				ResumeTime();
			else
				calendar->timeScale->value = std::max(savedTimeScale, kVanillaTimeScale);
			wasRestoredForWait = true;
		}
	} else if (!sleepWaitOpen && wasRestoredForWait) {
		if (wasPausedBeforeWait && !timePaused)
			PauseTime();
		wasRestoredForWait = false;
		wasPausedBeforeWait = false;
	}
}

bool EditorWindow::DrawGameHourSlider(const char* label, const char* format)
{
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->gameHour)
		return false;
	ImGui::SliderFloat(label, &calendar->gameHour->value, 0.0f, kGameHourMax, format);
	return true;
}

void EditorWindow::DrawTimeControls()
{
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	if (!calendar || !calendar->gameHour || !calendar->timeScale)
		return;

	const float framePadX = ImGui::GetStyle().FramePadding.x * 2.0f;
	const char* resumeTimeText = T(TKEY("resume_time"), "Resume Time");
	const char* pauseTimeText = T(TKEY("pause_time"), "Pause Time");
	const char* resetSpeedText = T(TKEY("reset_speed"), "Reset Speed");
	const float buttonWidth = std::max({ ImGui::CalcTextSize(resumeTimeText).x,
								  ImGui::CalcTextSize(pauseTimeText).x,
								  ImGui::CalcTextSize(resetSpeedText).x }) +
	                          framePadX;
	if (ImGui::Button(timePaused ? resumeTimeText : pauseTimeText, ImVec2(buttonWidth, 0)))
		TogglePause();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("pause_time_tooltip"), "Pause or resume game time progression"));
	ImGui::SameLine();
	DrawGameHourSlider(T(TKEY("game_time"), "Game Time"));
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("game_time_tooltip"), "Adjust the current game time"));

	// Sync slider with actual value
	if (timePaused)
		timeScaleSlider = std::max(savedTimeScale, kTimeScaleMin);
	else if (std::abs(calendar->timeScale->value - timeScaleSlider) > 0.01f)
		timeScaleSlider = calendar->timeScale->value;

	// Row 2: Reset Speed + TimeScale slider + speed label
	if (ImGui::Button(resetSpeedText, ImVec2(buttonWidth, 0)))
		ResetTimeScale();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("reset_speed_tooltip"), "Reset time speed to vanilla (%.1fx)"), kVanillaTimeScale);

	ImGui::SameLine();
	ImGui::BeginDisabled(timePaused);
	if (ImGui::SliderFloat("##TimeScale", &timeScaleSlider, kTimeScaleMin, kTimeScaleMax,
			timeScaleSlider == kVanillaTimeScale ? T(TKEY("vanilla_speed"), "Vanilla Speed") : "", ImGuiSliderFlags_Logarithmic))
		calendar->timeScale->value = timeScaleSlider;
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::Text("%.1fx", calendar->timeScale->value);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("time_scale_tooltip"), "Adjust how fast time passes (vanilla: %.1fx)"), kVanillaTimeScale);
}

bool EditorWindow::CanBeOpen()
{
	auto* player = globals::game::player;
	auto* state = globals::state;
	return player && player->parentCell && !state->isLoadingMenuOpen && !state->isMainMenuOpen;
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

void EditorWindow::HideGameMenus()
{
	if (gameMenusHidden)
		return;

	// ShowMenus(false) stops the game from rendering to the back buffer.
	// Without d3d12SwapChain, blur reads directly from that buffer and would freeze.
	if (!globals::features::upscaling.d3d12SwapChainActive)
		return;

	if (auto ui = RE::UI::GetSingleton()) {
		ui->ShowMenus(false);
		gameMenusHidden = true;
		logger::info("Game menus hidden for CS editor");
	}
}

void EditorWindow::ShowGameMenus()
{
	if (!gameMenusHidden)
		return;

	if (auto ui = RE::UI::GetSingleton()) {
		ui->ShowMenus(true);
		gameMenusHidden = false;
		logger::info("Game menus restored after CS editor");
	}
}

void EditorWindow::EnterPreviewMode(PreviewMode mode)
{
	if (mode == PreviewMode::None)
		return;

	// Already in free camera flying — ignore duplicate click
	if (mode == previewMode)
		return;

	// Re-enter flying from locked state via button click
	if (mode == PreviewMode::FreeCamera && previewMode == PreviewMode::FreeCameraLocked) {
		previewMode = PreviewMode::FreeCamera;
		logger::info("Free camera unlocked (re-entered flying)");
		return;
	}

	// Switch from a different active mode first
	if (previewMode != PreviewMode::None)
		ExitPreviewMode();

	previewMode = mode;
	savedMousePos = ImGui::GetIO().MousePos;

	if (mode == PreviewMode::FreeCamera) {
		flySpeed = kDefaultFlySpeed;
		RE::Console::ExecuteCommand("tfc");
		RE::Console::ExecuteCommand(std::format("sucsm {:.0f}", flySpeed).c_str());
	}

	logger::info("Entered preview mode: {}", mode == PreviewMode::FreeCamera ? "FreeCamera" : "PlayMode");
}

void EditorWindow::ExitPreviewMode()
{
	bool wasFlying = IsPreviewFlying();

	if (previewMode == PreviewMode::FreeCamera || previewMode == PreviewMode::FreeCameraLocked)
		RE::Console::ExecuteCommand("tfc");

	logger::info("Exited preview mode");
	previewMode = PreviewMode::None;

	// Only restore cursor if exiting from a flying state; FreeCameraLocked already has the cursor active
	if (wasFlying) {
		ImGui::GetIO().MousePos = savedMousePos;
		ImGui::GetIO().WantSetMousePos = true;
	}
}

void EditorWindow::ToggleFreeCameraLock()
{
	if (previewMode == PreviewMode::FreeCamera) {
		previewMode = PreviewMode::FreeCameraLocked;
		ImGui::GetIO().MousePos = savedMousePos;
		ImGui::GetIO().WantSetMousePos = true;
		logger::info("Free camera locked");
	} else if (previewMode == PreviewMode::FreeCameraLocked) {
		savedMousePos = ImGui::GetIO().MousePos;
		previewMode = PreviewMode::FreeCamera;
		logger::info("Free camera unlocked");
	}
}

void EditorWindow::AdjustFlySpeed(float scrollDelta)
{
	if (previewMode != PreviewMode::FreeCamera)
		return;

	flySpeed = std::clamp(flySpeed + scrollDelta * kFlySpeedScrollStep, kMinFlySpeed, kMaxFlySpeed);
	RE::Console::ExecuteCommand(std::format("sucsm {:.0f}", flySpeed).c_str());
}

bool EditorWindow::ShouldHandleEscapeKey() const
{
	return !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
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
		for (auto* collection : GetWidgetCollections()) {
			for (auto& w : *collection) {
				if (w->GetEditorID() == state.widgetId) {
					state.widget = w.get();
					break;
				}
			}
			if (state.widget)
				break;
		}
	}

	if (state.widget) {
		state.widget->js = state.settings;
		state.widget->LoadSettings();
		state.widget->ApplyChanges();
		ShowNotification(
			std::vformat(T(TKEY("undone_changes_to"), "Undone changes to {}"), std::make_format_args(state.widgetId)),
			Menu::GetSingleton()->GetSettings().Theme.StatusPalette.InfoColor,
			2.0f);
	}
}

void EditorWindow::ShowNotification(const std::string& message, const ImVec4& color, float duration)
{
	// Guard against calls before ImGui is initialized
	if (!ImGui::GetCurrentContext()) {
		logger::warn("ShowNotification called before ImGui initialization: {}", message);
		return;
	}

	Notification notif;
	notif.message = message;
	notif.color = color;
	notif.startTime = static_cast<float>(ImGui::GetTime());
	notif.duration = duration;
	notifications.push_back(notif);
}

void EditorWindow::RenderNotifications()
{
	// Guard against calls before ImGui is initialized
	if (!ImGui::GetCurrentContext()) {
		return;
	}

	float currentTime = static_cast<float>(ImGui::GetTime());
	const float scale = Util::GetUIScale();
	float yOffset = 10.0f * scale;

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
		ImGui::SetNextWindowPos(ImVec2(10.0f * scale, yOffset), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.8f * alpha);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f * scale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f * scale, 10.0f * scale));

		if (ImGui::Begin(std::format("##Notification{}", (void*)&notif).c_str(),
				nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking)) {
			ImVec4 colorWithAlpha = notif.color;
			colorWithAlpha.w *= alpha;
			ImGui::PushStyleColor(ImGuiCol_Text, colorWithAlpha);
			ImGui::TextUnformatted(notif.message.c_str());
			ImGui::PopStyleColor();

			yOffset += ImGui::GetWindowSize().y + 5.0f * scale;
		}
		ImGui::End();

		ImGui::PopStyleVar(2);
	}
}

void EditorWindow::RefreshJsonAttachmentCache(const std::vector<Widget*>& widgets)
{
	for (auto* widget : widgets) {
		if (!widget) {
			continue;
		}
		if (!jsonAttachmentCache.contains(widget)) {
			jsonAttachmentCache.emplace(widget, widget->HasSavedFile());
		}
	}
}

bool EditorWindow::HasCachedJsonAttachment(Widget* widget) const
{
	if (!widget) {
		return false;
	}
	if (auto it = jsonAttachmentCache.find(widget); it != jsonAttachmentCache.end()) {
		return it->second;
	}
	return false;
}

void EditorWindow::InvalidateJsonAttachmentCache(Widget* widget)
{
	if (widget) {
		jsonAttachmentCache.erase(widget);
		return;
	}
	jsonAttachmentCache.clear();
}

void EditorWindow::OnWidgetJsonAttachmentChanged(Widget* widget)
{
	InvalidateJsonAttachmentCache(widget);
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

	Save();
}

void EditorWindow::ToggleFavorite(const std::string& widgetId)
{
	auto it = std::find(settings.favoriteWidgets.begin(), settings.favoriteWidgets.end(), widgetId);
	if (it != settings.favoriteWidgets.end()) {
		settings.favoriteWidgets.erase(it);
	} else {
		settings.favoriteWidgets.push_back(widgetId);
	}
	Save();
}

bool EditorWindow::IsFavorite(const std::string& widgetId) const
{
	return std::find(settings.favoriteWidgets.begin(), settings.favoriteWidgets.end(), widgetId) != settings.favoriteWidgets.end();
}

#undef I18N_KEY_PREFIX
