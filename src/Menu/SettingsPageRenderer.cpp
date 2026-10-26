#include "SettingsPageRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <span>
#include <vector>

#include "AdvancedSettingsRenderer.h"
#include "CSEditor/Browser/BrowserWidgets.h"
#include "FeatureListRenderer.h"
#include "FontAtlasState.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "SettingsSections.h"
#include "ThemeManager.h"
#include "Utils/UI.h"

namespace
{
	namespace Ids = SettingsSectionIds;
	using PageState = SettingsPageRenderer::PageState;

	/** @brief Sub-nav group a section is listed under. */
	enum class Group : std::uint8_t
	{
		General,
		Appearance,
		Advanced
	};

	/** @brief One entry of the sub-nav: its ID, its group and how to draw it. */
	struct Section
	{
		std::string_view id;
		Group group;
		bool editsTheme;  ///< Edits values stored in the theme file, so the theme save note is shown above it
		void (*draw)(PageState& state);
	};

	// Sub-nav order. Each group's sections are contiguous; the first section is the fallback for unknown IDs.
	constexpr std::array kSections = {
		Section{ Ids::kShadersCache, Group::General, false, [](PageState&) {
					SettingsTabRenderer::RenderShadersSection();
					AdvancedSettingsRenderer::RenderCompilerSection();
				} },
		Section{ Ids::kKeybindings, Group::General, false, [](PageState& state) { SettingsTabRenderer::RenderKeybindingsSection(state.keys); } },
		Section{ Ids::kLanguage, Group::General, false, [](PageState&) { SettingsTabRenderer::RenderLanguageSection(); } },
		Section{ Ids::kBehavior, Group::General, true, [](PageState&) { SettingsTabRenderer::RenderBehaviorSection(); } },
		Section{ Ids::kTheme, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderThemesSection(); } },
		Section{ Ids::kFonts, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderFontsSection(); } },
		Section{ Ids::kColors, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderColorsSection(); } },
		Section{ Ids::kStyling, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderStylingSection(); } },
		Section{ Ids::kHeaderEffects, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderHeaderEffectsSection(); } },
		Section{ Ids::kDeveloper, Group::Advanced, false, [](PageState&) { AdvancedSettingsRenderer::RenderDeveloperSection(); } },
		Section{ Ids::kLogging, Group::Advanced, false, [](PageState&) { AdvancedSettingsRenderer::RenderLoggingSection(); } },
		Section{ Ids::kShaderDebug, Group::Advanced, false, [](PageState&) { AdvancedSettingsRenderer::RenderShaderDebugSection(); } },
		Section{ Ids::kTesting, Group::Advanced, false, [](PageState&) { AdvancedSettingsRenderer::RenderTestingSection(); } },
		Section{ Ids::kDisableAtBoot, Group::Advanced, false, [](PageState& state) {
					if (state.drawDisableAtBoot)
						state.drawDisableAtBoot();
				} },
	};

	const Section* FindSection(std::string_view id)
	{
		const auto it = std::ranges::find(kSections, id, &Section::id);
		return it != kSections.end() ? &*it : nullptr;
	}

	const Section& FirstSectionOf(Group group)
	{
		return *std::ranges::find(kSections, group, &Section::group);
	}

	const char* GetGroupLabel(Group group)
	{
		switch (group) {
		case Group::Appearance:
			return T("menu.settings.group_appearance", "Appearance");
		case Group::Advanced:
			return T("menu.features.advanced", "Advanced");
		case Group::General:
		default:
			return T("menu.features.general", "General");
		}
	}

	const char* GetSectionTitle(std::string_view id)
	{
		if (id == Ids::kShadersCache)
			return T("menu.settings.nav_shaders_cache", "Shaders & Cache");
		if (id == Ids::kKeybindings)
			return T("menu.settings.tab_keybindings", "Keybindings");
		if (id == Ids::kLanguage)
			return T("menu.settings.section_language", "Language");
		if (id == Ids::kBehavior)
			return T("menu.settings.tab_behavior", "Behavior");
		if (id == Ids::kTheme)
			return T("menu.settings.nav_theme", "Theme");
		if (id == Ids::kFonts)
			return T("menu.settings.tab_fonts", "Fonts");
		if (id == Ids::kColors)
			return T("menu.settings.tab_colors", "Colors");
		if (id == Ids::kStyling)
			return T("menu.settings.tab_styling", "Styling");
		if (id == Ids::kHeaderEffects)
			return T("menu.settings.nav_header_effects", "Header & Effects");
		if (id == Ids::kDeveloper)
			return T("menu.advanced.tab_developer", "Developer");
		if (id == Ids::kLogging)
			return T("menu.advanced.tab_logging", "Logging");
		if (id == Ids::kShaderDebug)
			return T("menu.advanced.tab_shader_debug", "Shader Debug");
		if (id == Ids::kTesting)
			return T("menu.advanced.tab_testing", "Testing");
		if (id == Ids::kDisableAtBoot)
			return T("menu.advanced.tab_disable_at_boot", "Disable at Boot");
		return "";
	}

	/** @brief Section title in the feature page title style, with its group as a muted breadcrumb below. */
	void DrawSectionHeader(const Section& section)
	{
		float titleScale = globals::menu->GetTheme().FeatureHeading.FeatureTitleScale;
		if (!std::isfinite(titleScale))
			titleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
		titleScale = std::clamp(titleScale, ThemeManager::Constants::SETTINGS_TITLE_SCALE_MIN, ThemeManager::Constants::SETTINGS_TITLE_SCALE_MAX);

		{
			MenuFonts::FontRoleGuard titleFont(Menu::FontRole::Title);
			ImGui::SetWindowFontScale(titleScale);
			ImGui::TextUnformatted(GetSectionTitle(section.id));
			ImGui::SetWindowFontScale(1.0f);
		}
		{
			MenuFonts::FontRoleGuard subtextFont(Menu::FontRole::Subtext);
			ImGui::TextDisabled("%s", GetGroupLabel(section.group));
		}
		ImGui::Separator();
		ImGui::Spacing();
	}

	/** @brief ASCII case-folded substring test; UTF-8 bytes past ASCII must match exactly, so translated labels still match. */
	bool ContainsFolded(std::string_view text, std::string_view query)
	{
		const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
		return !std::ranges::search(text, query, {}, lower, lower).empty();
	}

	const char* GetEntryLabel(const SettingsSearchEntry& entry)
	{
		return entry.labelKey ? T(entry.labelKey, entry.fallback) : entry.fallback;
	}

	/** @brief Search hits of one section: its title, and the setting labels that matched. */
	struct SectionMatches
	{
		bool title = false;
		std::vector<const SettingsSearchEntry*> entries;

		[[nodiscard]] bool Any() const { return title || !entries.empty(); }
	};

	/** @brief Hits per section, indexed like kSections. */
	using Matches = std::array<SectionMatches, kSections.size()>;

	size_t IndexOf(const Section& section)
	{
		return static_cast<size_t>(&section - kSections.data());
	}

	/** @brief Matches the trimmed query against every section title and indexed label, or nothing for an empty query. */
	std::optional<Matches> FindMatches(std::string_view query)
	{
		const auto first = query.find_first_not_of(" \t");
		if (first == std::string_view::npos)
			return std::nullopt;
		query = query.substr(first, query.find_last_not_of(" \t") - first + 1);

		Matches matches{};
		const auto searchIndex = [&](std::span<const SettingsSearchEntry> index) {
			for (const auto& entry : index) {
				const Section* section = FindSection(entry.sectionId);
				if (section && (ContainsFolded(GetEntryLabel(entry), query) || ContainsFolded(entry.fallback, query)))
					matches[IndexOf(*section)].entries.push_back(&entry);
			}
		};
		searchIndex(SettingsTabRenderer::GetSearchIndex());
		searchIndex(AdvancedSettingsRenderer::GetSearchIndex());
		for (const auto& section : kSections)
			matches[IndexOf(section)].title = ContainsFolded(GetSectionTitle(section.id), query);
		return matches;
	}

	/** @brief Draws a hit count at the right edge of the row just drawn. */
	void DrawRowCount(size_t count)
	{
		const std::string text = std::to_string(count);
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const ImVec2 size = ImGui::CalcTextSize(text.c_str());
		ImGui::GetWindowDrawList()->AddText(ImVec2(max.x - size.x - ImGui::GetStyle().FramePadding.x, min.y + (max.y - min.y - size.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
	}

	void RenderNav(PageState& state)
	{
		const bool searchChanged = BrowserUI::SearchField("##SettingsSearch", state.search.data(), state.search.size(),
			T("menu.settings.search_hint", "Search settings..."), ImGui::GetContentRegionAvail().x, false);
		const auto matches = FindMatches(state.search.data());

		// Typing moves off a section with no hits to the first one that has some
		if (const Section* current = FindSection(state.section); searchChanged && matches && current && !(*matches)[IndexOf(*current)].Any()) {
			const auto hit = std::ranges::find_if(*matches, &SectionMatches::Any);
			if (hit != matches->end())
				state.section = std::string(kSections[static_cast<size_t>(hit - matches->begin())].id);
		}

		ImGui::Spacing();

		const float groupSpacing = ThemeManager::Constants::SETTINGS_NAV_GROUP_SPACING * Util::GetUIScale();
		const Group* currentGroup = nullptr;
		for (const auto& section : kSections) {
			const SectionMatches* hits = matches ? &(*matches)[IndexOf(section)] : nullptr;
			if (hits && !hits->Any())
				continue;

			if (!currentGroup || *currentGroup != section.group) {
				if (currentGroup)
					ImGui::Dummy(ImVec2(0.0f, groupSpacing));
				currentGroup = &section.group;
				BrowserUI::SectionLabel(GetGroupLabel(section.group));
			}

			ImGui::PushID(section.id.data(), section.id.data() + section.id.size());
			if (ImGui::Selectable(GetSectionTitle(section.id), state.section == section.id))
				state.section = std::string(section.id);
			if (hits && !hits->entries.empty())
				DrawRowCount(hits->entries.size());
			ImGui::PopID();
		}

		if (matches && !currentGroup)
			ImGui::TextDisabled("%s", T("menu.settings.search_no_match", "No matching settings"));
	}

	/** @brief Lists the matched setting labels of the shown section above its content. */
	void DrawMatchHint(const SectionMatches& hits)
	{
		std::string labels;
		for (const auto* entry : hits.entries) {
			std::string_view label = GetEntryLabel(*entry);
			label = label.substr(0, label.find_last_not_of(" :") + 1);  // keybinding labels end in ':'
			if (!labels.empty())
				labels += ", ";
			labels += label;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetAccent());
		ImGui::TextWrapped("%s", I18n::GetSingleton()->Format("menu.settings.search_matches", { { "labels", labels } }, "Matching settings: {labels}").c_str());
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	void RenderContent(PageState& state, const Section& section)
	{
		DrawSectionHeader(section);
		if (const auto matches = FindMatches(state.search.data()); matches && !(*matches)[IndexOf(section)].entries.empty())
			DrawMatchHint((*matches)[IndexOf(section)]);
		if (section.editsTheme)
			SettingsTabRenderer::RenderThemeSaveNote();

		ImGui::PushID(section.id.data(), section.id.data() + section.id.size());
		section.draw(state);
		ImGui::PopID();
	}
}

void SettingsPageRenderer::Render(PageState& state)
{
	if (!IsKnownSection(state.section))
		state.section = std::string(GetFirstSection());
	if (!state.search.empty())
		state.search.back() = '\0';  // the search text is read as a C string

	// Fonts asks for a preview-font atlas while it is drawn. Drop it on any other section so it is not
	// rebaked into every later atlas rebuild for the rest of the session.
	if (auto& atlas = MenuFonts::GetAtlasState(); state.section != Ids::kFonts && atlas.wantsFontPreviewAtlas) {
		atlas.wantsFontPreviewAtlas = false;
		atlas.pendingFontReload = true;
	}

	const float navWidth = ThemeManager::Constants::SETTINGS_NAV_WIDTH * Util::GetUIScale();
	if (ImGui::BeginChild("##SettingsNav", ImVec2(navWidth, 0.0f), ImGuiChildFlags_None))
		RenderNav(state);
	ImGui::EndChild();

	ImGui::SameLine();

	// The nav may have changed the selection this frame
	const Section* section = FindSection(state.section);
	if (ImGui::BeginChild("##SettingsContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None) && section)
		RenderContent(state, *section);
	ImGui::EndChild();
}

bool SettingsPageRenderer::IsKnownSection(std::string_view id)
{
	return FindSection(id) != nullptr;
}

std::string_view SettingsPageRenderer::GetFirstSection()
{
	return kSections.front().id;
}

std::optional<std::string_view> SettingsPageRenderer::GetAliasSection(std::string_view pageId)
{
	if (pageId == MenuPageIds::kGeneral)
		return FirstSectionOf(Group::General).id;
	if (pageId == MenuPageIds::kAdvanced)
		return FirstSectionOf(Group::Advanced).id;
	return std::nullopt;
}
