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
#include "Menu/HomePageRenderer.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/MenuIcons.h"
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

		// Feature issues come first, in the error colour; clicking opens Needs Attention on Home
		if (FeatureIssues::HasFeatureIssues()) {
			nextSegment();
			const auto label = i18n->Format("menu.status.feature_issues",
				{ { "count", std::to_string(FeatureIssues::GetFeatureIssues().size()) } }, "Feature issues: {count}");
			if (Segment("##StatusIssues", Icons::TI(MenuIcons::kIssues), label.c_str(), Util::Colors::GetError(),
					T("menu.status.issues_tooltip", "Show the issues on Home")))
				HomePageRenderer::FocusNeedsAttention();
		}

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

		// Nothing needs attention: say so, so the left side is never empty
		if (first) {
			nextSegment();
			Icons::Text(Icons::TI(MenuIcons::kSave));
			ImGui::SameLine();
			ImGui::TextDisabled("%s", T("menu.status.all_saved", "All changes saved"));
		}

		// GPU, runtime and swap chain on the right, in whatever room the left side leaves
		std::string info;
		const auto addPart = [&info](const std::string& part) {
			if (part.empty())
				return;
			if (!info.empty())
				info += "  ·  ";
			info += part;
		};
		addPart(globals::state->adapterDescription);
		addPart(std::format("{} {}", magic_enum::enum_name(REL::Module::GetRuntime()),
			Util::GetFormattedVersion(REL::Module::get().version())));
		addPart(MenuHeaderRenderer::GetSwapChainText());

		ImGui::SameLine(0.0f, gap);
		// Keep clear of the window's bottom-right resize grip
		const float gripClearance = ImGui::GetFontSize() * ThemeManager::Constants::STATUS_BAR_GRIP_CLEARANCE_SCALE;
		const float helpWidth = HomePageRenderer::GetHelpButtonWidth();
		const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - gripClearance;
		const float available = ImGui::GetContentRegionAvail().x - gripClearance - helpWidth - ImGui::GetStyle().ItemSpacing.x;
		const float width = std::min(ImGui::CalcTextSize(info.c_str()).x, available);
		if (width >= ImGui::GetFontSize()) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
			BrowserUI::EllipsizedText(info.c_str(), width, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		// Help sits at the right end, clear of the resize grip
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::SetCursorPosX(rowRight - helpWidth);
		HomePageRenderer::RenderHelpButton();

		ImGui::PopID();
	}
}
