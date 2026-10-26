#include "FeatureListRenderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <imgui.h>
#include <imgui_internal.h>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include "CSEditor/Browser/BrowserWidgets.h"
#include "CSEditor/EditorWindow.h"
#include "CSEditor/SceneManager/FeatureOverwritesPanel.h"
#include "CSEditor/SceneManager/SceneSettingsManager.h"
#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "Feature.h"
#include "FeatureConstraints.h"
#include "FeatureIssues.h"
#include "Features/CSEditor.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/IconLoader.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/PresetsPageRenderer.h"
#include "Menu/ProfilingRenderer.h"
#include "Menu/SettingsPageRenderer.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"

namespace
{
	// Built-in pages pinned to the bottom of the list; every other built-in page sits in the top group
	constexpr std::array kBottomMenuIds = { MenuPageIds::kSettings, MenuPageIds::kProfiling };

	bool IsBottomMenu(std::string_view menuId)
	{
		return std::ranges::find(kBottomMenuIds, menuId) != kBottomMenuIds.end();
	}

	/// Height the bottom group took last frame, so the feature list above it can leave exactly that much room.
	float g_bottomGroupHeight = 0.0f;

	/// What the cached menu list was built from; any difference rebuilds it.
	struct MenuListKey
	{
		std::string search;
		std::uint32_t filter = 0;
		Menu::FeatureSortMode sort = Menu::FeatureSortMode::Category;
		std::string locale;
		std::uint64_t userEditsRevision = 0;
		std::uint64_t sceneRevision = 0;
		bool hasFeatureIssues = false;
		bool developerMode = false;

		bool operator==(const MenuListKey&) const = default;
	};

	/// The menu list, built only when its inputs change rather than every frame.
	struct MenuListCache
	{
		std::vector<FeatureListRenderer::MenuFuncInfo> list;
		MenuListKey key;
		bool valid = false;
		int lastFrame = -1;
	};
	MenuListCache g_menuList;

	/// Set by changes the key does not see: a category expanded or collapsed, a feature pinned or unpinned.
	bool g_menuListDirty = true;

	/// Chip counts from the last rebuild; they only change when the list does.
	FeatureListRenderer::FilterCounts g_filterCounts;

	/// The selected feature while the search, a chip or a collapsed category leaves it out of the list, so its
	/// page stays open. Only read while the selection index is outside the list.
	Feature* g_detachedFeature = nullptr;

	/** @brief Stable identity of a list entry, to find it again after a rebuild; empty for labels. */
	std::string GetEntryKey(const FeatureListRenderer::MenuFuncInfo& entry)
	{
		if (const auto* feature = std::get_if<Feature*>(&entry))
			return "feature:" + (*feature)->GetShortName();
		if (const auto* page = std::get_if<FeatureListRenderer::BuiltInMenu>(&entry))
			return "page:" + std::string(page->id);
		if (const auto* header = std::get_if<FeatureListRenderer::CategoryHeader>(&entry))
			return "category:" + header->name;
		return {};
	}

	/**
	 * @brief Selects the entry with this key in the rebuilt list. A feature that is no longer listed stays open
	 * as the detached feature; anything else that vanished clears the selection.
	 */
	void ReselectEntry(const std::string& key, const std::vector<FeatureListRenderer::MenuFuncInfo>& menuList, size_t& selectedMenu)
	{
		const auto it = std::ranges::find_if(menuList, [&](const auto& entry) { return GetEntryKey(entry) == key; });
		if (it != menuList.end()) {
			selectedMenu = static_cast<size_t>(std::distance(menuList.begin(), it));
			return;
		}
		selectedMenu = std::numeric_limits<size_t>::max();
		constexpr std::string_view featurePrefix = "feature:";
		if (key.starts_with(featurePrefix))
			g_detachedFeature = Feature::FindFeatureByShortName(key.substr(featurePrefix.size()));
	}

	/**
	 * @brief Draws one row of the top or bottom page group: icon, label, an optional muted hint line and an
	 * optional trailing glyph, styled by the page's NavStyle.
	 * @return True when clicked.
	 */
	bool DrawNavRow(const FeatureListRenderer::BuiltInMenu& menu, bool selected, bool disabled)
	{
		using NavStyle = FeatureListRenderer::NavStyle;
		using Constants = ThemeManager::Constants;

		const float scale = Util::GetUIScale();
		const bool styled = menu.style == NavStyle::Accent || menu.style == NavStyle::Launcher;
		const bool showHint = styled && !menu.hint.empty();

		MenuFonts::FontRoleGuard labelFont(Menu::FontRole::Subheading);
		const float labelHeight = ImGui::GetTextLineHeight();
		float hintHeight = 0.0f;
		if (showHint) {
			MenuFonts::FontRoleGuard hintFont(Menu::FontRole::Subtext);
			hintHeight = ImGui::GetTextLineHeight();
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float padX = style.FramePadding.x;
		const float padY = Constants::NAV_ROW_PADDING_Y * scale;
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + labelHeight + hintHeight + padY * 2.0f);

		bool clicked = false;
		{
			Util::DisableGuard disable(disabled);
			// Styled rows paint their own pill; the selectable only carries hover, focus and the click
			const int transparentColors = styled ? 3 : 0;
			if (styled) {
				for (auto col : { ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive })
					ImGui::PushStyleColor(col, ImVec4(0, 0, 0, 0));
			}
			clicked = ImGui::Selectable("##NavRow", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, max.y - min.y));
			ImGui::PopStyleColor(transparentColors);
		}
		const bool hovered = ImGui::IsItemHovered();

		auto* drawList = ImGui::GetWindowDrawList();
		const ImVec4 info = Util::Colors::GetInfo();
		const float rounding = (max.y - min.y) * 0.5f;
		if (menu.style == NavStyle::Accent) {
			const float alpha = selected ? Constants::NAV_ACCENT_SELECTED_ALPHA :
			                    hovered  ? Constants::NAV_ACCENT_HOVERED_ALPHA :
			                               Constants::NAV_ACCENT_IDLE_ALPHA;
			drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(info.x, info.y, info.z, info.w * alpha)), rounding);
		} else if (menu.style == NavStyle::Launcher) {
			if (hovered && !disabled)
				drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(info.x, info.y, info.z, info.w * Constants::NAV_LAUNCHER_HOVERED_ALPHA)), rounding);
			const ImVec4 border(info.x, info.y, info.z, info.w * (disabled ? Constants::NAV_LAUNCHER_BORDER_ALPHA * 0.5f : Constants::NAV_LAUNCHER_BORDER_ALPHA));
			drawList->AddRect(min, max, ImGui::GetColorU32(border), rounding, 0, std::max(1.0f, scale));
		}

		ImVec4 textColor = ImGui::GetStyleColorVec4(disabled ? ImGuiCol_TextDisabled : ImGuiCol_Text);
		ImVec4 iconColor = textColor;
		if (!disabled && menu.style == NavStyle::Alert)
			textColor = iconColor = Util::Colors::GetError();
		else if (!disabled && styled)
			iconColor = info;

		const float textX = min.x + padX + labelHeight + Constants::NAV_ROW_ICON_GAP * scale;
		float textRight = max.x - padX;
		if (menu.style == NavStyle::Launcher) {
			const Icons::GlyphRef open = Icons::TI(ICON_TI_EXTERNAL_LINK);
			textRight -= labelHeight + style.ItemInnerSpacing.x;
			Icons::DrawCenteredGlyph(drawList, ImVec2(max.x - padX - labelHeight, min.y + padY), ImVec2(labelHeight, labelHeight), open,
				ImGui::GetColorU32(disabled ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : Util::Colors::GetSecondary()));
		}
		if (menu.icon)
			Icons::DrawCenteredGlyph(drawList, ImVec2(min.x + padX, min.y + padY), ImVec2(labelHeight, labelHeight), menu.icon, ImGui::GetColorU32(iconColor));

		const auto drawText = [&](const std::string& text, float y, float height, const ImVec4& color) {
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::RenderTextEllipsis(drawList, ImVec2(textX, y), ImVec2(textRight, y + height), textRight, text.c_str(), nullptr, nullptr);
			ImGui::PopStyleColor();
		};
		drawText(menu.name, min.y + padY, labelHeight, textColor);
		if (showHint) {
			MenuFonts::FontRoleGuard hintFont(Menu::FontRole::Subtext);
			drawText(menu.hint, min.y + padY + labelHeight, hintHeight, disabled ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : Util::Colors::GetSecondary());
		}
		return clicked;
	}

	/** @brief Thin rule between two regions of the list, with a little room on either side. */
	void NavGroupRule()
	{
		const float gap = ThemeManager::Constants::NAV_GROUP_SPACING * Util::GetUIScale();
		ImGui::Dummy(ImVec2(0.0f, gap));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, gap));
	}

	/** @brief Glyph shown on the sort button for the given feature list sort mode. */
	Icons::GlyphRef GetSortModeIcon(Menu::FeatureSortMode mode)
	{
		switch (mode) {
		case Menu::FeatureSortMode::Alphabetical:
			return Icons::TI(ICON_TI_SORT_ASCENDING_LETTERS);
		case Menu::FeatureSortMode::Category:
		default:  // unknown values from a hand-edited config fall back to categories
			return Icons::TI(ICON_TI_CATEGORY);
		}
	}

	/// Set when a shortcut moved the selection, so the list scrolls the newly selected row into view.
	bool g_scrollToSelection = false;

	/**
	 * @brief Moves the selection to the nearest feature entry in a direction, skipping category headers,
	 * labels and built-in pages. Nothing changes when there is no feature that way.
	 * @param direction -1 for the previous feature, +1 for the next.
	 * @return True when the selection moved.
	 */
	bool StepFeatureSelection(const std::vector<FeatureListRenderer::MenuFuncInfo>& menuList, size_t& selectedMenu, int direction)
	{
		const auto count = static_cast<std::ptrdiff_t>(menuList.size());
		auto index = selectedMenu < menuList.size() ? static_cast<std::ptrdiff_t>(selectedMenu) : (direction > 0 ? -1 : count);
		for (index += direction; index >= 0 && index < count; index += direction) {
			if (std::holds_alternative<Feature*>(menuList[static_cast<size_t>(index)])) {
				selectedMenu = static_cast<size_t>(index);
				return true;
			}
		}
		return false;
	}

	void SeparatorTextWithFont(const char* text, Menu::FontRole role)
	{
		MenuFonts::FontRoleGuard guard(role);
		ImGui::SeparatorText(text);
	}

	void SeparatorTextWithFont(const std::string& text, Menu::FontRole role)
	{
		SeparatorTextWithFont(text.c_str(), role);
	}

	bool BeginTabItemWithFont(const char* label, Menu::FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None)
	{
		return MenuFonts::BeginTabItemWithFont(label, role, flags);
	}

	/**
	 * @brief Draws a feature header with the feature name in large text and version in smaller text
	 * @param featureName The display name of the feature
	 * @param version The version string (can be empty)
	 * @param description Short description shown below the title (single line, truncated if too long)
	 * @return The height of just the title line (for button alignment)
	 */
	float DrawFeatureHeader(const std::string& featureName, const std::string& version, const std::string& description = "", const std::string& stageTag = "", ImVec4 stageColor = {})
	{
		auto& themeSettings = globals::menu->GetTheme();
		auto& palette = themeSettings.Palette;
		auto& featureHeading = themeSettings.FeatureHeading;

		// Sanitize and clamp to UI slider range to prevent malformed theme JSON from destabilizing layout
		float titleScale = featureHeading.FeatureTitleScale;
		if (!std::isfinite(titleScale)) {
			titleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
		}
		titleScale = std::clamp(titleScale, 1.0f, 3.0f);

		ImVec2 startPos = ImGui::GetCursorScreenPos();

		// Calculate title size and draw feature name with Title font
		ImVec2 titleSize;
		{
			MenuFonts::FontRoleGuard titleGuard(Menu::FontRole::Title);
			titleSize = ImGui::CalcTextSize(featureName.c_str());
			titleSize.x *= titleScale;
			titleSize.y *= titleScale;

			ImGui::SetWindowFontScale(titleScale);
			ImGui::TextUnformatted(featureName.c_str());
			ImGui::SetWindowFontScale(1.0f);
		}

		// Store the title-only height for return value
		float titleOnlyHeight = titleSize.y;

		// Running x for bottom-aligned annotations (stage tag, then version) to the right of the title
		float annotationX = startPos.x + titleSize.x + ImGui::GetStyle().ItemSpacing.x;

		// Draw stage marker ([ALPHA]/[BETA]) on same line, bottom-aligned
		if (!stageTag.empty()) {
			ImVec2 tagSize;
			{
				MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
				tagSize = ImGui::CalcTextSize(stageTag.c_str());
				tagSize.x *= titleScale;
				tagSize.y *= titleScale;
			}

			ImGui::SetCursorScreenPos(ImVec2(annotationX, startPos.y + titleSize.y - tagSize.y));
			{
				MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
				ImGui::SetWindowFontScale(titleScale);
				ImGui::TextColored(stageColor, "%s", stageTag.c_str());
				ImGui::SetWindowFontScale(1.0f);
			}

			annotationX += tagSize.x + ImGui::GetStyle().ItemSpacing.x;
			ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + titleSize.y + ImGui::GetStyle().ItemSpacing.y * 0.25f));
		}

		// Draw version on same line with Body font, bottom-aligned if version exists
		if (!version.empty()) {
			// Format version: replace dashes with dots for consistency
			std::string formattedVersion = version;
			std::replace(formattedVersion.begin(), formattedVersion.end(), '-', '.');

			// Calculate version text size at scaled size
			ImVec2 versionSize;
			{
				MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
				versionSize = ImGui::CalcTextSize(("v" + formattedVersion).c_str());
				versionSize.x *= titleScale;
				versionSize.y *= titleScale;
			}

			// Position version text: right of the stage tag (or title), bottom-aligned
			float versionX = annotationX;
			float versionY = startPos.y + titleSize.y - versionSize.y;

			ImGui::SetCursorScreenPos(ImVec2(versionX, versionY));

			// Use dimmed text color for version
			ImVec4 versionColor = palette.Text;
			versionColor.w *= ThemeManager::Constants::VERSION_TEXT_OPACITY;

			{
				MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
				ImGui::SetWindowFontScale(titleScale);
				ImGui::TextColored(versionColor, "v%s", formattedVersion.c_str());
				ImGui::SetWindowFontScale(1.0f);
			}

			// Reset cursor to after the title block
			ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + titleSize.y + ImGui::GetStyle().ItemSpacing.y * 0.25f));
		}

		// Draw description if provided (wrapped to content width)
		if (!description.empty()) {
			MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
			ImVec4 descColor = palette.Text;
			descColor.w *= 0.7f;  // Slightly dimmed
			ImGui::PushStyleColor(ImGuiCol_Text, descColor);
			ImGui::TextWrapped("%s", description.c_str());
			ImGui::PopStyleColor();
		}

		// Draw plain separator below
		ImGui::Separator();

		return titleOnlyHeight;
	}

	// ---------------------------------------------------------------------------
	// Persistent state for the reactive constraint warning popup.
	// DrawMenuVisitor is reconstructed every frame (it's a temporary passed to
	// std::visit), so member state is lost immediately.  These file-scope
	// variables survive across frames so the popup can actually render.
	// ---------------------------------------------------------------------------

	// Set of constraint keys we have already "seen" (and therefore warned about
	// or suppressed).  Keyed as "featureShortName|settingPath".
	std::unordered_set<std::string> g_knownConstraintKeys;
	bool g_knownConstraintKeysInitialised = false;

	// Pending popup state: non-empty when we have new constraints to show.
	bool g_reactiveWarningShow = false;
	std::vector<std::pair<FeatureConstraints::SettingId, FeatureConstraints::ConstraintResult>> g_reactiveWarningConstraints;

	// "Don't show again" checkbox state inside the modal (reset each time popup opens).
	bool g_dontShowAgainCheckbox = false;

	/**
	 * @brief Queues the warning popup for constraints that appeared since the last call; the first call only seeds.
	 * Diffing every frame also catches changes made outside DrawSettings (e.g. Upscaling's render-loop resolutionScale).
	 */
	void DetectReactiveConstraints()
	{
		if (g_reactiveWarningShow)  // don't overwrite a pending popup
			return;

		auto currentConstraints = FeatureConstraints::GetAllActiveConstraints();

		if (!g_knownConstraintKeysInitialised) {
			// First time: seed known set, no popup
			for (const auto& [settingId, result] : currentConstraints) {
				g_knownConstraintKeys.insert(settingId.featureShortName + "|" + settingId.settingPath);
			}
			g_knownConstraintKeysInitialised = true;
			return;
		}

		// Diff: find keys present now but not previously known
		std::vector<std::pair<FeatureConstraints::SettingId, FeatureConstraints::ConstraintResult>> newConstraints;
		std::unordered_set<std::string> currentKeys;
		for (const auto& [settingId, result] : currentConstraints) {
			std::string key = settingId.featureShortName + "|" + settingId.settingPath;
			currentKeys.insert(key);
			if (g_knownConstraintKeys.find(key) == g_knownConstraintKeys.end()) {
				newConstraints.emplace_back(settingId, result);
			}
		}
		// Update known set to current (removes keys for constraints that went away)
		g_knownConstraintKeys = std::move(currentKeys);

		if (!newConstraints.empty() && !globals::menu->GetSettings().SkipConstraintWarning) {
			logger::info("Reactive constraint detection: {} new constraints", newConstraints.size());
			for (const auto& [settingId, result] : newConstraints) {
				logger::info("  - {}.{} forced to {} by {}", settingId.featureShortName, settingId.settingPath, FeatureConstraints::FormatConstraintValue(result.forcedValue), result.sources.empty() ? "?" : result.sources[0].featureName);
			}
			g_reactiveWarningShow = true;
			g_reactiveWarningConstraints = std::move(newConstraints);
			g_dontShowAgainCheckbox = false;
		}
	}
}

std::string FeatureListRenderer::TranslateCategory(std::string_view category)
{
	if (category == FeatureCategories::kCharacters)
		return T("feature.category.characters", "Characters");
	if (category == FeatureCategories::kDisplay)
		return T("feature.category.display", "Display");
	if (category == FeatureCategories::kGrass)
		return T("feature.category.grass", "Grass");
	if (category == FeatureCategories::kLandscapeAndTextures)
		return T("feature.category.landscape_and_textures", "Landscape & Textures");
	if (category == FeatureCategories::kLighting)
		return T("feature.category.lighting", "Lighting");
	if (category == FeatureCategories::kMaterials)
		return T("feature.category.materials", "Materials");
	if (category == "Post-Processing")
		return T("feature.category.post_processing", "Post-Processing");
	if (category == FeatureCategories::kOther)
		return T("feature.category.other", "Other");
	if (category == FeatureCategories::kSky)
		return T("feature.category.sky", "Sky");
	if (category == FeatureCategories::kUtility)
		return T("feature.category.utility", "Utility");
	if (category == FeatureCategories::kWater)
		return T("feature.category.water", "Water");

	return std::string(category);
}

void FeatureListRenderer::RenderFeatureList(
	float footerHeight,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::string& pendingFeatureSelection,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawSettings,
	Hooks& hooks)
{
	ImGui::BeginChild("Menus Table", ImVec2(0, -footerHeight));

	// A search filter or a collapsed category could hide the feature being navigated to
	if (!pendingFeatureSelection.empty()) {
		featureSearch.clear();
		if (const auto* feature = Feature::FindFeatureByShortName(pendingFeatureSelection))
			categoryExpansionStates[std::string(feature->GetCategory())] = true;
		g_menuListDirty = true;
	}

	// The entry selected in the list as it stood, so a rebuild keeps the same one selected (and a detached
	// feature the rebuilt list shows again takes its row back)
	const std::string selectedKey = selectedMenu < g_menuList.list.size() ? GetEntryKey(g_menuList.list[selectedMenu]) :
	                                g_detachedFeature                     ? "feature:" + g_detachedFeature->GetShortName() :
	                                                                        std::string();
	if (UpdateMenuList(featureSearch, categoryExpansionStates, drawSettings) && !selectedKey.empty())
		ReselectEntry(selectedKey, g_menuList.list, selectedMenu);
	const auto& menuList = g_menuList.list;

	HandlePendingFeatureSelection(pendingFeatureSelection, menuList, selectedMenu);

	if (hooks.featureStep != 0) {
		g_scrollToSelection = StepFeatureSelection(menuList, selectedMenu, hooks.featureStep > 0 ? 1 : -1);
		hooks.featureStep = 0;
	}
	if (selectedMenu < menuList.size())
		g_detachedFeature = nullptr;

	auto& menuSettings = globals::menu->GetSettings();
	// Ctrl+F needs the search field
	if (hooks.focusSearch)
		menuSettings.FeatureListCollapsed = false;

	if (menuSettings.FeatureListCollapsed) {
		RenderCollapsedStrip(hooks);
		ImGui::SameLine();
		RenderRightColumn(menuList, selectedMenu, pendingFeatureSelection, hooks);
	} else if (ImGui::BeginTable("Menus Table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("##ListOfMenus", 0, 2);
		ImGui::TableSetupColumn("##MenuConfig", 0, 8);
		ImGui::TableNextColumn();
		// Changes made here (search, chips, sort, a category, a pin) rebuild the list next frame
		RenderLeftColumn(menuList, selectedMenu, featureSearch, categoryExpansionStates, hooks);
		ImGui::TableNextColumn();
		RenderRightColumn(menuList, selectedMenu, pendingFeatureSelection, hooks);
		ImGui::EndTable();
	}
	hooks.focusSearch = false;
	g_scrollToSelection = false;  // a hidden list drops the request rather than scrolling later

	ImGui::EndChild();
}

bool FeatureListRenderer::UpdateMenuList(
	const std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawSettings)
{
	const auto& menuSettings = globals::menu->GetSettings();
	MenuListKey key{
		.search = featureSearch,
		.filter = menuSettings.FeatureListFilter & FeatureListFilter::kKnown,
		.sort = menuSettings.FeatureListSort,
		.locale = I18n::GetSingleton()->GetCurrentLocale(),
		.userEditsRevision = globals::state->GetUserEditsRevision(),
		.sceneRevision = globals::sceneSettingsManager ? globals::sceneSettingsManager->GetEntryPresentationRevision() : 0,
		.hasFeatureIssues = FeatureIssues::HasFeatureIssues(),
		.developerMode = globals::state->IsDeveloperMode(),
	};

	// A gap in frames means the menu was closed: anything could have changed meanwhile
	const int frame = ImGui::GetFrameCount();
	const bool reopened = frame != g_menuList.lastFrame + 1;
	g_menuList.lastFrame = frame;

	if (g_menuList.valid && !g_menuListDirty && !reopened && key == g_menuList.key) {
		// The Settings page draws through a callback the menu makes each frame
		for (auto& entry : g_menuList.list) {
			if (auto* page = std::get_if<BuiltInMenu>(&entry); page && page->id == MenuPageIds::kSettings)
				page->func = drawSettings;
		}
		return false;
	}

	g_menuList.list = BuildMenuList(featureSearch, categoryExpansionStates, drawSettings, g_filterCounts);
	g_menuList.key = std::move(key);
	g_menuList.valid = true;
	g_menuListDirty = false;
	return true;
}

std::vector<FeatureListRenderer::MenuFuncInfo> FeatureListRenderer::BuildMenuList(
	const std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawSettings,
	FilterCounts& counts)
{
	// Built-in pages, in list order: the top group (Home, Presets, CS Editor, then any page added after them),
	// then the bottom group (kBottomMenuIds)
	auto menuList = std::vector<MenuFuncInfo>{
		BuiltInMenu{ MenuPageIds::kHome, T("menu.features.home", "Home"), []() { HomePageRenderer::RenderHomePage(); },
			Icons::TI(ICON_TI_HOME), NavStyle::Plain, T("menu.features.home_hint", "Your setup at a glance"), true },
		BuiltInMenu{ MenuPageIds::kPresets, T("menu.features.presets", "Presets"), []() { PresetsPageRenderer::Render(); },
			Icons::TI(ICON_TI_PALETTE), NavStyle::Accent, T("menu.features.presets_hint", "Apply look presets"), true },
		BuiltInMenu{ MenuPageIds::kCSEditor, T("menu.features.cs_editor", "CS Editor"), []() { globals::features::csEditor.DrawSettings(); },
			Icons::TI(ICON_TI_ADJUSTMENTS_HORIZONTAL), NavStyle::Launcher, T("menu.features.cs_editor_hint", "Edit weather, lighting & scenes"), true },
	};
	// Problems loading features (rejected, obsolete, ...)
	if (FeatureIssues::HasFeatureIssues()) {
		menuList.push_back(BuiltInMenu{ MenuPageIds::kFeatureIssues, T("menu.features.feature_issues", "Feature Issues"),
			[]() { FeatureIssues::DrawFeatureIssuesUI(); }, Icons::TI(ICON_TI_ALERT_TRIANGLE), NavStyle::Alert });
	}
	menuList.push_back(BuiltInMenu{ MenuPageIds::kSettings, T("menu.features.settings", "Settings"), drawSettings, Icons::TI(ICON_TI_SETTINGS) });
	menuList.push_back(BuiltInMenu{ MenuPageIds::kProfiling, T("menu.features.profiling", "Profiling"), []() { ProfilingRenderer::RenderStatistics(); },
		Icons::TI(ICON_TI_CHART_BAR), NavStyle::Plain, T("menu.features.profiling_hint", "What each feature costs per frame"), true });

	auto* state = globals::state;
	auto* sceneManager = globals::sceneSettingsManager;
	const auto filter = globals::menu->GetSettings().FeatureListFilter & FeatureListFilter::kKnown;
	const bool showUnloaded = (filter & FeatureListFilter::kUnloaded) != 0;
	const bool developerMode = state->IsDeveloperMode();
	// The Modified answers for every feature at once, under one scene layer suspension
	state->PrimeUserEdits();

	// Display-name order; the names are translated, so they are taken once here rather than in the sort
	std::vector<std::pair<std::string, Feature*>> named;
	for (Feature* feat : Feature::GetFeatureList()) {
		if (feat->IsInMenu())
			named.emplace_back(feat->GetDisplayName(), feat);
	}
	std::ranges::sort(named, {}, &std::pair<std::string, Feature*>::first);

	counts = {};
	std::vector<Feature*> favorites;
	std::vector<Feature*> listed;  // passed every filter, favorites excluded
	for (const auto& [name, feat] : named) {
		const auto shortName = feat->GetShortName();
		const bool unloadedListed = !feat->loaded && !feat->IsHiddenUnreleased() &&
		                            (!FeatureIssues::IsObsoleteFeature(shortName) || developerMode);
		if (unloadedListed)
			counts.unloaded++;
		// Without the Unloaded chip only loaded features are listed; with it, only the unloaded ones
		if (showUnloaded ? !unloadedListed : !feat->loaded)
			continue;

		const bool favorite = feat->loaded && state->IsFeatureFavorite(shortName);
		const bool modified = state->HasUserEdits(feat);
		const bool scene = sceneManager && sceneManager->HasAnySceneEntriesForFeature(shortName);
		const bool preRelease = feat->GetReleaseStage() != Feature::ReleaseStage::Release;
		counts.favorites += favorite;
		counts.modified += modified;
		counts.scene += scene;
		counts.preRelease += preRelease;

		const bool passes = (!(filter & FeatureListFilter::kFavorites) || favorite) &&
		                    (!(filter & FeatureListFilter::kModified) || modified) &&
		                    (!(filter & FeatureListFilter::kScene) || scene) &&
		                    (!(filter & FeatureListFilter::kPreRelease) || preRelease) &&
		                    (featureSearch.empty() || Util::FeatureMatchesSearch(feat, featureSearch));
		if (!passes)
			continue;
		counts.shown++;
		(favorite ? favorites : listed).push_back(feat);
	}

	// Favorites are pinned above the rest, outside any category
	std::ranges::copy(favorites, std::back_inserter(menuList));

	// Group features by category, or list them flat when sorting alphabetically
	if (globals::menu->GetSettings().FeatureListSort == Menu::FeatureSortMode::Alphabetical) {
		std::ranges::copy(listed, std::back_inserter(menuList));  // already in display-name order
		return menuList;
	}
	std::map<std::string, std::vector<Feature*>> categorizedFeatures;  // unknown sort values fall back to categories
	for (Feature* feat : listed)
		categorizedFeatures[std::string(feat->GetCategory())].push_back(feat);

	// Each header counts the features it lists
	Menu::categoryCounts.clear();
	for (const auto& [category, features] : categorizedFeatures)
		Menu::categoryCounts[category] = static_cast<int>(features.size());

	// Categories start expanded, except Utility (debug and developer tools)
	const auto addCategory = [&](const std::string& category, const std::vector<Feature*>& features) {
		menuList.push_back(CategoryHeader{ category });
		if (categoryExpansionStates.try_emplace(category, category != FeatureCategories::kUtility).first->second)
			std::ranges::copy(features, std::back_inserter(menuList));
	};
	for (const auto categoryName : FeatureCategories::kMainMenuOrder) {
		if (const auto it = categorizedFeatures.find(std::string(categoryName)); it != categorizedFeatures.end())
			addCategory(it->first, it->second);
	}
	// Then any category not in the predefined order
	for (const auto& [category, features] : categorizedFeatures) {
		if (std::ranges::find(FeatureCategories::kMainMenuOrder, std::string_view(category)) == FeatureCategories::kMainMenuOrder.end())
			addCategory(category, features);
	}

	return menuList;
}

void FeatureListRenderer::HandlePendingFeatureSelection(
	std::string& pendingFeatureSelection,
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu)
{
	if (pendingFeatureSelection.empty())
		return;

	// General and Advanced are sections of Settings now: their page IDs open it at the matching section
	if (const auto section = SettingsPageRenderer::GetAliasSection(pendingFeatureSelection)) {
		globals::menu->GetSettings().LastSettingsSection = std::string(*section);
		pendingFeatureSelection = std::string(MenuPageIds::kSettings);
	}

	// Feature short names first, then canonical page IDs, then page display names for callers that pass a label
	const auto select = [&](auto&& matches, const char* kind) {
		const auto it = std::ranges::find_if(menuList, matches);
		if (it == menuList.end())
			return false;
		selectedMenu = static_cast<size_t>(std::distance(menuList.begin(), it));
		logger::info("Navigated to {} {}", pendingFeatureSelection, kind);
		return true;
	};
	const auto isFeature = [&](const MenuFuncInfo& item) {
		const auto* feature = std::get_if<Feature*>(&item);
		return feature && (*feature)->GetShortName() == pendingFeatureSelection;
	};
	const auto isPageId = [&](const MenuFuncInfo& item) {
		const auto* page = std::get_if<BuiltInMenu>(&item);
		return page && page->id == pendingFeatureSelection;
	};
	const auto isPageName = [&](const MenuFuncInfo& item) {
		const auto* page = std::get_if<BuiltInMenu>(&item);
		return page && page->name == pendingFeatureSelection;
	};
	if (!select(isFeature, "feature menu") && !select(isPageId, "page") && !select(isPageName, "page")) {
		// A feature the filter chips leave out still opens, without changing the user's filters
		if (auto* feature = Feature::FindFeatureByShortName(pendingFeatureSelection)) {
			selectedMenu = std::numeric_limits<size_t>::max();
			g_detachedFeature = feature;
			logger::info("Navigated to {} feature menu (not listed)", pendingFeatureSelection);
		}
	}
	pendingFeatureSelection.clear();  // Clear after processing
}

void FeatureListRenderer::RenderLeftColumn(
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates,
	Hooks& hooks)
{
	const auto builtInAt = [&](size_t i) { return std::get_if<BuiltInMenu>(&menuList[i]); };

	// Top group: every built-in page outside the bottom group, in list order
	for (size_t i = 0; i < menuList.size(); i++) {
		if (const auto* menu = builtInAt(i); menu && !IsBottomMenu(menu->id))
			RenderBuiltInRow(*menu, i, selectedMenu);
	}
	NavGroupRule();

	// Middle region: search and sort stay put, the features scroll
	const float iconButtonWidth = ImGui::GetFrameHeight();
	{
		// The field edits a C buffer; the menu keeps the text as a string, so copy it in and take changes back.
		// While the field is active ImGui edits its own copy, so the per-frame copy never fights the typing.
		char searchBuffer[256];
		strncpy_s(searchBuffer, featureSearch.c_str(), _TRUNCATE);
		if (hooks.focusSearch)
			ImGui::SetKeyboardFocusHere();
		// Ctrl+F comes from the menu's shortcut table (focusSearch), so the field's own Ctrl+F stays off
		if (BrowserUI::SearchField("##FeatureSearch", searchBuffer, sizeof(searchBuffer), T("ui.search_features", "Search Features..."),
				ImGui::GetContentRegionAvail().x - (iconButtonWidth + ImGui::GetStyle().ItemSpacing.x) * 2.0f, false))
			featureSearch = searchBuffer;
	}
	const float searchBarHeight = ImGui::GetItemRectSize().y;

	{
		auto style = Util::TransparentIconButtonStyle();
		ImGui::SameLine();
		const auto sortIcon = GetSortModeIcon(globals::menu->GetSettings().FeatureListSort);
		if (Icons::Button("###FeatureSort", sortIcon, ImVec2(iconButtonWidth, searchBarHeight)))
			ImGui::OpenPopup("##FeatureSortPopup");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T("menu.features.sort_tooltip", "Sort features"));

		ImGui::SameLine();
		if (Icons::Button("###CollapseFeatureList", Icons::TI(ICON_TI_LAYOUT_SIDEBAR_LEFT_COLLAPSE), ImVec2(iconButtonWidth, searchBarHeight)))
			globals::menu->GetSettings().FeatureListCollapsed = true;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(I18n::GetSingleton()->Format("menu.features.collapse_list_tooltip",
														   { { "shortcut", hooks.toggleListShortcut } }, "Hide the feature list ({shortcut})")
					.c_str());
		}
	}

	RenderFilterChips(g_filterCounts);

	if (ImGui::BeginPopup("##FeatureSortPopup")) {
		using enum Menu::FeatureSortMode;
		auto& sort = globals::menu->GetSettings().FeatureListSort;
		if (ImGui::MenuItem(T("menu.features.sort_by_category", "By Category"), nullptr, sort == Category))
			sort = Category;
		if (ImGui::MenuItem(T("menu.features.sort_alphabetically", "Alphabetical"), nullptr, sort == Alphabetical))
			sort = Alphabetical;
		ImGui::EndPopup();
	}

	// The bottom group's height from last frame; the first frame estimates it from its rows
	if (g_bottomGroupHeight <= 0.0f) {
		MenuFonts::FontRoleGuard rowFont(Menu::FontRole::Subheading);
		const float rowHeight = ImGui::GetTextLineHeight() + ThemeManager::Constants::NAV_ROW_PADDING_Y * Util::GetUIScale() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
		g_bottomGroupHeight = rowHeight * static_cast<float>(kBottomMenuIds.size()) + ThemeManager::Constants::NAV_GROUP_SPACING * Util::GetUIScale() * 2.0f;
	}

	if (ImGui::BeginChild("##FeatureListScroll", ImVec2(0.0f, -g_bottomGroupHeight))) {
		for (size_t i = 0; i < menuList.size(); i++) {
			if (!builtInAt(i))
				std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates, hooks }, menuList[i]);
		}
		auto& filter = globals::menu->GetSettings().FeatureListFilter;
		if (g_filterCounts.shown == 0 && (filter != 0 || !featureSearch.empty())) {
			BrowserUI::EmptyState(T("menu.features.no_matches", "No features match"),
				T("menu.features.no_matches_hint", "Try another search, or turn off a filter."));
			const char* clearLabel = T("menu.features.clear_filters", "Clear search and filters");
			const float clearWidth = ImGui::CalcTextSize(clearLabel).x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - clearWidth) * 0.5f));
			if (BrowserUI::Link("##ClearFilters", clearLabel)) {
				filter = 0;
				featureSearch.clear();
			}
		}
	}
	ImGui::EndChild();

	// Bottom group, measured so next frame's list leaves exactly its height
	const float bottomStart = ImGui::GetCursorPosY();
	NavGroupRule();
	for (size_t i = 0; i < menuList.size(); i++) {
		if (const auto* menu = builtInAt(i); menu && IsBottomMenu(menu->id))
			RenderBuiltInRow(*menu, i, selectedMenu);
	}
	g_bottomGroupHeight = ImGui::GetCursorPosY() - bottomStart;
}

void FeatureListRenderer::RenderFilterChips(const FilterCounts& counts)
{
	auto& filter = globals::menu->GetSettings().FeatureListFilter;
	const ImVec4 accent = Util::Colors::GetAccent();
	const BrowserUI::IconPainter sceneIcon = [](ImDrawList*, ImVec2 center, float size, ImU32 color) {
		Util::DrawIconCircle(center, size * ThemeManager::Constants::FILTER_CHIP_DOT_RATIO, color, false);
	};

	struct Chip
	{
		std::uint32_t bit;
		BrowserUI::IconPainter icon;
		int count;
		ImVec4 tint;
		const char* tooltip;
	};
	const Chip chips[] = {
		{ FeatureListFilter::kFavorites, BrowserUI::GlyphIcon(Icons::TI(ICON_TI_PIN)), counts.favorites, accent,
			T("menu.features.filter_favorites", "Pinned features") },
		{ FeatureListFilter::kModified, BrowserUI::DotIcon(), counts.modified, Util::Colors::GetWarning(),
			T("menu.features.filter_modified", "Modified: settings differ from defaults and your Baseline presets") },
		{ FeatureListFilter::kScene, sceneIcon, counts.scene, Util::Colors::GetInfo(),
			T("menu.features.filter_scene", "Scene: has Scene Manager settings") },
		{ FeatureListFilter::kPreRelease, BrowserUI::GlyphIcon(Icons::TI(ICON_TI_FLASK)), counts.preRelease,
			Feature::GetReleaseStageColor(Feature::ReleaseStage::Beta), T("menu.features.filter_pre_release", "Pre-release: alpha and beta features") },
		{ FeatureListFilter::kUnloaded, BrowserUI::GlyphIcon(Icons::TI(ICON_TI_PLUG_OFF)), counts.unloaded, accent,
			T("menu.features.filter_unloaded", "Unloaded: missing, failed, or disabled at boot. Only these are listed while it is on") },
	};

	// A chip with nothing to show stays hidden unless it is on, so it can still be turned off
	bool first = true;
	for (const auto& chip : chips) {
		const bool active = (filter & chip.bit) != 0;
		if (chip.count == 0 && !active)
			continue;
		if (!first)
			BrowserUI::SameLineIfFits(BrowserUI::MeasureToggleChip(chip.count));
		first = false;
		ImGui::PushID(static_cast<int>(chip.bit));
		if (BrowserUI::ToggleChip("##FilterChip", chip.icon, chip.count, active, chip.tint, chip.tooltip))
			filter ^= chip.bit;
		ImGui::PopID();
	}
	if (filter != 0) {
		BrowserUI::SameLineIfFits(BrowserUI::IconButtonSize());
		if (BrowserUI::IconButton("##ClearFilterChips", Icons::TI(ICON_TI_X), T("menu.features.clear_filter_chips", "Turn off all filters")))
			filter = 0;
	}
}

void FeatureListRenderer::RenderCollapsedStrip(const Hooks& hooks)
{
	const float width = ThemeManager::Constants::FEATURE_LIST_STRIP_WIDTH * Util::GetUIScale();
	if (ImGui::BeginChild("##FeatureListStrip", ImVec2(width, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 size(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f));
		if (ImGui::InvisibleButton("##ExpandFeatureList", size))
			globals::menu->GetSettings().FeatureListCollapsed = false;
		const bool hovered = ImGui::IsItemHovered();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(I18n::GetSingleton()->Format("menu.features.expand_list_tooltip",
														   { { "shortcut", hooks.toggleListShortcut } }, "Show the feature list ({shortcut})")
					.c_str());
		}

		auto* drawList = ImGui::GetWindowDrawList();
		const ImVec2 max(min.x + size.x, min.y + size.y);
		drawList->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
		Icons::DrawCenteredGlyph(drawList, min, ImVec2(size.x, size.x), Icons::TI(ICON_TI_LAYOUT_SIDEBAR_LEFT_EXPAND),
			ImGui::GetColorU32(hovered ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : Util::Colors::GetSecondary()));
	}
	ImGui::EndChild();
}

void FeatureListRenderer::RenderBuiltInRow(const BuiltInMenu& menu, size_t listId, size_t& selectedMenu)
{
	ImGui::PushID(menu.id.data(), menu.id.data() + menu.id.size());
	if (menu.id == MenuPageIds::kCSEditor) {
		// Launcher: opens the editor window without leaving the current page
		const bool canOpen = EditorWindow::CanBeOpen();
		if (DrawNavRow(menu, false, !canOpen))
			CSEditor::OpenEditorWindow();
		Util::AddTooltip(canOpen ? T("menu.features.cs_editor_tooltip", "Opens the CS Editor window") :
								   T("menu.features.cs_editor_needs_save", "Load a save to open the CS Editor"),
			Util::kTooltipWhenDisabled);
	} else if (DrawNavRow(menu, selectedMenu == listId, false)) {
		selectedMenu = listId;
	}
	ImGui::PopID();
}

void FeatureListRenderer::RenderRightColumn(
	const std::vector<MenuFuncInfo>& menuList,
	size_t selectedMenu,
	std::string& pendingFeatureSelection,
	const Hooks& hooks)
{
	if (selectedMenu < menuList.size()) {
		std::visit(DrawMenuVisitor{ pendingFeatureSelection, hooks }, menuList[selectedMenu]);
	} else if (g_detachedFeature) {
		DrawMenuVisitor{ pendingFeatureSelection, hooks }(g_detachedFeature);
	} else {
		BrowserUI::EmptyState(T("menu.features.select_item_left", "Please select an item on the left."));
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(const BuiltInMenu& menu)
{
	RenderBuiltInRow(menu, listId, selectedMenuRef);
}

void FeatureListRenderer::ListMenuVisitor::operator()(const std::string& label)
{
	// Themed via ImGuiCol_Separator
	SeparatorTextWithFont(label, Menu::FontRole::Subheading);
}

void FeatureListRenderer::ListMenuVisitor::operator()(const CategoryHeader& header)
{
	// Get expansion state from static map
	bool isExpanded = categoryExpansionStates[header.name];

	// Draw category header with custom styling using util:UI function
	// Use Heading font for category headers
	{
		MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Heading);
		int count = Menu::categoryCounts[std::string(header.name)];
		const auto categoryLabel = TranslateCategory(header.name);
		Util::DrawCategoryHeader(header.name.c_str(), categoryLabel.c_str(), isExpanded, count);
	}

	// Update expansion state; the list adds or drops the category's features on its next rebuild
	if (isExpanded != categoryExpansionStates[header.name]) {
		categoryExpansionStates[header.name] = isExpanded;
		g_menuListDirty = true;
	}
}

void FeatureListRenderer::ListMenuVisitor::operator()(Feature* feat)
{
	MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subheading);

	const auto featureName = feat->GetShortName();
	ImGui::PushID(featureName.c_str());
	bool isDisabled = globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = feat->loaded;
	bool hasFailedMessage = !feat->failedLoadedMessage.empty();
	auto& themeSettings = globals::menu->GetSettings().Theme;

	ImVec4 textColor;

	// Determine the text color based on the state
	if (isDisabled) {
		textColor = themeSettings.StatusPalette.Disable;
	} else if (isLoaded) {
		textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	} else if (hasFailedMessage) {
		textColor = feat->version.empty() ? themeSettings.StatusPalette.Disable : themeSettings.StatusPalette.Error;
	} else {
		// Installed but not loaded means the feature is only pending a restart (green),
		// otherwise it is simply missing (grey).
		textColor = feat->installed ? themeSettings.StatusPalette.RestartNeeded : themeSettings.StatusPalette.Disable;
	}

	// Create selectable item with semantic color; the ID comes from the short name, so it survives a language change
	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	if (ImGui::Selectable(std::format(" {} ###Row", feat->GetDisplayName()).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns)) {
		selectedMenuRef = listId;
	}
	ImGui::PopStyleColor();
	if (g_scrollToSelection && selectedMenuRef == listId && !ImGui::IsItemVisible())
		ImGui::SetScrollHereY();
	const ImVec2 rowMin = ImGui::GetItemRectMin();
	const ImVec2 rowMax = ImGui::GetItemRectMax();
	const bool rowHovered = ImGui::IsItemHovered();
	RenderRowContextMenu(feat);

	// Favorites are pinned at the top of the list, marked by a pin attached to the name (its label ends in a space)
	if (isLoaded && globals::state->IsFeatureFavorite(featureName)) {
		ImGui::SameLine(0.0f, 0.0f);
		const Icons::GlyphRef pin = Icons::TI(ICON_TI_PIN);
		const float lineHeight = ImGui::GetTextLineHeight();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		Icons::FontGuard pinFont(pin, 0.75f);
		const ImVec2 pinBox(ImGui::CalcTextSize(pin.utf8).x, lineHeight);
		ImGui::Dummy(pinBox);
		ImGui::GetWindowDrawList()->AddText(Icons::GetCenteredGlyphPos(origin, pinBox, pin.utf8),
			ImGui::GetColorU32(Util::Colors::GetAccent()), pin.utf8);
	}

	// The stage badge sits behind the name, regardless of loaded state
	if (const auto stage = feat->GetReleaseStage(); stage != Feature::ReleaseStage::Release) {
		MenuFonts::FontRoleGuard badgeFont(Menu::FontRole::Subtext);
		BrowserUI::InlineBadge(stage == Feature::ReleaseStage::Alpha ? T("menu.features.badge_alpha", "Alpha") : T("menu.features.badge_beta", "Beta"),
			Feature::GetReleaseStageColor(stage), rowMax.y - rowMin.y);
	}

	// A feature only authored for other scenes still gets a hollow dot, so its settings stay discoverable.
	if (auto* sceneManager = globals::sceneSettingsManager; sceneManager->HasAnySceneEntriesForFeature(featureName)) {
		const bool applying = sceneManager->IsFeatureOverridingBase(featureName);
		ImGui::SameLine();
		Util::DrawInlineIndicatorDot(ImGui::GetColorU32(Util::Colors::GetInfo()), applying);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(applying ?
									   T("menu.features.scene_indicator_overriding",
										   "Scene Manager is overriding settings here. Blue settings show its values.") :
									   T("menu.features.scene_indicator_configured",
										   "Has Scene Manager settings for other times, weathers or locations."));

		if (sceneManager->HasSketches(featureName)) {
			ImGui::SameLine();
			Util::DrawInlineIndicatorDot(ImGui::GetColorU32(Util::Colors::GetSuccess()), true);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(T("menu.features.scene_indicator_sketched",
					"Has settings sketched over the Scene Manager's values. They revert once you leave this feature."));
		}
	}

	// Modified: a small dot at the row's right edge, painted so it never pushes the row wider
	if (isLoaded && globals::state->HasUserEdits(feat)) {
		const float lineHeight = ImGui::GetTextLineHeight();
		const float radius = lineHeight * ThemeManager::Constants::FEATURE_ROW_MODIFIED_DOT_RATIO;
		const ImVec2 center(rowMax.x - ImGui::GetStyle().FramePadding.x - radius, (rowMin.y + rowMax.y) * 0.5f);
		Util::DrawIconCircle(center, radius, ImGui::GetColorU32(Util::Colors::GetWarning()), true);
		const float half = lineHeight * 0.5f;
		if (rowHovered && ImGui::IsMouseHoveringRect(ImVec2(center.x - half, rowMin.y), ImVec2(center.x + half, rowMax.y)))
			ImGui::SetTooltip("%s", T("menu.features.modified_tooltip", "Modified: differs from defaults and your Baseline presets"));
	}
	ImGui::PopID();
}

void FeatureListRenderer::ListMenuVisitor::RenderRowContextMenu(Feature* feat) const
{
	if (!ImGui::BeginPopupContextItem("##RowMenu"))
		return;

	MenuFonts::FontRoleGuard menuFont(Menu::FontRole::Body);
	auto* state = globals::state;
	const auto featureName = feat->GetShortName();
	const bool isLoaded = feat->loaded;
	const bool isDisabled = state->IsFeatureDisabled(featureName);

	const bool isFavorite = state->IsFeatureFavorite(featureName);
	if (ImGui::MenuItem(isFavorite ? T("menu.features.unpin", "Unpin") : T("menu.features.pin", "Pin to Top"), nullptr, false, isLoaded))
		ToggleFavorite(feat);
	if (!isLoaded)
		Util::AddTooltip(T("menu.features.pin_needs_loaded", "Only loaded features can be pinned"), Util::kTooltipWhenDisabled);

	// The same condition as the feature page's Restore Defaults, plus something to restore
	const bool canRestore = isLoaded && !isDisabled && feat->UsesMainSettings();
	if (ImGui::MenuItem(T("menu.features.restore_defaults", "Restore Defaults"), nullptr, false, canRestore && state->HasUserEdits(feat))) {
		const auto restore = [feat] {
			globals::state->RestoreFeatureLayers(feat);
			// The scene layer's cached apply document must take the restored values before anything replays it
			globals::sceneSettingsManager->CaptureExternalFeatureChanges(feat, true);
		};
		if (hooks.runFeatureCommand)
			hooks.runFeatureCommand(feat, restore);
		else
			restore();
	}
	Util::AddTooltip(canRestore ? T("menu.features.restore_defaults_tooltip", "Restore defaults: clears your edits to this feature, back to what defaults and your Baseline presets set") :
								  T("menu.features.restore_unavailable", "Only loaded features that keep their settings in the user config can be restored"),
		Util::kTooltipWhenDisabled);

	if (ImGui::MenuItem(T("menu.features.load_at_boot", "Load at Boot"), nullptr, !isDisabled)) {
		const bool enabled = feat->ToggleAtBootSetting();
		logger::info("{}: {} at boot.", featureName, enabled ? "Enabled" : "Disabled");
	}
	Util::AddTooltip(T("menu.features.load_at_boot_tooltip", "Whether the game loads this feature at startup. Takes effect after a restart."));

	ImGui::Separator();
	if (ImGui::MenuItem(T("menu.features.copy_name", "Copy Name")))
		ImGui::SetClipboardText(feat->GetDisplayName().c_str());

	ImGui::EndPopup();
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const BuiltInMenu& menu)
{
	if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, true)) {
		if (menu.showHeader) {
			BrowserUI::PageHeader(menu.icon, menu.name.c_str(), menu.hint.c_str());
			ImGui::Separator();
			ImGui::Spacing();
		}
		menu.func();
	}
	ImGui::EndChild();
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const std::string&)
{
	// std::unreachable() from c++23
	// you are not supposed to have selected a label!
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const CategoryHeader&)
{
	// Category headers are not selectable in the right panel
	BrowserUI::EmptyState(T("menu.features.select_feature_left", "Please select a feature from the left."));
}

void FeatureListRenderer::DrawMenuVisitor::operator()(Feature* feat)
{
	const auto featureName = feat->GetShortName();
	bool isDisabled = globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = feat->loaded;
	bool hasFailedMessage = !feat->failedLoadedMessage.empty();

	if (hooks.onFeatureShown)
		hooks.onFeatureShown(feat);

	if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, true)) {
		// Render feature header with integrated action buttons
		RenderFeatureHeader(feat, isDisabled, isLoaded);

		// Render feature settings content
		RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage);
	}
	ImGui::EndChild();
	// Render reactive constraint warning outside the child window so it can appear as a top-level popup
	DrawConstraintWarningDialog(pendingFeatureSelection);
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureHeader(Feature* feat, bool isDisabled, bool isLoaded)
{
	auto& themeSettings = globals::menu->GetSettings().Theme;
	const auto featureName = feat->GetShortName();
	const ImGuiStyle& style = ImGui::GetStyle();

	const float buttonSpacing = ThemeManager::Constants::BUTTON_SPACING;
	const float iconSize = ImGui::GetFrameHeight();
	const float bootToggleWidth = iconSize * 1.6f;
	const float bootToggleHeight = iconSize * 0.8f;

	const bool canExport = !isDisabled && isLoaded && FeatureOverwritesPanel::HasExportableSettings(feat);
	// Restore clears the user's edits in the user config, which a feature with its own store does not use.
	const bool canRestore = !isDisabled && isLoaded && feat->UsesMainSettings();
	// Offered while the user's edits hide values one of their Baseline packs sets.
	const bool canUseBaselines = canRestore && globals::state->HasEditsUnderBaselines(feat);

	// The boot toggle is labelled so it does not read as an on/off switch for the effect itself
	const char* bootLabel = T("menu.features.load_at_boot", "Load at Boot");
	float bootLabelWidth = 0.0f;
	{
		MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
		bootLabelWidth = ImGui::CalcTextSize(bootLabel).x;
	}

	// Right side: pin, overflow menu, then the labelled boot toggle
	float totalButtonWidth = bootLabelWidth + style.ItemInnerSpacing.x + bootToggleWidth;
	if (isLoaded)
		totalButtonWidth += (iconSize + buttonSpacing) * 2.0f;

	// Get available content width for positioning
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const ImVec2 titleStartPos = ImGui::GetCursorScreenPos();

	// Get feature description for subtitle
	auto [description, keyFeatures] = feat->GetFeatureSummary();
	(void)keyFeatures;  // Not used for subtitle display

	// Draw feature title, version, and description on the left
	// Returns title-only height for button alignment
	const auto stage = feat->GetReleaseStage();
	const std::string stageTag = Feature::GetReleaseStageTag(stage);  // empty for Release; color unused when tag is empty
	const float titleOnlyHeight = DrawFeatureHeader(feat->GetDisplayName(), isLoaded ? feat->version : "", description, stageTag, Feature::GetReleaseStageColor(stage));

	// Save cursor position after header (for restoring after buttons are drawn)
	const ImVec2 cursorPosAfterHeader = ImGui::GetCursorScreenPos();

	// Controls sit middle-aligned with the title line only (not the description)
	const float rowY = titleStartPos.y + (titleOnlyHeight - iconSize) * 0.5f;
	float x = titleStartPos.x + availableWidth - totalButtonWidth;

	if (isLoaded) {
		ImGui::SetCursorScreenPos(ImVec2(x, rowY));
		RenderFavoriteButton(feat);
		x += iconSize + buttonSpacing;

		ImGui::SetCursorScreenPos(ImVec2(x, rowY));
		RenderActionsMenu(feat, canExport, canRestore, canUseBaselines);
		x += iconSize + buttonSpacing;
	}

	// Enable/Disable at boot toggle, with its label; one tooltip covers both
	bool bootEnabled = !isDisabled;
	ImGui::BeginGroup();
	{
		MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
		ImGui::SetCursorScreenPos(ImVec2(x, rowY + (iconSize - ImGui::GetTextLineHeight()) * 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetSecondary());
		ImGui::TextUnformatted(bootLabel);
		ImGui::PopStyleColor();
	}
	x += bootLabelWidth + style.ItemInnerSpacing.x;

	// Apply disabled styling if feature has failed to load
	const bool failed = !feat->failedLoadedMessage.empty();
	if (failed)
		ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Error);

	// The toggle is shorter than the icon buttons; centre it on them instead of aligning the tops
	ImGui::SetCursorScreenPos(ImVec2(x, rowY + (iconSize - bootToggleHeight) * 0.5f));
	if (Util::FeatureToggle("##BootToggle", &bootEnabled)) {
		bool newState = feat->ToggleAtBootSetting();
		logger::info("{}: {} at boot.", featureName, newState ? "Enabled" : "Disabled");
	}

	if (failed)
		ImGui::PopStyleColor();
	ImGui::EndGroup();

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			T("menu.features.boot_toggle_tooltip",
				"Toggle feature loading at boot.\n"
				"Current state: %s\n"
				"Restart required for changes to take effect.\n"
				"Disabling removes performance impact."),
			bootEnabled ? T("menu.features.enabled", "Enabled") : T("menu.features.disabled", "Disabled"));
	}

	// Drawn after all header items so the modal cannot clobber their hover state.
	if (canExport)
		FeatureOverwritesPanel::DrawExport();

	// Restore cursor position after the title and separator
	ImGui::SetCursorScreenPos(cursorPosAfterHeader);
}

void FeatureListRenderer::DrawFeatureBody(Feature* feat)
{
	assert(feat && feat->loaded);

	const ImVec2 cursorPosBefore = ImGui::GetCursorPos();
	{
		// Sketches live only while their feature is on screen; the next Update drops any other.
		globals::sceneSettingsManager->RetainSketches(feat->GetShortName());
		SceneWidgetInterceptor::Scope baselineScope({ .feature = feat, .baseline = true });
		// ImGui raises this flag for any value edit; set only inside the call, it means this feature changed.
		// Buttons that rewrite settings (a reset, a preset) raise no flag, so a finished click is checked instead.
		const auto& context = *ImGui::GetCurrentContext();
		const bool editedBefore = context.ActiveIdHasBeenEditedThisFrame;
		feat->DrawSettings();
		if (feat->UsesMainSettings()) {
			auto* state = globals::state;
			const bool edited = !editedBefore && context.ActiveIdHasBeenEditedThisFrame;
			if (edited)
				state->MarkSettingsChanged();
			if (Util::InteractionEndedThisFrame()) {
				// Those buttons also bypass the interceptor, so the scene layer's cached apply document must
				// take their values before anything replays it.
				globals::sceneSettingsManager->CaptureExternalFeatureChanges(feat, true);
				if (!edited)
					state->CheckFeatureSettingsChanged(feat);
				state->InvalidateEditsUnderBaselines(feat);
			}
		}
	}
	const ImVec2 cursorPosAfter = ImGui::GetCursorPos();

	DetectReactiveConstraints();

	const float cursorEpsilon = 0.1f;
	bool cursorMoved = (std::abs(cursorPosAfter.x - cursorPosBefore.x) > cursorEpsilon ||
						std::abs(cursorPosAfter.y - cursorPosBefore.y) > cursorEpsilon);
	if (!cursorMoved)
		BrowserUI::EmptyState(T("menu.features.no_settings_available", "There are no settings available for this feature."));
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureSettings(Feature* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage)
{
	auto& themeSettings = globals::menu->GetSettings().Theme;

	if (isDisabled) {
		RenderDisabledAtBootNotice(feat);
	} else {
		if (isLoaded) {
			DrawFeatureBody(feat);

			if (feat != &globals::features::csEditor && ProfilingRenderer::HasFeatureTimers(feat->GetShortName())) {
				ImGui::SeparatorText(T("menu.features.profiling", "Profiling"));
				ProfilingRenderer::RenderFeatureTimers(feat->GetShortName());
			}
		} else {
			if (FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
				feat->DrawUnloadedUI();
			} else if (feat->installed) {
				ImGui::Text("%s", T("menu.features.available_after_restart", "This feature will be available after restart."));
			} else {
				feat->DrawUnloadedUI();
				if (!feat->GetFeatureModLink().empty()) {
					ImGui::Spacing();
					auto featureModLink = feat->GetFeatureModLink();
					const auto downloadText = std::vformat(
						T("menu.features.download_link", "Click here to download this feature ({})"), std::make_format_args(featureModLink));
					if (ImGui::Selectable(downloadText.c_str())) {
						ShellExecuteA(NULL, "open", featureModLink.c_str(), NULL, NULL, SW_SHOWNORMAL);
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("%s", T("menu.features.download_tooltip", "Download the feature from the mod page."));
					}
				}
			}
		}
	}

	if (hasFailedMessage && feat->DrawFailLoadMessage() && !FeatureIssues::IsObsoleteFeature(feat->GetShortName())) {
		ImGui::Spacing();
		SeparatorTextWithFont(T("menu.features.error_header", "Error"), Menu::FontRole::Subheading);
		ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", feat->failedLoadedMessage.c_str());
	}
}

void FeatureListRenderer::DrawMenuVisitor::RunFeatureCommand(Feature* feat, const std::function<void()>& command) const
{
	if (hooks.runFeatureCommand)
		hooks.runFeatureCommand(feat, command);
	else
		command();
}

void FeatureListRenderer::DrawMenuVisitor::RenderActionsMenu(Feature* feat, bool canExport, bool canRestore, bool canUseBaselines)
{
	const float size = ImGui::GetFrameHeight();
	{
		auto style = Util::TransparentIconButtonStyle();
		if (Icons::Button("##FeatureActions", Icons::TI(ICON_TI_DOTS), ImVec2(size, size)))
			ImGui::OpenPopup("##FeatureActionsMenu");
	}
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("menu.features.actions_tooltip", "More actions"));

	if (!ImGui::BeginPopup("##FeatureActionsMenu"))
		return;
	MenuFonts::FontRoleGuard menuFont(Menu::FontRole::Body);

	if (ImGui::MenuItem(T("feature.scene_manager.overwrites.export.button_title", "Export Feature Overwrite..."), nullptr, false, canExport))
		FeatureOverwritesPanel::BeginExport(feat);
	Util::AddTooltip(canExport ? T("feature.scene_manager.overwrites.export.button_tooltip",
									 "Writes this feature's settings into a Baseline preset pack, applied from the Presets page. "
									 "Scene layers are left out.\n"
									 "For a full preset with scene layers, use Export Preset in the CS Editor (Ctrl+Shift+S).") :
								 T("menu.features.export_unavailable", "Only loaded features with settings an overwrite can carry can be exported"),
		Util::kTooltipWhenDisabled);

	if (ImGui::MenuItem(T("menu.features.use_baselines", "Use Baseline Values"), nullptr, false, canUseBaselines))
		RunFeatureCommand(feat, [feat] { globals::state->ClearFeatureEditsUnderBaselines(feat); });
	Util::AddTooltip(canUseBaselines ? T("menu.features.use_baselines_tooltip",
										   "Use Baseline preset values: your changes override settings your Baseline presets set. "
										   "Click to drop those changes and use the presets' values. Your other changes stay.") :
									   T("menu.features.use_baselines_unavailable", "None of your changes here override a setting your Baseline presets set"),
		Util::kTooltipWhenDisabled);

	const bool hasEdits = canRestore && globals::state->HasUserEdits(feat);
	if (ImGui::MenuItem(T("menu.features.restore_defaults", "Restore Defaults"), nullptr, false, hasEdits)) {
		// Clears the user's edits, so the feature shows what defaults and Baseline packs set.
		RunFeatureCommand(feat, [feat] { globals::state->RestoreFeatureLayers(feat); });
	}
	Util::AddTooltip(hasEdits   ? T("menu.features.restore_defaults_tooltip", "Restore defaults: clears your edits to this feature, back to what defaults and your Baseline presets set") :
					 canRestore ? T("menu.features.restore_nothing", "Nothing to restore: this feature already matches its defaults and your Baseline presets") :
								  T("menu.features.restore_unavailable", "Only loaded features that keep their settings in the user config can be restored"),
		Util::kTooltipWhenDisabled);

	ImGui::EndPopup();
}

void FeatureListRenderer::DrawMenuVisitor::RenderDisabledAtBootNotice(Feature* feat)
{
	BrowserUI::EmptyState(T("menu.features.settings_hidden_disabled", "Feature settings are hidden because this feature is disabled at boot."),
		T("menu.features.enable_at_boot_hint", "Load it at boot to use it the next time the game starts."));
	ImGui::Spacing();
	const char* label = T("menu.features.enable_at_boot", "Enable at Boot");
	const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
	if (ImGui::Button(label)) {
		const bool enabled = feat->ToggleAtBootSetting();
		logger::info("{}: {} at boot.", feat->GetShortName(), enabled ? "Enabled" : "Disabled");
	}
}

void FeatureListRenderer::ToggleFavorite(Feature* feat)
{
	const auto featureName = feat->GetShortName();
	globals::state->SetFeatureFavorite(featureName, !globals::state->IsFeatureFavorite(featureName));
	globals::state->CheckCoreSettingsChanged();
	// The favorites move rows around; the rebuild keeps whatever is selected selected
	g_menuListDirty = true;
}

void FeatureListRenderer::DrawMenuVisitor::RenderFavoriteButton(Feature* feat)
{
	const bool isFavorite = globals::state->IsFeatureFavorite(feat->GetShortName());
	const float size = ImGui::GetFrameHeight();
	{
		auto style = Util::TransparentIconButtonStyle();
		if (Icons::Button("###Favorite", Icons::TI(isFavorite ? ICON_TI_PINNED_OFF : ICON_TI_PIN), ImVec2(size, size)))
			ToggleFavorite(feat);
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", isFavorite ? T("menu.features.remove_from_favorites_tooltip", "Remove from favorites: moves this feature back into the main list") : T("menu.features.add_to_favorites_tooltip", "Add to favorites: lists this feature at the top of the feature list"));
	}
}

void FeatureListRenderer::DrawConstraintWarningDialog(std::string& pendingFeatureSelection)
{
	if (!g_reactiveWarningShow) {
		return;
	}

	constexpr const char* popupId = "###SettingChangeWarning";
	const std::string popupTitle = fmt::format("{}{}", T("menu.features.setting_change_warning_title", "Setting Change Warning"), popupId);

	// OpenPopup is idempotent while the popup is already open, so calling it
	// every frame while the flag is set is safe and ensures we don't miss the
	// one-frame window where ImGui expects it.
	ImGui::OpenPopup(popupId);

	// Center the popup (ImGuiCond_Always matches the Clear Cache dialog pattern)
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	if (Util::BeginPopupModalWithRoundedClose(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("%s", T("menu.features.settings_adjusted_warning", "Some of your settings have been automatically adjusted due to feature incompatibilities."));
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		std::string navigationTarget;  // Set on a link click so the table and popup still end cleanly.

		// Table columns: Impacted Feature | Setting | Constrained By | Forced To
		if (ImGui::BeginTable("##ReactiveConstraintTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn(T("menu.features.col_impacted_feature", "Impacted Feature"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.col_setting", "Setting"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.col_constrained_by", "Constrained By"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.col_forced_to", "Forced To"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			size_t rowIndex = 0;
			for (const auto& [settingId, result] : g_reactiveWarningConstraints) {
				ImGui::TableNextRow();

				// --- Column 0: Impacted Feature (clickable -> navigate to that feature) ---
				ImGui::TableSetColumnIndex(0);
				{
					// Look up the display name of the target feature from its short name
					std::string targetDisplayName = settingId.featureShortName;
					for (auto* f : Feature::GetFeatureList()) {
						if (f->GetShortName() == settingId.featureShortName) {
							targetDisplayName = f->GetDisplayName();
							break;
						}
					}
					if (ImGui::Selectable(fmt::format("{}##imp{}", targetDisplayName, rowIndex).c_str())) {
						navigationTarget = settingId.featureShortName;
						break;
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(T("menu.features.click_to_navigate", "Click to navigate to %s"), targetDisplayName.c_str());
					}
				}

				// --- Column 1: Setting name ---
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", settingId.settingPath.c_str());

				// --- Column 2: Constrained By (source features, clickable) ---
				ImGui::TableSetColumnIndex(2);
				if (!result.sources.empty()) {
					if (ImGui::Selectable(fmt::format("{}##src{}", result.sources[0].featureName, rowIndex).c_str())) {
						navigationTarget = result.sources[0].featureShortName;
						break;
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(T("menu.features.click_to_navigate", "Click to navigate to %s"), result.sources[0].featureName.c_str());
						if (result.sources.size() > 1) {
							ImGui::Separator();
							for (size_t i = 1; i < result.sources.size(); ++i) {
								ImGui::Text(T("menu.features.also_feature", "Also: %s"), result.sources[i].featureName.c_str());
							}
						}
						ImGui::Separator();
						ImGui::Text("%s", result.sources[0].reason.c_str());
					}
				}

				// --- Column 3: Forced value ---
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%s", FeatureConstraints::FormatConstraintValue(result.forcedValue).c_str());

				rowIndex++;
			}

			ImGui::EndTable();
		}

		if (!navigationTarget.empty()) {
			pendingFeatureSelection = navigationTarget;
			g_reactiveWarningShow = false;
			g_reactiveWarningConstraints.clear();
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextWrapped(
			"%s",
			T("menu.features.constraints_explanation",
				"These settings are disabled in their respective feature menus while the constraints are active. "
				"Adjust the constraining features to remove them."));

		ImGui::Spacing();

		// "Don't show again" checkbox -- same pattern as Clear Cache dialog
		ImGui::Checkbox(T("menu.features.dont_show_warning", "Don't show this warning again"), &g_dontShowAgainCheckbox);

		ImGui::Spacing();

		// Centered OK button
		constexpr float buttonWidth = ThemeManager::Constants::POPUP_BUTTON_WIDTH;
		const float windowWidth = ImGui::GetWindowWidth();
		const float offset = (windowWidth - buttonWidth) * 0.5f;
		if (offset > 0)
			ImGui::SetCursorPosX(offset);

		if (ImGui::Button(T("menu.features.ok_button", "OK"), ImVec2(buttonWidth, 0))) {
			if (g_dontShowAgainCheckbox) {
				if (auto* menu = globals::menu) {
					menu->GetSettings().SkipConstraintWarning = true;
				}
			}
			g_reactiveWarningShow = false;
			g_reactiveWarningConstraints.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	} else {
		// Popup was closed externally (e.g. clicked outside), reset state
		g_reactiveWarningShow = false;
		g_reactiveWarningConstraints.clear();
	}
}
