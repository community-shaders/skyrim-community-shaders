#include "SettingsPageRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
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
#include "Utils/UIAnim.h"

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
		Section{ Ids::kTheme, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderThemesSection(); } },
		Section{ Ids::kFonts, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderFontsSection(); } },
		Section{ Ids::kColors, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderColorsSection(); } },
		Section{ Ids::kStyling, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderStylingSection(); } },
		Section{ Ids::kHeaderEffects, Group::Appearance, true, [](PageState&) { SettingsTabRenderer::RenderHeaderEffectsSection(); } },
		Section{ Ids::kDeveloper, Group::Advanced, false, [](PageState&) { AdvancedSettingsRenderer::RenderDeveloperSection(); } },
		Section{ Ids::kLogging, Group::Advanced, false, [](PageState&) { AdvancedSettingsRenderer::RenderLoggingSection(); } },
		Section{ Ids::kShaderDebug, Group::Advanced, false, [](PageState&) { AdvancedSettingsRenderer::RenderShaderDebugSection(); } },
		Section{ Ids::kActiveShaders, Group::Advanced, false, [](PageState&) { AdvancedSettingsRenderer::RenderActiveShadersSection(); } },
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
		if (id == Ids::kActiveShaders)
			return T("menu.advanced.active_shaders", "Active Shaders");
		if (id == Ids::kTesting)
			return T("menu.advanced.tab_testing", "Testing");
		if (id == Ids::kDisableAtBoot)
			return T("menu.advanced.tab_disable_at_boot", "Disable at Boot");
		return "";
	}

	/** @brief One-line summary shown under an Advanced section's title, or nullptr to show the group name instead. */
	const char* GetSectionDescription(std::string_view id)
	{
		if (id == Ids::kDeveloper)
			return T("menu.advanced.desc_developer", "Developer mode, shader compiler flags and build statistics, for working on Community Shaders itself.");
		if (id == Ids::kLogging)
			return T("menu.advanced.desc_logging", "How much goes into CommunityShaders.log, extra shader defines, and quick access to the logs.");
		if (id == Ids::kShaderDebug)
			return T("menu.advanced.desc_shader_debug", "Choose which game shaders Community Shaders replaces, to compare against vanilla or isolate a problem.");
		if (id == Ids::kActiveShaders)
			return T("menu.advanced.desc_active_shaders", "Shaders drawn in the last second. Click a row to block that shader, right-click to copy its details.");
		if (id == Ids::kTesting)
			return T("menu.advanced.desc_testing", "A/B comparisons and test setups for checking changes in game.");
		if (id == Ids::kDisableAtBoot)
			return T("menu.advanced.desc_disable_at_boot", "Keep features from loading when the game starts, without uninstalling them.");
		return nullptr;
	}

	/** @brief Section title in the feature page title style, with its description (or group) as a muted line below. */
	void DrawSectionHeader(const Section& section)
	{
		const float titleScale = globals::menu->GetFeatureTitleScale();

		{
			MenuFonts::FontRoleGuard titleFont(Menu::FontRole::Title);
			ImGui::SetWindowFontScale(titleScale);
			ImGui::TextUnformatted(GetSectionTitle(section.id));
			ImGui::SetWindowFontScale(1.0f);
		}
		{
			MenuFonts::FontRoleGuard subtextFont(Menu::FontRole::Subtext);
			const char* description = GetSectionDescription(section.id);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", description ? description : GetGroupLabel(section.group));
			ImGui::PopTextWrapPos();
		}
		ImGui::Separator();
		ImGui::Spacing();
	}

	void RenderNav(PageState& state)
	{
		ImGui::Spacing();

		// Rows fill half an item spacing beyond their text, so the list is indented past that plus a margin
		const float navInset = ImGui::GetStyle().ItemSpacing.x * 0.5f + ThemeManager::Constants::LEFT_PANE_EDGE_INSET * Util::GetUIScale();
		ImGui::Indent(navInset);

		const float groupSpacing = ThemeManager::Constants::SETTINGS_NAV_GROUP_SPACING * Util::GetUIScale();
		const Group* currentGroup = nullptr;
		for (const auto& section : kSections) {
			if (!currentGroup || *currentGroup != section.group) {
				if (currentGroup)
					ImGui::Dummy(ImVec2(0.0f, groupSpacing));
				currentGroup = &section.group;
				BrowserUI::SectionLabel(GetGroupLabel(section.group));
			}

			ImGui::PushID(section.id.data(), section.id.data() + section.id.size());
			if (Util::Selectable(GetSectionTitle(section.id), state.section == section.id))
				state.section = std::string(section.id);
			ImGui::PopID();
		}

		ImGui::Unindent(navInset);
	}

	void RenderContent(PageState& state, const Section& section)
	{
		Util::Anim::PageFade fade(ImGui::GetID(section.id.data(), section.id.data() + section.id.size()));
		DrawSectionHeader(section);
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

	// Fonts asks for a preview-font atlas while it is drawn. Drop it on any other section so it is not
	// rebaked into every later atlas rebuild for the rest of the session.
	if (auto& atlas = MenuFonts::GetAtlasState(); state.section != Ids::kFonts && atlas.wantsFontPreviewAtlas) {
		atlas.wantsFontPreviewAtlas = false;
		atlas.pendingFontReload = true;
	}

	// The section list in the left pane may have changed the selection this frame
	const Section* section = FindSection(state.section);
	if (ImGui::BeginChild("##SettingsContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None) && section)
		RenderContent(state, *section);
	ImGui::EndChild();
}

void SettingsPageRenderer::RenderSectionList(PageState& state)
{
	if (!IsKnownSection(state.section))
		state.section = std::string(GetFirstSection());
	RenderNav(state);
}

std::vector<SettingsPageRenderer::SectionInfo> SettingsPageRenderer::GetSections()
{
	std::vector<SectionInfo> sections;
	sections.reserve(kSections.size());
	for (const auto& section : kSections)
		sections.push_back({ section.id, GetSectionTitle(section.id), GetGroupLabel(section.group) });
	return sections;
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
