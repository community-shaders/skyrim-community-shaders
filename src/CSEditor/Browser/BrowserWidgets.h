#pragma once

#include "Menu/Icons/helpers/IconFonts.h"

#include <functional>
#include <imgui.h>
#include <optional>
#include <string>

/** @brief Hand-drawn five-point star (the icon font has no outline star). Defined in EditorWindow.cpp. */
void DrawIconStar(ImVec2 center, float radius, ImU32 color, bool filled);

/** @brief Hand-drawn pennant (the icon font only ships a solid flag). Defined in EditorWindow.cpp. */
void DrawIconFlag(ImVec2 center, float height, ImU32 color, bool filled);

/**
 * @brief Shared building blocks for the CS Editor Browser pages: chips, badges, row accents,
 * ellipsized meta text, page headers and the search field. Sizes follow Util::GetUIScale().
 */
namespace BrowserUI
{
	/** @brief Paints an icon centred in a square box. */
	using IconPainter = std::function<void(ImDrawList* drawList, ImVec2 center, float size, ImU32 color)>;

	/** @brief Painter for a font glyph. */
	IconPainter GlyphIcon(Icons::GlyphRef glyph);
	/** @brief Painter for the hand-drawn star, outline unless filled. */
	IconPainter StarIcon(bool filled);
	/** @brief Painter for the hand-drawn flag, outline unless filled. */
	IconPainter FlagIcon(bool filled);
	/** @brief Painter for a filled dot (unsaved changes). */
	IconPainter DotIcon();

	/**
	 * @brief Search input with a magnifier inside the frame and a clear button while it holds text.
	 * Escape clears the text without closing the editor. Item queries afterwards refer to the input.
	 * @param ctrlFFocus Ctrl+F focuses the field while its window (or a child) is focused.
	 * @param flags, callback, userData Passed on to the input, alongside EscapeClearsAll.
	 * @return True when the text changed this frame.
	 */
	bool SearchField(const char* id, char* buffer, size_t bufferSize, const char* hint, float width, bool ctrlFFocus,
		ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);

	/** @brief Width a ToggleChip with this count takes. */
	float MeasureToggleChip(int count);

	/**
	 * @brief Rounded filter toggle: icon plus a count, outlined in the tint while active.
	 * @return True when clicked.
	 */
	bool ToggleChip(const char* id, const IconPainter& icon, int count, bool active, const ImVec4& tint, const char* tooltip);

	/** @brief Width a Chip with this label takes. */
	float MeasureChip(const char* label, bool withIcon);

	/**
	 * @brief Rounded label chip, optionally led by an icon and tinted.
	 * @param tint When non-null the chip is filled and outlined in it; otherwise it uses the frame colour.
	 * @return True when clicked; check ImGui::IsMouseDoubleClicked afterwards for double clicks.
	 */
	bool Chip(const char* id, const char* label, Icons::GlyphRef icon = {}, const ImVec4* iconColor = nullptr,
		const ImVec4* tint = nullptr);

	/** @brief What a filter chip shows: its label, optionally led by a coloured glyph. */
	struct ChipFace
	{
		const char* label = nullptr;
		Icons::GlyphRef icon{};
		std::optional<ImVec4> iconColor;
	};

	/** @brief A filter toggle: a chip filled in the accent while selected. */
	bool FilterChip(const char* id, const ChipFace& face, bool selected);

	/** @brief Stays on the current line when `width` more pixels fit, else wraps to the next. */
	void SameLineIfFits(float width);

	/** @brief Tinted badge on the current line, frame-high so it sits level with AlignTextToFramePadding'd text. */
	void InlineBadge(const char* label, const ImVec4& tint);

	/** @brief Square size of IconButton. */
	float IconButtonSize();

	/**
	 * @brief Quiet square icon button: transparent until hovered, filled while active.
	 * @param color Glyph colour; 0 uses the secondary text colour.
	 */
	bool IconButton(const char* id, Icons::GlyphRef glyph, const char* tooltip, bool active = false, ImU32 color = 0);

	/** @brief Glyph colour for IconButtons that delete: the error colour, softened to sit quietly in a row. */
	ImU32 DestructiveIconColor();

	/** @brief IconButton variant for painted (non-font) icons such as the star and flag. */
	bool PaintedIconButton(const char* id, const IconPainter& icon, ImU32 color, const char* tooltip);

	/**
	 * @brief Text clipped to maxWidth with an ellipsis; the full text shows as a tooltip when it was cut.
	 * @param maxWidth Width limit; <= 0 uses the remaining content width.
	 */
	void EllipsizedText(const char* text, float maxWidth, const ImVec4& color);

	/** @brief EllipsizedText in the secondary colour, for plugin names, form keys and other details. */
	void MetaText(const char* text, float maxWidth = 0.0f);

	/**
	 * @brief Link-coloured clickable text, ellipsized to maxWidth.
	 * @return True when clicked.
	 */
	bool Link(const char* id, const char* label, float maxWidth = 0.0f);

	/**
	 * @brief Small muted label followed by a rule to the right edge, heading a group of controls.
	 * @param alignRight Puts the label at the right edge, with the rule running to it from the left.
	 */
	void SectionLabel(const char* label, bool alignRight = false);

	/** @brief Centred hint for a page or list with nothing to show. */
	void EmptyState(const char* title, const char* hint = nullptr);

	/**
	 * @brief Thin accent bar on the left edge of the current table row. Call in column 0.
	 * @param rowHeight Full row height, including cell padding.
	 */
	void RowStripe(ImU32 color, float rowHeight);

	/** @brief Selectable flags for a list row in a modal that has a tick box drawn over its start. */
	inline constexpr ImGuiSelectableFlags kTickRowFlags = ImGuiSelectableFlags_NoAutoClosePopups |
	                                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;

	/**
	 * @brief Opens a scrolling, outlined list table with shaded alternate rows. An empty list instead shows
	 * emptyHint across a single column and closes the table again.
	 * @return True when the caller should draw rows and then call EndListTable().
	 */
	bool BeginListTable(const char* id, int columns, const ImVec2& size, bool empty, const char* emptyHint);

	/** @brief Closes a table BeginListTable left open. */
	void EndListTable();

	/** @brief Keeps the alternate row tint visible under themes that clear it. */
	class RowShadeScope
	{
	public:
		RowShadeScope();
		~RowShadeScope();

		RowShadeScope(const RowShadeScope&) = delete;
		RowShadeScope& operator=(const RowShadeScope&) = delete;
	};

	/**
	 * @brief Page title row: accent icon, title, muted subtitle. Leaves the line open so the caller
	 * can right-align actions with RightAlign().
	 */
	void PageHeader(Icons::GlyphRef icon, const char* title, const char* subtitle);

	/** @brief Moves the cursor on the current line so the next `width` pixels end at the right edge. */
	void RightAlign(float width);

	/** @brief Util::HelpMarker at the right edge of the current line, for a dialog's guide. */
	void HelpMarkerRight(const char* text);

	/** @brief What the user asked of a SelectionFooter. */
	enum class SelectionAction
	{
		None,
		SelectShown,
		Clear
	};

	/**
	 * @brief Footer of a tickable list: a select-all-shown link and, once anything is ticked, a chip counting the
	 * ticks, hidden ones included, that clears them.
	 * @param selected Every tick. @param selectedShown Ticks among the rows currently listed.
	 * @param clearTooltip Tooltip of the clear chip.
	 */
	SelectionAction SelectionFooter(size_t selected, size_t selectedShown, const char* clearTooltip);

	/** @brief Table row height that fits one frame-height control plus cell padding. */
	float RowHeight();

	/** @brief Draws one pane of a SplitPane at the given size; a zero component fills the remaining space. */
	using PaneDrawer = std::function<void(const ImVec2& size)>;

	/** @brief Layout of a SplitPane. Widths are 1080p baseline and scaled by the UI scale. */
	struct SplitPaneSpec
	{
		/// Unique ID of the pane, also the table ID of the side-by-side layout.
		const char* id;
		/// Below this width the detail pane moves under the list.
		float besideMinWidth;
		/// Initial detail width of the side-by-side layout.
		float detailWidth;
		/// List share of the height in the stacked layout, restored by the editor's layout reset.
		float defaultStackedRatio;
	};

	/**
	 * @brief A top pane above a bottom pane, split by a draggable divider; the bottom pane fills the rest.
	 * @param topRatio Top share of the height; the caller keeps it and the divider updates it.
	 * @param defaultTopRatio What the editor's layout reset restores topRatio to.
	 */
	void StackedSplit(const char* id, float& topRatio, float defaultTopRatio, const PaneDrawer& drawTop, const PaneDrawer& drawBottom);

	/**
	 * @brief A list pane next to or, when narrow, above a detail pane, with a draggable divider either way.
	 * @param stackedRatio List share of the height when stacked; the caller keeps it and the divider updates it.
	 */
	void SplitPane(const SplitPaneSpec& spec, float& stackedRatio, const PaneDrawer& drawList, const PaneDrawer& drawDetail);
}
