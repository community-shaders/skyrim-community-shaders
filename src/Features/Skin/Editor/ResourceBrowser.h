#pragma once

#include "I18n/I18n.h"

namespace SkinEditor
{
	/// Browses the Data folder as the game sees it (including a mod manager's virtual files), so that a
	/// choice is a resource path such as `textures/myskin/body_rfaos.dds`, never a path on disk.
	struct ResourceBrowser
	{
		std::string directory;
		std::string filter;
		std::string error;
		std::vector<std::string> folders, files;
		bool listed = false;
	};
	/// Draws a button that opens the browser. `a_start` is a resource path whose folder is shown first;
	/// `a_suffix` is the lower-case file name ending to list, such as ".dds". Returns the chosen resource path.
	/// Only loose files are listed; archived resources can still be entered as text by the caller.
	std::optional<std::string> ResourcePicker(ResourceBrowser& a_state, const char* a_label, const std::string& a_start, std::string_view a_suffix);
	/// The same browser for a caller that draws its own button: open it, then draw the popup every frame in
	/// the same ID scope.
	void OpenResourceBrowser(ResourceBrowser& a_state, const std::string& a_start);
	std::optional<std::string> ResourceBrowserPopup(ResourceBrowser& a_state, std::string_view a_suffix);
}
