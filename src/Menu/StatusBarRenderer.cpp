#include "Menu/StatusBarRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <string>

#include "CSEditor/Browser/BrowserWidgets.h"
#include "FeatureIssues.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/ThemeManager.h"
#include "SaveManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

namespace
{
	/** @brief A clickable status part: a glyph and a label in one colour, on a transparent button. */
	bool Segment(const char* id, Icons::GlyphRef icon, const char* label, const ImVec4& color, const char* tooltip)
	{
		bool clicked;
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			auto style = Util::TransparentIconButtonStyle();
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			clicked = Icons::LabeledButton(id, icon, label);
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
		}
		if (tooltip)
			Util::AddTooltip(tooltip);
		return clicked;
	}
}

namespace StatusBarRenderer
{
	float GetHeight()
	{
		return ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3;
	}

	void Render(const std::vector<MenuShortcuts::Shortcut>& shortcuts)
	{
		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();

		ImGui::PushID("##StatusBar");
		ImGui::AlignTextToFramePadding();

		auto* i18n = I18n::GetSingleton();
		const float gap = ImGui::GetStyle().ItemSpacing.x * ThemeManager::Constants::STATUS_BAR_SEGMENT_GAP_SCALE;
		bool first = true;
		const auto nextSegment = [&first, gap]() {
			if (!first)
				ImGui::SameLine(0.0f, gap);
			first = false;
		};

		// Unsaved changes, with a Save link
		if (globals::state->HasUnsavedChanges()) {
			nextSegment();
			const ImVec4 unsavedColor = Util::Colors::GetUnsaved();
			Util::DrawInlineIndicatorDot(ImGui::GetColorU32(unsavedColor), true);
			ImGui::SameLine();
			ImGui::TextColored(unsavedColor, "%s", T("menu.status.unsaved", "Unsaved changes"));
			ImGui::SameLine();
			if (BrowserUI::Link("##StatusSave", T("menu.status.save", "Save")))
				SaveManager::SaveAll();
			Util::AddTooltip(i18n->Format("menu.header.save_tooltip",
									 { { "shortcut", MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kSave) } },
									 "Save settings ({shortcut})")
					.c_str());
		}

		auto* shaderCache = globals::shaderCache;

		// Shader compile progress while it runs
		if (shaderCache->IsCompiling()) {
			nextSegment();
			const ImVec4 infoColor = Util::Colors::GetInfo();
			{
				ImGui::PushStyleColor(ImGuiCol_Text, infoColor);
				Icons::Text(Icons::TI(ICON_TI_LOADER_2));
				ImGui::PopStyleColor();
			}
			ImGui::SameLine();
			ImGui::TextColored(infoColor, "%s",
				i18n->Format("menu.status.compiling",
						{ { "done", std::to_string(shaderCache->GetCompletedTasks()) }, { "total", std::to_string(shaderCache->GetTotalTasks()) } },
						"Compiling shaders {done}/{total}")
					.c_str());
		}

		// Failed shaders; clicking shows or hides the in-game error message
		if (const auto failed = shaderCache->GetFailedTasks()) {
			nextSegment();
			const auto label = i18n->Format("menu.status.failed_shaders", { { "count", std::to_string(failed) } }, "Failed shaders: {count}");
			if (Segment("##StatusFailed", Icons::TI(ICON_TI_CIRCLE_X), label.c_str(), Util::Colors::GetError(),
					T("menu.toggle_error_message_tooltip",
						"Hide or show the shader failure message. "
						"Your installation is broken and will likely see errors in game. "
						"Please double check you have updated all features and that your load order is correct. "
						"See CommunityShaders.log for details and check the Nexus Mods page or Discord server.")))
				shaderCache->ToggleErrorMessages();
		}

		// Feature issues; clicking opens their page
		if (FeatureIssues::HasFeatureIssues()) {
			nextSegment();
			const auto label = i18n->Format("menu.status.feature_issues",
				{ { "count", std::to_string(FeatureIssues::GetFeatureIssues().size()) } }, "Feature issues: {count}");
			if (Segment("##StatusIssues", Icons::TI(ICON_TI_ALERT_TRIANGLE), label.c_str(), Util::Colors::GetWarning(),
					T("menu.issues.open_tooltip", "Open the Issues page")))
				globals::menu->SelectFeatureMenu(std::string(MenuPageIds::kIssues));
		}

		// GPU, runtime and swap chain on the right, in whatever room the left side leaves
		const auto info = std::format("{}  \xC2\xB7  {} {}  \xC2\xB7  {}",
			globals::state->adapterDescription,
			magic_enum::enum_name(REL::Module::GetRuntime()),
			Util::GetFormattedVersion(REL::Module::get().version()),
			MenuHeaderRenderer::GetSwapChainText());
		if (!first)
			ImGui::SameLine(0.0f, gap);
		const float available = ImGui::GetContentRegionAvail().x;
		const float width = std::min(ImGui::CalcTextSize(info.c_str()).x, available);
		if (width >= ImGui::GetFontSize()) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
			BrowserUI::EllipsizedText(info.c_str(), width, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		ImGui::PopID();
	}
}
