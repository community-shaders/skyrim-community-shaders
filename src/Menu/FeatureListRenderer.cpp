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
#include "Menu/MenuIcons.h"
#include "Menu/PresetsPageRenderer.h"
#include "Menu/ProfilingRenderer.h"
#include "Menu/SettingReveal.h"
#include "Menu/SettingsPageRenderer.h"
#include "Menu/StatusBarRenderer.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"
#include "Utils/UISurface.h"

namespace
{
	// Built-in pages reached from the status bar's icon buttons rather than a list row
	constexpr std::array kBottomMenuIds = { MenuPageIds::kSettings, MenuPageIds::kProfiling };

	bool IsBottomMenu(std::string_view menuId)
	{
		return std::ranges::find(kBottomMenuIds, menuId) != kBottomMenuIds.end();
	}

	/**
	 * @brief Clears a feature's user edits through the menu's undo hook, so it shows what defaults and Baseline packs set.
	 * The scene layer's cached apply document takes the restored values too, or the next scene apply would replay the old ones.
	 */
	void RestoreFeatureDefaults(const FeatureListRenderer::Hooks& hooks, Feature* feat)
	{
		const auto restore = [feat] {
			globals::state->RestoreFeatureLayers(feat);
			globals::sceneSettingsManager->CaptureExternalFeatureChanges(feat, true);
		};
		if (hooks.runFeatureCommand)
			hooks.runFeatureCommand(feat, restore);
		else
			restore();
	}

	/// State of the auto-hide feature list (Menu::Settings::AutoHideFeatureList): whether it is meant to be shown,
	/// how far it has slid in, and the timers that keep it from flickering.
	struct AutoHideDrawer
	{
		bool open = false;
		float progress = 0.0f;     ///< 0 = hidden, 1 = fully shown
		double hoverSince = -1.0;  ///< When the cursor reached the edge zone; -1 while it is not there
		double leaveSince = -1.0;  ///< When the cursor left the shown list; -1 while it is there
		bool keepOpen = false;     ///< Last frame, something in the list was active or a popup was open
	};
	AutoHideDrawer g_drawer;

	/// Page ID the status bar's Settings / Profiling buttons asked to open (or toggle) this frame; empty when none.
	std::string g_requestedPage;
	/// ID of the built-in page open as of the last frame; empty while a feature or detached page is shown.
	std::string g_openPageId;

	/// What the cached menu list was built from; any difference rebuilds it.
	struct MenuListKey
	{
		std::uint32_t filter = 0;
		Menu::FeatureSortMode sort = Menu::FeatureSortMode::Category;
		std::uint64_t i18nGeneration = 0;
		std::uint64_t userEditsRevision = 0;
		std::uint64_t sceneRevision = 0;
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

	/// Filter counts from the last rebuild; they only change when the list does.
	FeatureListRenderer::FilterCounts g_filterCounts;

	/// The selected feature while the search, a filter or a collapsed category leaves it out of the list, so its
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

	/// The entry that was selected before the Settings page opened, so "Features" can go back to it.
	std::string g_backKey;
	/// Selection index g_backKey was last captured for.
	size_t g_backTrackedIndex = std::numeric_limits<size_t>::max() - 1;

	/** @brief Remembers the current non-Settings selection (only recomputed when the selection changes). */
	void TrackBackTarget(const std::vector<FeatureListRenderer::MenuFuncInfo>& menuList, size_t selectedMenu)
	{
		if (selectedMenu == g_backTrackedIndex && selectedMenu < menuList.size())
			return;
		g_backTrackedIndex = selectedMenu;
		g_backKey = selectedMenu < menuList.size() ? GetEntryKey(menuList[selectedMenu]) :
		            g_detachedFeature              ? "feature:" + g_detachedFeature->GetShortName() :
		                                             std::string();
	}

	/** @brief Leaves the Settings page for the page selected before it, or Home when that is unknown. */
	void LeaveSettings(const std::vector<FeatureListRenderer::MenuFuncInfo>& menuList, size_t& selectedMenu)
	{
		const std::string key = g_backKey.empty() ? "page:" + std::string(MenuPageIds::kHome) : g_backKey;
		ReselectEntry(key, menuList, selectedMenu);
		g_backTrackedIndex = std::numeric_limits<size_t>::max() - 1;
	}

	/** @brief "< Features" row heading the Settings section list. @return True when clicked. */
	bool DrawBackRow()
	{
		MenuFonts::FontRoleGuard labelFont(Menu::FontRole::Subheading);
		const float lineHeight = ImGui::GetTextLineHeight();
		const float padX = ImGui::CalcTextSize(" ").x;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const bool clicked = Util::Selectable("##BackToFeatures", false, ImGuiSelectableFlags_None, ImVec2(0.0f, lineHeight));
		auto* drawList = ImGui::GetWindowDrawList();
		Icons::DrawCenteredGlyph(drawList, ImVec2(pos.x + padX, pos.y), ImVec2(lineHeight, lineHeight), Icons::TI(ICON_TI_CHEVRON_LEFT),
			ImGui::GetColorU32(Util::Colors::GetSecondary()));
		drawList->AddText(ImVec2(pos.x + padX + lineHeight + ThemeManager::Constants::NAV_ROW_ICON_GAP * Util::GetUIScale(), pos.y),
			ImGui::GetColorU32(ImGuiCol_Text), T("menu.features.back_to_features", "Features"));
		return clicked;
	}

	/**
	 * @brief Draws one pill of the top page switcher: a centred accent icon and label. The selected pill gets an eased
	 * accent tint and outline, hover a soft fill.
	 * @param width Pill width; 0 fills the rest of the line.
	 * @return True when clicked.
	 */
	bool DrawNavRow(const FeatureListRenderer::BuiltInMenu& menu, bool selected, bool disabled, float width = 0.0f)
	{
		using Constants = ThemeManager::Constants;
		using NavStyle = FeatureListRenderer::NavStyle;

		const float scale = Util::GetUIScale();
		const float lineHeight = ImGui::GetTextLineHeight();
		const float height = ImGui::GetFrameHeight();
		const ImVec2 rowPos = ImGui::GetCursorScreenPos();
		const float rowWidth = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;

		bool clicked = false;
		{
			// The pill draws its own highlight, so the list highlight stays out of it
			Util::Anim::NoListHighlight ownHighlight;
			// A dimmed pill still takes the click: the caller explains why it cannot act
			ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32_BLACK_TRANS);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32_BLACK_TRANS);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32_BLACK_TRANS);
			clicked = ImGui::Selectable("##NavRow", false, ImGuiSelectableFlags_None, ImVec2(rowWidth, height));
			ImGui::PopStyleColor(3);
		}
		auto* drawList = ImGui::GetWindowDrawList();
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float rounding = ImGui::GetStyle().FrameRounding;
		const auto blend = Util::Anim::ItemBlend(ImGui::GetItemID(), ImGui::IsItemHovered() && !disabled, ImGui::IsItemActive(), selected);
		Util::Anim::FillLayer(drawList, min, max, rounding, ImGuiCol_HeaderHovered, blend.hover);
		Util::Anim::FillLayer(drawList, min, max, rounding, ImGuiCol_HeaderActive, blend.press);
		if (selected || blend.selected > 0.0f) {
			// The selected pill is one shape that glides between the pills; one that was just left fades out in place
			const auto pill = selected ? Util::Anim::SelectionGlide(ImHashStr("NavPillGlide"), min, max) : Util::Anim::GlideRect{ min, max };
			const float alpha = selected ? 1.0f : blend.selected;
			ImVec4 accent = Util::Colors::GetAccent();
			accent.w = Constants::SEGMENT_SELECTED_FILL_ALPHA * alpha;
			drawList->AddRectFilled(pill.min, pill.max, ImGui::GetColorU32(accent), rounding);
			accent.w = Constants::SEGMENT_SELECTED_OUTLINE_ALPHA * alpha;
			drawList->AddRect(pill.min, pill.max, ImGui::GetColorU32(accent), rounding);
		}

		const ImVec4 textColor = ImGui::GetStyleColorVec4(disabled ? ImGuiCol_TextDisabled : ImGuiCol_Text);
		const ImVec4 iconColor = (!disabled && menu.style != NavStyle::Plain) ? Util::Colors::GetAccent() : textColor;
		const float iconGap = Constants::NAV_ROW_ICON_GAP * scale;

		// Icon and label form one group centred in the pill, both centred on the pill's height by their ink. The label is
		// never trimmed: the pill clips it
		const float labelWidth = ImGui::CalcTextSize(menu.name.c_str()).x;
		const float iconWidth = menu.icon ? lineHeight + iconGap : 0.0f;
		float x = std::max(min.x, min.x + ((max.x - min.x) - iconWidth - labelWidth) * 0.5f);
		drawList->PushClipRect(min, max, true);
		if (menu.icon) {
			Icons::DrawCenteredGlyph(drawList, ImVec2(x, min.y), ImVec2(lineHeight, max.y - min.y), menu.icon, ImGui::GetColorU32(iconColor));
			x += iconWidth;
		}
		drawList->AddText(ImVec2(x, Util::CenterTextTopY(min.y, max.y - min.y)), ImGui::GetColorU32(textColor), menu.name.c_str());
		drawList->PopClipRect();
		return clicked;
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

	/**
	 * @brief Draws a feature header: the name in large text, then the stage tag and version bottom-aligned beside it,
	 * the description below and a separator.
	 * @param featureName The display name of the feature
	 * @param version The version string (can be empty)
	 * @param description Short description shown below the title, wrapped to the content width
	 * @param stageTag Release stage marker such as [BETA] (can be empty)
	 * @param stageColor Colour of the stage tag
	 * @param trailingX Receives the screen x just past the title, stage tag and version, where more annotations can go
	 * @return The height of just the title line (for button alignment)
	 */
	float DrawFeatureHeader(const std::string& featureName, const std::string& version, const std::string& description = "", const std::string& stageTag = "", ImVec4 stageColor = {}, float* trailingX = nullptr)
	{
		const auto& themeSettings = globals::menu->GetTheme();
		const ImVec4 textColor = themeSettings.Palette.Text;

		const float titleScale = globals::menu->GetFeatureTitleScale();

		// Size and draw text in a font role at the title scale
		const auto scaledSize = [titleScale](Menu::FontRole role, const char* text) {
			MenuFonts::FontRoleGuard guard(role);
			const ImVec2 size = ImGui::CalcTextSize(text);
			return ImVec2(size.x * titleScale, size.y * titleScale);
		};
		const auto drawScaled = [titleScale](Menu::FontRole role, const ImVec4& color, const char* text) {
			MenuFonts::FontRoleGuard guard(role);
			ImGui::SetWindowFontScale(titleScale);
			ImGui::TextColored(color, "%s", text);
			ImGui::SetWindowFontScale(1.0f);
		};

		const ImVec2 startPos = ImGui::GetCursorScreenPos();
		const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
		const ImVec2 titleSize = scaledSize(Menu::FontRole::Title, featureName.c_str());
		drawScaled(Menu::FontRole::Title, ImGui::GetStyleColorVec4(ImGuiCol_Text), featureName.c_str());

		// Running x for the bottom-aligned annotations (stage tag, then version) right of the title
		float annotationX = startPos.x + titleSize.x + itemSpacingX;

		if (!stageTag.empty()) {
			const ImVec2 tagSize = scaledSize(Menu::FontRole::Body, stageTag.c_str());
			ImGui::SetCursorScreenPos(ImVec2(annotationX, startPos.y + titleSize.y - tagSize.y));
			drawScaled(Menu::FontRole::Body, stageColor, stageTag.c_str());
			annotationX += tagSize.x + itemSpacingX;
		}

		if (!version.empty()) {
			std::string formattedVersion = "v" + version;
			std::ranges::replace(formattedVersion, '-', '.');
			const ImVec2 versionSize = scaledSize(Menu::FontRole::Body, formattedVersion.c_str());
			ImGui::SetCursorScreenPos(ImVec2(annotationX, startPos.y + titleSize.y - versionSize.y));
			drawScaled(Menu::FontRole::Body, Util::Anim::ScaleAlpha(textColor, ThemeManager::Constants::VERSION_TEXT_OPACITY), formattedVersion.c_str());
			annotationX += versionSize.x + itemSpacingX;
		}

		// Back under the title block
		if (!stageTag.empty() || !version.empty())
			ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + titleSize.y + ImGui::GetStyle().ItemSpacing.y * 0.25f));

		if (trailingX)
			*trailingX = annotationX;

		if (!description.empty()) {
			MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Anim::ScaleAlpha(textColor, ThemeManager::Constants::FEATURE_DESCRIPTION_OPACITY));
			ImGui::TextWrapped("%s", description.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Separator();
		return titleSize.y;
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
	std::string& pendingFeatureSelection,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawSettings,
	Hooks& hooks)
{
	ImGui::BeginChild("Menus Table", ImVec2(0, -footerHeight));

	// A collapsed category could hide the feature being navigated to
	if (!pendingFeatureSelection.empty()) {
		if (const auto* feature = Feature::FindFeatureByShortName(pendingFeatureSelection))
			categoryExpansionStates[std::string(feature->GetCategory())] = true;
		g_menuListDirty = true;
	}

	// The entry selected in the list as it stood, so a rebuild keeps the same one selected (and a detached
	// feature the rebuilt list shows again takes its row back)
	const std::string selectedKey = selectedMenu < g_menuList.list.size() ? GetEntryKey(g_menuList.list[selectedMenu]) :
	                                g_detachedFeature                     ? "feature:" + g_detachedFeature->GetShortName() :
	                                                                        std::string();
	if (UpdateMenuList(categoryExpansionStates, drawSettings) && !selectedKey.empty())
		ReselectEntry(selectedKey, g_menuList.list, selectedMenu);
	const auto& menuList = g_menuList.list;

	HandlePendingFeatureSelection(pendingFeatureSelection, menuList, selectedMenu);

	// A status bar page button: Settings toggles, a second click goes back to where it was opened from
	if (!g_requestedPage.empty()) {
		const auto page = std::exchange(g_requestedPage, std::string());
		for (size_t i = 0; i < menuList.size(); i++) {
			const auto* menu = std::get_if<BuiltInMenu>(&menuList[i]);
			if (!menu || menu->id != page)
				continue;
			if (menu->id == MenuPageIds::kSettings && selectedMenu == i)
				LeaveSettings(menuList, selectedMenu);
			else
				selectedMenu = i;
			break;
		}
	}
	{
		const auto* open = selectedMenu < menuList.size() ? std::get_if<BuiltInMenu>(&menuList[selectedMenu]) : nullptr;
		g_openPageId = open ? std::string(open->id) : std::string();
	}

	if (hooks.featureStep != 0) {
		g_scrollToSelection = StepFeatureSelection(menuList, selectedMenu, hooks.featureStep > 0 ? 1 : -1);
		hooks.featureStep = 0;
	}
	if (selectedMenu < menuList.size())
		g_detachedFeature = nullptr;

	// The Settings page keeps its section list pinned: auto-hiding it would leave no way to move between sections
	const auto* openPage = selectedMenu < menuList.size() ? std::get_if<BuiltInMenu>(&menuList[selectedMenu]) : nullptr;
	const bool settingsOpen = openPage && openPage->id == MenuPageIds::kSettings && hooks.drawSettingsNav;
	if (globals::menu->GetSettings().AutoHideFeatureList && !settingsOpen) {
		RenderAutoHidePane(menuList, selectedMenu, pendingFeatureSelection, categoryExpansionStates, hooks);
	} else if (ImGui::BeginTable("Menus Table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("##ListOfMenus", 0, 2);
		ImGui::TableSetupColumn("##MenuConfig", 0, 8);
		ImGui::TableNextColumn();
		// Changes made here (search, filters, sort, a category, a pin) rebuild the list next frame
		RenderLeftColumn(menuList, selectedMenu, categoryExpansionStates, hooks);
		ImGui::TableNextColumn();
		RenderRightColumn(menuList, selectedMenu, pendingFeatureSelection, hooks);
		ImGui::EndTable();
	}
	g_scrollToSelection = false;  // a hidden list drops the request rather than scrolling later

	ImGui::EndChild();
}

bool FeatureListRenderer::UpdateMenuList(
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawSettings)
{
	const auto& menuSettings = globals::menu->GetSettings();
	MenuListKey key{
		.filter = menuSettings.FeatureListFilter & FeatureListFilter::kKnown,
		.sort = menuSettings.FeatureListSort,
		.i18nGeneration = I18n::GetSingleton()->GetGeneration(),
		.userEditsRevision = globals::state->GetUserEditsRevision(),
		.sceneRevision = globals::sceneSettingsManager ? globals::sceneSettingsManager->GetEntryPresentationRevision() : 0,
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

	g_menuList.list = BuildMenuList(categoryExpansionStates, drawSettings, g_filterCounts);
	g_menuList.key = std::move(key);
	g_menuList.valid = true;
	g_menuListDirty = false;
	return true;
}

std::uint32_t FeatureListRenderer::GetFilterBits(Feature* feat)
{
	auto* sceneManager = globals::sceneSettingsManager;
	const auto shortName = feat->GetShortName();
	std::uint32_t bits = 0;
	if (feat->loaded && globals::state->IsFeatureFavorite(shortName))
		bits |= FeatureListFilter::kFavorites;
	if (globals::state->HasUserEdits(feat))
		bits |= FeatureListFilter::kModified;
	if (sceneManager && sceneManager->HasAnySceneEntriesForFeature(shortName))
		bits |= FeatureListFilter::kScene;
	if (feat->GetReleaseStage() != Feature::ReleaseStage::Release)
		bits |= FeatureListFilter::kPreRelease;
	if (!feat->loaded)
		bits |= FeatureListFilter::kUnloaded;
	return bits;
}

bool FeatureListRenderer::PassesFilter(std::uint32_t bits, std::uint32_t filter)
{
	constexpr std::uint32_t kAttributes = FeatureListFilter::kKnown & ~FeatureListFilter::kUnloaded;
	const std::uint32_t required = filter & kAttributes;
	return (bits & required) == required;
}

const FeatureListRenderer::FilterCounts& FeatureListRenderer::GetFilterCounts()
{
	return g_filterCounts;
}

std::vector<FeatureListRenderer::MenuFuncInfo> FeatureListRenderer::BuildMenuList(
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawSettings,
	FilterCounts& counts)
{
	// Built-in pages, in list order: the top group (Home, Presets, CS Editor, then any page added after them),
	// then the bottom group (kBottomMenuIds)
	auto menuList = std::vector<MenuFuncInfo>{
		BuiltInMenu{ MenuPageIds::kHome, T("menu.features.home", "Home"), []() { HomePageRenderer::RenderHomePage(); },
			Icons::TI(MenuIcons::kHome), NavStyle::Plain, {}, false },
		BuiltInMenu{ MenuPageIds::kPresets, T("menu.features.presets", "Presets"), []() { PresetsPageRenderer::Render(); },
			Icons::TI(MenuIcons::kPresets), NavStyle::Accent, T("menu.features.presets_hint", "Control, change and manage your visual Presets to customise your CS experience"), true },
		BuiltInMenu{ MenuPageIds::kCSEditor, T("menu.features.cs_editor", "CS Editor"), []() { globals::features::csEditor.DrawSettings(); },
			Icons::TI(MenuIcons::kCSEditor), NavStyle::Accent, {}, true },
	};
	menuList.push_back(BuiltInMenu{ MenuPageIds::kSettings, T("menu.features.settings", "Settings"), drawSettings, Icons::TI(MenuIcons::kSettings) });
	menuList.push_back(BuiltInMenu{ MenuPageIds::kProfiling, T("menu.features.profiling", "Profiling"), []() { ProfilingRenderer::RenderStatistics(); },
		Icons::TI(MenuIcons::kProfiling), NavStyle::Plain, T("menu.features.profiling_hint", "What each feature costs per frame"), true });

	auto* state = globals::state;
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
		// Without the Unloaded filter only loaded features are listed; with it, only the unloaded ones
		if (showUnloaded ? !unloadedListed : !feat->loaded)
			continue;

		const auto bits = GetFilterBits(feat);
		const bool favorite = (bits & FeatureListFilter::kFavorites) != 0;
		counts.favorites += favorite;
		counts.modified += (bits & FeatureListFilter::kModified) != 0;
		counts.scene += (bits & FeatureListFilter::kScene) != 0;
		counts.preRelease += (bits & FeatureListFilter::kPreRelease) != 0;

		if (!PassesFilter(bits, filter))
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
	for (const auto categoryName : FeatureCategories::kMenuOrder) {
		if (const auto it = categorizedFeatures.find(std::string(categoryName)); it != categorizedFeatures.end())
			addCategory(it->first, it->second);
	}
	// Then any category not in the predefined order
	for (const auto& [category, features] : categorizedFeatures) {
		if (std::ranges::find(FeatureCategories::kMenuOrder, std::string_view(category)) == FeatureCategories::kMenuOrder.end())
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

	// Issues live on the Home page now
	if (pendingFeatureSelection == MenuPageIds::kIssues)
		pendingFeatureSelection = std::string(MenuPageIds::kHome);

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
		// A feature the filters leave out still opens, without changing the user's filters
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
	std::map<std::string, bool>& categoryExpansionStates,
	Hooks& hooks)
{
	const auto builtInAt = [&](size_t i) { return std::get_if<BuiltInMenu>(&menuList[i]); };
	const auto* selectedPage = selectedMenu < menuList.size() ? std::get_if<BuiltInMenu>(&menuList[selectedMenu]) : nullptr;
	// The Settings page swaps the feature list for its own section list
	const bool settingsOpen = selectedPage && selectedPage->id == MenuPageIds::kSettings && hooks.drawSettingsNav;

	// With the status bar off, Settings and Profiling live in a row at the pane's bottom, so the list leaves room for it
	const bool pageButtonsHere = !globals::menu->GetSettings().Theme.ShowFooter;
	const float pageButtonsHeight = pageButtonsHere ? StatusBarRenderer::GetPageButtonsHeight() : 0.0f;

	// Top group: the built-in pages outside the bottom group, in list order, as buttons sharing one line.
	// Home has no button; its header icon opens it.
	std::array<size_t, 4> topIds{};
	size_t topCount = 0;
	for (size_t i = 0; i < menuList.size() && topCount < topIds.size(); i++) {
		if (const auto* menu = builtInAt(i); menu && !IsBottomMenu(menu->id) && menu->id != MenuPageIds::kHome)
			topIds[topCount++] = i;
	}
	if (topCount > 0) {
		// A rounded track holds the pages as pills, so the group reads as one segmented control
		const float pad = ThemeManager::Constants::SEGMENT_TRACK_PAD * Util::GetUIScale();
		// The pane clips at its own edge, which cut the top and the side borders; leave a little room
		const float inset = ThemeManager::Constants::LEFT_PANE_EDGE_INSET * Util::GetUIScale();
		ImVec2 trackMin = ImGui::GetCursorScreenPos();
		trackMin.x += inset;
		trackMin.y += inset;
		const float trackWidth = ImGui::GetContentRegionAvail().x - inset * 2.0f;
		const float pillGap = pad * ThemeManager::Constants::SEGMENT_PILL_GAP_RATIO;
		const float pillWidth = (trackWidth - pad * 2.0f - pillGap * static_cast<float>(topCount - 1)) / static_cast<float>(topCount);
		ImGui::GetWindowDrawList()->AddRectFilled(trackMin, ImVec2(trackMin.x + trackWidth, trackMin.y + ImGui::GetFrameHeight() + pad * 2.0f),
			ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding + pad);
		for (size_t n = 0; n < topCount; n++) {
			ImGui::SetCursorScreenPos(ImVec2(trackMin.x + pad + static_cast<float>(n) * (pillWidth + pillGap), trackMin.y + pad));
			RenderBuiltInRow(*builtInAt(topIds[n]), topIds[n], selectedMenu, pillWidth);
		}
		ImGui::SetCursorScreenPos(ImVec2(trackMin.x - inset, trackMin.y + ImGui::GetFrameHeight() + pad * 2.0f));
		ImGui::Dummy(ImVec2(trackWidth + inset * 2.0f, 0.0f));
		ImGui::Spacing();
	}

	if (!settingsOpen)
		TrackBackTarget(menuList, selectedMenu);

	if (settingsOpen) {
		if (DrawBackRow())
			LeaveSettings(menuList, selectedMenu);
		if (ImGui::BeginChild("##SettingsNavScroll", ImVec2(0.0f, -pageButtonsHeight))) {
			hooks.drawSettingsNav();
			// Escape with nothing being typed goes back to the features
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
				ImGui::GetCurrentContext()->ActiveIdPreviousFrame == 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				Menu::GetSingleton()->ConsumeEscape();
				LeaveSettings(menuList, selectedMenu);
			}
		}
		ImGui::EndChild();
	} else {
		auto& filter = globals::menu->GetSettings().FeatureListFilter;
		if (ImGui::BeginChild("##FeatureListScroll", ImVec2(0.0f, -pageButtonsHeight))) {
			// The first category icon reaches above its text line, which the child's top edge clipped
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ThemeManager::Constants::LEFT_PANE_EDGE_INSET * Util::GetUIScale());
			for (size_t i = 0; i < menuList.size(); i++) {
				if (!builtInAt(i))
					std::visit(ListMenuVisitor{ i, selectedMenu, categoryExpansionStates, hooks }, menuList[i]);
			}
			if (g_filterCounts.shown == 0 && filter != 0) {
				BrowserUI::EmptyState(T("menu.features.no_matches", "No features match"),
					T("menu.features.no_matches_hint", "Turn off a filter to list more features."));
				const char* clearLabel = T("menu.features.clear_filters", "Clear filters");
				const float clearWidth = ImGui::CalcTextSize(clearLabel).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - clearWidth) * 0.5f));
				if (BrowserUI::Link("##ClearFilters", clearLabel)) {
					filter = 0;
				}
			}
		}
		ImGui::EndChild();
	}

	if (pageButtonsHere)
		StatusBarRenderer::RenderPageButtons();
}

void FeatureListRenderer::RequestPage(std::string_view pageId)
{
	g_requestedPage = std::string(pageId);
}

bool FeatureListRenderer::IsPageOpen(std::string_view pageId)
{
	return g_openPageId == pageId;
}

void FeatureListRenderer::RenderSortPopup(const char* popupId)
{
	if (!ImGui::BeginPopup(popupId))
		return;
	const FilterCounts& counts = g_filterCounts;
	auto& menuSettings = globals::menu->GetSettings();

	ImGui::SeparatorText(T("menu.features.sort_section", "Sort"));
	using enum Menu::FeatureSortMode;
	auto& sort = menuSettings.FeatureListSort;
	if (ImGui::MenuItem(T("menu.features.sort_by_category", "By Category"), nullptr, sort == Category))
		sort = Category;
	if (ImGui::MenuItem(T("menu.features.sort_alphabetically", "Alphabetical"), nullptr, sort == Alphabetical))
		sort = Alphabetical;

	ImGui::SeparatorText(T("menu.features.filter_section", "Filter"));
	auto& filter = menuSettings.FeatureListFilter;
	struct Item
	{
		std::uint32_t bit;
		int count;
		const char* label;
		const char* tooltip;
	};
	const Item items[] = {
		{ FeatureListFilter::kFavorites, counts.favorites, T("menu.features.filter_label_favorites", "Pinned"),
			T("menu.features.filter_favorites", "Pinned features") },
		{ FeatureListFilter::kModified, counts.modified, T("menu.features.filter_label_modified", "Modified"),
			T("menu.features.filter_modified", "Modified: settings differ from defaults and your Baseline presets") },
		{ FeatureListFilter::kScene, counts.scene, T("menu.features.filter_label_scene", "Scene"),
			T("menu.features.filter_scene", "Scene: has Scene Manager settings") },
		{ FeatureListFilter::kPreRelease, counts.preRelease, T("menu.features.filter_label_pre_release", "Pre-release"),
			T("menu.features.filter_pre_release", "Pre-release: alpha and beta features") },
		{ FeatureListFilter::kUnloaded, counts.unloaded, T("menu.features.filter_label_unloaded", "Unloaded"),
			T("menu.features.filter_unloaded", "Unloaded: missing, failed, or disabled at boot. Only these are listed while it is on") },
	};
	// Filters combine, so toggling one leaves the popup open
	ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
	for (const auto& item : items) {
		const bool active = (filter & item.bit) != 0;
		const std::string countText = std::to_string(item.count);
		// A filter with nothing to show stays greyed unless it is on, so it can still be turned off
		if (ImGui::MenuItem(item.label, countText.c_str(), active, item.count > 0 || active))
			filter ^= item.bit;
		Util::AddTooltip(item.tooltip, Util::kTooltipWhenDisabled);
	}
	ImGui::PopItemFlag();

	ImGui::Separator();
	if (ImGui::MenuItem(T("menu.features.clear_filter_chips", "Clear filters"), nullptr, false, (filter & FeatureListFilter::kKnown) != 0))
		filter = 0;
	ImGui::EndPopup();
}

void FeatureListRenderer::RenderAutoHidePane(
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu,
	std::string& pendingFeatureSelection,
	std::map<std::string, bool>& categoryExpansionStates,
	Hooks& hooks)
{
	using Constants = ThemeManager::Constants;
	const float scale = Util::GetUIScale();
	const ImVec2 areaMin = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float drawerWidth = std::max(avail.x * Constants::AUTOHIDE_PANEL_WIDTH_RATIO, Constants::AUTOHIDE_PANEL_MIN_WIDTH * scale);

	// The page takes the full width, so it never reflows while the list slides over its left edge. It keeps the
	// gutter a table cell would give it, which is also where the grip sits.
	const float gutter = ImGui::GetStyle().CellPadding.x;
	ImGui::Indent(gutter);
	RenderRightColumn(menuList, selectedMenu, pendingFeatureSelection, hooks);
	ImGui::Unindent(gutter);

	auto& drawer = g_drawer;
	const double now = ImGui::GetTime();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const bool inY = ImGui::IsMousePosValid(&mouse) && mouse.y >= areaMin.y && mouse.y <= areaMin.y + avail.y;
	const bool inEdge = inY && mouse.x >= areaMin.x - Constants::AUTOHIDE_EDGE_SLOP * scale && mouse.x < areaMin.x + Constants::AUTOHIDE_ACTIVATION_ZONE_WIDTH * scale;
	const bool inDrawer = inY && mouse.x >= areaMin.x && mouse.x < areaMin.x + drawerWidth;

	if (!drawer.open) {
		// Opens after the cursor has rested on the edge for a moment, so passing by does not flash the list
		if (!inEdge)
			drawer.hoverSince = -1.0;
		else if (drawer.hoverSince < 0.0)
			drawer.hoverSince = now;
		else if (now - drawer.hoverSince >= Constants::AUTOHIDE_OPEN_DELAY)
			drawer.open = true;
	} else if (inDrawer || drawer.keepOpen) {
		drawer.leaveSince = -1.0;
	} else if (drawer.leaveSince < 0.0) {
		drawer.leaveSince = now;
	} else if (now - drawer.leaveSince >= Constants::AUTOHIDE_CLOSE_DELAY) {
		drawer.open = false;
		drawer.leaveSince = drawer.hoverSince = -1.0;
	}

	// Exponential smoothing: frame-rate independent, fast at first and settling softly
	const float target = drawer.open ? 1.0f : 0.0f;
	drawer.progress = Util::Anim::Smooth(drawer.progress, target, Constants::AUTOHIDE_ANIM_SPEED);

	if (drawer.progress <= 0.0f) {
		// A thin grip on the edge hints that the list is there
		const float grip = Constants::AUTOHIDE_GRIP_WIDTH * scale;
		ImVec4 gripColor = Util::Colors::GetSecondary();
		gripColor.w *= Constants::AUTOHIDE_GRIP_ALPHA;
		ImGui::GetWindowDrawList()->AddRectFilled(areaMin, ImVec2(areaMin.x + grip, areaMin.y + avail.y), ImGui::GetColorU32(gripColor));
		drawer.keepOpen = false;
		return;
	}

	ImGui::SetCursorScreenPos(ImVec2(areaMin.x - drawerWidth * (1.0f - drawer.progress), areaMin.y));
	ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
	background.w = 1.0f;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
	if (ImGui::BeginChild("##FeatureListDrawer", ImVec2(drawerWidth, avail.y), ImGuiChildFlags_Borders)) {
		RenderLeftColumn(menuList, selectedMenu, categoryExpansionStates, hooks);
		// An active field or an open popup holds the list open even with the cursor elsewhere
		drawer.keepOpen = ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void FeatureListRenderer::RenderBuiltInRow(const BuiltInMenu& menu, size_t listId, size_t& selectedMenu, float width)
{
	ImGui::PushID(menu.id.data(), menu.id.data() + menu.id.size());
	if (menu.id == MenuPageIds::kCSEditor) {
		// Launcher: opens the editor window without leaving the current page
		const bool canOpen = EditorWindow::CanBeOpen();
		if (DrawNavRow(menu, false, !canOpen, width)) {
			// Without a save the click says why, with the same warning the other ways of opening the editor give
			if (canOpen)
				CSEditor::OpenEditorWindow();
			else
				EditorWindow::GetSingleton()->WarnOpeningBlocked();
		}
		Util::AddTooltip(canOpen ? T("menu.features.cs_editor_tooltip", "Opens the CS Editor window") :
								   T("menu.features.cs_editor_needs_save", "Load a save to open the CS Editor"),
			Util::kTooltipWhenDisabled);
	} else if (DrawNavRow(menu, selectedMenu == listId, false, width)) {
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
		// A newly opened page fades in; the key is the page itself, so a rebuilt list does not restart it
		const ImGuiID pageId = std::visit(
			[](const auto& entry) -> ImGuiID {
				using Entry = std::decay_t<decltype(entry)>;
				if constexpr (std::is_same_v<Entry, FeatureListRenderer::BuiltInMenu>)
					return ImGui::GetID(entry.id.data(), entry.id.data() + entry.id.size());
				else if constexpr (std::is_same_v<Entry, Feature*>)
					return ImGui::GetID(entry);
				else
					return 0;
			},
			menuList[selectedMenu]);
		Util::Anim::PageFade fade(pageId);
		std::visit(DrawMenuVisitor{ pendingFeatureSelection, hooks }, menuList[selectedMenu]);
	} else if (g_detachedFeature) {
		Util::Anim::PageFade fade(ImGui::GetID(g_detachedFeature));
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
	bool isExpanded = categoryExpansionStates[header.name];
	{
		MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Heading);
		const int count = Menu::categoryCounts[header.name];
		const auto categoryLabel = TranslateCategory(header.name);
		Util::DrawCategoryHeader(header.name.c_str(), categoryLabel.c_str(), isExpanded, count);
	}

	// The list adds or drops the category's features on its next rebuild
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
	const bool isDisabled = globals::state->IsFeatureDisabled(featureName);
	const bool isLoaded = feat->loaded;
	const bool hasFailedMessage = !feat->failedLoadedMessage.empty();
	const auto& palette = globals::menu->GetSettings().Theme.StatusPalette;

	ImVec4 textColor;
	if (isDisabled)
		textColor = palette.Disable;
	else if (isLoaded)
		textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	else if (hasFailedMessage)
		textColor = feat->version.empty() ? palette.Disable : palette.Error;
	else  // Installed but not loaded: pending a restart (green); otherwise simply missing (grey)
		textColor = feat->installed ? palette.RestartNeeded : palette.Disable;

	// The ID comes from the short name, so it survives a language change.
	// Alphabetical order has no category headers, so each row carries its category icon in a fixed column left of the name.
	const bool showCategoryIcon = globals::menu->GetSettings().FeatureListSort == Menu::FeatureSortMode::Alphabetical;
	const float iconColumn = ImGui::GetTextLineHeight() * ThemeManager::Constants::FEATURE_ROW_CATEGORY_ICON_SCALE;
	const float spaceWidth = ImGui::CalcTextSize(" ").x;
	const std::string iconPad = showCategoryIcon ? std::string(static_cast<size_t>(std::ceil((iconColumn + spaceWidth) / spaceWidth)), ' ') : std::string();
	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	if (Util::Selectable(std::format(" {}{} ###Row", iconPad, feat->GetDisplayName()).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns))
		selectedMenuRef = listId;
	ImGui::PopStyleColor();
	if (showCategoryIcon) {
		if (auto* texture = Util::GetCategoryIcon(std::string(feat->GetCategory()).c_str())) {
			const ImVec2 itemMin = ImGui::GetItemRectMin();
			const float pad = (ImGui::GetItemRectSize().y - iconColumn) * 0.5f;
			ImGui::GetWindowDrawList()->AddImage(texture, ImVec2(itemMin.x + spaceWidth, itemMin.y + pad),
				ImVec2(itemMin.x + spaceWidth + iconColumn, itemMin.y + pad + iconColumn), ImVec2(0, 0), ImVec2(1, 1),
				IM_COL32(255, 255, 255, static_cast<int>(255.0f * ThemeManager::Constants::FEATURE_ROW_CATEGORY_ICON_ALPHA)));
		}
	}
	if (g_scrollToSelection && selectedMenuRef == listId && !ImGui::IsItemVisible())
		ImGui::SetScrollHereY();
	const ImVec2 rowMin = ImGui::GetItemRectMin();
	const ImVec2 rowMax = ImGui::GetItemRectMax();
	RenderRowContextMenu(feat);

	// Favorites are pinned at the top of the list, marked by a pin attached to the name (its label ends in a space)
	if (isLoaded && globals::state->IsFeatureFavorite(featureName)) {
		ImGui::SameLine(0.0f, 0.0f);
		const Icons::GlyphRef pin = Icons::TI(ICON_TI_PIN);
		const float lineHeight = ImGui::GetTextLineHeight();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		Icons::FontGuard pinFont(pin, ThemeManager::Constants::FEATURE_ROW_PIN_SCALE);
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
		Util::HoverTip(applying ?
						   T("menu.features.scene_indicator_overriding",
							   "Scene Manager is overriding settings here. Blue settings show its values.") :
						   T("menu.features.scene_indicator_configured",
							   "Has Scene Manager settings for other times, weathers or locations."));

		if (sceneManager->HasSketches(featureName)) {
			ImGui::SameLine();
			Util::DrawInlineIndicatorDot(ImGui::GetColorU32(Util::Colors::GetSuccess()), true);
			Util::HoverTip(T("menu.features.scene_indicator_sketched",
				"Has settings sketched over the Scene Manager's values. They revert once you leave this feature."));
		}
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
		RestoreFeatureDefaults(hooks, feat);
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
	// Labels are never selectable
}

void FeatureListRenderer::DrawMenuVisitor::operator()(const CategoryHeader&)
{
	BrowserUI::EmptyState(T("menu.features.select_feature_left", "Please select a feature from the left."));
}

void FeatureListRenderer::DrawMenuVisitor::operator()(Feature* feat)
{
	const auto featureName = feat->GetShortName();
	const bool isDisabled = globals::state->IsFeatureDisabled(featureName);
	const bool isLoaded = feat->loaded;
	const bool hasFailedMessage = !feat->failedLoadedMessage.empty();

	if (hooks.onFeatureShown)
		hooks.onFeatureShown(feat);

	if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, true)) {
		RenderFeatureHeader(feat, isDisabled, isLoaded);
		RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage);
	}
	ImGui::EndChild();
	// Outside the child window so the warning can appear as a top-level popup
	DrawConstraintWarningDialog(pendingFeatureSelection);
}

void FeatureListRenderer::DrawMenuVisitor::RenderFeatureHeader(Feature* feat, bool isDisabled, bool isLoaded)
{
	auto& themeSettings = globals::menu->GetSettings().Theme;
	const auto featureName = feat->GetShortName();
	const ImGuiStyle& style = ImGui::GetStyle();

	const float buttonSpacing = ThemeManager::Constants::BUTTON_SPACING;
	const float iconSize = ImGui::GetFrameHeight();
	const float bootToggleWidth = iconSize * ThemeManager::Constants::FEATURE_TOGGLE_WIDTH_RATIO;
	const float bootToggleHeight = iconSize * ThemeManager::Constants::FEATURE_TOGGLE_HEIGHT_RATIO;

	const bool canExport = !isDisabled && isLoaded && FeatureOverwritesPanel::HasExportableSettings(feat);
	// Restore clears the user's edits in the user config, which a feature with its own store does not use.
	const bool canRestore = !isDisabled && isLoaded && feat->UsesMainSettings();
	// Offered while the user's edits hide values one of their Baseline packs sets.
	const bool canUseBaselines = canRestore && globals::state->HasEditsUnderBaselines(feat);

	// The boot toggle is labelled so it does not read as an on/off switch for the effect itself
	const char* bootLabel = T("menu.features.load_at_boot", "Load at Boot");
	const float bootLabelWidth = [bootLabel] {
		MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
		return ImGui::CalcTextSize(bootLabel).x;
	}();

	// Right side: pin, overflow menu, then the labelled boot toggle
	float totalButtonWidth = bootLabelWidth + style.ItemInnerSpacing.x + bootToggleWidth;
	if (isLoaded)
		totalButtonWidth += (iconSize + buttonSpacing) * 2.0f;

	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const ImVec2 titleStartPos = ImGui::GetCursorScreenPos();

	const auto description = feat->GetFeatureSummary().first;

	// Title, version and description on the left; the title-only height aligns the buttons
	const auto stage = feat->GetReleaseStage();
	const std::string stageTag = Feature::GetReleaseStageTag(stage);  // empty for Release; color unused when tag is empty
	float trailingX = titleStartPos.x;
	const float titleOnlyHeight = DrawFeatureHeader(feat->GetDisplayName(), isLoaded ? feat->version : "", description, stageTag, Feature::GetReleaseStageColor(stage), &trailingX);

	const ImVec2 cursorPosAfterHeader = ImGui::GetCursorScreenPos();

	// Quiet marker while the feature differs from its defaults; clicking restores them like the actions menu does
	if (canRestore && globals::state->HasUserEdits(feat)) {
		ImGui::SetCursorScreenPos(ImVec2(trailingX, titleStartPos.y + titleOnlyHeight - iconSize));
		const ImVec4 secondary = Util::Colors::GetSecondary();
		bool restore = false;
		{
			MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
			ImGui::PushStyleColor(ImGuiCol_Text, secondary);
			restore = BrowserUI::Chip("##FeatureModified", T("menu.features.modified_chip", "Modified"), Icons::TI(ICON_TI_PENCIL), &secondary);
			ImGui::PopStyleColor();
		}
		Util::HoverTip(T("menu.features.modified_chip_tooltip", "Differs from the defaults and your Baseline presets. Click to restore defaults."));
		if (restore)
			RestoreDefaults(feat);
	}

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
		SettingReveal::BeginFeatureBody(feat);
		feat->DrawSettings();
		SettingReveal::EndFeatureBody();
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

	constexpr float cursorEpsilon = 0.1f;
	const bool cursorMoved = std::abs(cursorPosAfter.x - cursorPosBefore.x) > cursorEpsilon ||
	                         std::abs(cursorPosAfter.y - cursorPosBefore.y) > cursorEpsilon;
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
					if (Util::Selectable(downloadText.c_str()))
						ShellExecuteA(nullptr, "open", featureModLink.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					Util::HoverTip(T("menu.features.download_tooltip", "Download the feature from the mod page."));
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

void FeatureListRenderer::DrawMenuVisitor::RestoreDefaults(Feature* feat) const
{
	RestoreFeatureDefaults(hooks, feat);
}

void FeatureListRenderer::DrawMenuVisitor::RenderActionsMenu(Feature* feat, bool canExport, bool canRestore, bool canUseBaselines)
{
	const float size = ImGui::GetFrameHeight();
	{
		auto style = Util::TransparentIconButtonStyle();
		if (Icons::Button("##FeatureActions", Icons::TI(ICON_TI_DOTS), ImVec2(size, size)))
			ImGui::OpenPopup("##FeatureActionsMenu");
	}
	Util::HoverTip(T("menu.features.actions_tooltip", "More actions"));

	if (!ImGui::BeginPopup("##FeatureActionsMenu"))
		return;
	{
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
			RestoreDefaults(feat);
		}
		Util::AddTooltip(hasEdits   ? T("menu.features.restore_defaults_tooltip", "Restore defaults: clears your edits to this feature, back to what defaults and your Baseline presets set") :
						 canRestore ? T("menu.features.restore_nothing", "Nothing to restore: this feature already matches its defaults and your Baseline presets") :
									  T("menu.features.restore_unavailable", "Only loaded features that keep their settings in the user config can be restored"),
			Util::kTooltipWhenDisabled);
	}
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
	if (Util::Button(label)) {
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

	Util::HoverTip(isFavorite ? T("menu.features.remove_from_favorites_tooltip", "Remove from favorites: moves this feature back into the main list") : T("menu.features.add_to_favorites_tooltip", "Add to favorites: lists this feature at the top of the feature list"));
}

void FeatureListRenderer::DrawConstraintWarningDialog(std::string& pendingFeatureSelection)
{
	if (!g_reactiveWarningShow)
		return;

	constexpr const char* popupId = "###SettingChangeWarning";
	const std::string popupTitle = fmt::format("{}{}", T("menu.features.setting_change_warning_title", "Setting Change Warning"), popupId);

	// Idempotent while the popup is open, so it is safe to call every frame while the flag is set
	ImGui::OpenPopup(popupId);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

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

				// Impacted feature: click to navigate to it
				ImGui::TableSetColumnIndex(0);
				auto* target = Feature::FindFeatureByShortName(settingId.featureShortName);
				const std::string targetDisplayName = target ? target->GetDisplayName() : settingId.featureShortName;
				if (Util::Selectable(fmt::format("{}##imp{}", targetDisplayName, rowIndex).c_str())) {
					navigationTarget = settingId.featureShortName;
					break;
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(T("menu.features.click_to_navigate", "Click to navigate to %s"), targetDisplayName.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", settingId.settingPath.c_str());

				// Constraining features: click the first to navigate to it
				ImGui::TableSetColumnIndex(2);
				if (!result.sources.empty()) {
					if (Util::Selectable(fmt::format("{}##src{}", result.sources[0].featureName, rowIndex).c_str())) {
						navigationTarget = result.sources[0].featureShortName;
						break;
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(T("menu.features.click_to_navigate", "Click to navigate to %s"), result.sources[0].featureName.c_str());
						if (result.sources.size() > 1) {
							ImGui::Separator();
							for (size_t i = 1; i < result.sources.size(); ++i)
								ImGui::Text(T("menu.features.also_feature", "Also: %s"), result.sources[i].featureName.c_str());
						}
						ImGui::Separator();
						ImGui::Text("%s", result.sources[0].reason.c_str());
					}
				}

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

		ImGui::Checkbox(T("menu.features.dont_show_warning", "Don't show this warning again"), &g_dontShowAgainCheckbox);

		ImGui::Spacing();

		constexpr float buttonWidth = ThemeManager::Constants::POPUP_BUTTON_WIDTH;
		const float offset = (ImGui::GetWindowWidth() - buttonWidth) * 0.5f;
		if (offset > 0)
			ImGui::SetCursorPosX(offset);

		if (Util::Button(T("menu.features.ok_button", "OK"), ImVec2(buttonWidth, 0))) {
			if (g_dontShowAgainCheckbox)
				globals::menu->GetSettings().SkipConstraintWarning = true;
			g_reactiveWarningShow = false;
			g_reactiveWarningConstraints.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	} else {
		// Closed from outside (e.g. a click away): drop the pending warning
		g_reactiveWarningShow = false;
		g_reactiveWarningConstraints.clear();
	}
}
