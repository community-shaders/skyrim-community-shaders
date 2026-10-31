#include "SaveManager.h"

#include "CSEditor/EditorWindow.h"
#include "CSEditor/SceneManager/SceneSettingsManager.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "State.h"
#include "Utils/UI.h"

#include <format>

namespace SaveManager
{
	namespace
	{
		void WriteEditor(Trigger trigger)
		{
			// An automatic save commits only the scene edit behind it; widget edits wait for the user's save.
			auto* editor = EditorWindow::GetSingleton();
			if (trigger == Trigger::User)
				editor->SaveUnsavedWidgets();
			if (auto* sceneManager = SceneSettingsManager::GetSingleton())
				sceneManager->WriteUserSettings();
			if (trigger == Trigger::User)
				editor->Save();
		}

		void ConfirmSaved(Trigger trigger)
		{
			// The main menu flashes its own button; the toasts are for saves made inside the editor.
			auto* editor = EditorWindow::GetSingleton();
			if (trigger != Trigger::User || !editor->open)
				return;
			editor->ShowNotification(T("cs_editor.saved", "Saved"), Util::Colors::GetSuccess(), 1.5f);
			// Tools write into other mods only from their own buttons, so the save left these behind.
			for (auto* tool : editor->GetExternalTools()) {
				if (!tool->HasUnwrittenChanges())
					continue;
				const auto what = tool->DescribeChanges();
				const std::string target = tool->GetTargetName();
				editor->ShowNotification(
					std::vformat(T("cs_editor.tool_unwritten", "{}: changes not written to {}. Use its Write button to keep them."), std::make_format_args(what, target)),
					Util::Colors::GetWarning(), EditorWindow::kLongNotificationDuration);
			}
		}
	}

	void SaveEditor(Trigger trigger)
	{
		WriteEditor(trigger);
		ConfirmSaved(trigger);
	}

	void SaveAll(Trigger trigger)
	{
		if (auto* state = globals::state)
			state->Save();
		WriteEditor(trigger);
		ConfirmSaved(trigger);
	}

	const char* GetEditorSaveTooltip(bool unsaved)
	{
		return unsaved ?
		           T("cs_editor.save_unsaved_tooltip", "Unsaved changes. Click to save every CS editor change (Ctrl+S).") :
		           T("cs_editor.save_tooltip_short", "Save every CS editor change (Ctrl+S)");
	}
}
