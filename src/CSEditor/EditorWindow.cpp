#include "EditorWindow.h"

#include "../I18n/I18n.h"
#include "Browser/BrowserWidgets.h"
#include "Browser/FormListPage.h"
#include "FeatureSettingsWindow.h"
#include "Features/CSEditor.h"
#include "Features/Effects11.h"
#include "Features/Effects11/Editor/Effects11Editor.h"
#include "Features/HDRDisplay.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/BackgroundBlur.h"
#include "Menu/Fonts.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/SceneActionIcons.h"
#include "Menu/Icons/helpers/WeatherTypeIcons.h"
#include "PaletteWindow.h"
#include "SaveManager.h"
#include "SceneManager/ScenePresetExport.h"
#include "SceneManager/SceneSettingsManager.h"
#include "SceneManager/SceneSettingsUI.h"
#include "State.h"
#include "Utils/Climate.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include "Weather/LightingTemplateWidget.h"
#include "WeatherPickerWindow.h"
#include "WeatherUtils.h"
#include "imgui_internal.h"

#include <atomic>
#include <cmath>
#include <cstring>

#define I18N_KEY_PREFIX "cs_editor."

#include <algorithm>

namespace
{
	constexpr float kToggleIconButtonPadding = 1.0f;
	constexpr float kToggleActiveAlpha = 0.6f;
	constexpr float kToggleHoverAlpha = 0.8f;
	constexpr float kInactiveHoverAlpha = 0.25f;
	constexpr float kMenuShortcutScale = 0.85f;
	/// Game time slider width relative to the time speed slider in the toolbar.
	constexpr float kMenuBarGameTimeWidthScale = 2.0f;
	/// Help note wrap width in font sizes; a window-edge wrap (0) collapses on the popup's first auto-size frame.
	constexpr float kHelpNoteWrapFontScale = 30.0f;
	/// Slack when matching dock node edges, which are sums of split sizes and drift by float error.
	constexpr float kDockEdgeTolerance = 1.0f;

	Util::ConfirmationPopup deleteSceneChangesConfirmation;

	/** @brief Bottom corner flags of the action bar that a docked node hangs off, so they can be drawn square. */
	ImDrawFlags GetDockedBarCorners(const ImGuiDockNode* node, const ImRect& dockRect)
	{
		if (!node)
			return ImDrawFlags_None;
		if (!node->IsLeafNode())
			return GetDockedBarCorners(node->ChildNodes[0], dockRect) | GetDockedBarCorners(node->ChildNodes[1], dockRect);
		if (!node->IsVisible || node->IsCentralNode() || node->Windows.empty() || node->Pos.y > dockRect.Min.y + kDockEdgeTolerance)
			return ImDrawFlags_None;

		ImDrawFlags corners = ImDrawFlags_None;
		if (node->Pos.x <= dockRect.Min.x + kDockEdgeTolerance)
			corners |= ImDrawFlags_RoundCornersBottomLeft;
		if (node->Pos.x + node->Size.x >= dockRect.Max.x - kDockEdgeTolerance)
			corners |= ImDrawFlags_RoundCornersBottomRight;
		return corners;
	}

	/** @brief MenuItem with a smaller right-aligned shortcut subtext. */
	bool MenuItemWithShortcutSubtext(const char* label, const char* shortcut, bool enabled = true)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		const float prevScale = ImGui::GetStyle().FontScaleMain;
		ImVec2 shortcutSize{};
		if (shortcut && shortcut[0]) {
			ImGui::GetStyle().FontScaleMain = prevScale * kMenuShortcutScale;
			shortcutSize = ImGui::CalcTextSize(shortcut);
			ImGui::GetStyle().FontScaleMain = prevScale;
		}

		// Stretch like MenuItem and reserve room for the smaller shortcut column.
		const float shortcutReserve = shortcutSize.x > 0.0f ?
		                                  shortcutSize.x + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f :
		                                  0.0f;
		if (shortcutReserve > 0.0f)
			window->DC.CursorMaxPos.x = std::max(window->DC.CursorMaxPos.x, window->DC.CursorPos.x + ImGui::CalcTextSize(label, nullptr, true).x + shortcutReserve);

		if (!enabled)
			ImGui::BeginDisabled();
		const bool pressed = ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAvailWidth);
		if (!enabled)
			ImGui::EndDisabled();

		if (shortcutSize.x > 0.0f && ImGui::IsItemVisible()) {
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			ImGui::GetStyle().FontScaleMain = prevScale * kMenuShortcutScale;
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(max.x - ImGui::GetStyle().ItemInnerSpacing.x - shortcutSize.x,
					min.y + (max.y - min.y - shortcutSize.y) * 0.5f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), shortcut);
			ImGui::GetStyle().FontScaleMain = prevScale;
		}

		if (pressed && enabled) {
			ImGui::CloseCurrentPopup();
			return true;
		}
		return false;
	}

	/** @brief Toggle-style icon button with active fill chrome. Does not set cursor position. */
	bool DrawToggleIconButton(const char* id, Icons::GlyphRef glyph, bool isActive, const ImVec4& activeColor,
		const ImVec2& size, ImU32 glyphColor)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kToggleIconButtonPadding, kToggleIconButtonPadding));
		if (isActive) {
			auto color = activeColor;
			color.w = kToggleActiveAlpha;
			auto hover = color;
			hover.w = kToggleHoverAlpha;
			ImGui::PushStyleColor(ImGuiCol_Button, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
		} else {
			auto hover = ImGui::ColorConvertU32ToFloat4(glyphColor);
			hover.w = kInactiveHoverAlpha;
			ImGui::PushStyleColor(ImGuiCol_Button, WidgetUI::kIconButtonTransparent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
		}
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
		const bool clicked = ImGui::InvisibleButton(id, size);
		const ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		if (isActive || ImGui::IsItemHovered())
			ImGui::GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max,
				ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive :
														   (ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button)),
				ImGui::GetStyle().FrameRounding);
		Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), bb.Min, bb.GetSize(), glyph, glyphColor);
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		return clicked;
	}
}  // namespace

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings::PaletteColorEntry, r, g, b, useCount, lastUsedTime, isFavorite)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings::PaletteValueEntry, name, value, useCount, lastUsedTime, isFavorite)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings::PaletteFavoriteColor, hasValue, r, g, b)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorWindow::Settings, recordMarkers, markedRecords, autoApplyChanges, useTextButtons, enableInheritFromParent, showFeatureDebug, editorUIScale, favoriteWidgets, recentWidgets, maxRecentWidgets, showViewport, showFeaturesWindow, selectedCategory, browserShowInspector, browserSidebarCompact, skipToolDiscardPrompt, widgetTypeSizes, paletteColors, paletteValues, paletteFavorites)

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

// Pennant on a pole. The icon font only ships a solid flag, so the outline state is drawn by hand.
void DrawIconFlag(ImVec2 center, float height, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	const float width = height * 0.9f;
	const float thickness = std::max(1.0f, height * 0.1f);
	const ImVec2 top(center.x - width * 0.4f, center.y - height * 0.5f);
	const ImVec2 bottom(top.x, center.y + height * 0.5f);
	const ImVec2 tip(top.x + width, top.y + height * 0.25f);
	const ImVec2 hoist(top.x, top.y + height * 0.5f);

	drawList->AddLine(top, bottom, color, thickness);
	if (filled)
		drawList->AddTriangleFilled(top, tip, hoist, color);
	else
		drawList->AddTriangle(top, tip, hoist, color, thickness);
}

namespace
{
	// The editor can draw before globals are cached, so this falls back to the singleton.
	RE::UI* GetUI()
	{
		return globals::game::ui ? globals::game::ui : RE::UI::GetSingleton();
	}

	/// Shared chrome for every editor surface: one corner radius, and denser vertical rhythm than
	/// the main CS menu's theme (themes often ship ItemSpacing.y of 8-12, which reads as "airy"
	/// once every slider/header already has FramePadding of its own).
	class EditorChromeScope
	{
	public:
		EditorChromeScope()
		{
			const auto& style = ImGui::GetStyle();
			const float radius = style.FrameRounding;
			for (const auto var : kRoundingVars)
				ImGui::PushStyleVar(var, radius);

			// Keep horizontal spacing; only compress the vertical gap between consecutive rows.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
				ImVec2(style.ItemSpacing.x, style.ItemSpacing.y * kItemSpacingYScale));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
				ImVec2(style.WindowPadding.x, style.WindowPadding.y * kWindowPaddingYScale));
		}

		~EditorChromeScope() { ImGui::PopStyleVar(static_cast<int>(kRoundingVars.size() + kDensityVars)); }

		EditorChromeScope(const EditorChromeScope&) = delete;
		EditorChromeScope& operator=(const EditorChromeScope&) = delete;

	private:
		static constexpr float kItemSpacingYScale = 0.55f;
		static constexpr float kWindowPaddingYScale = 0.75f;
		static constexpr int kDensityVars = 2;
		static constexpr std::array<ImGuiStyleVar, 5> kRoundingVars{
			ImGuiStyleVar_WindowRounding, ImGuiStyleVar_ChildRounding, ImGuiStyleVar_PopupRounding,
			ImGuiStyleVar_GrabRounding, ImGuiStyleVar_TabRounding
		};
	};
}  // namespace

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

void EditorWindow::DrawActiveWeatherIndicator(bool drawTrailer)
{
	auto* sky = globals::game::sky;
	auto* weather = sky ? sky->currentWeather : nullptr;
	if (!weather)
		return;

	const auto id = weather->GetFormID();
	const auto openWeatherEditor = [&]() {
		for (const auto& widget : weatherWidgets) {
			if (widget->form && widget->form->GetFormID() == id) {
				widget->SetOpen(true);
				widget->RequestFocus();
				break;
			}
		}
	};

	// The same chip the form pages use for their active records.
	ImGui::AlignTextToFramePadding();
	Util::Text::Secondary("%s", T(TKEY("active"), "Active:"));
	ImGui::SameLine();
	const std::string weatherName = ResolveEditorId(weather, weatherWidgets);
	const Icons::GlyphRef icon = WeatherTypeIcons::ResolveGlyph(weather);
	const ImVec4 iconColor = CSEditor::GetWeatherTypeColor(weather);
	const ImVec4 live = Util::Colors::GetSuccess();
	if (BrowserUI::Chip("##active_weather_name", weatherName.c_str(), icon, &iconColor, &live))
		openWeatherEditor();
	Util::AddTooltip(std::format("{} (0x{:08X})", T(TKEY("open_active_weather_tooltip"), "Open this weather's editor window."), id).c_str());
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	if (BrowserUI::IconButton("##active_weather_indicator", Icons::TI(ICON_TI_EXTERNAL_LINK),
			T(TKEY("open_active_weather_tooltip"), "Open this weather's editor window.")))
		openWeatherEditor();
	if (drawTrailer)
		ImGui::Separator();
}

namespace
{
	/// Starting width of the browser's category sidebar at the 1080p baseline; the user can drag it.
	constexpr float kBrowserSidebarWidth = 200.0f;
	constexpr float kSidebarStripeWidth = 3.0f;

	enum BrowserGroup
	{
		kGroupForms,
		kGroupScene,
		kGroupTools
	};

	struct BrowserCategory
	{
		const char* id;  ///< Stable English id stored in settings
		const char* label;
		Icons::GlyphRef icon;
		BrowserGroup group;
	};

	std::array<BrowserCategory, 11> GetBrowserCategories()
	{
		return { {
			{ "Weather", T(TKEY("category_weather"), "Weather"), Icons::TI(ICON_TI_CLOUD), kGroupForms },
			{ "ImageSpace", T(TKEY("category_imagespace"), "ImageSpace"), Icons::TI(ICON_TI_APERTURE), kGroupForms },
			{ "Lighting Template", T(TKEY("category_lighting_template"), "Lighting Template"), Icons::TI(ICON_TI_SUN_LOW), kGroupForms },
			{ "Cell Lighting", T(TKEY("category_cell_lighting"), "Cell Lighting"), Icons::TI(ICON_TI_BORDER_ALL), kGroupForms },
			{ "Volumetric Lighting", T(TKEY("category_volumetric_lighting"), "Volumetric Lighting"), Icons::TI(ICON_TI_CLOUD_FOG), kGroupForms },
			{ "Shader Particle Geometry", T(TKEY("category_shader_particle"), "Shader Particle Geometry"), Icons::TI(ICON_TI_CHART_BUBBLE), kGroupForms },
			{ "Lens Flare", T(TKEY("category_lens_flare"), "Lens Flare"), Icons::TI(ICON_TI_NORTH_STAR), kGroupForms },
			{ "Visual Effect", T(TKEY("category_visual_effect"), "Visual Effect"), Icons::TI(ICON_TI_SPARKLES), kGroupForms },
			{ "Locations", T(TKEY("category_locations"), "Locations"), Icons::TI(ICON_TI_MAP_PIN), kGroupScene },
			{ "Scene Manager", T(TKEY("category_scene_manager"), "Scene Manager"), Icons::TI(ICON_TI_STACK_2), kGroupScene },
			{ "Light Editor", T(TKEY("category_lighting_editor"), "Light Editor"), Icons::TI(ICON_TI_BULB), kGroupTools },
		} };
	}

	const BrowserCategory* FindBrowserCategory(const std::array<BrowserCategory, 11>& categories, const std::string& id)
	{
		const auto it = std::ranges::find_if(categories, [&id](const BrowserCategory& category) { return id == category.id; });
		return it != categories.end() ? &*it : nullptr;
	}
}

void EditorWindow::DrawBrowserSidebar(bool compact)
{
	const auto& style = ImGui::GetStyle();
	const float scale = Util::GetUIScale();
	const float rowHeight = ImGui::GetFrameHeight();
	const auto categories = GetBrowserCategories();
	const char* groupNames[] = {
		T(TKEY("browser_group_forms"), "Forms"),
		T(TKEY("browser_group_scene"), "Scene"),
		T(TKEY("browser_group_tools"), "Tools"),
	};

	auto countFor = [this](const std::string& id) -> std::string {
		if (const auto* widgets = FormListPage::GetCollection(*this, id))
			return std::to_string(widgets->size());
		if (id == "Locations") {
			if (auto* manager = SceneSettingsManager::GetSingleton())
				return std::to_string(manager->GetAuthoredLocationTargets().size());
		} else if (id == "Light Editor") {
			if (const size_t lights = lightEditor.GetLightCount())
				return std::to_string(lights);
		}
		return {};
	};

	if (ImGui::BeginChild("##CategoriesList", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {
		int group = -1;
		for (const auto& category : categories) {
			if (category.group != group) {
				if (group != -1) {
					if (compact)
						ImGui::Separator();
					else
						ImGui::Spacing();
				}
				group = category.group;
				if (!compact)
					Util::Text::Disabled("%s", groupNames[group]);
			}

			ImGui::PushID(category.id);
			const bool selected = m_selectedCategory == category.id;
			const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_Header, clear);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, clear);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, clear);
			if (ImGui::Selectable("##category", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight)))
				m_selectedCategory = category.id;
			ImGui::PopStyleColor(3);

			const bool hovered = ImGui::IsItemHovered();
			// Selectable pads its rect by half ItemSpacing, so neighbours touch; pull x inside the child's clip and leave a row gap.
			const float padL = IM_TRUNC(style.ItemSpacing.x * 0.5f);
			const float halfGap = style.ItemInnerSpacing.x * 0.5f;
			const ImVec2 min(ImGui::GetItemRectMin().x + padL, ImGui::GetItemRectMin().y + halfGap);
			const ImVec2 max(ImGui::GetItemRectMax().x - (style.ItemSpacing.x - padL), ImGui::GetItemRectMax().y - halfGap);
			const float itemHeight = max.y - min.y;
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			if (selected || hovered)
				drawList->AddRectFilled(min, max, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), style.FrameRounding);
			if (selected) {
				drawList->PushClipRect(min, ImVec2(min.x + kSidebarStripeWidth * scale, max.y), true);
				drawList->AddRectFilled(min, max, ImGui::GetColorU32(Util::Colors::GetAccent()), style.FrameRounding);
				drawList->PopClipRect();
			}

			const ImU32 iconColor = ImGui::GetColorU32(selected ? Util::Colors::GetAccent() : Util::Colors::GetSecondary());
			const float iconBox = ImGui::GetFontSize();
			const float iconY = min.y + (itemHeight - iconBox) * 0.5f;
			if (compact) {
				Icons::DrawCenteredGlyph(drawList, ImVec2(min.x + (max.x - min.x - iconBox) * 0.5f, iconY), ImVec2(iconBox, iconBox), category.icon, iconColor);
				if (hovered)
					ImGui::SetTooltip("%s", category.label);
			} else {
				const float edgeInset = std::max(style.FramePadding.x, kSidebarStripeWidth * scale);
				const float iconX = min.x + edgeInset;
				Icons::DrawCenteredGlyph(drawList, ImVec2(iconX, iconY), ImVec2(iconBox, iconBox), category.icon, iconColor);

				const std::string count = countFor(category.id);
				const float countWidth = count.empty() ? 0.0f : ImGui::CalcTextSize(count.c_str()).x;
				const float textY = min.y + (itemHeight - ImGui::GetTextLineHeight()) * 0.5f;
				const float labelX = iconX + iconBox + style.ItemSpacing.x * 1.5f;
				const float labelMax = max.x - edgeInset - (countWidth > 0.0f ? countWidth + style.ItemSpacing.x : 0.0f);
				const ImVec2 labelSize = ImGui::CalcTextSize(category.label);
				ImGui::RenderTextEllipsis(drawList, ImVec2(labelX, textY), ImVec2(labelMax, textY + labelSize.y), labelMax,
					category.label, nullptr, &labelSize);
				if (labelX + labelSize.x > labelMax && hovered)
					ImGui::SetTooltip("%s", category.label);
				if (!count.empty())
					drawList->AddText(ImVec2(max.x - edgeInset - countWidth, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), count.c_str());
			}
			ImGui::PopID();

			// The Scene Manager panel has no room for a feature column, so it owns one here.
			if (selected && !compact && std::string_view(category.id) == "Scene Manager")
				SceneSettingsUI::DrawSceneManagerCategoryFeatures();
		}
	}
	ImGui::EndChild();

	// Collapse / expand, kept at the bottom of the column.
	const bool sceneManager = m_selectedCategory == "Scene Manager";
	const float button = BrowserUI::IconButtonSize();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - button) * (compact ? 0.5f : 1.0f));
	ImGui::BeginDisabled(sceneManager);
	if (BrowserUI::IconButton("##SidebarCompact", Icons::TI(compact ? ICON_TI_CHEVRON_RIGHT : ICON_TI_CHEVRON_LEFT),
			sceneManager ? T(TKEY("sidebar_compact_scene_manager"), "Scene Manager keeps the full sidebar for its feature list.") :
						   (compact ? T(TKEY("sidebar_expand"), "Expand the sidebar") : T(TKEY("sidebar_collapse"), "Collapse the sidebar to icons")))) {
		settings.browserSidebarCompact = !settings.browserSidebarCompact;
		Save();
	}
	ImGui::EndDisabled();
}

void EditorWindow::DrawBrowserPage()
{
	const auto categories = GetBrowserCategories();
	const BrowserCategory* category = FindBrowserCategory(categories, m_selectedCategory);
	if (!category) {
		// An id from an older settings file: fall back to the weather list.
		m_selectedCategory = "Weather";
		category = FindBrowserCategory(categories, m_selectedCategory);
	}

	const std::string id = category->id;
	ClaimTimePeriodSet(id == "Weather"                            ? TimePeriodSet::Weather :
					   id == "Locations" || id == "Scene Manager" ? TimePeriodSet::Scene :
																	TimePeriodSet::None);
	if (id == "Light Editor") {
		lightEditor.DrawSettings();
		return;
	}

	if (id == "Locations" || id == "Scene Manager") {
		const bool locations = id == "Locations";
		std::string subtitle;
		if (locations) {
			if (auto* manager = SceneSettingsManager::GetSingleton())
				subtitle = I18n::GetSingleton()->Format(TKEY("locations_count"),
					{ { "count", std::to_string(manager->GetAuthoredLocationTargets().size()) } }, "{count} on your list");
		}
		BrowserUI::PageHeader(category->icon, category->label, subtitle.c_str());
		BeginScrollableContent(locations ? "##LocationsScroll" : "##SceneManagerScroll");
		if (locations)
			SceneSettingsUI::DrawLocationBrowser();
		else
			SceneSettingsUI::DrawSceneManagerPanel();
		EndScrollableContent();
		return;
	}

	FormListPage::Context context;
	context.editor = this;
	context.category = id;
	context.title = category->label;
	context.icon = category->icon;
	context.widgets = FormListPage::GetCollection(*this, id);
	context.hasSavedFile = [this](Widget* widget) { return HasCachedJsonAttachment(widget); };
	context.refreshSavedFiles = [this](const std::vector<Widget*>& widgets) { RefreshJsonAttachmentCache(widgets); };
	context.requestDeleteSavedFile = [this](Widget* widget) {
		pendingDeleteWidget = widget;
		pendingDeletePopupRequested = true;
	};
	context.resolveEditorId = [](RE::TESForm* form, const WidgetVec& widgets) { return ResolveEditorId(form, widgets); };
	FormListPage::Draw(m_formList, context);
}

void EditorWindow::OpenBaseSettings(const std::string& featureShortName)
{
	FeatureSettingsWindow::Select(featureShortName);
	if (!settings.showFeaturesWindow) {
		settings.showFeaturesWindow = true;
		Save();
	}
}

void EditorWindow::ShowObjectsWindow()
{
	if (std::exchange(m_focusBrowser, false))
		ImGui::SetNextWindowFocus();
	Util::BeginWithCustomHeader(T(TKEY("weather_lighting_browser"), "CS Editor Browser"), nullptr);

	// Reset filter state when the user switches categories so stale column
	// selections (e.g. Status) don't hide all items in the new category.
	if (m_selectedCategory != m_previousSelectedCategory) {
		m_formList.ResetFilters();
		m_previousSelectedCategory = m_selectedCategory;
	}

	const float scale = Util::GetUIScale();
	// Scene Manager lists its features under its sidebar entry, so it always gets the full sidebar.
	const bool compact = settings.browserSidebarCompact && m_selectedCategory != "Scene Manager";
	const float expandedWidth = kBrowserSidebarWidth * scale;
	const float compactWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 4.0f;

	if (ImGui::BeginTable("ObjectTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
		// A NoResize fixed column is pinned to its init width each frame, overriding drags and imgui.ini.
		ImGui::TableSetupColumn(T(TKEY("categories"), "Categories"),
			ImGuiTableColumnFlags_WidthFixed | (compact ? ImGuiTableColumnFlags_NoResize : ImGuiTableColumnFlags_None),
			compact ? compactWidth : expandedWidth);
		ImGui::TableSetupColumn(T(TKEY("objects"), "Objects"), ImGuiTableColumnFlags_WidthStretch);

		// Widths must be set before TableNextRow: the first row locks the layout, after which they are ignored.
		const float currentWidth = ImGui::GetCurrentTable()->Columns[0].WidthRequest;
		if (compact) {
			// Remember the dragged width so expanding restores it rather than the default.
			if (m_sidebarWasCompact != true && currentWidth > compactWidth)
				m_sidebarExpandedWidth = currentWidth;
		} else if (resetLayout || m_sidebarWasCompact != false) {
			// On expand, and on the first frame, where imgui.ini may still hold the skinny compact width.
			const bool restoreDragged = !resetLayout && m_sidebarExpandedWidth > compactWidth;
			const bool keepSaved = !m_sidebarWasCompact.has_value() && !resetLayout && currentWidth > compactWidth * 1.5f;
			if (!keepSaved)
				ImGui::TableSetColumnWidth(0, restoreDragged ? m_sidebarExpandedWidth : expandedWidth);
		}
		m_sidebarWasCompact = compact;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		DrawBrowserSidebar(compact);

		ImGui::TableSetColumnIndex(1);
		if (ImGui::BeginChild("##ObjectsContent", { 0, 0 }, ImGuiChildFlags_None, kStickyHeaderFlags))
			DrawBrowserPage();
		ImGui::EndChild();

		ImGui::EndTable();
	}

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

	ImGui::End();
}

void EditorWindow::OpenCellLighting(RE::TESObjectCELL* cell, bool notify)
{
	if (!cell)
		return;
	if (!currentCellLightingWidget || currentCellLightingWidget->cell != cell) {
		currentCellLightingWidget = std::make_unique<CellLightingWidget>(cell);
		currentCellLightingWidget->CacheFormData();
		currentCellLightingWidget->Load(notify);
	}
	currentCellLightingWidget->SetOpen(true);
	currentCellLightingWidget->RequestFocus();
}

/// Insets that keep the preview clear of the window and frame borders it sits inside.
static ImVec2 GetViewportBorderInsets()
{
	const auto& style = ImGui::GetStyle();
	return ImVec2(
		style.WindowBorderSize > 0.0f ? std::ceil((style.WindowBorderSize + 1.0f) * 0.5f) : 0.0f,
		std::max(0.0f, std::ceil((style.FrameBorderSize - 1.0f) * 0.5f)));
}

void EditorWindow::ShowViewportWindow()
{
	const ImU32 borderHoveredColor = ImGui::GetColorU32(ImGuiCol_SeparatorHovered);
	const ImU32 borderActiveColor = ImGui::GetColorU32(ImGuiCol_SeparatorActive);
	const ImU32 gripHoveredColor = ImGui::GetColorU32(ImGuiCol_ResizeGripHovered);
	const ImU32 gripActiveColor = ImGui::GetColorU32(ImGuiCol_ResizeGripActive);
	// The preview fills the window, so the stock grips and borders are drawn by hand below instead.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_ResizeGrip, IM_COL32(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, IM_COL32(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, IM_COL32(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, IM_COL32(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_SeparatorActive, IM_COL32(0, 0, 0, 0));
	const auto popStyle = []() {
		ImGui::PopStyleColor(5);
		ImGui::PopStyleVar(2);
	};

	const char* windowName = T(TKEY("viewport"), "Viewport");
	const ImVec2 borderInsets = GetViewportBorderInsets();
	struct ViewportSizeConstraint
	{
		ImGuiWindow* window;
		ImVec2 frameSize;
	};
	ViewportSizeConstraint constraint{
		ImGui::FindWindowByName(windowName),
		ImVec2(borderInsets.x * 2.0f, ImGui::GetFrameHeight() + borderInsets.x + borderInsets.y)
	};
	// Resizing drives width from height on the vertical borders and splits the difference on the
	// corners, so the window keeps the game aspect ratio whichever edge is dragged.
	ImGui::SetNextWindowSizeConstraints(ImGui::GetStyle().WindowMinSize, ImVec2(FLT_MAX, FLT_MAX), [](ImGuiSizeCallbackData* data) {
		const auto& constraint = *static_cast<const ViewportSizeConstraint*>(data->UserData);
		const auto displaySize = ImGui::GetIO().DisplaySize;
		const float aspectRatio = displaySize.x / displaySize.y;
		float imageWidth = data->DesiredSize.x - constraint.frameSize.x;
		const float imageHeight = data->DesiredSize.y - constraint.frameSize.y;
		if (auto* window = constraint.window) {
			const ImGuiID activeID = ImGui::GetActiveID();
			if (activeID == ImGui::GetWindowResizeBorderID(window, ImGuiDir_Up) || activeID == ImGui::GetWindowResizeBorderID(window, ImGuiDir_Down)) {
				imageWidth = imageHeight * aspectRatio;
			} else if (activeID == ImGui::GetWindowResizeCornerID(window, 0) || activeID == ImGui::GetWindowResizeCornerID(window, 1)) {
				const float aspectRatioSquared = aspectRatio * aspectRatio;
				imageWidth = (imageWidth * aspectRatioSquared + imageHeight * aspectRatio) / (aspectRatioSquared + 1.0f);
			}
		}
		const auto minSize = ImGui::GetStyle().WindowMinSize;
		const float minHeight = std::max(minSize.y, ImGui::GetFrameHeight() + std::max(0.0f, ImGui::GetStyle().WindowRounding - 1.0f));
		const float minWidth = std::ceil(std::max(minSize.x, (minHeight - constraint.frameSize.y) * aspectRatio + constraint.frameSize.x));
		data->DesiredSize.x = std::max(std::round(imageWidth + constraint.frameSize.x), minWidth);
		data->DesiredSize.y = std::round((data->DesiredSize.x - constraint.frameSize.x) / aspectRatio + constraint.frameSize.y); }, &constraint);

	// Begin() returns false while collapsed; Draw() reads this to skip next frame's framebuffer copy.
	viewportCollapsed = !Util::BeginWithRoundedClose(windowName, nullptr,
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (viewportCollapsed) {
		ImGui::End();
		popStyle();
		return;
	}

	auto* window = ImGui::GetCurrentWindow();
	auto* drawList = ImGui::GetWindowDrawList();
	ImRect imageRect = window->InnerRect;
	if (!window->DockIsActive) {
		imageRect.Min.x += borderInsets.x;
		imageRect.Min.y += borderInsets.y;
		imageRect.Max.x -= borderInsets.x;
		imageRect.Max.y -= borderInsets.x;
	}
	ImRect clipRect = imageRect;
	clipRect.ClipWith(window->OuterRectClipped);
	ImGui::PushClipRect(clipRect.Min, clipRect.Max, false);
	if (window->DockIsActive) {
		// Docking ignores the size constraint, so letterbox the preview inside the node instead.
		const auto displaySize = ImGui::GetIO().DisplaySize;
		const float aspectRatio = displaySize.x / displaySize.y;
		const float imageWidth = std::min(imageRect.GetWidth(), imageRect.GetHeight() * aspectRatio);
		imageRect.Max = ImVec2(imageRect.Min.x + imageWidth, imageRect.Min.y + imageWidth / aspectRatio);
		drawList->AddRectFilled(window->InnerRect.Min, window->InnerRect.Max, ImGui::GetColorU32(ImGuiCol_WindowBg));
	}
	ImGui::SetCursorScreenPos(imageRect.Min);
	const ImVec2 imageSize = imageRect.GetSize();

	if (tempTexture && tempTexture->srv) {
		// tempTexture is a raw copy of the framebuffer RT; its alpha is not guaranteed to be 1,
		// so a plain ImGui::Image can show the dimmed backdrop through as a transparency cutout.
		drawList->AddRectFilled(imageRect.Min, imageRect.Max, ImGui::GetColorU32(IM_COL32_BLACK));
		ImGui::Image((void*)tempTexture->srv.get(), imageSize);
	} else {
		drawList->AddRectFilled(imageRect.Min, imageRect.Max, ImGui::GetColorU32(ImGuiCol_WindowBg));
		ImGui::TextDisabled("%s", T(TKEY("viewport_unavailable"), "Viewport unavailable"));
	}

	if (!window->DockIsActive) {
		const ImRect hostRect = window->Viewport->GetMainRect();
		drawList->PushClipRect(hostRect.Min, hostRect.Max);
		if (window->WindowBorderSize > 0.0f)
			drawList->AddRect(window->Pos, window->Rect().Max, ImGui::GetColorU32(ImGuiCol_Border), window->WindowRounding, ImDrawFlags_RoundCornersTop, window->WindowBorderSize);
		const bool borderHeld = window->ResizeBorderHeld != -1;
		const int resizeBorder = borderHeld ? window->ResizeBorderHeld : window->ResizeBorderHovered;
		if (resizeBorder != -1) {
			constexpr float minResizeBorderThickness = 2.0f;
			const float rounding = window->WindowRounding;
			const ImVec2 borderMin(window->Pos.x + 0.5f, window->Pos.y + 0.5f);
			const ImVec2 borderMax(window->Rect().Max.x - 0.5f, window->Rect().Max.y - 0.5f);
			const ImVec2 topLeftCenter(borderMin.x + rounding, borderMin.y + rounding);
			const ImVec2 topRightCenter(borderMax.x - rounding, borderMin.y + rounding);
			switch (resizeBorder) {
			case ImGuiDir_Left:
				drawList->PathLineTo(ImVec2(borderMin.x, borderMax.y));
				drawList->PathArcTo(topLeftCenter, rounding, IM_PI, IM_PI * 1.25f);
				break;
			case ImGuiDir_Right:
				drawList->PathArcTo(topRightCenter, rounding, -IM_PI * 0.25f, 0.0f);
				drawList->PathLineTo(ImVec2(borderMax.x, borderMax.y));
				break;
			case ImGuiDir_Up:
				drawList->PathArcTo(topLeftCenter, rounding, IM_PI * 1.25f, IM_PI * 1.5f);
				drawList->PathArcTo(topRightCenter, rounding, IM_PI * 1.5f, IM_PI * 1.75f);
				break;
			case ImGuiDir_Down:
				drawList->PathLineTo(ImVec2(borderMin.x, borderMax.y));
				drawList->PathLineTo(borderMax);
				break;
			}
			drawList->PathStroke(borderHeld ? borderActiveColor : borderHoveredColor, ImDrawFlags_None, std::max(minResizeBorderThickness, window->WindowBorderSize));
		}
		for (int cornerIndex = 0; cornerIndex < 2; ++cornerIndex) {
			const ImGuiID cornerID = ImGui::GetWindowResizeCornerID(window, cornerIndex);
			const bool held = ImGui::GetActiveID() == cornerID;
			if (!held && ImGui::GetHoveredID() != cornerID)
				continue;
			const ImU32 color = held ? gripActiveColor : gripHoveredColor;
			const float gripSize = ImGui::GetFontSize();
			const ImVec2 corner(cornerIndex == 0 ? window->Rect().Max.x : window->Pos.x, window->Rect().Max.y);
			const float direction = cornerIndex == 0 ? -1.0f : 1.0f;
			drawList->AddTriangleFilled(corner, ImVec2(corner.x + direction * gripSize, corner.y), ImVec2(corner.x, corner.y - gripSize), color);
		}
		drawList->PopClipRect();
	}

	ImGui::PopClipRect();
	ImGui::End();
	popStyle();
}

void EditorWindow::ShowWidgetWindow()
{
	const auto& io = ImGui::GetIO();
	auto* focusedWidget = io.KeyCtrl && !io.WantTextInput && lastFocusedWidgetKey ? FindWidget(*lastFocusedWidgetKey) : nullptr;
	if (focusedWidget && focusedWidget->IsOpen()) {
		// Ctrl+W closes the focused widget; Ctrl+R discards its unsaved changes.
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
			focusedWidget->SetOpen(false);
			lastFocusedWidgetKey.reset();
		} else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			DiscardWidgetChanges(*focusedWidget);
		}
	}

	ForEachWidget([this](Widget& widget) {
		if (widget.IsOpen())
			undoHistory.Watch(MakeUndoTarget(widget));
	});

	// Draw all open widgets using WidgetFactory template
	for (auto* collection : GetWidgetCollections())
		WidgetFactory::DrawOpenWidgets(*collection);

	// Draw current cell lighting widget if open
	if (currentCellLightingWidget && currentCellLightingWidget->IsOpen()) {
		currentCellLightingWidget->DrawWidget();
	}
}

void EditorWindow::DrawEditorWindows(float scale, float actionBarBottom)
{
	const float pad = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;
	auto width = ImGui::GetIO().DisplaySize.x;
	auto height = ImGui::GetIO().DisplaySize.y;
	const float availableWidth = width - pad * 3.0f;  // left pad + gap + right pad
	const float availableHeight = (height - actionBarBottom - pad * 2.0f) * 0.85f;
	const auto layoutCond = resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	// Browser gets up to half the available width, capped so ultrawide gives extra to viewport
	const float maxBrowserWidth = 960.0f * scale;
	const float browserWidth = std::min(availableWidth * 0.5f, maxBrowserWidth);
	const float viewportWidth = availableWidth - browserWidth;
	ImGui::SetNextWindowSize(ImVec2(browserWidth, availableHeight), layoutCond);
	ImGui::SetNextWindowPos(ImVec2(pad, actionBarBottom + pad), layoutCond);
	ShowObjectsWindow();

	if (IsViewportActive()) {
		// Size viewport height to match game aspect ratio so the preview fits snugly
		const float aspectRatio = width / height;
		const ImVec2 borderInsets = GetViewportBorderInsets();
		const float imageHeight = (viewportWidth - borderInsets.x * 2.0f) / aspectRatio;
		const float chromeHeight = ImGui::GetFrameHeight() + borderInsets.x + borderInsets.y;
		const float viewportHeight = imageHeight + chromeHeight;
		ImGui::SetNextWindowSize(ImVec2(viewportWidth, viewportHeight), layoutCond);
		ImGui::SetNextWindowPos(ImVec2(pad + browserWidth + pad, actionBarBottom + pad), layoutCond);
		viewportBottomY = actionBarBottom + pad + viewportHeight;
		ShowViewportWindow();
	} else {
		viewportBottomY = actionBarBottom + pad;
	}

	auto settingsWindowHeight = height * 0.25f;
	auto settingsWindowWidth = width * 0.25f;
	if (showSettingsWindow) {
		ImGui::SetNextWindowSize(ImVec2(settingsWindowWidth, settingsWindowHeight), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos({ (width / 2.0f) - (settingsWindowWidth / 2.0f), (height / 2.0f) - (settingsWindowHeight / 2.0f) }, ImGuiCond_Appearing);
		ShowSettingsWindow();
	}

	ShowWidgetWindow();

	// Locations are not form widgets, so their windows are drawn alongside the widget pass.
	SceneSettingsUI::DrawLocationWindows();

	// Show palette window
	PaletteWindow::GetSingleton()->Draw();

	// Show weather picker window
	WeatherPickerWindow::GetSingleton()->Draw();

	// Optional weather debug window
	CSEditor::DrawWeatherDebugWindow(&showWeatherDebug);

	// OverlayRenderer draws Effects11Editor only while the CS Editor is closed, so the editor hosts it.
	Effects11Editor::GetSingleton().Draw();

	const bool featuresWasOpen = settings.showFeaturesWindow;
	FeatureSettingsWindow::Draw(settings.showFeaturesWindow);
	if (featuresWasOpen != settings.showFeaturesWindow)
		Save();
}

void EditorWindow::RenderUI()
{
	// Apply editor UI scale
	ImGuiIO& io = ImGui::GetIO();
	float previousScale = ImGui::GetStyle().FontScaleMain;
	ImGui::GetStyle().FontScaleMain = settings.editorUIScale;
	// FontScaleMain only applies on a font-size refresh; without one the action bar measures at the old scale.
	ImGui::UpdateCurrentFontSize(0.0f);
	const EditorChromeScope chrome;

	Util::StepFade(widgetsFade, !widgetsHidden, io.DeltaTime);

	if (IsViewportActive() && widgetsFade > 0.0f) {
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * widgetsFade);
		auto* backgroundDrawList = ImGui::GetBackgroundDrawList();
		backgroundDrawList->AddRectFilled({ 0, 0 }, io.DisplaySize, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
		backgroundDrawList->AddRectFilled(
			{ 0, 0 }, io.DisplaySize,
			ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, ThemeManager::Constants::EDITOR_VIEWPORT_BACKGROUND_DIM_ALPHA)));
		ImGui::PopStyleVar();
	}

	// Global shortcuts yield to a focused text field so typing isn't hijacked.
	if (!io.WantTextInput) {
		// Ctrl+Z undoes; Ctrl+Y and Ctrl+Shift+Z redo.
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
			io.KeyShift ? PerformRedo() : PerformUndo();
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
			PerformRedo();

		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			if (io.KeyShift) {
				if (ScenePresetExport::CanExport())
					ScenePresetExport::Open();
			} else {
				SaveManager::SaveEditor(SaveManager::Trigger::User);
			}
		}
	}

	// Floating action bar: BeginMainMenuBar forces WindowRounding=0, so we own the window instead.
	// Edge inset stays in fixed screen pixels so a taller bar (from editor UI scale) sits closer to
	// the top instead of being pushed away by a scale-multiplied margin.
	const float scale = Util::GetUIScale();
	const float actionBarPad = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * 0.5f;
	// Same chrome height as widget title bars so File/icons/text share equal top/bottom air.
	const float actionBarHeight = Util::GetEditorChromeHeaderHeight();
	const float actionBarBottom = actionBarPad + actionBarHeight;
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const auto& style = ImGui::GetStyle();
		const float rounding = style.FrameRounding;
		const float frameH = ImGui::GetFrameHeight();
		const float yPad = std::max(0.0f, (actionBarHeight - frameH) * 0.5f);
		// Same fill as overlapping CS windows: theme WindowBg RGB at OVERLAP_MIN_ALPHA.
		const ImU32 bgRGB = ImGui::GetColorU32(ImGuiCol_WindowBg) & ~IM_COL32_A_MASK;
		ImVec4 barBg = ImGui::ColorConvertU32ToFloat4(bgRGB);
		barBg.w = ThemeManager::Constants::OVERLAP_MIN_ALPHA;

		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + actionBarPad, viewport->Pos.y + actionBarPad));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - actionBarPad * 2.0f, actionBarHeight));

		// Shrink the work area to the bar's footprint so dock nodes sit flush under it, aligned to its edges.
		auto* viewportP = static_cast<ImGuiViewportP*>(viewport);
		viewportP->BuildWorkInsetMin.x += actionBarPad;
		viewportP->BuildWorkInsetMin.y += actionBarBottom;
		viewportP->BuildWorkInsetMax.x += actionBarPad;
		viewportP->BuildWorkInsetMax.y += actionBarPad;

		const ImGuiDockNode* dockRoot = ImGui::DockBuilderGetNode(dockSpaceId);
		const ImDrawFlags barCorners = ImDrawFlags_RoundCornersAll & ~(dockRoot ? GetDockedBarCorners(dockRoot, dockRoot->Rect()) : ImDrawFlags_None);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.FramePadding.x, yPad));
		// Make action bar slightly more transparent
		barBg.w = ThemeManager::Constants::OVERLAP_MIN_ALPHA * 0.7f;
		// Background is drawn by hand below: window rounding can't square individual corners.
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32_BLACK_TRANS);

		constexpr ImGuiWindowFlags kActionBarFlags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoNavFocus;

		if (ImGui::Begin("##CSEditorActionBar", nullptr, kActionBarFlags)) {
			// BeginMenu outside a MenuBar uses vertical layout: SpanAvailWidth + open-on-hover,
			// so hovering anywhere on the bar opens File (Save/Close) and covers the icon row.
			// Force horizontal layout so menus behave like a toolbar without ImGuiWindowFlags_MenuBar
			// (which would reserve a second chrome strip under our custom-height bar).
			// MenuBarAppending must also be set: otherwise FindBestWindowPosForPopup treats the
			// dropdown as a side-child menu and parks it over this bar instead of below it.
			ImGuiWindow* barWindow = ImGui::GetCurrentWindow();
			const ImRect barRect = barWindow->Rect();
			barWindow->DrawList->AddRectFilled(barRect.Min, barRect.Max, ImGui::GetColorU32(barBg), rounding, barCorners);
			const ImGuiLayoutType barLayoutBackup = barWindow->DC.LayoutType;
			const bool menuBarAppendingBackup = barWindow->DC.MenuBarAppending;
			barWindow->DC.LayoutType = ImGuiLayoutType_Horizontal;
			barWindow->DC.MenuBarAppending = true;
			ImGui::AlignTextToFramePadding();
			// BeginMenu's highlight overhangs its label by ItemSpacing.x per side and it indents by half that; keep ItemInnerSpacing.x between highlights.
			const float menuGap = style.ItemSpacing.x * 1.5f + style.ItemInnerSpacing.x;

			if (ImGui::BeginMenu(T(TKEY("file"), "File"))) {
				if (MenuItemWithShortcutSubtext(T(TKEY("save"), "Save"), "Ctrl+S"))
					SaveManager::SaveEditor(SaveManager::Trigger::User);
				Util::AddTooltip(T(TKEY("save_tooltip"),
					"Saves your edited widgets and scene settings on this PC. To share them, use Export Preset."));
				if (ImGui::MenuItem(T(TKEY("discard_unsaved"), "Discard Unsaved Widget Changes"), nullptr, false, HasUnsavedChanges()))
					DiscardAllWidgetChanges();
				Util::AddTooltip(T(TKEY("discard_unsaved_tooltip"),
									 "Puts every widget with unsaved changes back to its saved file. Undo (Ctrl+Z) brings the changes back."),
					Util::kTooltipWhenDisabled);

				ImGui::Separator();

				const bool canExport = ScenePresetExport::CanExport();
				if (MenuItemWithShortcutSubtext(T(TKEY("export_preset"), "Export Preset..."), "Ctrl+Shift+S", canExport))
					ScenePresetExport::Open();
				Util::AddTooltip(ScenePresetExport::GetSummary(), Util::kTooltipWhenDisabled);

				ImGui::Separator();
				for (auto* collection : GetWidgetCollections())
					WidgetFactory::DrawCloseAllMenuItem(*collection);
				ImGui::EndMenu();
			}
			ImGui::SameLine(0.0f, menuGap);
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
					Util::AddTooltip(T(TKEY("interior_only_available"), "Only available in interior cells"), Util::kTooltipWhenDisabled);
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
			ImGui::SameLine(0.0f, menuGap);
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
					Util::AddTooltip(T(TKEY("viewport_unavailable_hdr"), "Viewport is unavailable when HDR Display is enabled"), Util::kTooltipWhenDisabled);
				}
				if (ImGui::Checkbox(T(TKEY("palette"), "Palette"), &PaletteWindow::GetSingleton()->open)) {
				}
				if (ImGui::Checkbox(T(TKEY("weather_picker"), "Weather Picker"), &WeatherPickerWindow::GetSingleton()->open)) {
				}
				ImGui::Checkbox(T(TKEY("weather_debug"), "Weather Debug"), &showWeatherDebug);
				Util::AddTooltip(T(TKEY("weather_debug_tooltip"), "Current and last weather details, plus rain & wetness analysis from other features"));
				if (ImGui::Checkbox(T(TKEY("base_settings_window"), "Base Settings"), &settings.showFeaturesWindow))
					Save();
				Util::AddTooltip(T(TKEY("base_settings_window_tooltip"),
					"Every feature's own settings, including Post Processing. They apply everywhere a scene layer does not "
					"override them."));

				// The editor, not Effects11Editor, owns restoring the main menu, hence Open/Close(false).
				auto& effects11Editor = Effects11Editor::GetSingleton();
				const bool effects11Loaded = globals::features::effects11.loaded;
				bool effects11Open = effects11Editor.IsOpen();
				ImGui::BeginDisabled(!effects11Loaded);
				if (ImGui::Checkbox(T(TKEY("effects11_window"), "Effects 11"), &effects11Open))
					effects11Open ? effects11Editor.Open(false) : effects11Editor.Close(false);
				ImGui::EndDisabled();
				if (!effects11Loaded)
					Util::AddTooltip(T(TKEY("effects11_window_unavailable"), "Effects 11 is not loaded"), Util::kTooltipWhenDisabled);

				if (ImGui::MenuItem(T(TKEY("reset_window_layout"), "Reset Window Layout"))) {
					resetLayout = true;
					Effects11Editor::GetSingleton().RequestLayoutReset();
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
			ImGui::SameLine(0.0f, menuGap);
			if (ImGui::BeginMenu(T(TKEY("help"), "Help"))) {
				ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor, "%s", T(TKEY("keyboard_shortcuts"), "Keyboard Shortcuts:"));
				ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_f"), "Ctrl+F: Focus search"));
				ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_s"), "Ctrl+S: Save every CS editor change"));
				ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_shift_s"), "Ctrl+Shift+S: Export preset"));
				ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_w"), "Ctrl+W: Close focused widget"));
				ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_r"), "Ctrl+R: Discard the focused widget's unsaved changes"));
				ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_z"), "Ctrl+Z: Undo"));
				ImGui::BulletText("%s", T(TKEY("shortcut_ctrl_y"), "Ctrl+Y or Ctrl+Shift+Z: Redo"));
				ImGui::BulletText("%s", T(TKEY("shortcut_enter"), "Enter: Open selected widget"));
				ImGui::BulletText("%s", T(TKEY("shortcut_esc"), "Esc: Close editor"));
				ImGui::Separator();
				ImGui::TextColored(Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor, "%s", T(TKEY("scene_settings_greyed_label"), "Why are some settings greyed?"));
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * kHelpNoteWrapFontScale);
				ImGui::TextUnformatted(T(TKEY("scene_settings_greyed_note"),
					"Greyed settings cannot be overridden on this page. Some cannot be overridden by any scene; others need a different kind, such as a location override."));
				ImGui::PopTextWrapPos();
				ImGui::Separator();
				ImGui::Text("%s", T(TKEY("total_objects"), "Total Objects:"));
				ImGui::BulletText(T(TKEY("weathers_count"), "Weathers: %d"), (int)weatherWidgets.size());
				ImGui::BulletText(T(TKEY("lighting_count"), "Lighting: %d"), (int)lightingTemplateWidgets.size());
				ImGui::BulletText(T(TKEY("imagespaces_count"), "ImageSpaces: %d"), (int)imageSpaceWidgets.size());
				ImGui::EndMenu();
			}

			auto menu = globals::menu;
			const auto& statusPalette = menu->GetTheme().StatusPalette;
			const auto& textColor = menu->GetTheme().Palette.Text;
			// One chrome size and one row Y for every icon button so scale changes move the bar as a unit.
			const float iconSize = frameH;
			const ImVec2 iconButtonSize(iconSize, iconSize);

			const ImVec2 barPos = ImGui::GetWindowPos();
			const float barMinY = barPos.y;
			const float iconY = barMinY + (actionBarHeight - iconSize) * 0.5f;
			const float textY = barMinY + Util::GetEditorChromeTextCursorOffsetY(actionBarHeight);

			const ImVec4 enabledColor = statusPalette.SuccessColor;
			const ImVec4 disabledColor = statusPalette.Error;
			// Soft red for paused time lives in DrawTimePauseToggle / DrawPausedAwareGameHourSlider.

			ImGui::SameLine();

			// Undo and redo: Tabler glyphs (not theme PNGs)
			const auto drawHistoryButton = [&](const char* id, const char* glyph, bool enabled, const std::string& tooltip, auto&& action) {
				ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, iconY));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
				{
					auto _style = Util::TransparentIconButtonStyle();
					ImGui::InvisibleButton(id, iconButtonSize);
					const ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
					Util::DrawRoundedButtonHighlight(bb, enabled && ImGui::IsItemHovered(), enabled && ImGui::IsItemActive());
					if (ImGui::IsItemClicked() && enabled)
						action();
					ImVec4 color = enabled ? textColor : statusPalette.Disable;
					if (!enabled)
						color.w = 0.5f;
					Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), bb.Min, bb.GetSize(), Icons::TI(glyph), ImGui::GetColorU32(color));
				}
				ImGui::PopStyleVar(2);
				Util::AddTooltip(tooltip.c_str());
			};
			{
				const bool canUndo = undoHistory.CanUndo();
				drawHistoryButton("##GlobalUndo", ICON_TI_ARROW_BACK_UP, canUndo,
					canUndo ? I18n::GetSingleton()->Format(TKEY("undo_states"), { { "count", std::to_string(undoHistory.GetUndoCount()) } }, "Undo (Ctrl+Z) - {count} states") :
							  T(TKEY("undo_no_changes"), "Undo (Ctrl+Z) - No changes to undo"),
					[this] { PerformUndo(); });
				ImGui::SameLine(0.0f, style.ItemSpacing.x);
				const bool canRedo = undoHistory.CanRedo();
				drawHistoryButton("##GlobalRedo", ICON_TI_ARROW_FORWARD_UP, canRedo,
					canRedo ? I18n::GetSingleton()->Format(TKEY("redo_states"), { { "count", std::to_string(undoHistory.GetRedoCount()) } }, "Redo (Ctrl+Y) - {count} states") :
							  T(TKEY("redo_no_changes"), "Redo (Ctrl+Y) - Nothing to redo"),
					[this] { PerformRedo(); });
			}

			// Export preset: the one on-screen entry to the universal export dialog. It covers every
			// context, so it sits with the other editor-wide actions instead of on each scene page.
			ImGui::SameLine(0.0f, style.ItemSpacing.x);
			{
				const bool canExport = ScenePresetExport::CanExport();
				ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, iconY));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
				{
					auto _style = Util::TransparentIconButtonStyle();
					ImGui::InvisibleButton("##GlobalExportPreset", iconButtonSize);
					const ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
					Util::DrawRoundedButtonHighlight(bb, canExport && ImGui::IsItemHovered(), canExport && ImGui::IsItemActive());
					if (ImGui::IsItemClicked() && canExport)
						ScenePresetExport::Open();
					ImVec4 exportCol = canExport ? textColor : statusPalette.Disable;
					if (!canExport)
						exportCol.w = 0.5f;
					Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), bb.Min, bb.GetSize(),
						SceneActionIcons::kExport, ImGui::GetColorU32(exportCol));
				}
				ImGui::PopStyleVar(2);
				const std::string exportTooltip = std::format("{} (Ctrl+Shift+S)\n{}", T(TKEY("export_preset"), "Export Preset..."),
					canExport ?
						ScenePresetExport::GetSummary() :
						T(TKEY("export_preset_empty_tooltip"), "Nothing to export yet: author scene settings, save form edits, or load Post Processing or an Effects 11 preset first."));
				Util::AddTooltip(exportTooltip.c_str(), Util::kTooltipWhenDisabled);
			}

			// Delete authored scene changes: global action, same row as Undo.
			ImGui::SameLine(0.0f, style.ItemSpacing.x);
			{
				auto* sceneManager = globals::sceneSettingsManager;
				const bool canDelete = sceneManager && sceneManager->HasAnyUserEntries();
				ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, iconY));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
				{
					auto _style = Util::TransparentIconButtonStyle();
					ImGui::InvisibleButton("##DeleteSceneChanges", iconButtonSize);
					const ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
					Util::DrawRoundedButtonHighlight(bb, canDelete && ImGui::IsItemHovered(), canDelete && ImGui::IsItemActive());
					if (ImGui::IsItemClicked() && canDelete) {
						deleteSceneChangesConfirmation.title = T(TKEY("scene_page_delete_changes_title"), "Delete changes");
						deleteSceneChangesConfirmation.message = T(TKEY("scene_page_delete_changes_message"),
							"Remove every setting you have authored, in every context, and let the installed preset drive the "
							"scene?\n\nSettings you removed from the preset come back too. This cannot be undone.");
						deleteSceneChangesConfirmation.confirmLabel = T(TKEY("scene_page_delete_changes"), "Delete Changes");
						deleteSceneChangesConfirmation.cancelLabel = T(TKEY("cancel"), "Cancel");
						deleteSceneChangesConfirmation.Request();
					}
					ImVec4 deleteCol = canDelete ? statusPalette.Error : statusPalette.Disable;
					if (!canDelete)
						deleteCol.w = 0.5f;
					Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), bb.Min, bb.GetSize(),
						SceneActionIcons::kDelete, ImGui::GetColorU32(deleteCol));
				}
				ImGui::PopStyleVar(2);
				Util::AddTooltip(canDelete ?
									 T(TKEY("scene_page_delete_changes_tooltip"),
										 "Deletes your authored scene changes everywhere so the installed preset applies again.") :
									 T(TKEY("scene_page_delete_changes_empty_tooltip"),
										 "No authored scene changes to delete."),
					Util::kTooltipWhenDisabled);
			}

			// Right-aligned items: X from the trailing edge; Y shares iconY / textY with the left cluster.
			const float clipRight = barPos.x + ImGui::GetWindowSize().x - style.FramePadding.x;
			const float barMinX = barPos.x;
			const float barWidth = ImGui::GetWindowSize().x;
			const float& itemSpacing = ImGui::GetStyle().ItemSpacing.x;
			const float sliderWidth = kMenuBarSliderWidth * scale * 0.75f;
			const float halfSliderWidth = sliderWidth * 0.5f;

			float rightCursor = clipRight;

			rightCursor -= iconSize;
			const float xButtonX = rightCursor;

			rightCursor -= itemSpacing + iconSize;
			const float saveButtonX = rightCursor;

			constexpr float kDividerThickness = 1.0f;
			std::array<float, 2> dividerX{};
			int dividerCount = 0;
			rightCursor -= itemSpacing + kDividerThickness;
			dividerX[dividerCount++] = rightCursor;

			// Time scrubber split in half - game time and time speed
			rightCursor -= itemSpacing + halfSliderWidth;
			const float timeSpeedSliderX = rightCursor;

			const float gameTimeSliderWidth = halfSliderWidth * kMenuBarGameTimeWidthScale;
			rightCursor -= itemSpacing + gameTimeSliderWidth;
			const float gameTimeSliderX = rightCursor;

			rightCursor -= itemSpacing + iconSize;
			const float pauseButtonX = rightCursor;

			float freeCameraX = 0, playModeX = 0;
			rightCursor -= itemSpacing + kDividerThickness;
			dividerX[dividerCount++] = rightCursor;
			rightCursor -= itemSpacing + iconSize;
			playModeX = rightCursor;
			rightCursor -= itemSpacing + iconSize;
			freeCameraX = rightCursor;

			// Preview mode status text
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

			// Weather lock + name, and period title, centered in the header
			const bool weatherLocked = IsWeatherLockedByUser();
			RE::TESWeather* statusWeather = GetLockedWeather();
			if (!statusWeather) {
				if (auto* sky = RE::Sky::GetSingleton())
					statusWeather = sky->currentWeather;
			}
			const char* weatherName = nullptr;
			if (statusWeather)
				weatherName = statusWeather->GetFormEditorID();
			if (!weatherName || !*weatherName)
				weatherName = statusWeather ? T(TKEY("unknown"), "Unknown") : nullptr;

			char periodBuf[64];
			std::snprintf(periodBuf, sizeof(periodBuf), "(%s)", TOD::GetPeriodName(TOD::GetActivePeriod()));
			const float periodWidth = ImGui::CalcTextSize(periodBuf).x;

			const float lockBadgeSize = Util::GetLockStatusBadgeSize();
			const float weatherNameW = weatherName ? ImGui::CalcTextSize(weatherName).x : 0.0f;
			const float weatherStatusGap = itemSpacing * 2.0f;
			const float weatherBlockW = weatherName ? (lockBadgeSize + weatherStatusGap + weatherNameW) : 0.0f;
			const float centerClusterW = weatherBlockW + (weatherBlockW > 0.0f ? itemSpacing : 0.0f) + periodWidth;
			const float centerClusterX = barMinX + (barWidth - centerClusterW) * 0.5f;

			if (showPreviewStatus) {
				ImGui::SetCursorScreenPos(ImVec2(previewStatusX, textY));
				ImGui::TextColored(Util::GetPulsingColor(enabledColor), "%s", previewStatusBuf);
			}

			{
				auto* drawList = ImGui::GetWindowDrawList();
				const ImU32 dividerColor = ImGui::GetColorU32(ImGuiCol_Separator);
				const float inset = actionBarHeight * 0.2f;
				for (int i = 0; i < dividerCount; ++i)
					drawList->AddLine({ dividerX[i], barMinY + inset }, { dividerX[i], barMinY + actionBarHeight - inset }, dividerColor, kDividerThickness);
			}

			float centerX = centerClusterX;
			if (weatherName) {
				const float badgeY = iconY + (iconSize - lockBadgeSize) * 0.5f;
				ImGui::SetCursorScreenPos(ImVec2(centerX, badgeY));
				if (Util::LockStatusBadgeButton("##ActionBarWeatherLock", weatherLocked,
						weatherLocked ? T(TKEY("unlock_weather"), "Unlock Weather") : T(TKEY("force_this_weather"), "Force This Weather"))) {
					if (weatherLocked)
						ReleaseUserWeatherLock();
					else if (statusWeather)
						LockWeatherForUser(statusWeather);
				}
				centerX += lockBadgeSize + weatherStatusGap;
				if (ImGuiWindow* window = ImGui::GetCurrentWindow())
					window->DC.CurrLineTextBaseOffset = 0.0f;
				ImGui::SetCursorScreenPos(ImVec2(centerX, textY));
				ImGui::TextColored(weatherLocked ? enabledColor : disabledColor, "%s", weatherName);
				centerX += weatherNameW + itemSpacing;
				ImGui::SetCursorScreenPos(ImVec2(centerX, textY));
				ImGui::TextUnformatted(periodBuf);
			} else {
				ImGui::SetCursorScreenPos(ImVec2(centerX, textY));
				ImGui::TextUnformatted(periodBuf);
			}

			// Toggle-style FA/Tabler glyph button: same chrome as the old image toggles.
			auto PlaceToggleIconButton = [&](const char* id, Icons::GlyphRef glyph, bool isActive, float posX, const ImVec4& activeColor) -> bool {
				ImGui::SetCursorScreenPos(ImVec2(posX, iconY));
				return DrawToggleIconButton(id, glyph, isActive, activeColor, iconButtonSize, ImGui::GetColorU32(textColor));
			};

			// Sits right of the centered cluster so toggling never shifts the status text.
			if (PlaceToggleIconButton("##ToggleWidgets", Icons::TI(widgetsHidden ? ICON_TI_CHEVRON_DOWN : ICON_TI_CHEVRON_UP), false,
					centerClusterX + centerClusterW + itemSpacing, enabledColor))
				widgetsHidden = !widgetsHidden;
			Util::AddTooltip(widgetsHidden ? T(TKEY("show_widgets"), "Show Widgets") : T(TKEY("hide_widgets"), "Hide Widgets"));

			{
				bool isActive = previewMode == PreviewMode::FreeCamera || previewMode == PreviewMode::FreeCameraLocked;
				if (PlaceToggleIconButton("##FreeCamera", Icons::TI(ICON_TI_SCAN_EYE), isActive, freeCameraX, enabledColor)) {
					if (isActive)
						ExitPreviewMode();
					else
						EnterPreviewMode(PreviewMode::FreeCamera);
				}
				Util::AddTooltip(isActive ?
									 T(TKEY("exit_free_camera"), "Exit Free Camera (right-click or Shift+End)") :
									 T(TKEY("free_camera_scroll"), "Free Camera (scroll to adjust speed)"));
			}
			{
				bool isActive = previewMode == PreviewMode::PlayMode;
				if (PlaceToggleIconButton("##PlayMode", Icons::TI(ICON_TI_WALK), isActive, playModeX, enabledColor)) {
					if (isActive)
						ExitPreviewMode();
					else
						EnterPreviewMode(PreviewMode::PlayMode);
				}
				Util::AddTooltip(isActive ?
									 T(TKEY("exit_play_mode"), "Exit Play Mode (right-click or Shift+End)") :
									 T(TKEY("play_mode_walk"), "Play Mode - Walk around normally"));
			}

			{
				ImGui::SetCursorScreenPos(ImVec2(pauseButtonX, iconY));
				DrawTimePauseToggle("##GlobalPauseTime", iconButtonSize);
				Util::AddTooltip(IsTimePaused() ? T(TKEY("resume_time"), "Resume Time") : T(TKEY("pause_time"), "Pause Time"));
			}

			auto calendar = Util::Climate::GetCalendar();
			if (calendar && calendar->gameHour && calendar->timeScale) {
				ImGui::SetCursorScreenPos(ImVec2(gameTimeSliderX, iconY));
				ImGui::SetNextItemWidth(gameTimeSliderWidth);
				DrawPausedAwareGameHourSlider("##MenuBarGameTimeSlider");

				ImGui::SetCursorScreenPos(ImVec2(timeSpeedSliderX, iconY));
				ImGui::SetNextItemWidth(halfSliderWidth);
				DrawTimeScaleSlider("##MenuBarTimeScaleSlider", calendar);
			}

			// Save: the editor's own save, red while any widget has unsaved changes.
			{
				const bool unsaved = HasUnsavedChanges();
				ImGui::SetCursorScreenPos(ImVec2(saveButtonX, iconY));
				if (DrawToggleIconButton("##ActionBarSave", Icons::TI(ICON_TI_DEVICE_FLOPPY), false, enabledColor, iconButtonSize,
						ImGui::GetColorU32(unsaved ? Util::Colors::GetUnsaved() : textColor)))
					SaveManager::SaveEditor(SaveManager::Trigger::User);
				Util::AddTooltip(SaveManager::GetEditorSaveTooltip(unsaved));
			}

			// Close: white cross, no red fill
			ImGui::SetCursorScreenPos(ImVec2(xButtonX, iconY));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
			{
				auto _style = Util::TransparentIconButtonStyle();
				const bool closeClicked = ImGui::InvisibleButton("##ActionBarClose", iconButtonSize);
				const ImRect closeBb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				if (ImGui::IsItemHovered())
					Util::DrawRoundedButtonHighlight(closeBb, true, ImGui::IsItemActive(), ImGui::GetWindowDrawList());
				Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), closeBb.Min, closeBb.GetSize(),
					Icons::TI(ICON_TI_X), ImGui::GetColorU32(textColor));
				if (closeClicked)
					open = false;
			}
			ImGui::PopStyleVar();
			Util::AddTooltip(T(TKEY("close_cs_editor"), "Close CS Editor (Esc)"));

			barWindow->DC.LayoutType = barLayoutBackup;
			barWindow->DC.MenuBarAppending = menuBarAppendingBackup;
		}
		ImGui::End();
		ImGui::PopStyleColor(1);
		ImGui::PopStyleVar(3);
	}

	// Establish a viewport-wide DockSpace so all editor windows are snappable and dockable
	dockSpaceId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

	if (widgetsFade > 0.0f) {
		const Util::FadeScope fadeScope(widgetsFade);
		DrawEditorWindows(scale, actionBarBottom);
	}

	ScenePresetExport::Draw();

	DrawToolPrompt();

	if (deleteSceneChangesConfirmation.Draw()) {
		if (auto* sceneManager = globals::sceneSettingsManager) {
			// An export earlier this session left the mod layer holding what was on disk before it.
			sceneManager->ReloadOverwrites();
			undoHistory.Change(SceneSettingsUI::MakeUndoTarget(), [sceneManager] { sceneManager->ClearAllUserEntries(); });
		}
	}

	if (resetLayout)
		ResetWidgetTypeSizes();
	resetLayout = false;

	// Every target is drawn by now, so an interaction that ended this frame can be judged.
	undoHistory.EndFrame();

	// Render notifications on top of everything
	RenderNotifications();

	// Restore previous font scale
	ImGui::GetStyle().FontScaleMain = previousScale;
	ImGui::UpdateCurrentFontSize(0.0f);
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
	WeatherPickerWindow::GetSingleton()->Load();
	InvalidateJsonAttachmentCache();

	// Widgets load their saves as they are made, and saved form references resolve through the
	// collections of the forms they name, so those are populated first.
	Util::FormEditorIDIndex editorIDs;
	WidgetFactory::PopulateSimpleWidgets<RE::BGSArtObject>(artObjectWidgets, editorIDs);
	WidgetFactory::PopulateSimpleWidgets<RE::TESEffectShader>(effectShaderWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<ImageSpaceWidget, RE::TESImageSpace>(imageSpaceWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<VolumetricLightingWidget, RE::BGSVolumetricLighting>(volumetricLightingWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<PrecipitationWidget, RE::BGSShaderParticleGeometryData>(precipitationWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<LensFlareWidget, RE::BGSLensFlare>(lensFlareWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<ReferenceEffectWidget, RE::BGSReferenceEffect>(referenceEffectWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<LightingTemplateWidget, RE::BGSLightingTemplate>(lightingTemplateWidgets, editorIDs);
	WidgetFactory::PopulateWidgets<WeatherWidget, RE::TESWeather>(weatherWidgets, editorIDs);
}

void EditorWindow::ReloadFormEdits(const FormEditSources::FormKeySet& keys)
{
	ForEachWidget([&](Widget& widget) {
		if (keys.contains(widget.GetFormKey()))
			widget.Load(false);
	});
	InvalidateJsonAttachmentCache();
}

bool EditorWindow::IsViewportActive() const
{
	return settings.showViewport && !(globals::features::hdrDisplay.loaded && globals::features::hdrDisplay.settings.enableHDR);
}

void EditorWindow::UpdateOpenState()
{
	static bool wasOpen = false;

	// Runs even while closed so a Scene Manager panel's pause is always released.
	SceneSettingsUI::SyncTimePause();

	// The user can release the lock from the weather controls; it stops being ours the moment they do.
	if (weatherLockedByOverlay && !IsWeatherLocked())
		weatherLockedByOverlay = false;

	// Closing would discard a tool's unwritten changes, so a user's close waits for an answer. A forced
	// close (a loading screen) cannot wait, so it discards and says so.
	if (!open && wasOpen && !std::exchange(skipToolPrompt, false)) {
		const auto tools = GetExternalTools();
		if (const auto found = std::ranges::find_if(tools, [](ExternalTool* tool) { return tool->HasUnwrittenChanges(); }); found != tools.end()) {
			if (CanBeOpen()) {
				open = true;
				RequestToolPrompt(**found, ToolPromptReason::Close, [this] {
					skipToolPrompt = true;
					open = false;
				});
			} else {
				const auto what = (*found)->DescribeChanges();
				const std::string target = (*found)->GetTargetName();
				ShowNotification(std::vformat(T(TKEY("tool_changes_discarded"), "{}: changes discarded, not written to {}"), std::make_format_args(what, target)),
					Util::Colors::GetWarning(), kLongNotificationDuration);
			}
		}
	}

	if (open && !wasOpen) {
		widgetsHidden = false;
		widgetsFade = 1.0f;
		HideGameMenus();
		BackgroundBlur::SetCSEditorActive(IsViewportActive());
		overlayWeatherLockPending = true;
		Effects11Editor::GetSingleton().Close();  // Restores the menu first so returnToMenu below records it.
		returnToMenu = globals::menu->IsEnabled;
		globals::menu->IsEnabled = false;

	} else if (!open && wasOpen) {
		lightEditor.ResetOverrides();
		ShowGameMenus();
		BackgroundBlur::SetCSEditorActive(false);
		Effects11Editor::GetSingleton().Close(false);
		if (std::exchange(returnToMenu, false))
			globals::menu->IsEnabled = true;
		overlayWeatherLockPending = false;
		if (weatherLockedByOverlay) {
			UnlockWeather();
			weatherLockedByOverlay = false;
		} else if (auto* locked = GetLockedWeather(); locked && IsWeatherLocked()) {
			const char* name = locked->GetFormEditorID();
			const std::string weatherName = name && *name ? name : T(TKEY("unknown"), "Unknown");
			ShowNotification(
				std::vformat(T(TKEY("weather_still_locked"), "Weather is still locked: {}"), std::make_format_args(weatherName)),
				Util::Colors::GetWarning(), kLongNotificationDuration);
		}
		delete tempTexture;
		tempTexture = nullptr;
		for (auto& widget : weatherWidgets)
			if (auto* weatherWidget = dynamic_cast<WeatherWidget*>(widget.get()))
				weatherWidget->cloudTextureCache.clear();
	}

	if (overlayWeatherLockPending)
		LockWeatherForOverlay();

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

	if (!IsViewportActive()) {
		delete tempTexture;
		tempTexture = nullptr;
	} else if (!viewportCollapsed) {
		// Kept allocated while collapsed so expanding costs one stale frame rather than a reupload.
		auto renderer = globals::game::renderer;
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

						tempTexture = new Texture2D(texDesc, "CSEditor::ViewportCopy");
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

void EditorWindow::SaveUnsavedWidgets()
{
	ForEachWidget([](Widget& widget) {
		if (widget.HasUnsavedChanges())
			widget.Save();
	});
	anyWidgetUnsaved = false;
	lastUnsavedScan = -kUnsavedScanInterval;
}

bool EditorWindow::HasUnsavedChanges()
{
	// Scene Manager saves itself as it goes; it is unsaved only while an edit waits on that save.
	if (auto* sceneManager = SceneSettingsManager::GetSingleton(); sceneManager && sceneManager->HasUnsavedChanges())
		return true;

	const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
	const bool fullPass = now - lastUnsavedScan >= kUnsavedScanInterval;
	if (fullPass)
		lastUnsavedScan = now;

	bool found = false;
	ForEachWidget([&](Widget& widget) {
		if (!found && (fullPass || widget.IsOpen()))
			found = widget.HasUnsavedChanges();
	});
	if (fullPass)
		anyWidgetUnsaved = found;
	return anyWidgetUnsaved || found;
}

void EditorWindow::SaveSettings()
{
	settings.selectedCategory = m_selectedCategory;
	settings.widgetTypeSizes = GetWidgetTypeSizesJson();
	j = settings;
}

void EditorWindow::LoadSettings()
{
	if (!j.empty()) {
		try {
			settings = j;
		} catch (const nlohmann::json::exception& e) {
			logger::warn("Failed to deserialize editor settings, using defaults: {}", e.what());
		}
	}
	m_selectedCategory = settings.selectedCategory;
	if (m_selectedCategory == "Interior Only") {
		m_selectedCategory = "Weather";
		settings.selectedCategory = m_selectedCategory;
	}
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
			{ "Features", T(TKEY("features"), "Features") },
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

			if (ImGui::Checkbox(T(TKEY("skip_tool_discard_prompt"), "Discard unwritten tool changes without asking"), &settings.skipToolDiscardPrompt))
				Save();
			Util::AddTooltip(T(TKEY("skip_tool_discard_prompt_tooltip"), "Switching lights or closing the editor discards Light Editor changes not written to Light Placer, without asking first"));

			ImGui::Separator();
			ImGui::TextUnformatted(T(TKEY("ui_scale"), "UI Scale"));

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(T(TKEY("editor_ui_scale"), "Editor UI Scale"));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (BrowserUI::IconButton("##ResetUIScale", Icons::TI(ICON_TI_RESTORE), T(TKEY("reset_ui_scale_tooltip"), "Reset UI scale to default (100%)"))) {
				settings.editorUIScale = 1.0f;
				Save();
			}

			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat("##EditorUIScale", &settings.editorUIScale, 0.5f, 2.0f, "%.2f")) {
				Save();
			}
			Util::AddTooltip(T(TKEY("editor_ui_scale_tooltip"), "Scale the size of all editor UI elements (0.5 = 50%, 2.0 = 200%)"));

			ImGui::Separator();
			ImGui::TextUnformatted(T(TKEY("session_history"), "Session & History"));

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

		} else if (settingsSelectedCategory == "Features") {
			if (ImGui::Checkbox(T(TKEY("show_feature_debug"), "Show Debug sections"), &settings.showFeatureDebug)) {
				Save();
			}
			Util::AddTooltip(T(TKEY("show_feature_debug_tooltip"),
				"Show Debug dropdowns in the Features and Post Processing editors.\n"
				"Hidden by default because they are rarely useful when authoring feature preset baselines."));

		} else if (settingsSelectedCategory == "Flags") {
			if (ImGui::BeginTable("FlagsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn(T(TKEY("label"), "Label"), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(T(TKEY("colour"), "Colour"), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(T(TKEY("actions"), "Actions"), ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());

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
					ImGui::SetNextItemWidth(-1);
					if (ImGui::ColorEdit3(std::format("##Color{}", recordMarker.first).c_str(), (float*)&recordMarker.second)) {
						Save();
					}

					ImGui::TableSetColumnIndex(2);
					if (BrowserUI::IconButton(std::format("##{}", recordMarker.first).c_str(), Icons::TI(ICON_TI_TRASH), T(TKEY("delete"), "Delete")))
						markerToDelete = recordMarker.first;
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

				if (recordMarkers.size() < maxRecordMarkers) {
					const ImVec2 cellStart = ImGui::GetCursorScreenPos();
					const bool addMarker = Util::TableRowSelectable("##AddMarker", false, ImGuiSelectableFlags_SpanAllColumns);
					ImGui::SetCursorScreenPos(cellStart);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(T(TKEY("add_new_marker"), "Add new marker"));
					if (addMarker) {
						recordMarkers.insert({ std::format("New marker {}", recordMarkers.size()), { 0.5f, 0.5f, 0.5f, 1.0f } });
						Save();
					}
				}

				ImGui::EndTable();
			}
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

/** @brief Path of the editor's own settings file in the CommunityShaders folder. */
static std::filesystem::path GetEditorSettingsPath(const std::string& settingsFilename)
{
	return Util::PathHelpers::GetCommunityShaderPath() / (settingsFilename + ".json");
}

void EditorWindow::Save()
{
	// Favourites and status markers change through here; the browser rebuilds its rows from them.
	m_formList.Invalidate();
	SaveSettings();
	const auto file = GetEditorSettingsPath(settingsFilename);

	logger::info("Saving settings file: {}", file.string());

	if (!Util::FileHelpers::WriteJsonAtomically(file, j, 1, "editor settings"))
		logger::warn("Failed to write settings file: {}", file.string());
}

void EditorWindow::Load()
{
	const auto path = GetEditorSettingsPath(settingsFilename);
	// No settings file yet: keep the defaults untouched.
	if (!std::filesystem::exists(path))
		return;

	std::string error;
	auto loaded = Util::FileHelpers::ReadJsonFile(path, "editor settings", 0, &error);
	if (!loaded && error.empty()) {
		logger::warn("Failed to open settings file: {}", path.string());
		return;
	}
	if (loaded)
		j = std::move(*loaded);
	LoadSettings();
}

// Weather lock guard. A plain ForceWeather does not hold: scripts, climate timers and cell
// transitions keep driving the weather, so the engine's own call sites are redirected instead.
//
// Credits: Isoprovophlex
namespace
{
	// Toggled from the render thread, read by the hooked engine calls.
	std::atomic<RE::TESWeather*> g_lockedWeather{ nullptr };
	std::atomic_bool g_weatherLockActive{ false };

	// Set once InstallWeatherLockHooks has actually redirected both call sets; gates the
	// Lock Weather UI so a failed install can't be engaged as a silent no-op.
	std::atomic_bool g_weatherLockHooksInstalled{ false };

	constexpr std::uint8_t kCallOpcode = 0xE8;           // CALL rel32
	constexpr std::uint8_t kJumpOpcode = 0xE9;           // JMP rel32 (tail call)
	constexpr std::size_t kRelativeInstructionSize = 5;  // opcode + int32 displacement

	// A real SetWeather/ForceWeather call count is low single digits; far more than this means a
	// stale relocation ID matched unrelated bytes, not real sites.
	constexpr std::size_t kMaxPlausibleCallSites = 32;

	/** @brief A direct E8/E9 reference to a game function. */
	struct WeatherCallSite
	{
		std::uintptr_t address = 0;
		bool isCall = false;
	};

	RE::TESWeather* GetActiveLock()
	{
		return g_weatherLockActive.load(std::memory_order_acquire) ? g_lockedWeather.load(std::memory_order_acquire) : nullptr;
	}

	/** @brief Forces the sky onto the locked weather, claiming the override slot so nothing lerps away from it. */
	void ReapplyLock(RE::Sky* sky, RE::TESWeather* weather)
	{
		if (!sky || !weather)
			return;

		// ForceWeather resets the sky (sounds, region, flags); when the weather is already showing, only the override slot is missing.
		if (sky->currentWeather == weather && !sky->lastWeather) {
			sky->overrideWeather = weather;
			sky->lastWeatherUpdate = sky->currentGameHour;  // Renews the override's expiry, as ForceWeather does.
			return;
		}
		Util::ForceWeather(sky, weather, true);
	}

	void SetWeatherThunk(RE::Sky* sky, RE::TESWeather* weather, bool isOverride, bool accelerate)
	{
		if (!sky)
			return;

		auto* locked = GetActiveLock();
		if (!locked)
			sky->SetWeather(weather, isOverride, accelerate);
		else if (weather == locked)
			sky->SetWeather(locked, true, accelerate);
		else if (sky->currentWeather != locked || sky->overrideWeather != locked)
			ReapplyLock(sky, locked);
	}

	void ForceWeatherThunk(RE::Sky* sky, RE::TESWeather* weather, bool isOverride)
	{
		if (!sky)
			return;

		if (auto* locked = GetActiveLock())
			ReapplyLock(sky, locked);
		else
			Util::ForceWeather(sky, weather, isOverride);
	}

	/** @brief Scans the game's executable segment for direct call/jump references to a function. */
	std::vector<WeatherCallSite> FindDirectReferences(std::uintptr_t target)
	{
		std::vector<WeatherCallSite> sites;
		const auto text = REL::Module::get().segment(REL::Segment::textx);
		const auto begin = text.address();
		const auto end = begin + text.size();

		for (auto address = begin; address + kRelativeInstructionSize <= end; ++address) {
			const auto opcode = *reinterpret_cast<const std::uint8_t*>(address);
			if (opcode != kCallOpcode && opcode != kJumpOpcode)
				continue;

			std::int32_t displacement = 0;
			std::memcpy(&displacement, reinterpret_cast<const void*>(address + 1), sizeof(displacement));
			if (address + kRelativeInstructionSize + displacement == target)
				sites.push_back({ address, opcode == kCallOpcode });
		}
		return sites;
	}

	/** @brief Redirects every call site to the thunk, preserving call vs tail-jump semantics. */
	template <class Thunk>
	void InstallCallSiteHooks(const std::vector<WeatherCallSite>& sites, Thunk thunk)
	{
		auto& trampoline = SKSE::GetTrampoline();
		for (const auto& site : sites) {
			if (site.isCall)
				trampoline.write_call<kRelativeInstructionSize>(site.address, thunk);
			else
				trampoline.write_branch<kRelativeInstructionSize>(site.address, thunk);
		}
	}
}

void EditorWindow::InstallWeatherLockHooks()
{
	// Re-entry would rewrite an already-hooked site to branch at its own thunk.
	static std::atomic_bool installAttempted{ false };
	if (installAttempted.exchange(true, std::memory_order_acq_rel)) {
		logger::debug("[CSEditor] Weather lock hook install already attempted this session, skipping");
		return;
	}

	// Not exported by CommonLib, which keeps them in function-local statics (RE/S/Sky.cpp).
	// A relocation ID miss hard-terminates via stl::report_and_fail; that is unavoidable here.
	const REL::Relocation<std::uintptr_t> setWeather{ REL::RelocationID(25694, 26241) };
	const REL::Relocation<std::uintptr_t> forceWeather{ REL::RelocationID(25696, 26243) };

	const auto setWeatherSites = FindDirectReferences(setWeather.address());
	const auto forceWeatherSites = FindDirectReferences(forceWeather.address());

	if (setWeatherSites.empty() || forceWeatherSites.empty()) {
		logger::warn(
			"[CSEditor] Weather lock found {} SetWeather and {} ForceWeather call sites -- "
			"relocation IDs may be stale for this game version, hooks disabled",
			setWeatherSites.size(), forceWeatherSites.size());
		return;
	}
	if (setWeatherSites.size() > kMaxPlausibleCallSites || forceWeatherSites.size() > kMaxPlausibleCallSites) {
		logger::error(
			"[CSEditor] Weather lock found {} SetWeather and {} ForceWeather call sites, "
			"exceeding the plausible bound of {} -- likely a false-positive byte match, hooks disabled",
			setWeatherSites.size(), forceWeatherSites.size(), kMaxPlausibleCallSites);
		return;
	}

	// No AllocTrampoline: it would free the block other hooks branch through. write_call/write_branch
	// memoize their stub per destination, so this needs 2 stubs total regardless of call-site count.
	constexpr std::size_t kWorstCaseStubBytes = 2 * 14;
	if (SKSE::GetTrampoline().free_size() < kWorstCaseStubBytes) {
		logger::error("[CSEditor] Weather lock skipped: trampoline has {} bytes free, needs {}",
			SKSE::GetTrampoline().free_size(), kWorstCaseStubBytes);
		return;
	}

	try {
		InstallCallSiteHooks(setWeatherSites, SetWeatherThunk);
		InstallCallSiteHooks(forceWeatherSites, ForceWeatherThunk);
	} catch (const std::exception& e) {
		logger::error("[CSEditor] Weather lock hook installation failed, hooks disabled: {}", e.what());
		return;
	}

	g_weatherLockHooksInstalled.store(true, std::memory_order_release);
	logger::info("[CSEditor] Weather lock hooked {} SetWeather and {} ForceWeather call sites", setWeatherSites.size(), forceWeatherSites.size());
}

bool EditorWindow::AreWeatherLockHooksInstalled()
{
	return g_weatherLockHooksInstalled.load(std::memory_order_acquire);
}

void EditorWindow::MaintainWeatherLock()
{
	auto* locked = GetActiveLock();
	auto* sky = globals::game::sky;
	if (!locked || !sky)
		return;

	// The engine applies the release on its next sky update, so clear the flag before it lands.
	const bool releasePending = sky->flags.any(RE::Sky::Flags::kReleaseWeatherOverride);
	if (!releasePending && sky->currentWeather == locked && sky->overrideWeather == locked)
		return;

	sky->flags.reset(RE::Sky::Flags::kReleaseWeatherOverride);
	ReapplyLock(sky, locked);
}

bool EditorWindow::IsWeatherLocked() const
{
	return g_weatherLockActive.load(std::memory_order_acquire);
}

RE::TESWeather* EditorWindow::GetLockedWeather() const
{
	return g_lockedWeather.load(std::memory_order_acquire);
}

void EditorWindow::LockWeather(RE::TESWeather* weather)
{
	if (!weather)
		return;

	// ForceWeather clears the sky's region; UnlockWeather restores it so the engine doesn't reroll.
	if (auto* sky = globals::game::sky; sky && !IsWeatherLocked())
		regionBeforeLock = sky->region;

	g_lockedWeather.store(weather, std::memory_order_release);
	g_weatherLockActive.store(true, std::memory_order_release);
	MaintainWeatherLock();

	logger::info("Weather locked: {}", weather->GetFormEditorID() ? weather->GetFormEditorID() : "Unknown");
}

bool EditorWindow::IsWeatherLockedByUser() const
{
	return IsWeatherLocked() && !weatherLockedByOverlay;
}

void EditorWindow::LockWeatherForUser(RE::TESWeather* weather)
{
	LockWeather(weather);
	if (IsWeatherLocked()) {
		weatherLockedByOverlay = false;
		overlayWeatherLockPending = false;
	}
}

void EditorWindow::ReleaseUserWeatherLock()
{
	// The editor keeps weather locked while open, so hand the lock back to it rather than letting the sky drift.
	if (open && IsWeatherLocked())
		weatherLockedByOverlay = true;
	else
		UnlockWeather();
}

void EditorWindow::LockWeatherForOverlay()
{
	// Weather drifting mid-session changes the scene under whatever is being edited.
	auto* sky = globals::game::sky;
	if (IsWeatherLocked()) {
		overlayWeatherLockPending = false;
		return;
	}

	// ForceWeather ends a transition instantly, snapping the sky to the incoming weather.
	if (!sky || (sky->lastWeather && sky->currentWeatherPct < 1.0f))
		return;

	LockWeather(sky->currentWeather);
	weatherLockedByOverlay = IsWeatherLocked();
	overlayWeatherLockPending = !weatherLockedByOverlay;
}

void EditorWindow::UnlockWeather()
{
	auto* locked = GetActiveLock();
	if (!locked)
		return;

	g_weatherLockActive.store(false, std::memory_order_release);
	g_lockedWeather.store(nullptr, std::memory_order_release);

	// ReleaseWeatherOverride makes the next sky update pick a random region weather; hand the
	// current weather back as the natural one instead so progression resumes from it.
	if (auto* sky = globals::game::sky) {
		sky->overrideWeather = nullptr;
		sky->defaultWeather = sky->currentWeather;
		sky->region = regionBeforeLock;
	}
	regionBeforeLock = nullptr;

	logger::info("Weather unlocked: {}", locked->GetFormEditorID() ? locked->GetFormEditorID() : "Unknown");
}

void EditorWindow::PauseTime()
{
	if (timePaused)
		return;
	auto calendar = Util::Climate::GetCalendar();
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
	auto calendar = Util::Climate::GetCalendar();
	if (calendar && calendar->timeScale) {
		calendar->timeScale->value = savedTimeScale;
		timePaused = false;
		logger::info("Time resumed (timescale: {})", savedTimeScale);
	}
}

void EditorWindow::ResetTimeScale()
{
	auto calendar = Util::Climate::GetCalendar();
	if (!calendar || !calendar->timeScale)
		return;
	if (timePaused)
		savedTimeScale = kVanillaTimeScale;
	else
		calendar->timeScale->value = kVanillaTimeScale;
	timeScaleSlider = kVanillaTimeScale;
}

namespace
{
	// Time must keep running while these are up: the engine hangs on fast travel and sleep/wait
	// never completes when game time cannot advance. MapMenu covers the fast travel launch point.
	constexpr std::array kTimeSensitiveMenus{ RE::LoadingMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME, RE::MapMenu::MENU_NAME };

	/** @brief Returns true if the menu is one that must not run with a zero timescale. */
	bool IsTimeSensitiveMenu(const RE::BSFixedString& a_menuName)
	{
		return std::ranges::any_of(kTimeSensitiveMenus, [&](std::string_view name) { return a_menuName == name; });
	}

	/** @brief Returns true if any menu that must not run with a zero timescale is still open. */
	bool IsAnyTimeSensitiveMenuOpen()
	{
		auto ui = GetUI();
		return ui && std::ranges::any_of(kTimeSensitiveMenus, [&](std::string_view name) { return ui->IsMenuOpen(name); });
	}
}

void EditorWindow::SetTimeRunningForMenu(bool a_needsRunningTime)
{
	auto calendar = Util::Climate::GetCalendar();
	if (!calendar || !calendar->timeScale)
		return;

	if (a_needsRunningTime) {
		if (timeRestoredForMenu || calendar->timeScale->value != 0.0f)
			return;
		// Only re-pause afterwards if the pause is ours; a zero timescale we did not set (stale
		// save, console, other mod) stays restored so it cannot freeze the game again.
		wasPausedBeforeMenu = timePaused;
		// A non-positive snapshot would restore straight back to frozen time.
		if (savedTimeScale <= 0.0f)
			savedTimeScale = kVanillaTimeScale;
		if (timePaused)
			ResumeTime();
		else
			calendar->timeScale->value = std::max(savedTimeScale, kVanillaTimeScale);
		timeRestoredForMenu = true;
	} else if (timeRestoredForMenu) {
		if (wasPausedBeforeMenu)
			PauseTime();
		timeRestoredForMenu = false;
		wasPausedBeforeMenu = false;
	}
}

RE::BSEventNotifyControl EditorWindow::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// Fast travel overlaps these menus (map then loading), so a close only ends the guard once
	// the whole set is closed.
	if (a_event && IsTimeSensitiveMenu(a_event->menuName))
		GetSingleton()->SetTimeRunningForMenu(a_event->opening || IsAnyTimeSensitiveMenuOpen());

	return RE::BSEventNotifyControl::kContinue;
}

bool EditorWindow::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	auto ui = GetUI();

	if (!ui) {
		logger::error("[CSEditor] UI event source not found");
		return false;
	}

	ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&singleton);
	logger::info("[CSEditor] Registered MenuOpenCloseEventHandler");

	return true;
}

namespace
{
	constexpr float kHoursPerDay = 24.0f;
	constexpr float kPeriodStripHeight = 3.0f;
	constexpr float kPeriodDividerAlpha = 0.35f;
	/// ImGui's slider inset of the grab travel from the frame edge (hardcoded in imgui_widgets.cpp).
	constexpr float kSliderGrabPadding = 2.0f;

	constexpr ImU32 kDawnColor = IM_COL32(125, 90, 180, 255);
	constexpr ImU32 kSunriseColor = IM_COL32(230, 150, 75, 255);
	constexpr ImU32 kDayColor = IM_COL32(240, 215, 90, 255);
	constexpr ImU32 kSunsetColor = IM_COL32(230, 100, 75, 255);
	constexpr ImU32 kDuskColor = IM_COL32(150, 80, 150, 255);
	constexpr ImU32 kNightColor = IM_COL32(40, 55, 125, 255);

	constexpr std::size_t kMaxPeriodSegments = 6;

	/// A period's hour span; endHour exceeds 24 when the period wraps past midnight.
	struct PeriodSegment
	{
		float startHour;
		float endHour;
		ImU32 color;
	};

	struct PeriodSegments
	{
		std::array<PeriodSegment, kMaxPeriodSegments> items{};
		std::size_t count = 0;
	};

	PeriodSegments GetPeriodSegments(EditorWindow::TimePeriodSet periodSet)
	{
		PeriodSegments segments;
		if (periodSet == EditorWindow::TimePeriodSet::Weather) {
			const auto sky = globals::game::sky;
			if (!sky || !sky->currentClimate)
				return segments;
			const auto day = Util::Climate::GetDayHours(*sky->currentClimate);
			segments.items = { { { day.nightEnd, day.dayStart, kSunriseColor },
				{ day.dayStart, day.dayEnd, kDayColor },
				{ day.dayEnd, day.nightStart, kSunsetColor },
				{ day.nightStart, day.nightEnd + kHoursPerDay, kNightColor } } };
			segments.count = 4;
		} else if (periodSet == EditorWindow::TimePeriodSet::Scene) {
			constexpr std::array periodColors{ kDawnColor, kSunriseColor, kDayColor, kSunsetColor, kDuskColor, kNightColor };
			static_assert(periodColors.size() == SceneSettingsManager::kPeriodCount);
			const auto starts = SceneSettingsManager::GetPeriodStartHours();
			for (std::size_t index = 0; index < starts.size(); ++index) {
				const float endHour = index + 1 < starts.size() ? starts[index + 1] : starts.front() + kHoursPerDay;
				segments.items[index] = { starts[index], endHour, periodColors[index] };
			}
			segments.count = starts.size();
		}
		return segments;
	}

	/** @brief Colour strip along the bottom of the slider frame, with a divider at each period start. */
	void DrawPeriodStrip(const PeriodSegments& segments, float hourMax)
	{
		const ImVec2 frameMin = ImGui::GetItemRectMin();
		const ImVec2 frameMax = ImGui::GetItemRectMax();
		const float grabHalf = ImGui::GetStyle().GrabMinSize * 0.5f;
		const float trackMin = frameMin.x + kSliderGrabPadding + grabHalf;
		const float trackWidth = frameMax.x - frameMin.x - 2.0f * (kSliderGrabPadding + grabHalf);
		auto hourToX = [&](float hour) { return trackMin + std::clamp(hour / hourMax, 0.0f, 1.0f) * trackWidth; };

		auto* drawList = ImGui::GetWindowDrawList();
		const ImU32 dividerColor = ImGui::GetColorU32(ImGuiCol_Text, kPeriodDividerAlpha);
		// The first and last spans run to the frame edges rather than the grab travel limits.
		auto stripX = [&](float hour) { return hour <= 0.0f ? frameMin.x : hour >= hourMax ? frameMax.x :
			                                                                                 hourToX(hour); };
		// Clip the full rounded frame to the strip so the bottom corners match the frame rounding.
		auto fillSpan = [&](float startHour, float endHour, ImU32 color) {
			drawList->PushClipRect(ImVec2(stripX(startHour), frameMax.y - kPeriodStripHeight), ImVec2(stripX(endHour), frameMax.y), true);
			drawList->AddRectFilled(frameMin, frameMax, color, ImGui::GetStyle().FrameRounding);
			drawList->PopClipRect();
		};

		for (std::size_t index = 0; index < segments.count; ++index) {
			const auto& segment = segments.items[index];
			fillSpan(segment.startHour, std::min(segment.endHour, kHoursPerDay), segment.color);
			if (segment.endHour > kHoursPerDay)
				fillSpan(0.0f, segment.endHour - kHoursPerDay, segment.color);
			const float dividerX = hourToX(segment.startHour);
			drawList->AddLine(ImVec2(dividerX, frameMin.y), ImVec2(dividerX, frameMax.y - kPeriodStripHeight), dividerColor);
		}
	}
}

void EditorWindow::ClaimTimePeriodSet(TimePeriodSet periodSet)
{
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		timePeriodSet = periodSet;
}

bool EditorWindow::DrawGameHourSlider(const char* label, const char* format)
{
	auto calendar = Util::Climate::GetCalendar();
	if (!calendar || !calendar->gameHour)
		return false;
	const bool changed = ImGui::SliderFloat(label, &calendar->gameHour->value, 0.0f, kGameHourMax, format);
	if (const auto segments = GetPeriodSegments(timePeriodSet); segments.count > 0)
		DrawPeriodStrip(segments, kGameHourMax);
	// Backward clock edits otherwise look like a midnight wrap and expire the weather override.
	if (auto* sky = globals::game::sky; changed && sky && GetActiveLock())
		sky->lastWeatherUpdate = calendar->gameHour->value;
	if (ImGui::IsItemActivated())
		gameHourScrubRefreshIssued = false;

	if (changed && ImGui::IsItemActive()) {
		const double currentTime = ImGui::GetTime();
		if (!gameHourScrubRefreshIssued || currentTime - lastGameHourScrubRefreshTime >= kGameHourScrubRefreshIntervalSeconds) {
			Util::RequestTimeJumpTransition();
			lastGameHourScrubRefreshTime = currentTime;
			gameHourScrubRefreshIssued = true;
		}
	}

	// Always refresh on release so the final value is reflected even if the throttle swallowed it.
	if (ImGui::IsItemDeactivatedAfterEdit())
		Util::RequestTimeJumpTransition();
	return true;
}

bool EditorWindow::DrawPausedAwareGameHourSlider(const char* id)
{
	const bool isPaused = IsTimePaused();
	const char* timeFormat = isPaused ?
	                             T(TKEY("time_slider_paused"), "Time: %.2f (paused)") :
	                             "Time: %.2f";
	if (isPaused)
		ImGui::PushStyleColor(ImGuiCol_Text, Menu::GetSingleton()->GetTheme().StatusPalette.Error);
	const bool drawn = DrawGameHourSlider(id, timeFormat);
	if (isPaused)
		ImGui::PopStyleColor();
	return drawn;
}

bool EditorWindow::DrawTimePauseToggle(const char* id, const ImVec2& size)
{
	ImVec2 buttonSize = size;
	if (buttonSize.x <= 0.0f || buttonSize.y <= 0.0f) {
		const float frameH = ImGui::GetFrameHeight();
		buttonSize = ImVec2(frameH, frameH);
	}

	const ImVec4 pausedColor = Menu::GetSingleton()->GetTheme().StatusPalette.Error;
	const ImU32 glyphColor = ImGui::GetColorU32(ImGuiCol_Text);
	const bool isPaused = IsTimePaused();
	const bool clicked = DrawToggleIconButton(id, Icons::TI(ICON_TI_CLOCK_PAUSE), isPaused, pausedColor, buttonSize, glyphColor);
	if (clicked)
		TogglePause();
	return clicked;
}

void EditorWindow::DrawTimeScaleSlider(const char* id, RE::Calendar* calendar)
{
	assert(calendar && calendar->timeScale);
	if (timePaused)
		timeScaleSlider = std::max(savedTimeScale, kTimeScaleMin);
	else if (std::abs(calendar->timeScale->value - timeScaleSlider) > 0.01f)
		timeScaleSlider = calendar->timeScale->value;

	ImGui::BeginDisabled(timePaused);
	if (ImGui::SliderFloat(id, &timeScaleSlider, kTimeScaleMin, kTimeScaleMax,
			timeScaleSlider == kVanillaTimeScale ? T(TKEY("vanilla_speed"), "Vanilla Speed") : "%.1fx", ImGuiSliderFlags_Logarithmic))
		calendar->timeScale->value = timeScaleSlider;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		ResetTimeScale();
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("time_scale_tooltip"), "Adjust how fast time passes (vanilla: %.1fx). Right-click to reset."), kVanillaTimeScale);
}

void EditorWindow::DrawTimeControls()
{
	auto calendar = Util::Climate::GetCalendar();
	if (!calendar || !calendar->gameHour || !calendar->timeScale)
		return;

	// An external timescale change (console, other mods, the menu guard) overrides our pause
	if (timePaused && calendar->timeScale->value > 0.0f)
		timePaused = false;

	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const float iconSize = ImGui::GetFrameHeight();
	const float avail = ImGui::GetContentRegionAvail().x;
	const float sliderWidth = std::max(40.0f, (avail - iconSize * 2.0f - gap * 3.0f) * 0.5f);

	ImGui::SetNextItemWidth(sliderWidth);
	DrawPausedAwareGameHourSlider("##FeatureGameTime");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("game_time_tooltip"), "Adjust the current game time"));

	ImGui::SameLine();
	DrawTimePauseToggle("##FeaturePauseTime", ImVec2(iconSize, iconSize));
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("pause_time_tooltip"), "Pause or resume game time progression"));

	ImGui::SameLine();
	ImGui::SetNextItemWidth(sliderWidth);
	DrawTimeScaleSlider("##TimeScale", calendar);

	ImGui::SameLine();
	{
		auto _style = Util::TransparentIconButtonStyle();
		if (Icons::Button("##FeatureResetSpeed", Icons::TI(ICON_TI_RESTORE), ImVec2(iconSize, iconSize)))
			ResetTimeScale();
	}
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(T(TKEY("reset_speed_tooltip"), "Reset time speed to vanilla (%.1fx)"), kVanillaTimeScale);
}

bool EditorWindow::CanBeOpen()
{
	auto* player = globals::game::player;
	auto* state = globals::state;
	// The live UI check catches a loading screen the cached menu flags have not caught up with yet.
	return player && player->parentCell && state && !state->IsMainOrLoadingMenuOpen(GetUI());
}

bool EditorWindow::IsOpeningBlocked()
{
	auto* ui = GetUI();
	if (globals::state && globals::state->IsMainOrLoadingMenuOpen(ui))
		return true;
	if (!ui)
		return false;

	// Gameplay menus own the cursor and input; the editor opening on top of them leaves both stuck.
	static constexpr std::array kBlockingMenus{
		RE::MainMenu::MENU_NAME, RE::LoadingMenu::MENU_NAME, RE::MapMenu::MENU_NAME, RE::InventoryMenu::MENU_NAME,
		RE::MagicMenu::MENU_NAME, RE::StatsMenu::MENU_NAME, RE::TweenMenu::MENU_NAME, RE::JournalMenu::MENU_NAME,
		RE::ContainerMenu::MENU_NAME, RE::BarterMenu::MENU_NAME, RE::GiftMenu::MENU_NAME, RE::LockpickingMenu::MENU_NAME,
		RE::BookMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME, RE::LevelUpMenu::MENU_NAME, RE::Console::MENU_NAME,
		RE::RaceSexMenu::MENU_NAME, RE::FavoritesMenu::MENU_NAME, RE::TrainingMenu::MENU_NAME, RE::CraftingMenu::MENU_NAME,
		RE::DialogueMenu::MENU_NAME, RE::MessageBoxMenu::MENU_NAME
	};
	return std::ranges::any_of(kBlockingMenus, [ui](std::string_view name) { return ui->IsMenuOpen(name); });
}

void EditorWindow::WarnOpeningBlocked()
{
	const std::string message = T(TKEY("open_blocked"), "Wait! You cannot open the editor in a loading screen or menu.");
	// A repeated hotkey press while the card is still up would only stack copies of it.
	if (std::ranges::any_of(notifications, [&message](const Notification& notification) { return notification.message == message; }))
		return;
	ShowNotification(message, Util::Colors::GetWarning(), 3.0f);
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
		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = savedMousePos;
		io.WantSetMousePos = true;
		// Mouse events are swallowed while flying, so ImGui may have missed button releases.
		for (int button = 0; button < ImGuiMouseButton_COUNT; ++button)
			io.AddMouseButtonEvent(button, false);
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

bool EditorWindow::ShouldHandleEscapeKey()
{
	if (suppressNextEditorEscape) {
		suppressNextEditorEscape = false;
		return false;
	}
	return !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

bool EditorWindow::ClosePopupOnEscape()
{
	if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
		return false;
	// The menu or editor ignores Escape only while a popup is open, which this close is about to end.
	GetSingleton()->suppressNextEditorEscape = true;
	ImGui::CloseCurrentPopup();
	return true;
}

Widget* EditorWindow::FindWidget(const FormEditSources::FormKey& key)
{
	Widget* found = nullptr;
	ForEachWidget([&](Widget& widget) {
		if (!found && widget.GetFormKey() == key)
			found = &widget;
	});
	return found;
}

UndoHistory::Target EditorWindow::MakeUndoTarget(const Widget& widget)
{
	const auto key = widget.GetFormKey();
	UndoHistory::Target target{ .key = std::format("widget:{}/{}", key.first, key.second), .label = widget.GetEditorID() };
	target.capture = [this, key]() -> UndoHistory::SnapshotPtr {
		auto* found = FindWidget(key);
		return found ? found->CaptureEdits() : nullptr;
	};
	target.restore = [this, key](const UndoHistory::Snapshot& snapshot) {
		if (auto* found = FindWidget(key))
			found->RestoreEdits(snapshot);
	};
	return target;
}

void EditorWindow::PerformUndo()
{
	if (const auto label = undoHistory.Undo())
		ShowNotification(std::vformat(T(TKEY("undone_changes_to"), "Undone changes to {}"), std::make_format_args(*label)),
			Util::Colors::GetInfo(), 2.0f);
}

void EditorWindow::PerformRedo()
{
	if (const auto label = undoHistory.Redo())
		ShowNotification(std::vformat(T(TKEY("redone_changes_to"), "Redone changes to {}"), std::make_format_args(*label)),
			Util::Colors::GetInfo(), 2.0f);
}

void EditorWindow::DiscardWidgetChanges(Widget& widget)
{
	if (!widget.HasUnsavedChanges())
		return;
	undoHistory.Change(MakeUndoTarget(widget), [&] { widget.Load(false); });
}

void EditorWindow::DiscardAllWidgetChanges()
{
	ForEachWidget([this](Widget& widget) { DiscardWidgetChanges(widget); });
	anyWidgetUnsaved = false;
	lastUnsavedScan = -kUnsavedScanInterval;
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

void EditorWindow::RequestToolPrompt(ExternalTool& tool, ToolPromptReason reason, std::function<void()> proceed)
{
	promptedTool = &tool;
	toolPromptProceed = std::move(proceed);
	const bool closing = reason == ToolPromptReason::Close;
	const auto what = tool.DescribeChanges();
	const std::string target = tool.GetTargetName();
	toolPrompt.title = T(TKEY("tool_close_title"), "Unwritten Changes");
	if (tool.CanWriteChanges()) {
		toolPrompt.message = std::vformat(closing ? T(TKEY("tool_close_message"), "{}: changes not written to {}. Write them before closing?") :
													T(TKEY("tool_switch_message"), "{}: changes not written to {}. Write them before switching?"),
			std::make_format_args(what, target));
		toolPrompt.confirmLabel = std::vformat(T(TKEY("tool_close_write"), "Write to {}"), std::make_format_args(target));
		toolPrompt.primaryConfirm = true;
		toolPrompt.focus = Util::ConfirmationPopup::Focus::Confirm;
	} else {
		toolPrompt.message = std::vformat(closing ? T(TKEY("tool_close_preview_message"), "{}: these edits are a preview and cannot be written to {}. Closing discards them.") :
													T(TKEY("tool_switch_preview_message"), "{}: these edits are a preview and cannot be written to {}. Switching discards them."),
			std::make_format_args(what, target));
		toolPrompt.confirmLabel.clear();
		toolPrompt.primaryConfirm = false;
		toolPrompt.focus = Util::ConfirmationPopup::Focus::Cancel;
	}
	toolPrompt.alternateLabel = T(TKEY("tool_close_discard"), "Discard");
	toolPrompt.cancelLabel = T(TKEY("cancel"), "Cancel");
	toolPrompt.showDontAskAgain = true;
	toolPrompt.dontAskAgainLabel = T(TKEY("tool_dont_ask_discard"), "Don't ask me again (always discard)");
	toolPrompt.dontAskAgainPersist = &settings.skipToolDiscardPrompt;
	toolPrompt.rememberedChoice = Util::ConfirmationPopup::Choice::Alternate;
	toolPrompt.Request();
}

void EditorWindow::DrawToolPrompt()
{
	using Choice = Util::ConfirmationPopup::Choice;
	const bool wasSkipping = settings.skipToolDiscardPrompt;
	const auto choice = toolPrompt.DrawChoice();
	if (settings.skipToolDiscardPrompt != wasSkipping)
		Save();
	switch (choice) {
	case Choice::Confirm:
		// A failed write leaves things as they were; the tool has already said why.
		if (!promptedTool || !promptedTool->WriteChanges())
			return;
		break;
	case Choice::Alternate:
		if (promptedTool)
			promptedTool->DiscardChanges();
		break;
	case Choice::Cancel:
		toolPromptProceed = nullptr;
		return;
	default:
		return;
	}
	if (auto proceed = std::exchange(toolPromptProceed, nullptr))
		proceed();
}

void EditorWindow::RenderNotifications()
{
	// Guard against calls before ImGui is initialized
	if (!ImGui::GetCurrentContext()) {
		return;
	}

	const float currentTime = static_cast<float>(ImGui::GetTime());
	const float scale = Util::GetUIScale();
	constexpr float kFadeIn = 0.18f;
	constexpr float kFadeOut = 0.55f;

	notifications.erase(
		std::remove_if(notifications.begin(), notifications.end(),
			[currentTime](const Notification& n) { return currentTime - n.startTime > n.duration; }),
		notifications.end());

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float maxTextWidth = std::min(380.0f * scale, viewport->WorkSize.x * 0.5f);
	float stackUp = 0.0f;

	for (auto& notif : notifications) {
		const float elapsed = currentTime - notif.startTime;
		const float remaining = notif.duration - elapsed;
		float alpha = 1.0f;
		if (elapsed < kFadeIn)
			alpha = elapsed / kFadeIn;
		else if (remaining < kFadeOut)
			alpha = std::max(0.0f, remaining / kFadeOut);

		const ImVec2 textSize = ImGui::CalcTextSize(notif.message.c_str(), nullptr, false, maxTextWidth);
		const ImVec2 pad{ 16.0f * scale, 12.0f * scale };
		const ImVec2 cardSize{ textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f };
		const float x = viewport->WorkPos.x + (viewport->WorkSize.x - cardSize.x) * 0.5f;
		const float y = viewport->WorkPos.y + viewport->WorkSize.y - cardSize.y - (32.0f * scale + stackUp);

		ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
		ImGui::SetNextWindowSize(cardSize, ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.0f);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetStyle().FrameRounding * 1.25f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

		if (ImGui::Begin(std::format("##Notification{}", (void*)&notif).c_str(), nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
					ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
					ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar)) {
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 p0 = ImGui::GetWindowPos();
			const ImVec2 p1{ p0.x + cardSize.x, p0.y + cardSize.y };
			const float rounding = ImGui::GetStyle().WindowRounding;
			ImVec4 cardBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
			cardBg.w = ThemeManager::Constants::OVERLAP_MIN_ALPHA * alpha;
			draw->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(cardBg), rounding);
			const ImU32 accent = ImGui::ColorConvertFloat4ToU32(
				ImVec4(notif.color.x, notif.color.y, notif.color.z, 0.65f * alpha));
			draw->AddRect(p0, p1, accent, rounding, 0, 1.15f * scale);
			draw->PushClipRect(p0, ImVec2(p0.x + 3.0f * scale, p1.y), true);
			draw->AddRectFilled(p0, p1, accent, rounding);
			draw->PopClipRect();

			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxTextWidth);
			ImVec4 colorWithAlpha = notif.color;
			colorWithAlpha.w *= alpha;
			ImGui::PushStyleColor(ImGuiCol_Text, colorWithAlpha);
			ImGui::TextWrapped("%s", notif.message.c_str());
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
		}
		ImGui::End();
		ImGui::PopStyleVar(3);

		stackUp += cardSize.y + 8.0f * scale;
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
	m_formList.Invalidate();
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
