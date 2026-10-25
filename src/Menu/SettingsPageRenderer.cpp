#include "SettingsPageRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>

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
		void (*draw)(PageState& state);
	};

	// Sub-nav order. Each group's sections are contiguous; the first section is the fallback for unknown IDs.
	constexpr std::array kSections = {
		Section{ Ids::kShadersCache, Group::General, [](PageState&) { SettingsTabRenderer::RenderShadersSection(); } },
		Section{ Ids::kKeybindings, Group::General, [](PageState& state) { SettingsTabRenderer::RenderKeybindingsSection(state.keys); } },
		Section{ Ids::kBehavior, Group::General, [](PageState&) { SettingsTabRenderer::RenderBehaviorSection(); } },
		Section{ Ids::kTheme, Group::Appearance, [](PageState&) { SettingsTabRenderer::RenderThemesSection(); } },
		Section{ Ids::kFonts, Group::Appearance, [](PageState&) { SettingsTabRenderer::RenderFontsSection(); } },
		Section{ Ids::kColors, Group::Appearance, [](PageState&) { SettingsTabRenderer::RenderColorsSection(); } },
		Section{ Ids::kStyling, Group::Appearance, [](PageState&) { SettingsTabRenderer::RenderStylingSection(); } },
		Section{ Ids::kDeveloper, Group::Advanced, [](PageState&) { AdvancedSettingsRenderer::RenderDeveloperSection(); } },
		Section{ Ids::kLogging, Group::Advanced, [](PageState&) { AdvancedSettingsRenderer::RenderLoggingSection(); } },
		Section{ Ids::kShaderDebug, Group::Advanced, [](PageState&) { AdvancedSettingsRenderer::RenderShaderDebugSection(); } },
		Section{ Ids::kTesting, Group::Advanced, [](PageState&) { AdvancedSettingsRenderer::RenderTestingSection(); } },
		Section{ Ids::kDisableAtBoot, Group::Advanced, [](PageState& state) { if (state.drawDisableAtBoot) state.drawDisableAtBoot(); } },
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

	void RenderNav(PageState& state)
	{
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
			if (ImGui::Selectable(GetSectionTitle(section.id), state.section == section.id))
				state.section = std::string(section.id);
			ImGui::PopID();
		}
	}

	void RenderContent(PageState& state, const Section& section)
	{
		DrawSectionHeader(section);

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
