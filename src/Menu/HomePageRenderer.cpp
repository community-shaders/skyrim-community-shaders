#include "HomePageRenderer.h"
#include "PCH.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <functional>
#include <ranges>
#include <string>
#include <vector>

#include "CSEditor/Browser/BrowserWidgets.h"
#include "CSEditor/EditorWindow.h"
#include "CSEditor/SceneManager/SceneLayerHeader.h"
#include "CSEditor/SceneManager/SceneSettingsUI.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "Features/CSEditor.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/AdvancedSettingsRenderer.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/IconLoader.h"
#include "Menu/IssuesPageRenderer.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/MenuIcons.h"
#include "Menu/MenuLinks.h"
#include "Menu/MenuShortcuts.h"
#include "Menu/PresetsPageRenderer.h"
#include "Menu/ShaderCacheStatusWidget.h"
#include "Menu/ThemeManager.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/UIAnim.h"
#include "Utils/UISurface.h"

// Static member definitions
bool HomePageRenderer::isFirstTimeSetupShown = false;
int HomePageRenderer::lastShownFrame = -1000;
bool HomePageRenderer::helpPopoverRequested = false;
bool HomePageRenderer::attentionScrollPending = false;
double HomePageRenderer::attentionFocusUntil = 0.0;

namespace
{
	using Constants = ThemeManager::Constants;

	constexpr const char* kHelpPopupId = "##HomeHelpPopover";

	/// The cards drawn this frame, for the staggered fade-in; rebuilt at the top of the page.
	Util::Anim::EnterGroup pageCards(0);

	/** @brief One dashboard card: a bordered, auto-height child with an icon and a section-label title. */
	class Card
	{
	public:
		/**
		 * @param id Stable child id.
		 * @param icon Tabler glyph before the title.
		 * @param title Translated title; null leaves the icon on an open line for the caller's content.
		 * @param border Border and icon colour; null keeps the theme border and the accent icon.
		 * @param trailing Drawn on the title row after the title (e.g. a right-aligned icon button); empty for none.
		 */
		Card(const char* id, Icons::GlyphRef icon, const char* title, const ImVec4* border = nullptr,
			const std::function<void()>& trailing = {})
		{
			// Cards fade in one after another each time the page is opened
			const float enter = pageCards.Next(ImGui::GetID(id));
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * enter);
			const auto& cardSurface = Util::Surface::Get(Util::Surface::Role::HomeCard);
			const float pad = cardSurface.padding * Util::GetUIScale();
			// Without a drawn surface the vertical padding only adds dead space above and below the content
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, cardSurface.enabled ? pad : 0.0f));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
			ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
				ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
			// Surface backing, drawn first so the content sits on it
			Util::Surface::Options surface;
			surface.rounding = ImGui::GetStyle().ChildRounding;
			surface.clip = Util::Surface::ParentClipRect();
			if (border)
				surface.borderColor = ImGui::GetColorU32(*border);
			Util::Surface::Draw(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
				ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
				Util::Surface::Role::HomeCard, surface);

			ImGui::AlignTextToFramePadding();
			{
				Icons::FontGuard font(icon);
				ImGui::PushStyleColor(ImGuiCol_Text, border ? *border : Util::Colors::GetAccent());
				ImGui::TextUnformatted(icon.utf8);
				ImGui::PopStyleColor();
			}
			if (title) {
				ImGui::SameLine();
				BrowserUI::SectionLabel(title);
				if (trailing)
					trailing();
				ImGui::Spacing();
			}
		}

		~Card()
		{
			ImGui::EndChild();
			ImGui::PopStyleVar();
		}

		Card(const Card&) = delete;
		Card& operator=(const Card&) = delete;
	};

	/** @brief Draws text word-wrapped to the available width, each line centred on it, in the secondary colour. */
	void CenteredWrappedText(const char* text)
	{
		const float avail = ImGui::GetContentRegionAvail().x;
		const float startX = ImGui::GetCursorPosX();
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetSecondary());
		std::string line;
		const auto flush = [&] {
			if (line.empty())
				return;
			ImGui::SetCursorPosX(startX + std::max(0.0f, (avail - ImGui::CalcTextSize(line.c_str()).x) * 0.5f));
			ImGui::TextUnformatted(line.c_str());
			line.clear();
		};
		std::string_view rest(text);
		while (!rest.empty()) {
			const size_t space = rest.find(' ');
			const std::string word(rest.substr(0, space));
			const std::string candidate = line.empty() ? word : line + ' ' + word;
			if (!line.empty() && ImGui::CalcTextSize(candidate.c_str()).x > avail)
				flush();
			line = line.empty() ? word : line + ' ' + word;
			rest = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
		}
		flush();
		ImGui::PopStyleColor();
	}

	/** @brief Glyph box edge for the cards' icon buttons. */
	float IconBoxSize()
	{
		return ImGui::GetFontSize() * Constants::HOME_CARD_ICON_BOX_SCALE;
	}

	/** @brief Gap above a card's action row. */
	void BeginActions()
	{
		ImGui::Dummy(ImVec2(0.0f, Constants::HOME_CARD_ACTION_GAP * Util::GetUIScale()));
	}

	/** @brief A card action: a labelled icon button, kept on the line of the previous one when it fits. */
	bool Action(const char* id, Icons::GlyphRef icon, const char* label, bool first)
	{
		if (!first)
			BrowserUI::SameLineIfFits(Icons::MeasureLabeledButton(icon, label));
		return Icons::LabeledButton(id, icon, label);
	}

	/** @brief A glyph followed by a line of text, both in one colour. */
	void IconLine(const ImVec4& color, Icons::GlyphRef icon, const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		Icons::Text(icon);
		ImGui::SameLine();
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
	}

	/** @brief Comma-separated list of names, for one ellipsized line. */
	std::string JoinNames(const std::vector<std::string>& names)
	{
		return names | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>();
	}

	/** @brief Display name of a feature short name, or the short name when no such feature exists. */
	std::string FeatureDisplayName(const std::string& shortName)
	{
		auto* feature = Feature::FindFeatureByShortName(shortName);
		return feature ? feature->GetDisplayName() : shortName;
	}

	/** @brief Features whose state changes at the next start: installed but not loaded, or loaded but now disabled at boot. */
	std::vector<std::string> GetRestartPendingFeatures()
	{
		std::vector<std::string> names;
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->IsInMenu() || feature->IsHiddenUnreleased() || !feature->failedLoadedMessage.empty())
				continue;
			const bool disabledAtBoot = globals::state->IsFeatureDisabled(feature->GetShortName());
			const bool pendingLoad = !feature->loaded && feature->installed && !disabledAtBoot &&
			                         !FeatureIssues::IsObsoleteFeature(feature->GetShortName());
			const bool pendingUnload = feature->loaded && disabledAtBoot;
			if (pendingLoad || pendingUnload)
				names.push_back(feature->GetDisplayName());
		}
		std::ranges::sort(names);
		return names;
	}

	/** @brief The active preset pack and the enabled Baselines, or an invitation to browse presets. */
	void RenderPresetCard()
	{
		PresetsPageRenderer::EnsureDiscovered();
		auto& catalog = UnifiedPresetCatalog::GetSingleton();
		const std::string& activeId = catalog.GetActivePackId();

		const ImVec4 accent = Util::Colors::GetAccent();
		Card card("##HomePreset", Icons::TI(MenuIcons::kPresets), T("menu.home.preset_title", "Preset"), &accent);

		if (activeId.empty()) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(T("menu.home.preset_none", "No preset active."));
			// Browse sits on this line, flush right
			const char* browseLabel = T("menu.home.browse_presets", "Browse Presets");
			const Icons::GlyphRef browseIcon = Icons::TI(MenuIcons::kPresets);
			ImGui::SameLine();
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - Icons::MeasureLabeledButton(browseIcon, browseLabel)));
			if (Icons::LabeledButton("##HomeBrowsePresets", browseIcon, browseLabel))
				globals::menu->SelectFeatureMenu(std::string(MenuPageIds::kPresets));
			Util::Text::WrappedDisabled("%s", T("menu.home.preset_none_hint", "Browse presets to apply a ready-made look across many features at once."));
		} else {
			const auto* pack = catalog.FindPack(activeId);
			// Artwork is only shown once the Presets page has loaded it: loading it here would decode every screenshot
			if (pack && pack->artworkLoaded && pack->logoSRV) {
				const float thumb = Constants::HOME_PRESET_THUMB_SIZE * Util::GetUIScale();
				ImGui::Dummy(ImVec2(thumb, thumb));
				ImGui::GetWindowDrawList()->AddImageRounded(reinterpret_cast<ImTextureID>(pack->logoSRV.get()),
					ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImGui::GetColorU32(IM_COL32_WHITE),
					ImGui::GetStyle().FrameRounding);
				ImGui::SameLine();
			}
			ImGui::BeginGroup();
			{
				MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
				BrowserUI::EllipsizedText(pack ? pack->name.c_str() : activeId.c_str(), 0.0f, ImGui::GetStyleColorVec4(ImGuiCol_Text));
			}
			if (pack)
				PresetsPageRenderer::DrawBackendBadges(pack->IsE11(), pack->IsCS(), pack->IsBaseline(), true);
			ImGui::EndGroup();
		}

		// Baselines layer under the active pack and can be enabled without one
		const auto& baselineIds = catalog.GetBaselinePackIds();
		ImGui::Spacing();
		if (baselineIds.empty()) {
			Util::Text::Disabled("%s", T("menu.home.baselines_none", "No Baselines enabled"));
		} else {
			std::vector<std::string> names;
			names.reserve(baselineIds.size());
			for (const auto& id : baselineIds) {
				const auto* pack = catalog.FindPack(id);
				names.push_back(pack ? pack->name : id);
			}
			ImGui::TextUnformatted(I18n::GetSingleton()->Format("menu.home.baselines_count",
														   { { "count", std::to_string(baselineIds.size()) } }, "Baselines enabled: {count}")
					.c_str());
			BrowserUI::MetaText(JoinNames(names).c_str());
		}

		if (!activeId.empty()) {
			BeginActions();
			if (Action("##HomeBrowsePresets", Icons::TI(MenuIcons::kPresets), T("menu.home.browse_presets", "Browse Presets"), true))
				globals::menu->SelectFeatureMenu(std::string(MenuPageIds::kPresets));
			if (Action("##HomeDisablePreset", Icons::TI(ICON_TI_CIRCLE_X), T("menu.presets.disable", "Disable Preset"), false))
				catalog.DisableActivePack();
			Util::AddTooltip(T("menu.presets.disable_tooltip",
				"Turns this preset off. Scene settings and form edits fall back to your own, and an Effects 11 preset switches to Vanilla."));
		}
	}

	/** @brief What the Scene Manager applies where the player is, with links into the CS Editor. */
	void RenderSceneCard()
	{
		const ImVec4 accent = Util::Colors::GetAccent();
		Card card("##HomeScene", Icons::TI(MenuIcons::kCSEditor), T("menu.home.scene_title", "CS Editor"), &accent);

		const bool inGame = EditorWindow::CanBeOpen();
		if (inGame)
			SceneLayerHeader::DrawSummary();
		else
			Util::Text::WrappedDisabled("%s",
				T("feature.scene_manager.not_in_game", "Load a save to see what applies here and to open the CS Editor."));

		BeginActions();
		Util::DisableGuard disable(!inGame);
		if (Action("##HomeOpenEditor", Icons::TI(MenuIcons::kCSEditor), T("menu.home.open_cs_editor", "Open CS Editor"), true))
			CSEditor::OpenEditorWindow();
		Util::AddTooltip(T("menu.presets.open_cs_editor_tooltip", "Open the CS Editor for weather, lighting, and scene editing."),
			Util::kTooltipWhenDisabled);
		if (Action("##HomeOpenLocations", Icons::TI(MenuIcons::kLocations), T("feature.scene_manager.open_locations", "Locations"), false))
			SceneSettingsUI::OpenLocationsPage();
		Util::AddTooltip(T("feature.scene_manager.open_locations_tooltip", "Open the CS Editor's Locations page, to add places to override."),
			Util::kTooltipWhenDisabled);
	}

	/**
	 * @brief The header's clear-cache icon (its PNG, or the glyph if the texture is missing), right-aligned on the
	 * current line: the Shaders card's status row. Opens the clear-cache confirmation.
	 */
	void DrawClearCacheButton()
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float box = IconBoxSize();
		// Square icon buttons pad by the vertical padding on both axes (GlyphIconButton)
		const float buttonSize = box + style.FramePadding.y * 2.0f;
		const float clearX = ImGui::GetWindowWidth() - style.WindowPadding.x - buttonSize;
		ImGui::SameLine();
		// The buttons are taller than the status text; centre them on its ink rather than hanging below it
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Util::GetTextInkCenterY() - buttonSize * 0.5f);
		// Developer mode: the test scene button sits just left of the clear-cache icon
		if (globals::state->IsDeveloperMode()) {
			ImGui::SetCursorPosX(clearX - style.ItemSpacing.x - buttonSize);
			if (MenuHeaderRenderer::GlyphIconButton("##HomeTestConditions", Icons::TI(MenuIcons::kTestScene), box, AdvancedSettingsRenderer::GetTestConditionsTooltip()))
				AdvancedSettingsRenderer::RunTestConditions();
			ImGui::SameLine();
		}
		ImGui::SetCursorPosX(clearX);
		const char* tooltip = T("menu.home.clear_cache_tooltip", "Clear shader cache...");
		bool clicked;
		if (auto* texture = globals::menu->GetUIIcons().clearCache.texture) {
			// Drawn like the header's nav buttons: eased theme highlight, then the icon
			const float pad = style.FramePadding.y;
			clicked = ImGui::InvisibleButton("##HomeClearCache", ImVec2(buttonSize, buttonSize), ImGuiButtonFlags_EnableNav);
			const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			Util::DrawAnimatedButtonHighlight(rect, ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::IsItemActive(), false);
			ImGui::GetWindowDrawList()->AddImage(texture, ImVec2(rect.Min.x + pad, rect.Min.y + pad), ImVec2(rect.Max.x - pad, rect.Max.y - pad),
				ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(IM_COL32_WHITE));
			Util::AddTooltip(tooltip);
		} else {
			clicked = MenuHeaderRenderer::GlyphIconButton("##HomeClearCache", Icons::TI(MenuIcons::kClearCache), box, tooltip);
		}
		if (clicked)
			Util::RequestClearShaderCacheConfirmation();
	}

	/** @brief The last shader build: compile breakdown, duration and failures. */
	void RenderShadersCard()
	{
		Card card("##HomeShaders", Icons::TI(MenuIcons::kShaders), T("menu.home.shaders_title", "Shaders"));

		auto* shaderCache = globals::shaderCache;
		auto* i18n = I18n::GetSingleton();
		const uint64_t failed = shaderCache->GetFailedTasks();
		if (shaderCache->IsCompiling()) {
			ImGui::TextColored(Util::Colors::GetInfo(), "%s",
				i18n->Format("menu.status.compiling",
						{ { "done", std::to_string(shaderCache->GetCompletedTasks()) }, { "total", std::to_string(shaderCache->GetTotalTasks()) } },
						"Compiling shaders {done}/{total}")
					.c_str());
		} else if (shaderCache->GetTotalTasks() == 0) {
			Util::Text::Disabled("%s", T("menu.home.shaders_none", "No shaders compiled yet"));
		} else {
			ImGui::TextUnformatted(i18n->Format("menu.home.shaders_build_time",
										   { { "time", shaderCache->GetShaderStatsString(true, true) } }, "Last build: {time}")
					.c_str());
		}
		DrawClearCacheButton();
		if (failed > 0)
			ImGui::TextColored(Util::Colors::GetError(), "%s",
				i18n->Format("menu.status.failed_shaders", { { "count", std::to_string(failed) } }, "Failed shaders: {count}").c_str());
		else if (shaderCache->GetTotalTasks() > 0)
			Util::Text::Success("%s", T("menu.home.shaders_no_failures", "No failed shaders"));

		ShaderCacheStatusWidget::DrawBreakdownBar();

		if (failed > 0) {
			BeginActions();
			const char* errorsLabel = shaderCache->IsHideErrors() ? T("menu.home.show_errors", "Show Errors") :
			                                                        T("menu.home.hide_errors", "Hide Errors");
			if (Action("##HomeShaderErrors", Icons::TI(ICON_TI_EYE), errorsLabel, true))
				shaderCache->ToggleErrorMessages();
			Util::AddTooltip(T("menu.toggle_error_message_tooltip",
				"Hide or show the shader failure message. "
				"Your installation is broken and will likely see errors in game. "
				"Please double check you have updated all features and that your load order is correct. "
				"See CommunityShaders.log for details and check the Nexus Mods page or Discord server."));
		}
	}

	/**
	 * @brief Everything that needs a look: feature issues with their details and actions, the settings other features
	 * constrain, and features waiting on a restart. A compact all-clear when there is none.
	 */
	void RenderAttentionCard()
	{
		const size_t issueCount = IssuesPageRenderer::GetFeatureIssueCount();
		const size_t constraintCount = IssuesPageRenderer::GetConstraintCount();
		const auto restartPending = GetRestartPendingFeatures();
		const bool unsaved = globals::state->HasUnsavedChanges();
		const bool allClear = !unsaved && issueCount == 0 && constraintCount == 0 && restartPending.empty();

		const ImVec4 warning = Util::Colors::GetWarning();
		const ImVec4 error = Util::Colors::GetError();
		const ImVec4 unsavedColor = Util::Colors::GetUnsaved();
		const ImVec4 border = issueCount > 0 ? error : (restartPending.empty() && constraintCount == 0 ? unsavedColor : warning);
		Card card("##HomeAttention", Icons::TI(MenuIcons::kIssues), T("menu.home.attention_title", "Needs Attention"),
			allClear ? nullptr : &border);

		if (allClear) {
			IconLine(Util::Colors::GetSuccess(), Icons::TI(ICON_TI_CHECK), T("menu.home.all_good", "All good: no issues, constraints or pending restarts."));
			return;
		}

		auto* i18n = I18n::GetSingleton();

		// Spacing goes between the entries only, so the card ends at its last one
		bool firstEntry = true;
		const auto entryGap = [&firstEntry] {
			if (!std::exchange(firstEntry, false))
				ImGui::Spacing();
		};

		// Unsaved changes: the header and footer hold the save and restore actions
		if (unsaved) {
			entryGap();
			IconLine(unsavedColor, Icons::TI(MenuIcons::kSave), T("menu.home.unsaved_text", "You have unsaved changes."));
		}

		// Feature issues: the rejected, obsolete or mismatched feature files and failed Baselines, with their actions
		if (issueCount > 0) {
			entryGap();
			IconLine(error, Icons::TI(MenuIcons::kIssues),
				i18n->Format("menu.status.feature_issues", { { "count", std::to_string(issueCount) } }, "Feature issues: {count}").c_str());
			BrowserUI::HelpMarkerRight(FeatureIssues::GetCleanupGuide().c_str());
			FeatureIssues::DrawFeatureIssuesUI();
		}

		// Restart pending
		if (!restartPending.empty()) {
			entryGap();
			ImGui::TextColored(globals::menu->GetTheme().StatusPalette.RestartNeeded, "%s",
				i18n->Format("menu.home.restart_pending", { { "count", std::to_string(restartPending.size()) } }, "Restart needed for {count} features")
					.c_str());
			Util::AddTooltip(T("menu.home.restart_pending_tooltip",
				"These features were installed, or had Load at boot changed, since the game started. The change applies at the next start."));
			BrowserUI::MetaText(JoinNames(restartPending).c_str());
		}

		// Setting constraints
		if (constraintCount > 0) {
			entryGap();
			IssuesPageRenderer::RenderConstraintsSection();
		}

		// Brief outline after a click on the header or footer issues indicator
		if (HomePageRenderer::IsAttentionFocused()) {
			const float fade = Util::Anim::EaseOutCubic(static_cast<float>(HomePageRenderer::GetAttentionFocusRemaining() / Constants::ATTENTION_FOCUS_FADE_SECONDS));
			const ImVec4 outline = Util::Anim::ScaleAlpha(border, fade);
			ImGui::GetWindowDrawList()->AddRect(ImGui::GetWindowPos(),
				ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
				ImGui::GetColorU32(outline), ImGui::GetStyle().ChildRounding, 0, Constants::ATTENTION_FOCUS_THICKNESS * Util::GetUIScale());
		}
	}

	/** @brief Feature chips that open the feature's page. */
	void RenderFeatureChips(const char* idPrefix, const std::vector<std::string>& shortNames, Icons::GlyphRef icon)
	{
		ImGui::PushID(idPrefix);
		bool first = true;
		for (const auto& shortName : shortNames) {
			const std::string label = FeatureDisplayName(shortName);
			if (!first)
				BrowserUI::SameLineIfFits(BrowserUI::MeasureChip(label.c_str(), true));
			first = false;
			ImGui::PushID(shortName.c_str());
			if (BrowserUI::Chip("##chip", label.c_str(), icon))
				globals::menu->SelectFeatureMenu(shortName);
			ImGui::PopID();
		}
		ImGui::PopID();
	}

	/** @brief Pinned features, then the recently opened ones that are not pinned, one click away. */
	void RenderQuickAccessCard()
	{
		Card card("##HomeQuickAccess", Icons::TI(MenuIcons::kPin), T("menu.home.quick_access_title", "Quick Access"));

		std::vector<Feature*> pinnedFeatures;
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature->IsInMenu() && feature->loaded && globals::state->IsFeatureFavorite(feature->GetShortName()))
				pinnedFeatures.push_back(feature);
		}
		std::ranges::sort(pinnedFeatures, [](Feature* a, Feature* b) { return a->GetDisplayName() < b->GetDisplayName(); });
		std::vector<std::string> pinned;
		pinned.reserve(pinnedFeatures.size());
		for (auto* feature : pinnedFeatures)
			pinned.push_back(feature->GetShortName());

		std::vector<std::string> recent;
		for (const auto& shortName : globals::menu->GetSettings().RecentFeatures) {
			const auto* feature = Feature::FindFeatureByShortName(shortName);
			if (feature && feature->IsInMenu() && std::ranges::find(pinned, shortName) == pinned.end())
				recent.push_back(shortName);
		}

		if (pinned.empty() && recent.empty()) {
			Util::Text::WrappedDisabled("%s", T("menu.home.quick_access_hint", "Pinned and recently opened features show here."));
			return;
		}
		RenderFeatureChips("##Pinned", pinned, Icons::TI(MenuIcons::kPin));
		if (!recent.empty()) {
			ImGui::Spacing();
			Util::Text::Disabled("%s", T("menu.home.recent", "Recently opened"));
			RenderFeatureChips("##Recent", recent, Icons::TI(ICON_TI_CLOCK));
		}
	}

	/// Height of the pinned bottom strip as measured last frame, so the card area above leaves exactly that much.
	float g_stripHeight = 0.0f;

	/**
	 * @brief The Discord banner and the project link chips, pinned to the bottom of the Home page across its full
	 * width. The banner grows with the width up to a height cap, keeping its aspect ratio; the links sit in a row below.
	 */
	void RenderBottomStrip()
	{
		const float scale = Util::GetUIScale();
		const float startY = ImGui::GetCursorPosY();
		ImGui::Separator();

		const auto& discord = Util::IconLoader::GetIcons().discord;
		const float avail = ImGui::GetContentRegionAvail().x;
		if (discord.texture && discord.size.x > 0.0f && discord.size.y > 0.0f) {
			const float aspect = discord.size.x / discord.size.y;
			const float width = std::max(1.0f, std::min(avail, Constants::HOME_DISCORD_BANNER_MAX_HEIGHT * scale * aspect));
			const ImVec2 size = Util::IconLoader::SnapToPixel(ImVec2(width, width / aspect));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);

			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Constants::HOME_DISCORD_BANNER_HOVER_COLOR);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, Constants::HOME_DISCORD_BANNER_ACTIVE_COLOR);
			if (ImGui::ImageButton("##DiscordButton", Util::IconLoader::ViewForSize(discord.texture, size), size))
				MenuLinks::Open(MenuLinks::Link::Discord);
			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar(2);
		} else if (Util::Button(T("menu.home.join_discord", "Join our Discord"), ImVec2(avail, 0.0f))) {
			MenuLinks::Open(MenuLinks::Link::Discord);
		}
		Util::AddTooltip(T("menu.home.join_discord", "Join our Discord"));

		using MenuLinks::Link;
		constexpr std::array links = { Link::NexusMods, Link::GitHub, Link::Wiki };
		const Icons::GlyphRef linkIcon = Icons::TI(MenuIcons::kExternal);
		// The row is centred under the banner
		float rowWidth = ImGui::GetStyle().ItemSpacing.x * static_cast<float>(links.size() - 1);
		for (const auto link : links)
			rowWidth += BrowserUI::MeasureChip(MenuLinks::GetLabel(link), true);
		if (rowWidth < avail)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - rowWidth) * 0.5f);
		for (size_t i = 0; i < links.size(); ++i) {
			const char* label = MenuLinks::GetLabel(links[i]);
			if (i > 0)
				BrowserUI::SameLineIfFits(BrowserUI::MeasureChip(label, true));
			ImGui::PushID(static_cast<int>(links[i]));
			if (BrowserUI::Chip("##link", label, linkIcon))
				MenuLinks::Open(links[i]);
			ImGui::PopID();
			Util::AddTooltip(MenuLinks::GetUrl(links[i]));
		}
		g_stripHeight = ImGui::GetCursorPosY() - startY + ImGui::GetStyle().ItemSpacing.y;
	}
}

bool HomePageRenderer::IsShowing()
{
	return ImGui::GetFrameCount() - lastShownFrame <= 1;
}

void HomePageRenderer::RequestHelpPopover()
{
	helpPopoverRequested = true;
}

void HomePageRenderer::FocusNeedsAttention()
{
	attentionScrollPending = true;
	attentionFocusUntil = ImGui::GetTime() + ThemeManager::Constants::ATTENTION_FOCUS_SECONDS;
	globals::menu->SelectFeatureMenu(std::string(MenuPageIds::kHome));
}

bool HomePageRenderer::IsAttentionFocused()
{
	return GetAttentionFocusRemaining() > 0.0;
}

double HomePageRenderer::GetAttentionFocusRemaining()
{
	return attentionFocusUntil - ImGui::GetTime();
}

float HomePageRenderer::GetHelpButtonWidth()
{
	return ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
}

void HomePageRenderer::RenderHelpButton()
{
	if (std::exchange(helpPopoverRequested, false))
		ImGui::OpenPopup(kHelpPopupId);

	const auto tooltip = I18n::GetSingleton()->Format("menu.header.help_tooltip",
		{ { "shortcut", MenuShortcuts::GetHint(globals::menu->GetShortcuts(), MenuShortcuts::Ids::kHelp) } },
		"Shortcuts and help ({shortcut})");
	if (MenuHeaderRenderer::GlyphIconButton("##StatusHelp", Icons::TI(MenuIcons::kHelp), ImGui::GetFontSize(), tooltip.c_str()))
		ImGui::OpenPopup(kHelpPopupId);

	RenderHelpPopover();
}

void HomePageRenderer::RenderHelpPopover()
{
	if (!ImGui::BeginPopup(kHelpPopupId))
		return;

	// Escape closes the popover, not the menu
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		globals::menu->ConsumeEscape();
		ImGui::CloseCurrentPopup();
	}

	{
		MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
		ImGui::SeparatorText(T("menu.help.shortcuts", "Keyboard Shortcuts"));
	}
	if (ImGui::BeginTable("##HelpShortcuts", 2, ImGuiTableFlags_SizingFixedFit)) {
		for (const auto& row : MenuShortcuts::BuildHelpRows(globals::menu->GetShortcuts())) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(row.label);
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", row.chords.c_str());
		}
		ImGui::EndTable();
	}

	{
		MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
		ImGui::SeparatorText(T("menu.help.help", "Help"));
	}
	for (size_t i = 0; i < MenuLinks::kAll.size(); ++i) {
		const auto link = MenuLinks::kAll[i];
		if (i > 0) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", "\xC2\xB7");  // middle dot
			ImGui::SameLine();
		}
		ImGui::PushID(static_cast<int>(link));
		if (ImGui::TextLink(MenuLinks::GetLabel(link)))
			MenuLinks::Open(link);
		ImGui::PopID();
		Util::HoverTip(MenuLinks::GetUrl(link));
	}

	{
		MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
		ImGui::SeparatorText(T("menu.help.about", "About"));
	}
	const auto buildTag = MenuHeaderRenderer::GetBuildTagText();
	const auto title = MenuHeaderRenderer::GetTitleText();
	ImGui::TextUnformatted(buildTag.empty() ? title.c_str() : std::format("{} {}", title, buildTag).c_str());
	ImGui::TextUnformatted(MenuHeaderRenderer::GetGameVersionText().c_str());
	ImGui::TextUnformatted(MenuHeaderRenderer::GetGpuText().c_str());
	ImGui::TextUnformatted(MenuHeaderRenderer::GetSwapChainText().c_str());

	ImGui::EndPopup();
}

void HomePageRenderer::RenderHomePage()
{
	lastShownFrame = ImGui::GetFrameCount();
	ImGui::BeginChild("HomePage", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);

	pageCards = Util::Anim::EnterGroup(ImGui::GetID("HomeCards"));
	// Cards scroll above; the Discord banner and links stay pinned below them
	const float stripReserve = g_stripHeight > 0.0f ? g_stripHeight : ImGui::GetFrameHeight() * Constants::HOME_STRIP_FIRST_FRAME_FRAMES;
	ImGui::BeginChild("##HomeCards", ImVec2(0.0f, -stripReserve), ImGuiChildFlags_NavFlattened);
	if (std::exchange(attentionScrollPending, false))
		ImGui::SetScrollY(0.0f);

	// What CS is, straight at the top of the page
	CenteredWrappedText(T("menu.home.intro",
		"Community Shaders is the open-source, next-generation graphics framework for Skyrim: modern lighting, materials, "
		"shader effects and post processing, built in the open and shaped by the community."));

	// Needs Attention always heads the page, across its full width
	RenderAttentionCard();
	ImGui::Dummy(ImVec2(0.0f, Constants::HOME_CARD_SPACING * Util::GetUIScale()));

	// Two columns once each card gets about 30 characters of room, else one
	const float charWidth = ImGui::CalcTextSize("0").x;
	const int columns = ImGui::GetContentRegionAvail().x >= Constants::HOME_TWO_COLUMN_MIN_CHARS * charWidth ? 2 : 1;
	if (ImGui::BeginTable("##HomeGrid", columns, ImGuiTableFlags_SizingStretchSame)) {
		constexpr std::array<void (*)(), 4> cards = { &RenderPresetCard, &RenderSceneCard, &RenderShadersCard, &RenderQuickAccessCard };
		for (auto* card : cards) {
			ImGui::TableNextColumn();
			card();
			ImGui::Dummy(ImVec2(0.0f, Constants::HOME_CARD_SPACING * Util::GetUIScale()));
		}
		ImGui::EndTable();
	}

	ImGui::EndChild();
	RenderBottomStrip();

	ImGui::EndChild();
}

void HomePageRenderer::RenderFirstTimeSetupDialog()
{
	if (!ShouldShowFirstTimeSetup())
		return;

	// Block input to the game and make cursor visible - input blocking is handled by ShouldSwallowInput()
	auto& io = ImGui::GetIO();
	io.WantCaptureMouse = true;
	io.WantCaptureKeyboard = true;
	io.MouseDrawCursor = true;  // Show ImGui cursor

	const float uiScale = Util::GetUIScale();
	const ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(DIALOG_MIN_WIDTH * uiScale, 0), ImVec2(DIALOG_MAX_WIDTH * uiScale, FLT_MAX));
	ImGui::SetNextWindowFocus();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, DIALOG_CORNER_ROUNDING * uiScale);

	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;

	if (!ImGui::Begin("##FirstTimeSetup", nullptr, flags)) {
		ImGui::PopStyleVar();
		ImGui::End();
		return;
	}

	// Fullscreen fade on the dialog's own draw list, so it covers every window beneath it
	auto* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRectFullScreen();
	drawList->AddRectFilled(ImVec2(0, 0), io.DisplaySize, ImGui::GetColorU32(IM_COL32(0, 0, 0, MODAL_OVERLAY_ALPHA)));
	drawList->PopClipRect();

	auto menu = Menu::GetSingleton();

	// The logo as a faint watermark behind the dialog, keeping its aspect ratio
	if (menu && Util::IconLoader::GetIcons().logo.texture) {
		const ImVec2 windowPos = ImGui::GetWindowPos();
		const ImVec2 windowSize = ImGui::GetWindowSize();
		const ImVec2 textureSize = Util::IconLoader::GetIcons().logo.size;
		const float logoHeight = LOGO_WATERMARK_HEIGHT * uiScale;
		const float logoWidth = logoHeight * textureSize.x / textureSize.y;
		const ImVec2 logoMin(windowPos.x + (windowSize.x - logoWidth) * 0.5f, windowPos.y + (windowSize.y - logoHeight) * 0.5f);
		const ImVec2 logoMax(logoMin.x + logoWidth, logoMin.y + logoHeight);

		ImVec4 monoColor = menu->GetSettings().Theme.Palette.Text;
		monoColor.w = Constants::HOME_SETUP_WATERMARK_MONO_ALPHA;
		const ImU32 watermarkColor = menu->GetSettings().Theme.UseMonochromeLogo ? ImGui::GetColorU32(monoColor) : ImGui::GetColorU32(Constants::HOME_SETUP_WATERMARK_COLOR);
		drawList->AddImage(Util::IconLoader::GetIcons().logo.texture, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), watermarkColor);
	}

	// Center all content
	const float windowWidth = ImGui::GetWindowWidth();
	auto centerText = [windowWidth](const char* text) {
		ImGui::SetCursorPosX((windowWidth - ImGui::CalcTextSize(text).x) * 0.5f);
	};
	auto centerWidth = [windowWidth](float width) {
		ImGui::SetCursorPosX((windowWidth - width) * 0.5f);
	};

	// Two centred lines with the gap between them tightened
	const char* versionLine1 = T("menu.setup.new_install_line1", "This appears to be a new install, update, or");
	const char* versionLine2 = T("menu.setup.new_install_line2", "reinstallation of Community Shaders.");

	centerText(versionLine1);
	ImGui::Text("%s", versionLine1);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - DIALOG_LINE_TIGHTEN * uiScale);
	centerText(versionLine2);
	ImGui::Text("%s", versionLine2);

	ImGui::Spacing();

	const char* description = T("menu.setup.choose_hotkey", "Click the key below to choose a menu hotkey:");
	centerText(description);
	ImGui::Text("%s", description);

	// The current toggle key, as text the user clicks to change it
	auto& themeSettings = menu->GetTheme();
	const bool isCapturing = menu->settingToggleKey;

	// Bigger while capturing
	ImGui::SetWindowFontScale(isCapturing ? HOTKEY_TEXT_SCALE_CAPTURING : HOTKEY_TEXT_SCALE);

	// Format hotkey with brackets to make it look like a button
	const std::string toggleKeyName = Util::Input::KeyIdToString(menu->GetSettings().ToggleKey);
	const std::string hotkeyStr = isCapturing ? "[ ... ]" : "[ " + toggleKeyName + " ]";

	const ImVec2 hotkeyTextSize = ImGui::CalcTextSize(hotkeyStr.c_str());

	centerWidth(hotkeyTextSize.x);
	const ImVec2 buttonPos = ImGui::GetCursorScreenPos();

	// An invisible button over the text takes the hover and click; the text is drawn on top of it
	ImGui::PushID("HotkeyButton");
	const bool clicked = ImGui::InvisibleButton("##HotkeyClick", hotkeyTextSize);
	const bool hovered = ImGui::IsItemHovered();
	ImGui::PopID();
	ImGui::SetCursorScreenPos(buttonPos);

	const ImVec4 baseHotkeyColor = themeSettings.StatusPalette.CurrentHotkey;
	ImVec4 hotkeyColor = baseHotkeyColor;
	if (isCapturing)
		hotkeyColor = Util::GetPulsingColor(baseHotkeyColor);
	else if (hovered)
		hotkeyColor = ImVec4(baseHotkeyColor.x * HOTKEY_HOVER_DIM_FACTOR, baseHotkeyColor.y * HOTKEY_HOVER_DIM_FACTOR,
			baseHotkeyColor.z * HOTKEY_HOVER_DIM_FACTOR, baseHotkeyColor.w);

	ImGui::TextColored(hotkeyColor, "%s", hotkeyStr.c_str());

	ImGui::SetWindowFontScale(1.0f);

	// A click that came from Enter (keyboard nav) is ignored, so a reflexive Enter press does not start capture
	if (clicked && !isCapturing && !ImGui::IsKeyPressed(ImGuiKey_Enter))
		menu->settingToggleKey = true;

	if (isCapturing) {
		const char* pressKeyText = T("menu.setup.press_any_key", "Press any key to set as toggle key...");
		centerText(pressKeyText);
		ImGui::TextDisabled("%s", pressKeyText);
	}

	// CS Editor hotkey status — updates live as user picks keys
	const auto& csEditorKey = menu->GetSettings().CSEditorToggleKey;
	if (csEditorKey.empty()) {
		const char* warnText = T("menu.setup.cs_editor_unbound", "CS Editor hotkey unbound - chosen key uses Shift");
		centerText(warnText);
		Util::Text::Warning("%s", warnText);
	} else {
		const std::string infoStr = I18n::GetSingleton()->Format("menu.setup.cs_editor_will_be",
			{ { "key", Util::Input::KeyIdToString(csEditorKey) } }, "CS Editor hotkey will be: {key}");
		centerText(infoStr.c_str());
		ImGui::TextDisabled("%s", infoStr.c_str());
	}

	ImGui::Spacing();

	const char* laterText = T("menu.setup.change_later", "You can change this later in General > Keybindings.");
	centerText(laterText);
	ImGui::Text("%s", laterText);

	ImGui::Spacing();

	// Help text with breathing animation
	const std::string helpText = I18n::GetSingleton()->Format("menu.setup.press_hotkey_to_continue",
		{ { "key", toggleKeyName } },
		"Press {key} to continue");

	ImGui::SetWindowFontScale(HELP_TEXT_SCALE);
	centerText(helpText.c_str());
	Util::DrawBreathingText(helpText.c_str(), themeSettings.StatusPalette.InfoColor);

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
	ImGui::PopStyleVar();
}

bool HomePageRenderer::ShouldShowFirstTimeSetup()
{
	if (isFirstTimeSetupShown)
		return false;
	return !Menu::GetSingleton()->GetSettings().FirstTimeSetupCompleted;
}

void HomePageRenderer::MarkFirstTimeSetupComplete()
{
	auto menu = Menu::GetSingleton();
	menu->GetSettings().FirstTimeSetupCompleted = true;
	menu->settingToggleKey = false;  // never leave a hotkey capture running behind the closed dialog

	// Saved at once so the welcome screen cannot return when the user never saves by hand
	globals::state->Save();

	isFirstTimeSetupShown = true;
}
