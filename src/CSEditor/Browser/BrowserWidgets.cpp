#include "BrowserWidgets.h"

#include "../../I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "Menu/MenuIcons.h"
#include "Utils/UI.h"
#include "imgui_internal.h"

#include "Utils/UIAnim.h"
#include "Utils/UISurface.h"
#include <algorithm>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <format>

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	// Pixel values are authored at the 1080p baseline and multiplied by Util::GetUIScale().
	constexpr float kChipPadX = 9.0f;
	constexpr float kChipGap = 6.0f;
	/// Icon box inside chips and painted buttons, relative to the font size.
	constexpr float kChipIconScale = 0.8f;
	constexpr float kToggleFillAlpha = 0.2f;
	constexpr float kToggleFillHoveredAlpha = 0.3f;
	constexpr float kToggleBorderAlpha = 0.65f;
	constexpr float kTintedChipFillAlpha = 0.14f;
	constexpr float kTintedChipFillHoveredAlpha = 0.22f;
	constexpr float kTintedChipBorderAlpha = 0.5f;
	/// Floor for the alternating row tint, so long lists stay scannable under themes that leave it clear.
	constexpr float kMinRowShadeAlpha = 0.04f;
	constexpr float kStarRadiusScale = 0.5f;
	constexpr float kFlagHeightScale = 0.85f;
	constexpr float kDotRadiusScale = 0.28f;
	constexpr float kSectionLabelGap = 8.0f;
	constexpr float kEmptyStateTopPad = 24.0f;
	constexpr float kInlineBadgeFillAlpha = 0.22f;
	constexpr float kDestructiveIconAlpha = 0.8f;
	/// How far an inline badge's label moves from the tint toward the text colour, for legibility.
	constexpr float kInlineBadgeLabelTowardText = 0.35f;

	ImU32 WithAlpha(const ImVec4& color, float alpha)
	{
		return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha));
	}

	/// Where text drawn by hand should start so it lines up with AlignTextToFramePadding'd neighbours.
	ImVec2 TextCursor(ImGuiWindow* window)
	{
		return ImVec2(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
	}

	/// Draws `text` in `color` inside [pos, pos + width], with an ellipsis when it does not fit.
	void DrawClippedText(ImDrawList* drawList, ImVec2 pos, float width, const char* text, const ImVec2& size,
		const ImVec4& color)
	{
		if (size.x <= width) {
			drawList->AddText(pos, ImGui::GetColorU32(color), text);
			return;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::RenderTextEllipsis(drawList, pos, ImVec2(pos.x + width, pos.y + size.y), pos.x + width, text, nullptr, &size);
		ImGui::PopStyleColor();
	}
}

void DrawIconStar(ImVec2 center, float radius, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	constexpr int numPoints = 5;
	const float angleStep = IM_PI / numPoints;
	ImVec2 points[10];

	for (int i = 0; i < numPoints * 2; i++) {
		float angle = -IM_PI * 0.5f + i * angleStep;
		float r = (i % 2 == 0) ? radius : radius * 0.38f;
		points[i] = ImVec2(center.x + cosf(angle) * r, center.y + sinf(angle) * r);
	}

	if (filled) {
		// Disable AA fill temporarily — ImGui adds a 1px fringe around each filled shape
		// that creates visible seams at the pentagon/triangle boundaries.
		ImDrawListFlags oldFlags = drawList->Flags;
		drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;

		ImVec2 innerPentagon[5];
		for (int i = 0; i < 5; i++) {
			innerPentagon[i] = points[i * 2 + 1];  // inner points
		}
		drawList->AddConvexPolyFilled(innerPentagon, 5, color);
		for (int i = 0; i < 5; i++) {
			drawList->AddTriangleFilled(points[i * 2], points[i * 2 + 1], points[(i * 2 + 9) % 10], color);
		}

		drawList->Flags = oldFlags;

		// Draw an AA polyline over the outer perimeter to restore smooth edges
		drawList->AddPolyline(points, 10, color, ImDrawFlags_Closed, 1.5f);
	} else {
		drawList->AddPolyline(points, 10, color, ImDrawFlags_Closed, 1.5f);
	}
}

// Pennant on a pole. The icon font only ships a solid flag, so the outline state is drawn by hand.
void DrawIconFlag(ImVec2 center, float height, ImU32 color, bool filled)
{
	auto* drawList = ImGui::GetWindowDrawList();
	const float width = height * 0.9f;
	const float thickness = std::max(1.0f, height * 0.1f);
	const ImVec2 top(center.x - width * 0.4f, center.y - height * 0.5f);
	const ImVec2 bottom(top.x, center.y + height * 0.5f);
	const ImVec2 tip(top.x + width, top.y + height * 0.25f);
	const ImVec2 hoist(top.x, top.y + height * 0.5f);

	drawList->AddLine(top, bottom, color, thickness);
	if (filled)
		drawList->AddTriangleFilled(top, tip, hoist, color);
	else
		drawList->AddTriangle(top, tip, hoist, color, thickness);
}

namespace BrowserUI
{
	IconPainter GlyphIcon(Icons::GlyphRef glyph)
	{
		return [glyph](ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
			Icons::DrawCenteredGlyph(drawList, ImVec2(center.x - size * 0.5f, center.y - size * 0.5f),
				ImVec2(size, size), glyph, color);
		};
	}

	IconPainter StarIcon(bool filled)
	{
		return [filled](ImDrawList*, ImVec2 center, float size, ImU32 color) {
			DrawIconStar(center, size * kStarRadiusScale, color, filled);
		};
	}

	IconPainter FlagIcon(bool filled)
	{
		return [filled](ImDrawList*, ImVec2 center, float size, ImU32 color) {
			DrawIconFlag(center, size * kFlagHeightScale, color, filled);
		};
	}

	IconPainter DotIcon()
	{
		return [](ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
			drawList->AddCircleFilled(center, size * kDotRadiusScale, color);
		};
	}

	bool SearchField(const char* id, char* buffer, size_t bufferSize, const char* hint, float width, bool ctrlFFocus,
		ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
	{
		ImGui::PushID(id);
		const auto& style = ImGui::GetStyle();
		const float searchScale = Util::GetSearchUIScale();
		const float iconX = ThemeManager::Constants::COMBO_SEARCH_ICON_OFFSET_X * searchScale;
		const float iconSize = Icons::CalcGlyphSize(Icons::TI(MenuIcons::kSearch)).x;
		const float padLeft = iconX + iconSize + style.ItemInnerSpacing.x;
		const float frameHeight = ImGui::GetFrameHeight();

		if (ctrlFFocus && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
			ImGui::SetKeyboardFocusHere();

		// The clear button sits over the frame's right end, so the input has to let it take the hover.
		const bool hadText = buffer[0] != '\0';
		if (hadText)
			ImGui::SetNextItemAllowOverlap();
		ImGui::SetNextItemWidth(width);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padLeft, style.FramePadding.y));
		bool changed = ImGui::InputTextWithHint("##input", hint, buffer, bufferSize,
			ImGuiInputTextFlags_EscapeClearsAll | flags, callback, userData);
		ImGui::PopStyleVar();
		const ImGuiLastItemData inputItem = ImGui::GetCurrentContext()->LastItemData;

		// Escape here belongs to the field; without this the same press would also close the editor or menu.
		if ((ImGui::IsItemActive() || ImGui::IsItemDeactivated()) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			Menu::GetSingleton()->ConsumeEscape();

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		{
			ImVec4 iconColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			iconColor.w *= ThemeManager::Constants::COMBO_SEARCH_ICON_ALPHA;
			Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(),
				ImVec2(min.x + iconX, min.y),
				ImVec2(iconSize, frameHeight), Icons::TI(MenuIcons::kSearch), ImGui::GetColorU32(iconColor));
		}

		if (buffer[0] != '\0') {
			const ImVec2 clearMin(max.x - frameHeight, min.y);
			ImGui::SetCursorScreenPos(clearMin);
			if (ImGui::InvisibleButton("##clear", ImVec2(frameHeight, frameHeight))) {
				buffer[0] = '\0';
				changed = true;
			}
			const bool hovered = ImGui::IsItemHovered();
			if (hovered)
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), clearMin, ImVec2(frameHeight, frameHeight),
				Icons::TI(ICON_TI_X), ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled));
			Util::AddTooltip(T(TKEY("search_clear"), "Clear search"));
			ImGui::GetCurrentContext()->LastItemData = inputItem;
		}

		ImGui::PopID();
		return changed;
	}

	float MeasureToggleChip(int count)
	{
		char text[16];
		std::snprintf(text, sizeof(text), "%d", count);
		const float scale = Util::GetUIScale();
		return kChipPadX * scale * 2.0f + ImGui::GetFontSize() * kChipIconScale + kChipGap * scale + ImGui::CalcTextSize(text).x;
	}

	bool ToggleChip(const char* id, const IconPainter& icon, int count, bool active, const ImVec4& tint, const char* tooltip)
	{
		char text[16];
		std::snprintf(text, sizeof(text), "%d", count);
		const float scale = Util::GetUIScale();
		const float height = ImGui::GetFrameHeight();
		const float iconBox = ImGui::GetFontSize() * kChipIconScale;

		const bool clicked = ImGui::InvisibleButton(id, ImVec2(MeasureToggleChip(count), height));
		const float hover = Util::Anim::ItemBlend(ImGui::GetItemID(), ImGui::IsItemHovered(), false, false).hover;
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float rounding = height * 0.5f;

		if (active) {
			drawList->AddRectFilled(min, max, WithAlpha(tint, kToggleFillAlpha + (kToggleFillHoveredAlpha - kToggleFillAlpha) * hover), rounding);
			drawList->AddRect(min, max, WithAlpha(tint, kToggleBorderAlpha), rounding, 0, std::max(1.0f, scale));
		} else {
			drawList->AddRectFilled(min, max, Util::Anim::LerpU32(ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetColorU32(ImGuiCol_FrameBgHovered), hover), rounding);
		}

		const ImU32 secondary = ImGui::GetColorU32(Util::Colors::GetSecondary());
		const float centerY = (min.y + max.y) * 0.5f;
		const float iconX = min.x + kChipPadX * scale;
		if (icon)
			icon(drawList, ImVec2(iconX + iconBox * 0.5f, centerY), iconBox, active ? ImGui::GetColorU32(tint) : secondary);
		drawList->AddText(ImVec2(iconX + iconBox + kChipGap * scale, Util::CenterTextTopY(min.y, max.y - min.y)),
			active ? ImGui::GetColorU32(ImGuiCol_Text) : secondary, text);

		if (tooltip)
			Util::AddTooltip(tooltip);
		return clicked;
	}

	float MeasureChip(const char* label, bool withIcon)
	{
		const float scale = Util::GetUIScale();
		float width = kChipPadX * scale * 2.0f + ImGui::CalcTextSize(label).x;
		if (withIcon)
			width += ImGui::GetFontSize() * kChipIconScale + kChipGap * scale;
		return width;
	}

	bool Chip(const char* id, const char* label, Icons::GlyphRef icon, const ImVec4* iconColor, const ImVec4* tint)
	{
		const float scale = Util::GetUIScale();
		const float height = ImGui::GetFrameHeight();
		const float iconBox = ImGui::GetFontSize() * kChipIconScale;

		const bool clicked = ImGui::InvisibleButton(id, ImVec2(MeasureChip(label, icon.IsValid()), height));
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float rounding = height * 0.5f;

		const float hover = Util::Anim::ItemBlend(ImGui::GetItemID(), hovered, false, false).hover;
		if (tint) {
			drawList->AddRectFilled(min, max, WithAlpha(*tint, kTintedChipFillAlpha + (kTintedChipFillHoveredAlpha - kTintedChipFillAlpha) * hover), rounding);
			drawList->AddRect(min, max, WithAlpha(*tint, kTintedChipBorderAlpha), rounding, 0, std::max(1.0f, scale));
		} else {
			drawList->AddRectFilled(min, max, Util::Anim::LerpU32(ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetColorU32(ImGuiCol_FrameBgHovered), hover), rounding);
		}

		const float centerY = (min.y + max.y) * 0.5f;
		float x = min.x + kChipPadX * scale;
		if (icon) {
			const ImU32 color = iconColor ? ImGui::GetColorU32(*iconColor) : ImGui::GetColorU32(Util::Colors::GetSecondary());
			Icons::DrawCenteredGlyph(drawList, ImVec2(x, centerY - iconBox * 0.5f), ImVec2(iconBox, iconBox), icon, color);
			x += iconBox + kChipGap * scale;
		}
		drawList->AddText(ImVec2(x, Util::CenterTextTopY(min.y, max.y - min.y)), ImGui::GetColorU32(ImGuiCol_Text), label);
		if (hovered)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		return clicked;
	}

	bool FilterChip(const char* id, const ChipFace& face, bool selected)
	{
		const ImVec4 accent = Util::Colors::GetAccent();
		return Chip(id, face.label, face.icon, face.iconColor ? &*face.iconColor : nullptr, selected ? &accent : nullptr);
	}

	void SameLineIfFits(float width)
	{
		ImGui::SameLine();
		if (ImGui::GetContentRegionAvail().x < width)
			ImGui::NewLine();
	}

	void InlineBadge(const char* label, const ImVec4& tint, float height)
	{
		ImGui::SameLine();
		const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		const ImVec4 bg(tint.x, tint.y, tint.z, kInlineBadgeFillAlpha);
		const ImVec4 fg(tint.x + (text.x - tint.x) * kInlineBadgeLabelTowardText,
			tint.y + (text.y - tint.y) * kInlineBadgeLabelTowardText,
			tint.z + (text.z - tint.z) * kInlineBadgeLabelTowardText, 1.0f);
		if (height <= 0.0f)
			height = ImGui::GetFrameHeight();
		const float width = Util::DrawBadgeAt(ImGui::GetCursorScreenPos(), height, label, bg, fg);
		ImGui::Dummy(ImVec2(width, height));
	}

	float IconButtonSize()
	{
		return ImGui::GetFrameHeight();
	}

	namespace
	{
		/// What QuietButton reports: the click and the eased hover / press / active state.
		struct QuietState
		{
			bool clicked;
			Util::Anim::Blend blend;
		};

		/// Shared chrome for the quiet icon buttons: a fill that eases in and out with hover, press and active.
		QuietState QuietButton(const char* id, bool active)
		{
			const float size = IconButtonSize();
			const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
			if (ImGui::IsItemHovered())
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			const auto blend = Util::Anim::ItemBlend(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::IsItemActive(), active);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			const float rounding = ImGui::GetStyle().FrameRounding;
			Util::Anim::FillBlend(drawList, min, max, rounding, blend);
			return { clicked, blend };
		}
	}

	bool IconButton(const char* id, Icons::GlyphRef glyph, const char* tooltip, bool active, ImU32 color)
	{
		const auto [clicked, blend] = QuietButton(id, active);
		ImU32 glyphColor = color;
		if (!glyphColor) {
			glyphColor = Util::QuietIconColor(blend.hover, blend.selected);
		}
		const float size = IconButtonSize();
		Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImVec2(size, size), glyph, glyphColor);
		if (tooltip)
			Util::AddTooltip(tooltip, Util::kTooltipWhenDisabled);
		return clicked;
	}

	ImU32 DestructiveIconColor()
	{
		return WithAlpha(Util::Colors::GetError(), kDestructiveIconAlpha);
	}

	bool PaintedIconButton(const char* id, const IconPainter& icon, ImU32 color, const char* tooltip)
	{
		const auto clicked = QuietButton(id, false).clicked;
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		if (icon)
			icon(ImGui::GetWindowDrawList(), ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
				ImGui::GetFontSize() * kChipIconScale, color);
		if (tooltip)
			Util::AddTooltip(tooltip, Util::kTooltipWhenDisabled);
		return clicked;
	}

	void EllipsizedText(const char* text, float maxWidth, const ImVec4& color)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return;
		if (!text)
			text = "";
		if (maxWidth <= 0.0f)
			maxWidth = ImGui::GetContentRegionAvail().x;
		maxWidth = std::max(maxWidth, 1.0f);

		const ImVec2 size = ImGui::CalcTextSize(text);
		const ImVec2 pos = TextCursor(window);
		const float width = std::min(size.x, maxWidth);
		const ImRect bb(pos, ImVec2(pos.x + width, pos.y + size.y));
		ImGui::ItemSize(ImVec2(width, size.y), 0.0f);
		if (!ImGui::ItemAdd(bb, 0))
			return;

		DrawClippedText(window->DrawList, pos, width, text, size, color);
		if (size.x > maxWidth && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", text);
	}

	void MetaText(const char* text, float maxWidth)
	{
		EllipsizedText(text, maxWidth, Util::Colors::GetSecondary());
	}

	bool Link(const char* id, const char* label, float maxWidth)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;
		if (maxWidth <= 0.0f)
			maxWidth = ImGui::GetContentRegionAvail().x;
		maxWidth = std::max(maxWidth, 1.0f);

		const ImVec2 size = ImGui::CalcTextSize(label);
		const ImVec2 pos = TextCursor(window);
		const float width = std::min(size.x, maxWidth);
		const ImRect bb(pos, ImVec2(pos.x + width, pos.y + size.y));
		const ImGuiID itemId = window->GetID(id);
		ImGui::ItemSize(ImVec2(width, size.y), 0.0f);
		if (!ImGui::ItemAdd(bb, itemId))
			return false;

		bool hovered = false;
		bool held = false;
		const bool pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);
		DrawClippedText(window->DrawList, pos, width, label, size, ImGui::GetStyleColorVec4(ImGuiCol_TextLink));
		if (hovered) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			window->DrawList->AddLine(ImVec2(bb.Min.x, bb.Max.y), bb.Max, ImGui::GetColorU32(ImGuiCol_TextLink));
			if (size.x > maxWidth)
				ImGui::SetTooltip("%s", label);
		}
		return pressed;
	}

	void SectionLabel(const char* label, bool alignRight)
	{
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float right = start.x + ImGui::GetContentRegionAvail().x;
		if (alignRight)
			ImGui::SetCursorScreenPos(ImVec2(std::max(start.x, right - ImGui::CalcTextSize(label).x), start.y));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextUnformatted(label);
		ImGui::PopStyleColor();

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float gap = kSectionLabelGap * Util::GetUIScale();
		const float x0 = alignRight ? start.x : max.x + gap;
		const float x1 = alignRight ? min.x - gap : right;
		if (x0 < x1) {
			const float y = (min.y + max.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(x1, y), ImGui::GetColorU32(ImGuiCol_Separator));
		}
	}

	void EmptyState(const char* title, const char* hint)
	{
		ImGui::Dummy(ImVec2(0.0f, kEmptyStateTopPad * Util::GetUIScale()));
		const float avail = ImGui::GetContentRegionAvail().x;
		const float wrapPosX = ImGui::GetCursorPosX() + avail;
		auto centredLine = [avail, wrapPosX](const char* text, const ImVec4& color) {
			const float width = ImGui::CalcTextSize(text).x;
			if (width < avail)
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::PushTextWrapPos(wrapPosX);
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		};
		if (title)
			centredLine(title, Util::Colors::GetSecondary());
		if (hint)
			centredLine(hint, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}

	void InsetRowRect(ImVec2& min, ImVec2& max)
	{
		const auto& style = ImGui::GetStyle();
		const float padLeft = IM_TRUNC(style.ItemSpacing.x * 0.5f);
		const float halfGap = style.ItemInnerSpacing.x * 0.5f;
		min = ImVec2(ImGui::GetItemRectMin().x + padLeft, ImGui::GetItemRectMin().y + halfGap);
		max = ImVec2(ImGui::GetItemRectMax().x - (style.ItemSpacing.x - padLeft), ImGui::GetItemRectMax().y - halfGap);
	}

	void RowStripe(ImU32 color, float rowHeight)
	{
		ImGuiTable* table = ImGui::GetCurrentTable();
		if (!table)
			return;
		const float x0 = ImGui::TableGetCellBgRect(table, table->CurrentColumn).Min.x;
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x0, table->RowPosY1),
			ImVec2(x0 + ThemeManager::Constants::ANIM_SELECTED_BAR_WIDTH * Util::GetUIScale(), table->RowPosY1 + rowHeight), color);
	}

	namespace
	{
		void PushRowShade()
		{
			ImVec4 shade = ImGui::GetStyleColorVec4(ImGuiCol_TableRowBgAlt);
			if (shade.w < kMinRowShadeAlpha) {
				shade = ImGui::GetStyleColorVec4(ImGuiCol_Text);
				shade.w = kMinRowShadeAlpha;
			}
			ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, shade);
		}
	}

	RowShadeScope::RowShadeScope()
	{
		PushRowShade();
	}

	RowShadeScope::~RowShadeScope()
	{
		ImGui::PopStyleColor();
	}

	bool BeginListTable(const char* id, int columns, const ImVec2& size, bool empty, const char* emptyHint)
	{
		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;
		PushRowShade();
		// The hint takes a single column so it centres across the whole table.
		if (!ImGui::BeginTable(id, empty ? 1 : columns, kFlags, size)) {
			ImGui::PopStyleColor();
			return false;
		}
		if (!empty)
			return true;
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		EmptyState(emptyHint);
		EndListTable();
		return false;
	}

	void EndListTable()
	{
		ImGui::EndTable();
		ImGui::PopStyleColor();
	}

	void PageHeader(Icons::GlyphRef icon, const char* title, const char* subtitle)
	{
		ImGui::AlignTextToFramePadding();
		if (icon) {
			Icons::FontGuard font(icon);
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetAccent());
			ImGui::TextUnformatted(icon.utf8);
			ImGui::PopStyleColor();
			ImGui::SameLine();
		}
		{
			MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
			ImGui::TextUnformatted(title);
		}
		if (subtitle && subtitle[0]) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextUnformatted(subtitle);
			ImGui::PopStyleColor();
		}
	}

	void RightAlign(float width)
	{
		ImGui::SameLine();
		const float target = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
		if (target > ImGui::GetCursorPosX())
			ImGui::SetCursorPosX(target);
	}

	void HelpMarkerRight(const char* text)
	{
		RightAlign(Util::HelpMarkerWidth());
		Util::HelpMarker(text);
	}

	SelectionAction SelectionFooter(size_t selected, size_t selectedShown, const char* clearTooltip)
	{
		assert(selectedShown <= selected);
		auto action = SelectionAction::None;
		ImGui::AlignTextToFramePadding();
		if (Link("##selectShown", T(TKEY("scene_copy_select_shown"), "Select all shown")))
			action = SelectionAction::SelectShown;
		if (selected == 0)
			return action;

		const size_t hidden = selected - selectedShown;
		const auto label = hidden == 0 ? std::vformat(T(TKEY("scene_copy_selected"), "{} selected"), std::make_format_args(selected)) :
		                                 std::vformat(T(TKEY("scene_copy_selected_hidden"), "{} selected · {} hidden"),
											 std::make_format_args(selected, hidden));
		const ImVec4 accent = Util::Colors::GetAccent();
		RightAlign(MeasureChip(label.c_str(), true));
		if (Chip("##clearTicks", label.c_str(), Icons::TI(ICON_TI_X), nullptr, &accent))
			action = SelectionAction::Clear;
		Util::AddTooltip(clearTooltip);
		return action;
	}

	bool BeginCard(const char* id, const ImVec2& size, ImGuiChildFlags flags)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
		const bool visible = ImGui::BeginChild(id, size, flags | ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleColor();

		// The section card's surface, so these follow the theme like the menu's cards
		Util::Surface::Options surface;
		surface.rounding = ImGui::GetStyle().ChildRounding;
		surface.clip = Util::Surface::ParentClipRect();
		const ImVec2 min = ImGui::GetWindowPos();
		const ImVec2 cardSize = ImGui::GetWindowSize();
		Util::Surface::Draw(ImGui::GetWindowDrawList(), min, ImVec2(min.x + cardSize.x, min.y + cardSize.y), Util::Surface::Role::SectionCard, surface);
		return visible;
	}

	float RowHeight()
	{
		return ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
	}

	void StackedSplit(const char* id, float& topRatio, float defaultTopRatio, bool resetLayout, const PaneDrawer& drawTop,
		const PaneDrawer& drawBottom)
	{
		constexpr float kSplitterThickness = 6.0f;
		constexpr float kMinPaneHeight = 80.0f;

		const float scale = Util::GetUIScale();
		if (resetLayout)
			topRatio = defaultTopRatio;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float thickness = kSplitterThickness * scale;
		const float minPaneHeight = kMinPaneHeight * scale;
		float topHeight = std::clamp(avail.y * topRatio, minPaneHeight, std::max(minPaneHeight, avail.y - minPaneHeight - thickness));
		drawTop(ImVec2(avail.x, topHeight));

		const ImVec2 splitterPos = ImGui::GetCursorScreenPos();
		float bottomHeight = ImGui::GetContentRegionAvail().y - thickness;
		const ImRect splitterBounds(splitterPos, ImVec2(splitterPos.x + avail.x, splitterPos.y + thickness));
		ImGui::PushID(id);
		if (ImGui::SplitterBehavior(splitterBounds, ImGui::GetID("##StackedSplit"), ImGuiAxis_Y, &topHeight, &bottomHeight,
				minPaneHeight, minPaneHeight, 0.0f, 0.0f, ImGui::GetColorU32(ImGuiCol_Separator)))
			topRatio = topHeight / avail.y;
		ImGui::PopID();
		ImGui::Dummy(ImVec2(avail.x, thickness));
		drawBottom(ImVec2(avail.x, 0.0f));
	}

	void SplitPane(const SplitPaneSpec& spec, float& stackedRatio, const PaneDrawer& drawList, const PaneDrawer& drawDetail)
	{
		const float scale = Util::GetUIScale();
		const ImVec2 avail = ImGui::GetContentRegionAvail();

		if (avail.x < spec.besideMinWidth * scale) {
			StackedSplit(spec.id, stackedRatio, spec.defaultStackedRatio, spec.resetLayout, drawList, drawDetail);
			return;
		}

		// The split is a two-column table so the divider is a native, remembered resize handle.
		const auto& style = ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, 0.0f));
		const bool split = ImGui::BeginTable(spec.id, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, avail);
		ImGui::PopStyleVar();
		if (!split)
			return;
		ImGui::TableSetupColumn("##List", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("##Detail", ImGuiTableColumnFlags_WidthFixed, spec.detailWidth * scale);
		// Must precede TableNextRow: the first row locks the layout, after which widths are ignored.
		if (spec.resetLayout)
			ImGui::TableSetColumnWidth(1, spec.detailWidth * scale);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		drawList(ImVec2(0.0f, ImGui::GetContentRegionAvail().y));
		ImGui::TableSetColumnIndex(1);
		drawDetail(ImVec2(0.0f, 0.0f));
		ImGui::EndTable();
	}
}

#undef I18N_KEY_PREFIX
