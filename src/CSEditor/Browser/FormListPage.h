#pragma once

#include "BrowserState.h"
#include "Menu/Icons/helpers/IconFonts.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class EditorWindow;
class Widget;

/**
 * @brief The CS Editor Browser page for form categories (Weather, ImageSpace, Lighting Template,
 * Volumetric Lighting, Shader Particle Geometry, Lens Flare, Visual Effect) and the Cell Lighting card:
 * active records, filter toolbar, recent chips, the record list and the inspector.
 */
namespace FormListPage
{
	using WidgetVec = std::vector<std::unique_ptr<Widget>>;

	/** @brief What the page needs from EditorWindow; private members are reached through the callbacks. */
	struct Context
	{
		EditorWindow* editor = nullptr;
		/// Stable English category id, as stored in EditorWindow settings.
		std::string category;
		/// Localized category name for the page title.
		const char* title = "";
		Icons::GlyphRef icon;
		/// The category's records; nullptr for Cell Lighting, which has no list.
		WidgetVec* widgets = nullptr;
		std::function<bool(Widget*)> hasSavedFile;
		std::function<void(const std::vector<Widget*>&)> refreshSavedFiles;
		std::function<void(Widget*)> requestDeleteSavedFile;
		std::function<std::string(RE::TESForm*, const WidgetVec&)> resolveEditorId;
	};

	/** @brief Draws the whole page into the current window, filling the remaining space. */
	void Draw(Browser::FormListState& state, const Context& context);

	/** @brief The widget collection a category lists, or nullptr for categories without one. */
	WidgetVec* GetCollection(EditorWindow& editor, const std::string& category);
}
