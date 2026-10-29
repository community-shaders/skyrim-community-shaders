#include "ScenePageToolbar.h"

#include <format>
#include <string>

#include "../../I18n/I18n.h"
#include "../Browser/BrowserWidgets.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/Icons/helpers/SceneActionIcons.h"
#include "SceneCopyModal.h"
#include "SceneTransitionField.h"
#include "Utils/UI.h"

#include <imgui_internal.h>

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	using SceneContextId = SceneSettingsManager::SceneContextId;
	using SceneContextType = SceneSettingsManager::SceneContextType;
	using EntrySummary = SceneSettingsManager::ContextEntrySummary;

	/// Keeps the toolbar off the window's scrollbar, like the widget gutter does.
	constexpr float kRightMargin = 8.0f;

	/// Divider that keeps the transition label from reading as part of the control before the toolbar.
	constexpr float kDividerThickness = 1.0f;

	/// Clear is page-local, so each page keys its own confirmation and only that page draws it.
	struct PageConfirmation
	{
		void Request(const SceneContextId& a_page)
		{
			context = a_page;
			requested = true;
			popup.Request();
		}

		/** @brief Runs a_confirmed on the requesting page once accepted, and drops the request as
		 *  soon as the popup is gone, however it went. */
		template <typename Action>
		void Draw(const SceneContextId& a_page, Action&& a_confirmed)
		{
			if (!requested || context != a_page)
				return;
			if (popup.Draw())
				a_confirmed();
			else if (popup.IsOpen())
				return;
			requested = false;
		}

		Util::ConfirmationPopup popup;
		SceneContextId context;
		bool requested = false;
	};
	PageConfirmation clearConfirmation;

	const char* TransitionLabel()
	{
		return T(TKEY("scene_page_transition"), "Transition");
	}

	/// Divider between the toolbar's groups, tighter than Util::ToolbarDivider so the row stays narrow.
	void CompactDivider()
	{
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		ImGui::SameLine(0.0f, gap);
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, kDividerThickness);
		ImGui::SameLine(0.0f, gap);
	}

	/// Width CompactDivider() occupies, so the toolbar can right-align before drawing anything.
	float CompactDividerWidth()
	{
		return ImGui::GetStyle().ItemSpacing.x * 2.0f + kDividerThickness;
	}

	/// Icon-only buttons name their action in the tooltip's first line.
	std::string ActionTooltip(const char* a_label, const char* a_description)
	{
		return std::format("{}\n{}", a_label, a_description);
	}

	/// Width the whole toolbar occupies, so it can right-align (or wrap) before drawing anything.
	float ToolbarWidth(bool a_hasTransitionField)
	{
		const auto& style = ImGui::GetStyle();
		const float transitionWidth = a_hasTransitionField ?
		                                  kDividerThickness + style.ItemSpacing.x +
		                                      ImGui::CalcTextSize(TransitionLabel()).x + style.ItemInnerSpacing.x +
		                                      SceneTransitionField::GetWidth() + style.ItemSpacing.x :
		                                  0.0f;
		// Pause, copy, and clear, each set apart by a divider.
		return BrowserUI::IconButtonSize() * 3.0f + transitionWidth + CompactDividerWidth() * 2.0f;
	}

	/// Right-aligns a toolbar of a_width, moving it to its own row when the current one cannot hold it.
	void AlignToolbar(float a_width)
	{
		const float margin = kRightMargin * Util::GetUIScale();
		// Sharing a row it cannot fit on would push the actions past the panel edge.
		if (ImGui::GetCurrentWindowRead()->DC.IsSameLine && ImGui::GetContentRegionAvail().x < a_width + margin)
			ImGui::NewLine();
		if (const auto avail = ImGui::GetContentRegionAvail().x; avail > a_width + margin)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - a_width - margin);
	}

	/// Location-layer transition duration, labelled and set apart from the controls before it.
	void DrawTransitionField(SceneSettingsManager& a_manager)
	{
		const auto& style = ImGui::GetStyle();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, kDividerThickness);
		ImGui::SameLine();

		// Bare text is top-aligned, which would float it above the framed row it labels.
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(TransitionLabel());
		ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

		// Emptying the field drops the user's value, handing it back to the preset or the default.
		using TransitionControl = SceneSettingsManager::TransitionControl;
		auto seconds = a_manager.GetUserTransitionControl(TransitionControl::LocationSeconds);
		if (SceneTransitionField::Draw("##ScenePageTransition", seconds,
				a_manager.GetTransitionControl(TransitionControl::LocationSeconds), true))
			a_manager.SetTransitionControl(TransitionControl::LocationSeconds, seconds);
		Util::AddTooltip(T(TKEY("scene_page_transition_tooltip"),
			"Seconds every value on this page takes to ease in and out when the location changes.\n"
			"A single setting can override this from its own transition field."));
	}

	/// Pause / play toggle. The glyph shows what a click does: pause while anything applies, play
	/// (highlighted) once all of it is held.
	void DrawPauseToggle(SceneSettingsManager& a_manager, const SceneContextId& a_context, const EntrySummary& a_summary)
	{
		// A mixed page pauses rather than resumes: the button is a way out of that state, not into it.
		const bool pauseTarget = !a_summary.AllPaused();
		const std::string tooltip = pauseTarget ?
		                                ActionTooltip(T(TKEY("scene_page_pause_all"), "Pause All"),
											T(TKEY("scene_page_pause_all_tooltip"), "Holds back every override on this page without losing its value.")) :
		                                ActionTooltip(T(TKEY("scene_page_resume_all"), "Resume All"),
											T(TKEY("scene_page_resume_all_tooltip"), "Applies every override on this page again."));
		ImGui::BeginDisabled(a_summary.total == 0);
		if (BrowserUI::IconButton("##ScenePageToggle", pauseTarget ? SceneActionIcons::kPause : SceneActionIcons::kResume,
				tooltip.c_str(), !pauseTarget))
			a_manager.SetContextEntriesPaused(a_context, pauseTarget);
		ImGui::EndDisabled();
	}

	/// Copy to or from another context. Export is editor-wide, so it lives on the editor's action bar.
	void DrawCopyAction(const SceneContextId& a_context, bool a_hasEntries)
	{
		// A page holding entries always has somewhere to offer them, so destinations are never walked just to grey the button.
		const std::string tooltip = ActionTooltip(T(TKEY("scene_page_copy"), "Copy"),
			T(TKEY("scene_page_copy_tooltip"),
				"Copies settings between this page and other times of day, interiors, weathers or locations, with a "
				"preview before anything changes."));
		ImGui::BeginDisabled(!a_hasEntries && !SceneCopyModal::HasSources(a_context));
		if (BrowserUI::IconButton("##ScenePageCopy", SceneActionIcons::kCopy, tooltip.c_str()))
			SceneCopyModal::Open(a_context);
		ImGui::EndDisabled();
	}

	/// Clear-page button; the removal itself waits for the confirmation drawn by Draw.
	void DrawClearAction(SceneSettingsManager& a_manager, const SceneContextId& a_context, const EntrySummary& a_summary)
	{
		const char* clearLabel = T(TKEY("scene_page_clear"), "Clear");
		const std::string tooltip = ActionTooltip(clearLabel,
			T(TKEY("scene_page_clear_tooltip"), "Removes every override this page holds."));
		ImGui::BeginDisabled(a_summary.total == 0);
		if (BrowserUI::IconButton("##ScenePageClear", SceneActionIcons::kDelete, tooltip.c_str(), false,
				BrowserUI::DestructiveIconColor())) {
			auto count = a_summary.total;
			auto pageName = a_manager.GetSceneContextDisplayName(a_context);
			clearConfirmation.popup.title = T(TKEY("scene_page_clear_title"), "Clear page");
			clearConfirmation.popup.message = std::vformat(T(TKEY("scene_page_clear_message"),
															   "Remove all {} settings from {}? Mod overrides are left alone.\n\n"
															   "Settings you removed come back too."),
				std::make_format_args(count, pageName));
			clearConfirmation.popup.confirmLabel = clearLabel;
			clearConfirmation.popup.cancelLabel = T(TKEY("cancel"), "Cancel");
			clearConfirmation.Request(a_context);
		}
		ImGui::EndDisabled();
	}
}

void ScenePageToolbar::Draw(const SceneContextId& context)
{
	auto* manager = SceneSettingsManager::GetSingleton();
	if (!manager)
		return;

	const auto summary = manager->GetContextUserEntrySummary(context);
	// The global duration only governs the location layer, so it is absent everywhere else.
	const bool hasTransitionField = context.type == SceneContextType::Location;
	AlignToolbar(ToolbarWidth(hasTransitionField));

	ImGui::PushID("ScenePageToolbar");

	if (hasTransitionField) {
		DrawTransitionField(*manager);
		ImGui::SameLine();
	}
	DrawPauseToggle(*manager, context, summary);
	CompactDivider();
	DrawCopyAction(context, summary.total != 0);
	CompactDivider();
	DrawClearAction(*manager, context, summary);

	clearConfirmation.Draw(context, [&] { manager->ClearContextEntries(context); });
	SceneCopyModal::Draw(context);

	ImGui::PopID();
}

#undef I18N_KEY_PREFIX
