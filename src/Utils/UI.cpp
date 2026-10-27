#include "UI.h"

#include "../CSEditor/EditorWindow.h"
#include "../I18n/I18n.h"
#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "D3D.h"
#include "FileSystem.h"
#include "Format.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/IconLoader.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/MenuIcons.h"
#include "Menu/ThemeManager.h"
#include "PerfUtils.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/UIAnim.h"
#include "Utils/UISurface.h"

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION 0x0800
#endif
#include <DirectXTex.h>
#include <d3d11.h>
#include <dinput.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <wrl/client.h>

#include "../Feature.h"
#include "../Globals.h"
#include "../Menu.h"
#include "FileSystem.h"

#define STB_IMAGE_IMPLEMENTATION
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <iomanip>
#include <mutex>
#include <numbers>
#include <sstream>
#include <stb_image.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Util
{
	using C = ThemeManager::Constants;

	static ImVec2 g_screenScaleRatio = { 1.0f, 1.0f };
	static ImVec2 g_displaySize = { 0.0f, 0.0f };

	static int g_lastWindowWidth = 0;
	static int g_lastWindowHeight = 0;

	void RefreshScreenScale(HWND hwnd, float bufferWidth, float bufferHeight)
	{
		RECT rect{};
		if (!GetClientRect(hwnd, &rect) || rect.right <= 0 || rect.bottom <= 0)
			return;

		if (rect.right == g_lastWindowWidth && rect.bottom == g_lastWindowHeight)
			return;

		g_displaySize.x = bufferWidth;
		g_displaySize.y = bufferHeight;

		g_screenScaleRatio.x = bufferWidth / static_cast<float>(rect.right);
		g_screenScaleRatio.y = bufferHeight / static_cast<float>(rect.bottom);

		g_lastWindowWidth = rect.right;
		g_lastWindowHeight = rect.bottom;
	}

	void UpdateImGuiInput(HWND hwnd, float bufferWidth, float bufferHeight)
	{
		RefreshScreenScale(hwnd, bufferWidth, bufferHeight);

		auto& io = ImGui::GetIO();
		io.DisplaySize = g_displaySize;

		POINT cursorPos{};
		if (GetCursorPos(&cursorPos) &&
			ScreenToClient(hwnd, &cursorPos)) {
			io.AddMousePosEvent(
				static_cast<float>(cursorPos.x) * g_screenScaleRatio.x,
				static_cast<float>(cursorPos.y) * g_screenScaleRatio.y);
		}
	}

	HoverTooltipWrapper::HoverTooltipWrapper() :
		previousFont(nullptr)
	{
		hovered = ImGui::IsItemHovered(kTooltipWhenDisabled);
		if (hovered) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * ThemeManager::Constants::TOOLTIP_WRAP_FONT_SIZES);
			// Apply Subtext font for consistent tooltip styling
			if (auto* menu = globals::menu) {
				if (auto* subtextFont = menu->GetFont(Menu::FontRole::Subtext)) {
					previousFont = ImGui::GetFont();
					ImGui::PushFont(subtextFont, subtextFont->LegacySize);
				}
			}
		}
	}

	HoverTooltipWrapper::~HoverTooltipWrapper()
	{
		if (hovered) {
			if (previousFont) {
				ImGui::PopFont();
			}
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	CenteredPopupModal::CenteredPopupModal(const char* name, bool* p_open, ImGuiWindowFlags flags, ImVec2 pos, ImVec2 pivot)
	{
		if (pos.x == -FLT_MAX && pos.y == -FLT_MAX)
			pos = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
		// Fix first-frame vertical stretch: AlwaysAutoResize resets width to 0 on the hidden
		// measurement frame, causing TextWrapped to wrap at 0px and produce an enormous height.
		// Setting an initial width gives TextWrapped a sensible wrap column on that frame.
		ImGui::SetNextWindowSize(ImVec2(400.0f * GetUIScale(), 0.0f), ImGuiCond_Appearing);
		isOpen = BeginPopupModalWithRoundedClose(name, p_open, flags | ImGuiWindowFlags_NoSavedSettings);
	}

	CenteredPopupModal::~CenteredPopupModal()
	{
		if (isOpen)
			ImGui::EndPopup();
	}

	DisableGuard::DisableGuard(bool disable) :
		disable(disable)
	{
		if (disable)
			ImGui::BeginDisabled();
	}
	DisableGuard::~DisableGuard()
	{
		if (disable)
			ImGui::EndDisabled();
	}

	bool InteractionEndedThisFrame()
	{
		// NewFrame copies ActiveId into ActiveIdPreviousFrame, so a change since then is an item letting go.
		const auto& context = *ImGui::GetCurrentContext();
		return context.ActiveIdPreviousFrame != 0 && context.ActiveId != context.ActiveIdPreviousFrame;
	}

	namespace
	{
		bool debugSectionsHidden = false;
	}

	HideDebugSectionsGuard::HideDebugSectionsGuard(bool hide) :
		previous(std::exchange(debugSectionsHidden, hide))
	{}

	HideDebugSectionsGuard::~HideDebugSectionsGuard()
	{
		debugSectionsHidden = previous;
	}

	bool ShowDebugSections()
	{
		return !debugSectionsHidden;
	}

	void StepFade(float& fade, bool visible, float deltaTime)
	{
		const float step = deltaTime / ThemeManager::Constants::UI_FADE_SECONDS;
		fade = std::clamp(fade + (visible ? step : -step), 0.0f, 1.0f);
	}

	FadeScope::FadeScope(float fade) :
		context(*ImGui::GetCurrentContext()),
		hoveredWindow(context.HoveredWindow),
		hoveredWindowUnderMoving(context.HoveredWindowUnderMovingWindow)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * fade);
		if (fade < 1.0f)
			context.HoveredWindow = context.HoveredWindowUnderMovingWindow = nullptr;
	}

	FadeScope::~FadeScope()
	{
		context.HoveredWindow = hoveredWindow;
		context.HoveredWindowUnderMovingWindow = hoveredWindowUnderMoving;
		ImGui::PopStyleVar();
	}

	void TextUnformattedDisabled(const char* a_text, const char* a_textEnd)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextUnformatted(a_text, a_textEnd);
		ImGui::PopStyleColor();
	}

	bool TableRowSelectable(const char* label, bool selected, ImGuiSelectableFlags flags)
	{
		return ImGui::Selectable(label, selected, flags, ImVec2(0, ImGui::GetFrameHeight()));
	}

	void SetTooltipPositionNearMouse(float estimatedHeight, float estimatedWidth)
	{
		const ImVec2 mousePos = ImGui::GetMousePos();
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		constexpr float kTooltipOffsetX = 16.0f;
		constexpr float kTooltipOffsetY = 12.0f;

		const float viewportLeft = viewport->WorkPos.x;
		const float viewportRight = viewport->WorkPos.x + viewport->WorkSize.x;
		const float viewportTop = viewport->WorkPos.y;
		const float viewportBottom = viewport->WorkPos.y + viewport->WorkSize.y;

		// Vertical: flip above cursor when it would overflow the bottom.
		const bool placeAboveCursor = (mousePos.y + kTooltipOffsetY + estimatedHeight) > viewportBottom;
		float posY;
		float pivotY;
		if (placeAboveCursor) {
			const float tentativeTopY = mousePos.y - kTooltipOffsetY - estimatedHeight;
			posY = (tentativeTopY < viewportTop) ? (viewportTop + estimatedHeight) : (mousePos.y - kTooltipOffsetY);
			pivotY = 1.0f;
		} else {
			posY = mousePos.y + kTooltipOffsetY;
			pivotY = 0.0f;
		}

		// Horizontal: clamp so the tooltip stays within viewport bounds.
		float posX = mousePos.x + kTooltipOffsetX;
		if (estimatedWidth > 0.0f) {
			const float maxX = viewportRight - estimatedWidth;
			posX = ImMax(viewportLeft, ImMin(posX, maxX));
		}

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always, ImVec2(0.0f, pivotY));
	}

	void AddTooltip(const char* a_desc, ImGuiHoveredFlags a_flags)
	{
		if (ImGui::IsItemHovered(a_flags)) {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8, 8 });
			const ImVec2 pad = ImGui::GetStyle().WindowPadding;
			const float wrapWidth = ImGui::GetFontSize() * 50.0f;
			const ImVec2 wrappedTextSize = ImGui::CalcTextSize(a_desc, nullptr, false, wrapWidth);
			const float estimatedTooltipHeight = wrappedTextSize.y + pad.y * 2.0f;
			const float estimatedTooltipWidth = wrappedTextSize.x + pad.x * 2.0f;
			SetTooltipPositionNearMouse(estimatedTooltipHeight, estimatedTooltipWidth);

			if (ImGui::BeginTooltip()) {
				ImGui::PushTextWrapPos(wrapWidth);
				ImGui::TextUnformatted(a_desc);
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::PopStyleVar();
		}
	}

	void HelpMarker(const char* a_desc)
	{
		ImGui::AlignTextToFramePadding();
		TextUnformattedDisabled(kHelpMarkerLabel);
		AddTooltip(a_desc, ImGuiHoveredFlags_DelayShort);
	}

	namespace
	{
		struct GlassSegmentPalette
		{
			ImVec4 track;
			ImVec4 selectedFill;
			ImVec4 selectedText;
			ImVec4 idleText;
			ImVec4 hoverFill;
			float rounding = 0.0f;
			float inset = 0.0f;
			float segmentHeight = 0.0f;
			float trackHeight = 0.0f;
		};

		GlassSegmentPalette MakeGlassSegmentPalette(bool muted = false)
		{
			const auto& style = ImGui::GetStyle();
			GlassSegmentPalette p;
			p.trackHeight = ImGui::GetFrameHeight();
			p.inset = std::max(2.0f, std::floor(style.FramePadding.y * 0.55f));
			p.segmentHeight = p.trackHeight - p.inset * 2.0f;
			p.rounding = std::max(style.FrameRounding, p.trackHeight * 0.5f);

			// Translucent mica-style track over the blurred panel.
			p.track = ImVec4(1.0f, 1.0f, 1.0f, C::GLASS_TRACK_IDLE_ALPHA);
			const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
			if (frame.w > 0.01f) {
				p.track.x = frame.x * C::GLASS_TRACK_FRAME_MIX + (1.0f - C::GLASS_TRACK_FRAME_MIX);
				p.track.y = frame.y * C::GLASS_TRACK_FRAME_MIX + (1.0f - C::GLASS_TRACK_FRAME_MIX);
				p.track.z = frame.z * C::GLASS_TRACK_FRAME_MIX + (1.0f - C::GLASS_TRACK_FRAME_MIX);
				p.track.w = std::clamp(frame.w * C::GLASS_TRACK_ALPHA_SCALE + C::GLASS_TRACK_ALPHA_MIN, C::GLASS_TRACK_ALPHA_MIN, C::GLASS_TRACK_ALPHA_MAX);
			}

			p.selectedFill = ImGui::GetStyleColorVec4(ImGuiCol_Header);
			p.selectedFill.w = std::clamp(std::max(p.selectedFill.w, C::GLASS_SELECTED_ALPHA_FLOOR) * C::GLASS_SELECTED_ALPHA_SCALE, C::GLASS_SELECTED_ALPHA_MIN, C::GLASS_SELECTED_ALPHA_MAX);
			p.selectedText = style.Colors[ImGuiCol_Text];
			p.idleText = Colors::GetSecondary();
			p.hoverFill = p.selectedFill;
			p.hoverFill.w *= C::GLASS_HOVER_ALPHA_SCALE;

			if (muted) {
				p.selectedFill.w *= C::GLASS_MUTED_SELECTED_SCALE;
				p.selectedText = p.idleText;
				p.idleText.w *= C::GLASS_MUTED_TEXT_SCALE;
			}
			return p;
		}

		std::string_view PillTabDisplayLabel(const char* label)
		{
			std::string_view view(label ? label : "");
			if (const auto hash = view.find("##"); hash != std::string_view::npos)
				view = view.substr(0, hash);
			return view;
		}

		struct PillTabBarState
		{
			ImGuiID id = 0;
			int selected = 0;
			int submitIndex = 0;
			std::vector<std::string> labelsThisFrame;
			std::vector<std::string> labelsForStrip;
		};

		std::unordered_map<ImGuiID, PillTabBarState> g_pillTabBars;
		std::vector<ImGuiID> g_pillTabStack;

		PillTabBarState* CurrentPillTabBar()
		{
			if (g_pillTabStack.empty())
				return nullptr;
			auto it = g_pillTabBars.find(g_pillTabStack.back());
			return it != g_pillTabBars.end() ? &it->second : nullptr;
		}

		/** @brief Draws the glass track + pills. Returns true if the selection changed. */
		bool DrawGlassPillStrip(const char* id, const std::vector<std::string>& labels, int& selected, int marked = -1, bool muted = false)
		{
			if (labels.empty())
				return false;

			const auto palette = MakeGlassSegmentPalette(muted);
			const auto& style = ImGui::GetStyle();
			const float padX = std::max(C::GLASS_SEGMENT_MIN_PAD_X * GetUIScale(), style.FramePadding.x * C::GLASS_SEGMENT_PAD_X_SCALE);

			float contentWidth = palette.inset * static_cast<float>(labels.size() + 1);
			for (const auto& label : labels)
				contentWidth += ImGui::CalcTextSize(label.c_str()).x + padX * 2.0f;

			const float avail = ImGui::GetContentRegionAvail().x;
			const bool needScroll = contentWidth > avail + 0.5f;
			const float trackWidth = needScroll ? contentWidth : std::min(contentWidth, avail);

			if (needScroll) {
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
				ImGui::BeginChild(std::format("##PillStripScroll{}", id).c_str(), ImVec2(avail, palette.trackHeight),
					ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			}

			const ImVec2 trackMin = ImGui::GetCursorScreenPos();
			const ImVec2 trackMax{ trackMin.x + trackWidth, trackMin.y + palette.trackHeight };
			auto* drawList = ImGui::GetWindowDrawList();

			drawList->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(palette.track), palette.rounding);
			drawList->AddRect(trackMin, trackMax, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.10f)), palette.rounding);

			ImGui::PushID(id);
			ImGui::BeginGroup();
			ImGui::SetCursorScreenPos({ trackMin.x + palette.inset, trackMin.y + palette.inset });
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { palette.inset, 0.0f });
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { padX, (palette.segmentHeight - ImGui::GetFontSize()) * 0.5f });
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::max(0.0f, palette.rounding - palette.inset));

			// The selected pill is one shape that glides to the chosen tab; the buttons themselves stay clear.
			// Its rectangle follows from the label widths, since the buttons are laid out in a row.
			if (selected >= 0 && selected < static_cast<int>(labels.size())) {
				float selectedX = trackMin.x + palette.inset;
				for (int i = 0; i < selected; ++i)
					selectedX += ImGui::CalcTextSize(labels[static_cast<size_t>(i)].c_str()).x + padX * 2.0f + palette.inset;
				const float selectedWidth = ImGui::CalcTextSize(labels[static_cast<size_t>(selected)].c_str()).x + padX * 2.0f;
				const auto pill = Anim::SelectionGlide(ImGui::GetID("pillGlide"), ImVec2(selectedX, trackMin.y + palette.inset),
					ImVec2(selectedX + selectedWidth, trackMin.y + palette.inset + palette.segmentHeight));
				drawList->AddRectFilled(pill.min, pill.max, ImGui::GetColorU32(palette.selectedFill), std::max(0.0f, palette.rounding - palette.inset));
			}
			ImVec4 clearFill = palette.hoverFill;
			clearFill.w = 0.0f;

			bool changed = false;
			for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
				if (i > 0)
					ImGui::SameLine();
				const bool isSelected = i == selected;
				ImGui::PushID(i);
				ImGui::PushStyleColor(ImGuiCol_Button, clearFill);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? clearFill : palette.hoverFill);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, clearFill);
				ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? palette.selectedText : palette.idleText);
				if (ImGui::Button(labels[i].c_str())) {
					selected = i;
					changed = true;
				}
				ImGui::PopStyleColor(4);

				if (i == marked && !isSelected) {
					const ImVec2 min = ImGui::GetItemRectMin();
					const ImVec2 max = ImGui::GetItemRectMax();
					const float radius = std::max(C::GLASS_MARK_MIN_RADIUS, ImGui::GetFontSize() * C::GLASS_MARK_RADIUS_RATIO);
					drawList->AddCircleFilled({ max.x - padX * 0.35f, (min.y + max.y) * 0.5f }, radius,
						ImGui::GetColorU32(Colors::GetAccent()));
				}
				ImGui::PopID();
			}

			ImGui::SameLine(0.0f, 0.0f);
			ImGui::Dummy({ palette.inset, palette.segmentHeight });
			ImGui::Dummy({ 0.0f, palette.inset });
			ImGui::PopStyleVar(4);
			ImGui::EndGroup();
			ImGui::PopID();

			if (needScroll) {
				if (ImGui::IsWindowHovered() && std::abs(ImGui::GetIO().MouseWheel) > 0.0f)
					ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 40.0f);
				ImGui::EndChild();
				ImGui::PopStyleVar();
			}

			return changed;
		}
	}  // namespace

	bool SegmentedControl(const char* a_id, const char* const* a_labels, int a_count, int& a_selected, int a_marked,
		bool a_muted)
	{
		if (!a_labels || a_count <= 0)
			return false;
		std::vector<std::string> labels;
		labels.reserve(static_cast<size_t>(a_count));
		for (int i = 0; i < a_count; ++i)
			labels.emplace_back(a_labels[i] ? a_labels[i] : "");
		return SceneWidgetInterceptor::InterceptInt(a_id, a_selected,
			[&](int& a_bound) { return DrawGlassPillStrip(a_id, labels, a_bound, a_marked, a_muted); });
	}

	bool IsInsidePillTabBar()
	{
		return !g_pillTabStack.empty();
	}

	bool BeginPillTabBar(const char* str_id)
	{
		const ImGuiID id = ImGui::GetID(str_id);
		auto& state = g_pillTabBars[id];
		state.id = id;
		state.submitIndex = 0;
		state.labelsThisFrame.clear();
		g_pillTabStack.push_back(id);

		{
			MenuFonts::FontRoleGuard bodyFont(Menu::FontRole::Body);
			DrawGlassPillStrip(str_id, state.labelsForStrip, state.selected);
		}
		ImGui::Spacing();
		return true;
	}

	bool BeginPillTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags)
	{
		auto* state = CurrentPillTabBar();
		if (!state)
			return false;

		if (p_open && !*p_open)
			return false;

		const int idx = state->submitIndex++;
		state->labelsThisFrame.emplace_back(PillTabDisplayLabel(label));

		if (flags & ImGuiTabItemFlags_SetSelected)
			state->selected = idx;

		if (state->selected < 0 || (state->labelsForStrip.empty() && idx == 0))
			state->selected = 0;

		return state->selected == idx;
	}

	void EndPillTabItem()
	{
	}

	void EndPillTabBar()
	{
		auto* state = CurrentPillTabBar();
		if (!state)
			return;

		state->labelsForStrip = state->labelsThisFrame;
		if (!state->labelsForStrip.empty())
			state->selected = std::clamp(state->selected, 0, static_cast<int>(state->labelsForStrip.size()) - 1);
		else
			state->selected = 0;

		g_pillTabStack.pop_back();
	}

	bool StatusBanner(const char* a_icon, const char* a_message, const ImVec4& a_color)
	{
		const float scale = GetUIScale();
		const ImVec2 padding{ 12.0f * scale, 8.0f * scale };
		const float gap = C::STATUS_BANNER_GAP * scale;
		const float width = ImGui::GetContentRegionAvail().x;
		const Icons::GlyphRef icon = Icons::TI(a_icon);
		const float iconWidth = Icons::CalcGlyphSize(icon).x;
		const float wrapWidth = std::max(1.0f, width - padding.x * 2.0f - iconWidth - gap);
		const ImVec2 textSize = ImGui::CalcTextSize(a_message, nullptr, false, wrapWidth);
		const float height = textSize.y + padding.y * 2.0f;

		const ImVec2 min = ImGui::GetCursorScreenPos();
		ImVec4 fill = a_color;
		fill.w *= ThemeManager::Constants::STATUS_BANNER_FILL_ALPHA;
		auto* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(min, { min.x + width, min.y + height }, ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);
		Icons::DrawCenteredGlyph(drawList, { min.x + padding.x, min.y + padding.y },
			{ iconWidth, textSize.y }, icon, ImGui::GetColorU32(a_color));

		ImGui::SetCursorScreenPos({ min.x + padding.x + iconWidth + gap, min.y + padding.y });
		ImGui::PushStyleColor(ImGuiCol_Text, a_color);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
		ImGui::TextUnformatted(a_message);
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();

		ImGui::SetCursorScreenPos(min);
		const bool clicked = ImGui::InvisibleButton("##StatusBanner", { width, height });
		if (ImGui::IsItemHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		return clicked;
	}

	float GetLockStatusBadgeSize()
	{
		const float scale = GetUIScale();
		const char* icon = ICON_TI_LOCK;
		const ImVec2 iconSize = Icons::CalcGlyphSize(Icons::TI(icon));
		constexpr float kPad = 3.0f;
		return std::max(iconSize.x, iconSize.y) + kPad * 2.0f * scale;
	}

	void DrawLockStatusBadge(ImVec2 a_min, bool a_locked, ImDrawList* a_drawList, bool a_hovered, bool a_held)
	{
		if (!a_drawList)
			a_drawList = ImGui::GetWindowDrawList();

		const float size = GetLockStatusBadgeSize();
		const char* icon = a_locked ? ICON_TI_LOCK : ICON_TI_LOCK_OPEN_2;
		const auto& statusPalette = Menu::GetSingleton()->GetTheme().StatusPalette;
		const ImVec4 statusColor = a_locked ? statusPalette.SuccessColor : statusPalette.Error;
		ImVec4 fill = statusColor;
		fill.w = ThemeManager::Constants::LOCK_BUTTON_FILL_ALPHA;
		ImVec4 border = statusColor;
		border.w = ThemeManager::Constants::LOCK_BUTTON_BORDER_ALPHA;

		const ImVec2 max(a_min.x + size, a_min.y + size);
		const float rounding = std::min(ImGui::GetStyle().FrameRounding, size * ThemeManager::Constants::LOCK_BUTTON_ROUNDING_RATIO);
		auto _style = TransparentIconButtonStyle();
		DrawRoundedButtonHighlight(a_min, max, a_hovered, a_held, rounding, a_drawList);
		a_drawList->AddRectFilled(a_min, max, ImGui::GetColorU32(fill), rounding);
		a_drawList->AddRect(a_min, max, ImGui::GetColorU32(border), rounding);
		Icons::DrawCenteredGlyph(a_drawList, a_min, ImVec2(size, size),
			Icons::TI(icon), ImGui::GetColorU32(statusColor));
	}

	bool LockStatusBadgeButton(const char* a_id, bool a_locked, const char* a_tooltip)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (!window)
			return false;

		const float size = GetLockStatusBadgeSize();
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImRect bb(min, ImVec2(min.x + size, min.y + size));
		const ImGuiID id = window->GetID(a_id);
		ImGui::ItemSize(bb.GetSize());
		if (!ImGui::ItemAdd(bb, id))
			return false;

		bool hovered = false;
		bool held = false;
		const bool clicked = ImGui::ButtonBehavior(bb, id, &hovered, &held);
		DrawLockStatusBadge(min, a_locked, nullptr, hovered, held);
		if (a_tooltip)
			AddTooltip(a_tooltip);
		return clicked;
	}

	namespace
	{
		constexpr float kBadgePadX = 5.0f;
		constexpr float kBadgePadY = 1.5f;
		constexpr float kBadgeRounding = 3.0f;
		constexpr float kBadgeFillAlpha = 0.22f;
		constexpr float kBadgeLabelTowardText = 0.35f;
	}

	float MeasureBadgeWidth(const char* a_label)
	{
		return ImGui::CalcTextSize(a_label).x + kBadgePadX * GetUIScale() * 2.0f;
	}

	float DrawBadgeAt(ImVec2 a_min, float a_rowHeight, const char* a_label, const ImVec4& a_bg, const ImVec4& a_fg,
		ImDrawList* a_drawList)
	{
		if (!a_drawList)
			a_drawList = ImGui::GetWindowDrawList();

		const float scale = GetUIScale();
		const ImVec2 textSize = ImGui::CalcTextSize(a_label);
		const float padX = kBadgePadX * scale;
		const float padY = kBadgePadY * scale;
		const float pillW = textSize.x + padX * 2.0f;
		const float pillH = textSize.y + padY * 2.0f;
		const float y = a_min.y + (a_rowHeight - pillH) * 0.5f;

		a_drawList->AddRectFilled(ImVec2(a_min.x, y), ImVec2(a_min.x + pillW, y + pillH),
			ImGui::ColorConvertFloat4ToU32(a_bg), kBadgeRounding * scale);
		a_drawList->AddText(ImVec2(a_min.x + padX, y + padY), ImGui::ColorConvertFloat4ToU32(a_fg), a_label);
		return pillW;
	}

	void Badge(const char* a_label, const ImVec4& a_tint)
	{
		const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		const ImVec4 bg(a_tint.x, a_tint.y, a_tint.z, kBadgeFillAlpha);
		const ImVec4 fg(a_tint.x + (text.x - a_tint.x) * kBadgeLabelTowardText,
			a_tint.y + (text.y - a_tint.y) * kBadgeLabelTowardText,
			a_tint.z + (text.z - a_tint.z) * kBadgeLabelTowardText, 1.0f);
		const float lineH = ImGui::GetTextLineHeight();
		const float width = DrawBadgeAt(ImGui::GetCursorScreenPos(), lineH, a_label, bg, fg);
		ImGui::Dummy(ImVec2(width, lineH));
	}

	float MeasureHdrSdrCapabilityPillWidth(bool a_supportsHDR)
	{
		return MeasureBadgeWidth(a_supportsHDR ? T("ui.badge.hdr", "HDR") : T("ui.badge.sdr", "SDR"));
	}

	void DrawHdrSdrCapabilityPillAt(ImVec2 a_min, float a_rowHeight, bool a_supportsHDR, ImDrawList* a_drawList)
	{
		const char* label = a_supportsHDR ?
		                        T("ui.badge.hdr", "HDR") :
		                        T("ui.badge.sdr", "SDR");

		const ImVec4 bg = a_supportsHDR ?
		                      ThemeManager::Constants::BADGE_HDR_BG :
		                      ThemeManager::Constants::BADGE_SDR_BG;
		const ImVec4 fg = a_supportsHDR ?
		                      ThemeManager::Constants::BADGE_HDR_FG :
		                      ThemeManager::Constants::BADGE_SDR_FG;

		DrawBadgeAt(a_min, a_rowHeight, label, bg, fg, a_drawList);
	}

	void DrawHdrSdrCapabilityPill(bool a_supportsHDR)
	{
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		const float lineH = ImGui::GetTextLineHeight();
		DrawHdrSdrCapabilityPillAt(cursor, lineH, a_supportsHDR);
		ImGui::Dummy(ImVec2(MeasureHdrSdrCapabilityPillWidth(a_supportsHDR), lineH));
	}

	LockedSection::LockedSection(bool a_locked, const char* a_message, bool* a_outBannerClicked) :
		m_locked(a_locked)
	{
		if (!m_locked)
			return;
		const bool clicked = StatusBanner(ICON_TI_LOCK, a_message, Colors::GetWarning());
		if (a_outBannerClicked && clicked)
			*a_outBannerClicked = true;
		ImGui::Spacing();
		ImGui::BeginDisabled();
	}

	LockedSection::~LockedSection()
	{
		if (m_locked)
			ImGui::EndDisabled();
	}

	void Explainer(const char* a_label, const char* a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, Colors::GetSecondary());
		if (ImGui::TreeNodeEx(a_label, ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
			ImGui::Indent();
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(a_text);
			ImGui::PopTextWrapPos();
			ImGui::Unindent();
		}
		ImGui::PopStyleColor();
	}

	void ToolbarDivider(bool a_continueLine)
	{
		if (!a_continueLine) {
			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 1.0f);
			return;
		}
		const float spacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SameLine(0.0f, spacing);
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 1.0f);
		ImGui::SameLine(0.0f, spacing);
	}

	// Static state for clear shader cache confirmation popup
	static bool showClearCacheConfirmation = false;
	static bool dontAskAgainCheckbox = false;

	// Helper function to perform the actual cache clearing
	static void PerformClearShaderCache()
	{
		auto* shaderCache = globals::shaderCache;
		if (shaderCache) {
			shaderCache->Clear();
			if (shaderCache->IsDiskCache()) {
				shaderCache->DeleteDiskCache();
			}
			shaderCache->ClearContentStore();
			shaderCache->SetRecachePending(true);
		}
	}

	void DrawRecacheBanner()
	{
		auto* shaderCache = globals::shaderCache;
		if (!shaderCache || !shaderCache->IsRecachePending())
			return;
		if (StatusBanner(ICON_TI_REFRESH,
				T("ui.recache_banner",
					"A full shader recache is pending and will run on the next game restart. Click to dismiss."),
				Colors::GetWarning()))
			shaderCache->SetRecachePending(false);
		ImGui::Spacing();
	}

	void RequestClearShaderCacheConfirmation()
	{
		auto* menu = globals::menu;
		if (!menu)
			return;

		// If user has opted to skip confirmation, clear immediately
		if (menu->GetSettings().SkipClearCacheConfirmation) {
			PerformClearShaderCache();
			return;
		}

		// Show confirmation popup
		showClearCacheConfirmation = true;
		dontAskAgainCheckbox = false;
	}

	void DrawClearShaderCacheConfirmation()
	{
		if (!showClearCacheConfirmation)
			return;

		ImGui::OpenPopup(T("ui.clear_shader_cache", "Clear Shader Cache?"));

		if (auto popup = CenteredPopupModal(T("ui.clear_shader_cache", "Clear Shader Cache?"), &showClearCacheConfirmation)) {
			ImGui::Text("%s", T("ui.clear_cache_confirm", "Are you sure you want to clear the shader cache?"));
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TextWrapped(
				"%s", T("ui.clear_cache_desc",
						  "This will clear all compiled shaders from memory and disk cache (if enabled). "
						  "Shaders will be recompiled when the game next encounters them."));
			ImGui::Spacing();
			ImGui::TextWrapped(
				"%s", T("ui.clear_cache_explainer",
						  "The shader cache is the collection of compiled shaders that replace the vanilla shaders at runtime. "
						  "The disk cache is a copy of those compiled shaders kept on disk."));
			if (globals::shaderCache) {
				// Measured once when the popup opens, since it walks the store directory.
				static std::optional<SIE::ShaderCache::ContentStoreUsage> storeUsage;
				if (ImGui::IsWindowAppearing() || !storeUsage)
					storeUsage = globals::shaderCache->GetContentStoreUsage();
				if (storeUsage->blobs > 0) {
					ImGui::Spacing();
					ImGui::TextWrapped("%s", I18n::GetSingleton()->Format("ui.clear_cache_store_note",
																	 { { "count", std::to_string(storeUsage->blobs) },
																		 { "size", std::format("{:.0f}", static_cast<double>(storeUsage->bytes) / (1024.0 * 1024.0)) } },
																	 "This also deletes the persistent shader store ({count} shaders, {size} MB).")
												 .c_str());
				}
			}
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::Checkbox(T("ui.dont_ask_again", "Don't ask me again"), &dontAskAgainCheckbox);

			ImGui::Spacing();

			// Center buttons
			constexpr float buttonWidth = ThemeManager::Constants::POPUP_BUTTON_WIDTH;
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float totalWidth = buttonWidth * 2 + spacing;
			const float windowWidth = ImGui::GetWindowWidth();
			const float offset = (windowWidth - totalWidth) * 0.5f;
			if (offset > 0)
				ImGui::SetCursorPosX(offset);

			if (ImGui::Button(T("ui.clear_cache", "Clear Cache"), ImVec2(buttonWidth, 0))) {
				// Save preference if checkbox is checked
				if (dontAskAgainCheckbox) {
					if (auto* menu = globals::menu) {
						menu->GetSettings().SkipClearCacheConfirmation = true;
					}
				}

				PerformClearShaderCache();
				showClearCacheConfirmation = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button(T("ui.cancel", "Cancel"), ImVec2(buttonWidth, 0))) {
				showClearCacheConfirmation = false;
				ImGui::CloseCurrentPopup();
			}
		}
	}

	// Shared by the header icon and the text button, drawn once per frame from Menu::DrawSettings
	static ConfirmationPopup restoreSavedSettingsPopup;

	void RequestRestoreSavedSettingsConfirmation()
	{
		auto* state = globals::state;
		if (!state)
			return;

		// Nothing to lose: restore straight away
		if (!state->HasUnsavedChanges()) {
			state->Load();
			globals::menu->ClearUndoHistory();
			return;
		}

		restoreSavedSettingsPopup.Request();
	}

	void DrawRestoreSavedSettingsConfirmation()
	{
		if (!restoreSavedSettingsPopup.IsOpen())
			return;

		// Refreshed every frame so a language switch while open updates the text; the ### id keeps the popup open
		restoreSavedSettingsPopup.title = std::format("{}###RestoreSavedSettingsConfirmation", T("ui.restore_saved_settings_title", "Restore Saved Settings?"));
		restoreSavedSettingsPopup.message = T("ui.restore_saved_settings_confirm",
			"You have unsaved changes. Restoring the saved settings will discard them. This cannot be undone.");
		restoreSavedSettingsPopup.confirmLabel = T("ui.restore", "Restore");
		restoreSavedSettingsPopup.cancelLabel = T("ui.cancel", "Cancel");
		restoreSavedSettingsPopup.focus = ConfirmationPopup::Focus::Cancel;

		if (restoreSavedSettingsPopup.Draw()) {
			if (auto* state = globals::state) {
				state->Load();
				globals::menu->ClearUndoHistory();
			}
		}
	}

	// --- Reusable ConfirmationPopup ---

	void ConfirmationPopup::Request()
	{
		assert(rememberedChoice == Choice::Confirm || rememberedChoice == Choice::Alternate);
		if (dontAskAgainPersist && *dontAskAgainPersist) {
			pendingChoice = rememberedChoice;
			return;
		}
		show = true;
		pendingChoice = Choice::None;
		dontAskCheckbox = false;
	}

	bool ConfirmationPopup::Draw()
	{
		return DrawChoice() == Choice::Confirm;
	}

	ConfirmationPopup::Choice ConfirmationPopup::DrawChoice()
	{
		if (pendingChoice != Choice::None)
			return std::exchange(pendingChoice, Choice::None);
		if (!show)
			return Choice::None;

		ImGui::OpenPopup(title.c_str());

		Choice result = Choice::None;
		if (auto popup = CenteredPopupModal(title.c_str(), &show)) {
			ImGui::TextWrapped("%s", message.c_str());
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (showDontAskAgain)
				ImGui::Checkbox(dontAskAgainLabel.empty() ? T("ui.dont_ask_again", "Don't ask me again") : dontAskAgainLabel.c_str(), &dontAskCheckbox);

			const ImVec2 buttonSize(ThemeManager::Constants::POPUP_BUTTON_WIDTH, 0.0f);
			const int buttonCount = 1 + !confirmLabel.empty() + !alternateLabel.empty();
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float totalWidth = buttonSize.x * buttonCount + spacing * (buttonCount - 1);
			const float offset = (ImGui::GetWindowWidth() - totalWidth) * 0.5f;
			if (offset > 0)
				ImGui::SetCursorPosX(offset);

			const auto pick = [&](Choice choice) {
				if (choice == rememberedChoice && showDontAskAgain && dontAskCheckbox && dontAskAgainPersist)
					*dontAskAgainPersist = true;
				result = choice;
				show = false;
				ImGui::CloseCurrentPopup();
			};

			if (!confirmLabel.empty()) {
				if (primaryConfirm ? SuccessButton(confirmLabel.c_str(), buttonSize) : ImGui::Button(confirmLabel.c_str(), buttonSize))
					pick(Choice::Confirm);
				if (focus == Focus::Confirm)
					ImGui::SetItemDefaultFocus();
				ImGui::SameLine();
			}

			if (!alternateLabel.empty()) {
				if (ErrorButton(alternateLabel.c_str(), buttonSize))
					pick(Choice::Alternate);
				ImGui::SameLine();
			}

			if (ImGui::Button(cancelLabel.empty() ? T("ui.cancel", "Cancel") : cancelLabel.c_str(), buttonSize))
				pick(Choice::Cancel);
			if (focus == Focus::Cancel)
				ImGui::SetItemDefaultFocus();
		}
		// Closed from its title bar.
		if (result == Choice::None && !show)
			result = Choice::Cancel;
		return result;
	}

	constexpr float kPercentageScale = 1e2f;

	bool PercentageSlider(const char* label, float* data, float lb, float ub, const char* format, ImGuiSliderFlags flags)
	{
		// The slider binds a temporary, so name the member it stands for or scene authoring misses it.
		SceneWidgetInterceptor::ProxyScope sceneProxy(data, kPercentageScale);

		float percentageData = (*data) * kPercentageScale;
		bool retval = ImGui::SliderFloat(label, &percentageData, lb, ub, format, flags);
		// Only on an edit: the scaled round trip is not exact in float, so an idle write-back drifts the member.
		if (retval && !SceneWidgetInterceptor::IsArmed())
			(*data) = percentageData / kPercentageScale;
		return retval;
	}

	ImVec2 GetNativeViewportSizeScaled(float scale)
	{
		const auto Size = ImGui::GetMainViewport()->Size;
		return { Size.x * scale, Size.y * scale };
	}

	bool InitializeMenuIcons(Menu* menu)
	{
		return IconLoader::InitializeMenuIcons(menu);
	}

	// Text rendering helpers
	ImVec2 DrawSharpText(const char* text, bool alignToPixelGrid, float scale)
	{
		ImVec2 startPos = ImGui::GetCursorPos();

		if (alignToPixelGrid) {
			// Get current position
			ImVec2 pos = ImGui::GetCursorPos();

			// Align to pixel grid for sharper rendering
			pos.x = std::round(pos.x);
			pos.y = std::round(pos.y);

			// Set aligned position
			ImGui::SetCursorPos(pos);
		}
		// Apply scale if needed
		if (scale != 1.0f) {
			ImGui::SetWindowFontScale(scale);
		}

		// Use Text instead of TextUnformatted for better rendering
		ImGui::Text("%s", text);
		// Restore original scale if needed
		if (scale != 1.0f)
			ImGui::SetWindowFontScale(1.0f);

		// Calculate and return the rendered size
		ImVec2 endPos = ImGui::GetCursorPos();
		return ImVec2(endPos.x - startPos.x, endPos.y - startPos.y);
	}

	ImVec2 DrawAlignedTextWithLogo(ID3D11ShaderResourceView* logoTexture, const ImVec2& logoSize, const char* text, float textScale, ImU32 logoTint)
	{
		// Save current cursor position
		ImVec2 startPos = ImGui::GetCursorPos();

		// Centre the logo on the title's capital-letter ink (not its line box), so both read as one row
		float logoHeight = logoSize.y;
		ImGui::SetWindowFontScale(textScale);
		float verticalOffset = GetTextInkCenterY() - logoHeight * 0.5f;
		ImGui::SetWindowFontScale(1.0f);

		// Position cursor for logo with vertical alignment
		ImGui::SetCursorPos(ImVec2(startPos.x, startPos.y + verticalOffset));

		// Render logo using draw list with tint color support
		ImVec2 logoPos = ImGui::GetCursorScreenPos();
		const ImVec2 logoMin = IconLoader::SnapToPixel(logoPos);
		const ImVec2 snappedSize = IconLoader::SnapToPixel(logoSize);
		const ImVec2 logoMax = ImVec2(logoMin.x + snappedSize.x, logoMin.y + snappedSize.y);
		ImGui::GetWindowDrawList()->AddImage(IconLoader::ViewForSize(logoTexture, snappedSize), logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), logoTint);

		// Advance cursor past logo
		ImGui::Dummy(logoSize);
		ImGui::SameLine();

		// Add consistent spacing between logo and text
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + C::LOGO_TEXT_GAP * GetUIScale());

		// Reset cursor for text with proper vertical alignment
		ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX(), startPos.y));
		// Use windowed font scale for sharper text
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGui::SetWindowFontScale(textScale);

		// Render text aligned to pixel grid for sharpness
		ImGui::Text("%s", text);
		// Restore style
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleVar();

		// Calculate and return the total rendered size
		ImVec2 endPos = ImGui::GetCursorPos();
		return ImVec2(endPos.x - startPos.x, endPos.y - startPos.y);
	}

	// StyledButtonWrapper implementation
	StyledButtonWrapper::StyledButtonWrapper(const ImVec4& normalColor, const ImVec4& hoveredColor, const ImVec4& activeColor) :
		m_pushedStyles(0)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, normalColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
		m_pushedStyles = 3;
	}

	StyledButtonWrapper::~StyledButtonWrapper()
	{
		if (m_pushedStyles > 0) {
			ImGui::PopStyleColor(m_pushedStyles);
		}
	}

	namespace ButtonHelpers
	{
		ImVec4 AdjustButtonColor(const ImVec4& color, float amount)
		{
			const float maxChannel = std::max({ color.x, color.y, color.z });
			const float minChannel = ThemeManager::Constants::BUTTON_MIN_COLOR_CHANNEL;
			const float maxColorChannel = ThemeManager::Constants::BUTTON_MAX_COLOR_CHANNEL;
			const float adjustment = maxChannel <= (maxColorChannel - amount) ? amount : -amount;
			return ImVec4(
				std::clamp(color.x + adjustment, minChannel, maxColorChannel),
				std::clamp(color.y + adjustment, minChannel, maxColorChannel),
				std::clamp(color.z + adjustment, minChannel, maxColorChannel),
				color.w);
		}

		ImVec4 WithAlpha(const ImVec4& color, float alpha)
		{
			return ImVec4(color.x, color.y, color.z, alpha);
		}
	}

	StyledButtonWrapper StatusButtonStyle(const ImVec4& color)
	{
		auto hover = ButtonHelpers::AdjustButtonColor(color, ThemeManager::Constants::BUTTON_HOVER_BRIGHTEN);
		auto active = ButtonHelpers::AdjustButtonColor(color, ThemeManager::Constants::BUTTON_ACTIVE_BRIGHTEN);
		return StyledButtonWrapper(color, hover, active);
	}

	StyledButtonWrapper DestructiveButtonStyle()
	{
		return StatusButtonStyle(Menu::GetSingleton()->GetTheme().StatusPalette.Error);
	}

	bool ErrorButton(const char* label, const ImVec2& size)
	{
		auto _style = DestructiveButtonStyle();
		return ImGui::Button(label, size);
	}

	bool ErrorButtonWithFlash(const char* label, const ImVec2& size, int flashDurationMs)
	{
		auto _style = DestructiveButtonStyle();
		return ButtonWithFlash(label, size, flashDurationMs);
	}

	StyledButtonWrapper StatusTextButtonStyle(const ImVec4& color)
	{
		return StyledButtonWrapper(color,
			ButtonHelpers::WithAlpha(color, ThemeManager::Constants::BUTTON_STATUS_TEXT_HOVER_ALPHA),
			ButtonHelpers::WithAlpha(color, ThemeManager::Constants::BUTTON_STATUS_TEXT_ACTIVE_ALPHA));
	}

	StyledButtonWrapper SuccessButtonStyle()
	{
		return StatusTextButtonStyle(Menu::GetSingleton()->GetTheme().StatusPalette.SuccessColor);
	}

	StyledButtonWrapper WarningButtonStyle()
	{
		return StatusTextButtonStyle(Menu::GetSingleton()->GetTheme().StatusPalette.Warning);
	}

	bool SuccessButton(const char* label, const ImVec2& size)
	{
		auto _style = SuccessButtonStyle();
		return ImGui::Button(label, size);
	}

	bool WarningButton(const char* label, const ImVec2& size)
	{
		auto _style = WarningButtonStyle();
		return ImGui::Button(label, size);
	}

	bool ErrorTextButton(const char* label, const ImVec2& size)
	{
		auto _style = StatusTextButtonStyle(Menu::GetSingleton()->GetTheme().StatusPalette.Error);
		return ImGui::Button(label, size);
	}

	StyledButtonWrapper TransparentIconButtonStyle()
	{
		auto hoverColor = Menu::GetSingleton()->GetTheme().Palette.Text;
		hoverColor.w = ThemeManager::Constants::BUTTON_TRANSPARENT_HOVER_ALPHA;
		return StyledButtonWrapper(ImVec4(0, 0, 0, 0), hoverColor, hoverColor);
	}

	ImVec4 GetIconTint()
	{
		const auto& theme = Menu::GetSingleton()->GetTheme();
		return theme.UseMonochromeIcons ? theme.Palette.Text : ImVec4(1, 1, 1, 1);
	}

	static float GetPillRounding(const ImVec2& min, const ImVec2& max)
	{
		IM_ASSERT(max.x >= min.x && max.y >= min.y);
		return ImMin(max.x - min.x, max.y - min.y) * 0.5f;
	}

	static float GetThemedButtonHighlightRounding(const ImVec2& min, const ImVec2& max)
	{
		const float frameRounding = ImGui::GetStyle().FrameRounding;
		IM_ASSERT(frameRounding >= 0.0f);
		return ImMin(ImMax(frameRounding, 0.0f), GetPillRounding(min, max));
	}

	bool DrawRoundedButtonHighlight(const ImVec2& min, const ImVec2& max, bool hovered, bool active, ImDrawList* drawList)
	{
		return DrawRoundedButtonHighlight(min, max, hovered, active, GetThemedButtonHighlightRounding(min, max), drawList);
	}

	bool DrawRoundedButtonHighlight(const ImRect& rect, bool hovered, bool active, ImDrawList* drawList)
	{
		return DrawRoundedButtonHighlight(rect.Min, rect.Max, hovered, active, drawList);
	}

	bool DrawRoundedButtonHighlight(const ImVec2& min, const ImVec2& max, bool hovered, bool active, float rounding, ImDrawList* drawList)
	{
		if (!hovered && !active)
			return false;

		IM_ASSERT(max.x >= min.x && max.y >= min.y);
		IM_ASSERT(rounding >= 0.0f);
		if (!drawList)
			drawList = ImGui::GetWindowDrawList();

		drawList->AddRectFilled(min, max, ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), rounding);
		return true;
	}

	UnderlayScope::UnderlayScope() :
		list(ImGui::GetWindowDrawList())
	{
		split = list->_Splitter._Count <= 1 && !ImGui::GetCurrentTable();
		if (split) {
			list->ChannelsSplit(2);
			list->ChannelsSetCurrent(1);
		}
	}

	UnderlayScope::~UnderlayScope()
	{
		if (split)
			list->ChannelsMerge();
	}

	ImDrawList* UnderlayScope::Background()
	{
		if (split)
			list->ChannelsSetCurrent(0);
		return list;
	}

	namespace
	{
		/// The eased state an item reached by the end of the last frame, read before the item is submitted so its
		/// colours can be set first. Items that are not interactive this frame (disabled) ease back to rest.
		Anim::Blend PendingBlend(ImGuiID id, bool selected)
		{
			const ImGuiContext& g = *ImGui::GetCurrentContext();
			const bool enabled = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) == 0;
			return Anim::ItemBlend(id, enabled && g.HoveredIdPreviousFrame == id, enabled && g.ActiveId == id, selected);
		}
	}  // namespace

	bool Button(const char* label, const ImVec2& size)
	{
		if (Anim::ReducedMotion())
			return ImGui::Button(label, size);
		const auto blend = PendingBlend(ImGui::GetID(label), false);
		const ImVec4 rest = ImGui::GetStyleColorVec4(ImGuiCol_Button);
		const ImVec4 hovered = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
		const ImVec4 active = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
		// Every state shows the eased colour, so the instant hover / press swap of ImGui is replaced by the blend
		const ImVec4 eased = Anim::Lerp(Anim::Lerp(rest, hovered, blend.hover), active, blend.press);
		ImGui::PushStyleColor(ImGuiCol_Button, eased);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, eased);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, eased);
		const bool clicked = ImGui::Button(label, size);
		ImGui::PopStyleColor(3);
		return clicked;
	}

	bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size)
	{
		// ImGui::Selectable is hooked (UIHooks), so every row already gets the gliding list highlight
		return ImGui::Selectable(label, selected, flags, size);
	}

	bool Selectable(const char* label, bool* selected, ImGuiSelectableFlags flags, const ImVec2& size)
	{
		return ImGui::Selectable(label, selected, flags, size);
	}

	void AddressButton(const char* a_name, uintptr_t a_address)
	{
		if (ImGui::Button(a_name))
			ImGui::SetClipboardText(std::format("{0:x}", a_address).c_str());
		if (ImGui::IsItemHovered()) {
			const auto tooltip = I18n::GetSingleton()->Format("ui.copy_address_tooltip", { { "name", a_name } }, "Copy {name} Address to Clipboard");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
	}

	ImU32 QuietIconColor(float hover, float selected)
	{
		const ImVec4 resting = Anim::Lerp(Colors::GetSecondary(), ImGui::GetStyleColorVec4(ImGuiCol_Text), hover);
		return ImGui::GetColorU32(Anim::Lerp(resting, Colors::GetAccent(), selected));
	}

	bool DrawAnimatedButtonHighlight(const ImRect& rect, ImGuiID id, bool hovered, bool held, bool current, ImDrawList* drawList)
	{
		const auto blend = Anim::ItemBlend(id, hovered || current, held, false);
		if (blend.hover <= 0.0f && blend.press <= 0.0f)
			return false;
		if (!drawList)
			drawList = ImGui::GetWindowDrawList();
		const float rounding = GetThemedButtonHighlightRounding(rect.Min, rect.Max);
		Anim::FillLayer(drawList, rect.Min, rect.Max, rounding, ImGuiCol_ButtonHovered, blend.hover);
		Anim::FillLayer(drawList, rect.Min, rect.Max, rounding, ImGuiCol_ButtonActive, blend.press);
		return true;
	}

	void DrawIconCircle(ImVec2 center, float radius, ImU32 color, bool filled)
	{
		auto* drawList = ImGui::GetWindowDrawList();
		if (filled)
			drawList->AddCircleFilled(center, radius, color, ThemeManager::Constants::ICON_CIRCLE_SEGMENTS);
		else
			drawList->AddCircle(center, radius, color, ThemeManager::Constants::ICON_CIRCLE_SEGMENTS,
				ThemeManager::Constants::ICON_OUTLINE_THICKNESS * GetUIScale());
	}

	void DrawInlineIndicatorDot(ImU32 color, bool filled)
	{
		const float lineHeight = ImGui::GetTextLineHeight();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(lineHeight, lineHeight));
		const float halfLine = lineHeight * 0.5f;
		DrawIconCircle(ImVec2(origin.x + halfLine, origin.y + halfLine),
			lineHeight * ThemeManager::Constants::SCENE_INDICATOR_RADIUS_RATIO, color, filled);
	}

	// Shared constants for title-bar button overlays
	static constexpr float kTitleBarButtonPadding = 2.0f;
	static constexpr float kCloseCrossDiagonalScale = 0.5f / std::numbers::sqrt2_v<float>;
	static constexpr float kCloseCrossInset = 1.0f;
	static constexpr ImVec4 kTransparentButtonChrome(0, 0, 0, 0);

	static ImRect TitleBarButtonRect(const ImVec2& origin, float fontSize)
	{
		const float full = fontSize + kTitleBarButtonPadding * 2.0f;
		return ImRect(origin, ImVec2(origin.x + full, origin.y + full));
	}

	static ImVec2 RightTitleBarButtonOrigin(ImGuiWindow* window, float fontSize, float offset = 0.0f)
	{
		const auto& style = ImGui::GetStyle();
		return ImVec2(window->Rect().Max.x - window->WindowBorderSize - style.FramePadding.x - fontSize - offset - kTitleBarButtonPadding,
			window->Rect().Min.y + style.FramePadding.y - kTitleBarButtonPadding);
	}

	static ImVec2 CollapseTitleBarButtonOrigin(ImGuiWindow* window, bool hasCloseButton, float fontSize)
	{
		const auto& style = ImGui::GetStyle();
		IM_ASSERT(style.WindowMenuButtonPosition == ImGuiDir_Left || style.WindowMenuButtonPosition == ImGuiDir_Right);

		if (style.WindowMenuButtonPosition == ImGuiDir_Right)
			return RightTitleBarButtonOrigin(window, fontSize, hasCloseButton ? fontSize : 0.0f);

		return ImVec2(window->Pos.x + window->WindowBorderSize + style.FramePadding.x - kTitleBarButtonPadding,
			window->Pos.y + style.FramePadding.y - kTitleBarButtonPadding);
	}

	static bool IsTitleBarButtonHovered(ImGuiWindow* window, const ImRect& bb)
	{
		ImGuiContext& g = *ImGui::GetCurrentContext();
		return g.HoveredWindow == window && ImGui::IsMouseHoveringRect(bb.Min, bb.Max, false);
	}

	class NativeTitleBarButtonHighlightGuard
	{
	public:
		NativeTitleBarButtonHighlightGuard()
		{
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kTransparentButtonChrome);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, kTransparentButtonChrome);
		}

		~NativeTitleBarButtonHighlightGuard() { ImGui::PopStyleColor(2); }
	};

	// Draws the rounded hover/press fill of a title-bar button at `pos`; `drawGlyph` redraws the glyph when the fill was drawn.
	template <typename GlyphFn>
	static void DrawRoundedTitleBarButton(ImGuiWindow* window, const ImVec2& pos, float sz, GlyphFn drawGlyph)
	{
		const ImRect bb = TitleBarButtonRect(pos, sz);
		const bool hovered = IsTitleBarButtonHovered(window, bb);
		const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

		window->DrawList->PushClipRect(window->Rect().Min, window->Rect().Max);
		if (DrawRoundedButtonHighlight(bb, hovered, held, window->DrawList))
			drawGlyph(bb);
		window->DrawList->PopClipRect();
	}

	// Draws a rounded close button overlay, matching native ImGui CloseButton position.
	static void DrawRoundedCloseHighlight(ImGuiWindow* window)
	{
		if (window->Flags & ImGuiWindowFlags_NoTitleBar)
			return;

		const float sz = ImGui::GetFontSize();
		DrawRoundedTitleBarButton(window, RightTitleBarButtonOrigin(window, sz), sz, [&](const ImRect& bb) {
			// Cross lines match ImGui's internal RenderCloseButton geometry.
			const ImVec2 c = bb.GetCenter();
			const float d = sz * kCloseCrossDiagonalScale - kCloseCrossInset;
			const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
			window->DrawList->AddLine({ c.x - d, c.y - d }, { c.x + d, c.y + d }, col);
			window->DrawList->AddLine({ c.x + d, c.y - d }, { c.x - d, c.y + d }, col);
		});
	}

	// Draws a rounded highlight for the collapse/triangle button in the title bar.
	static void DrawRoundedCollapseHighlight(ImGuiWindow* window, bool hasCloseButton)
	{
		if (window->Flags & (ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse))
			return;
		if (ImGui::GetStyle().WindowMenuButtonPosition == ImGuiDir_None)
			return;

		const float sz = ImGui::GetFontSize();
		const ImVec2 pos = CollapseTitleBarButtonOrigin(window, hasCloseButton, sz);
		DrawRoundedTitleBarButton(window, pos, sz, [&](const ImRect&) {
			const ImVec2 arrowPos(pos.x + kTitleBarButtonPadding, pos.y + kTitleBarButtonPadding);
			const ImGuiDir dir = window->Collapsed ? ImGuiDir_Right : ImGuiDir_Down;
			ImGui::RenderArrow(window->DrawList, arrowPos, ImGui::GetColorU32(ImGuiCol_Text), dir, 1.0f);
		});
	}

	static void DrawRoundedTitleBarButtonHighlights(ImGuiWindow* window, bool hasCloseButton, bool hasCollapseButton)
	{
		if (!window)
			return;

		if (hasCollapseButton)
			DrawRoundedCollapseHighlight(window, hasCloseButton);
		if (hasCloseButton)
			DrawRoundedCloseHighlight(window);
	}

	bool BeginWithRoundedClose(const char* name, bool* p_open, ImGuiWindowFlags flags)
	{
		bool visible = false;
		{
			NativeTitleBarButtonHighlightGuard guard;
			visible = ImGui::Begin(name, p_open, flags);
		}
		DrawRoundedTitleBarButtonHighlights(ImGui::GetCurrentWindowRead(), p_open != nullptr, true);
		return visible;
	}

	bool BeginPopupModalWithRoundedClose(const char* name, bool* p_open, ImGuiWindowFlags flags)
	{
		bool visible = false;
		{
			NativeTitleBarButtonHighlightGuard guard;
			visible = ImGui::BeginPopupModal(name, p_open, flags);
		}
		if (visible)
			DrawRoundedTitleBarButtonHighlights(ImGui::GetCurrentWindowRead(), p_open != nullptr, false);
		return visible;
	}

	// Whether each custom-header window was docked last frame, keyed by its full "Label###id"
	// title. NoTitleBar has to be decided before Begin(), but IsWindowDocked() only reports the
	// true state after it - so, like the main window's own header, this frame draws whatever last
	// frame was and re-checks are one frame behind a dock/undock transition.
	static std::unordered_map<std::string, bool> s_customHeaderWasDocked;

	// Draws the close button's crossed lines, matching DrawRoundedCloseHighlight's native geometry.
	// Unlike the native button, nothing else renders the X, so it has to be drawn unconditionally
	// rather than only while highlighted.
	static void DrawCustomHeaderCloseCross(const ImVec2& min, const ImVec2& max)
	{
		const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
		const float sz = max.x - min.x;
		const float d = sz * kCloseCrossDiagonalScale - kCloseCrossInset;
		const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddLine({ c.x - d, c.y - d }, { c.x + d, c.y + d }, col);
		drawList->AddLine({ c.x + d, c.y - d }, { c.x - d, c.y + d }, col);
	}

	float GetEditorChromeHeaderHeight()
	{
		// FontSize + FramePadding.y on each side of the glyph, then the same pad again as outer air
		// so title glyphs / icons are not flush with the window's top edge.
		return ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * C::EDITOR_CHROME_PAD_FACTOR;
	}

	float GetEditorChromeTextCursorOffsetY(float rowHeight)
	{
		return rowHeight * 0.5f - GetTextInkCenterY();
	}

	float GetTextInkCenterY()
	{
		// Measured from 'H' since cap height and ascent vary per font.
		const ImFontGlyph* glyph = ImGui::GetFontBaked()->FindGlyph('H');
		return glyph ? (glyph->Y0 + glyph->Y1) * 0.5f : ImGui::GetFontSize() * 0.5f;
	}

	float CenterTextTopY(float rowTop, float rowHeight)
	{
		return rowTop + rowHeight * 0.5f - GetTextInkCenterY();
	}

	// Draws the custom floating header: the title (also the drag handle), an optional
	// caller-supplied control cluster, and a close button pinned to the right edge. Runs
	// unconditionally (not gated on Begin()'s return value), same as a native title bar always
	// drawing regardless of what the body does - collapsing is disabled for these windows (see
	// BeginWithCustomHeader), so there is no risk of a hidden body stranding the header.
	static void DrawCustomHeaderRow(ImGuiWindow* window, const char* name, bool* p_open,
		const std::function<void()>& drawExtras,
		const std::function<void(ImVec2 iconMin, float iconSize)>& drawLeading)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float rowHeight = GetEditorChromeHeaderHeight();
		const float avail = ImGui::GetContentRegionAvail().x;
		const ImVec2 contentStart = ImGui::GetCursorScreenPos();
		// Begin() lands the cursor below WindowPadding; pull the header flush under the border so
		// the title/icons aren't sitting in a padded gap beneath the window's top edge.
		const ImVec2 rowStart(contentStart.x, contentStart.y - style.WindowPadding.y);
		// AlwaysAutoResize sizes from CursorMaxPos. Drag/close chrome laid out to last-frame
		// `avail` would otherwise lock that width on scale-down (Palette grew but never shrank).
		const bool autoResize = (window->Flags & ImGuiWindowFlags_AlwaysAutoResize) != 0;
		const ImVec2 cursorMaxBeforeHeader = window->DC.CursorMaxPos;

		ImGui::SetCursorScreenPos(rowStart);
		// AllowOverlap so title/extras/close drawn afterward still receive hover/click.
		ImGui::InvisibleButton("##CustomHeaderDrag", ImVec2(avail, rowHeight), ImGuiButtonFlags_AllowOverlap);
		// Drag only while actively dragging this item: a click on the close button must not start a move.
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			ImGui::StartMouseMovingWindow(window);

		float titleX = rowStart.x;
		if (drawLeading) {
			const float iconSize = ImGui::GetFontSize();
			const float iconY = rowStart.y + (rowHeight - iconSize) * 0.5f;
			drawLeading(ImVec2(rowStart.x, iconY), iconSize);
			titleX += iconSize + style.ItemInnerSpacing.x + 2.0f;
		}

		// Title: centre capital-letter ink in the row (see GetEditorChromeTextCursorOffsetY).
		ImGui::SetCursorScreenPos(ImVec2(titleX, rowStart.y + GetEditorChromeTextCursorOffsetY(rowHeight)));
		std::string_view displayTitle(name);
		if (const auto hash = displayTitle.find("##"); hash != std::string_view::npos)
			displayTitle = displayTitle.substr(0, hash);
		ImGui::TextUnformatted(displayTitle.data(), displayTitle.data() + displayTitle.size());

		if (drawExtras) {
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			drawExtras();
		}

		if (p_open) {
			// Square hit target centred in the row so top/bottom air matches the title glyphs.
			const float closeSize = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
			ImGui::SetCursorScreenPos(ImVec2(rowStart.x + avail - closeSize,
				rowStart.y + (rowHeight - closeSize) * 0.5f));
			auto _style = TransparentIconButtonStyle();
			const bool clicked = ImGui::Button("##CustomHeaderClose", ImVec2(closeSize, closeSize));
			DrawCustomHeaderCloseCross(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			if (clicked)
				*p_open = false;
		}

		// Separator is the bottom edge of the bar; no ItemInnerSpacing above it (that was
		// doubling the bottom air vs the top). Zero ItemSpacing so Separator itself doesn't
		// insert another half-gap; restore WindowPadding below for the body, matching native
		// title-bar → content layout.
		ImGui::SetCursorScreenPos(ImVec2(contentStart.x, rowStart.y + rowHeight));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
		ImGui::Separator();
		ImGui::PopStyleVar();
		ImGui::SetCursorScreenPos(ImVec2(contentStart.x, ImGui::GetCursorScreenPos().y + style.WindowPadding.y));

		if (autoResize) {
			// Keep the visual full-width header, but let the body alone decide auto-fit width.
			window->DC.CursorMaxPos.x = cursorMaxBeforeHeader.x;
			window->DC.IdealMaxPos.x = std::min(window->DC.IdealMaxPos.x, cursorMaxBeforeHeader.x);
		}
	}

	bool BeginWithCustomHeader(const char* name, bool* p_open,
		const std::function<void()>& drawExtras, ImGuiWindowFlags flags,
		const std::function<void(ImVec2 iconMin, float iconSize)>& drawLeading)
	{
		bool& wasDocked = s_customHeaderWasDocked[name];
		// Collapsing relies on the native title bar staying interactive while the body is skipped,
		// which the floating custom header can't reproduce safely - a window collapsed through it
		// would have nothing left on screen able to expand it again. Disabled unconditionally,
		// same as the main Community Shaders window's own custom header.
		ImGuiWindowFlags windowFlags = flags | ImGuiWindowFlags_NoCollapse;
		if (!wasDocked)
			windowFlags |= ImGuiWindowFlags_NoTitleBar;

		bool visible = false;
		{
			NativeTitleBarButtonHighlightGuard guard;
			visible = ImGui::Begin(name, p_open, windowFlags);
		}

		ImGuiWindow* window = ImGui::GetCurrentWindowRead();
		const bool isDocked = ImGui::IsWindowDocked();
		wasDocked = isDocked;

		if (isDocked) {
			// A shared dock tab bar already supplies the title and close x; only the rounded
			// highlight polish applies here, same as every other BeginWithRoundedClose window.
			DrawRoundedTitleBarButtonHighlights(window, p_open != nullptr, false);
		} else {
			// Drawn unconditionally (not gated on `visible`) so the drag handle and close button
			// are always reachable, matching a native title bar's always-on behavior.
			DrawCustomHeaderRow(window, name, p_open, drawExtras, drawLeading);
		}
		return visible;
	}

	// SectionWrapper implementation
	SectionWrapper::SectionWrapper(const char* title, const char* description, const ImVec4& titleColor, bool isVisible) :
		m_shouldDraw(isVisible),
		m_treeNodeOpened(false)
	{
		if (!m_shouldDraw) {
			return;
		}

		// Title, description and content sit tight; sections are separated by the rule below
		ImGui::TextColored(titleColor, "%s", title);
		if (description && strlen(description) > 0)
			ImGui::TextWrapped("%s", description);
	}

	SectionWrapper::~SectionWrapper()
	{
		if (m_shouldDraw) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
	}

	SectionWrapper::operator bool() const
	{
		return m_shouldDraw;
	}

	ID3D11ShaderResourceView* GetCategoryIcon(const char* categoryKey)
	{
		auto& icons = Util::IconLoader::GetIcons();

		if (strcmp(categoryKey, "Characters") == 0) {
			return icons.characters.texture;
		} else if (strcmp(categoryKey, "Display") == 0) {
			return icons.display.texture;
		} else if (strcmp(categoryKey, "Grass") == 0) {
			return icons.grass.texture;
		} else if (strcmp(categoryKey, "Lighting") == 0) {
			return icons.lighting.texture;
		} else if (strcmp(categoryKey, "Sky") == 0) {
			return icons.sky.texture;
		} else if (strcmp(categoryKey, "Landscape & Textures") == 0) {
			return icons.landscape.texture;
		} else if (strcmp(categoryKey, "Water") == 0) {
			return icons.water.texture;
		} else if (strcmp(categoryKey, "Utility") == 0) {
			return icons.debug.texture;
		} else if (strcmp(categoryKey, "Materials") == 0) {
			return icons.materials.texture;
		} else if (strcmp(categoryKey, "Post-Processing") == 0) {
			return icons.postProcessing.texture;
		}
		return nullptr;
	}

	bool DrawCategoryHeader(const char* categoryKey, const char* displayName, bool& isExpanded, int categoryCount)
	{
		// Get the appropriate icon for this category
		ID3D11ShaderResourceView* categoryIcon = GetCategoryIcon(categoryKey);

		// Icon lookup uses the stable category key; the translated label is drawn separately.
		// Draw category header with custom styling
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float availableWidth = ImGui::GetContentRegionAvail().x;

		// Calculate icon size based on current font size to match text scaling
		// This ensures icons scale consistently with text when the font scale changes
		const float currentFontSize = ImGui::GetFontSize();
		const float iconSize = currentFontSize * C::SECTION_ICON_SCALE;
		const float iconSpacing = currentFontSize * C::SECTION_ICON_SPACING_SCALE;
		const ImVec2 textSize = ImGui::CalcTextSize(displayName);

		// Left-aligned label row: icon, secondary-coloured name, dimmer count, then a faint rule to the right edge
		const float scale = Util::GetUIScale();

		ImGui::PushID(categoryKey);
		ImGui::SetCursorScreenPos(pos);
		const bool clicked = ImGui::InvisibleButton("##CategoryHeader", ImVec2(availableWidth, textSize.y + ThemeManager::Constants::CATEGORY_HEADER_PAD * scale));
		const bool hovered = ImGui::IsItemHovered();

		ImVec4 color = Util::Colors::GetSecondary();
		if (!isExpanded)
			color.w *= ThemeManager::Constants::CATEGORY_HEADER_COLLAPSED_ALPHA;
		if (hovered)
			color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		const ImU32 headerColor = ImGui::GetColorU32(color);
		ImVec4 countColor = color;
		countColor.w *= ThemeManager::Constants::CATEGORY_HEADER_COUNT_ALPHA;

		const float padX = ImGui::GetStyle().FramePadding.x;
		float currentX = pos.x + padX;
		if (categoryIcon) {
			const ImVec2 iconPos(currentX, pos.y + (textSize.y - iconSize) * 0.5f);
			drawList->AddImage(categoryIcon, iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize), ImVec2(0, 0), ImVec2(1, 1), headerColor);
			currentX += iconSize + iconSpacing;
		}
		drawList->AddText(ImVec2(currentX, pos.y), headerColor, displayName);
		currentX += textSize.x + iconSpacing;
		const std::string countText = std::format("{}", categoryCount);
		drawList->AddText(ImVec2(currentX, pos.y), ImGui::GetColorU32(countColor), countText.c_str());
		currentX += ImGui::CalcTextSize(countText.c_str()).x + iconSpacing;

		const float lineY = pos.y + textSize.y * 0.5f;
		if (currentX < pos.x + availableWidth - padX) {
			ImVec4 line = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
			line.w *= ThemeManager::Constants::CATEGORY_HEADER_LINE_ALPHA;
			drawList->AddLine(ImVec2(currentX, lineY), ImVec2(pos.x + availableWidth - padX, lineY), ImGui::GetColorU32(line), 1.0f);
		}

		if (clicked)
			isExpanded = !isExpanded;

		ImGui::PopID();

		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + ThemeManager::Constants::CATEGORY_HEADER_PAD * scale));
		ImGui::Dummy(ImVec2(availableWidth, 0.0f));
		return clicked;
	}

	bool DrawSectionHeader(const char* sectionName, bool useWhiteText, bool isCollapsible, bool* isExpanded)
	{
		bool stateChanged = false;

		// Use Menu theme colors for consistent styling
		auto& theme = globals::menu->GetTheme().FeatureHeading;
		auto& palette = globals::menu->GetTheme().Palette;
		// When useWhiteText is true, use the theme's text color instead of hardcoded white
		ImVec4 color = useWhiteText ? palette.Text : theme.ColorDefault;

		ImU32 headerColor = ImGui::GetColorU32(color);

		if (isCollapsible && isExpanded) {
			// Use collapsible header similar to DrawCategoryHeader
			ImGui::PushID(sectionName);

			ImGui::PushStyleColor(ImGuiCol_Text, headerColor);

			if (ImGui::CollapsingHeader(sectionName, ImGuiTreeNodeFlags_DefaultOpen)) {
				if (!*isExpanded) {
					stateChanged = true;
				}
				*isExpanded = true;
			} else {
				if (*isExpanded) {
					stateChanged = true;
				}
				*isExpanded = false;
			}

			ImGui::PopStyleColor();
			ImGui::PopID();
		} else {
			// Non-collapsible header - use custom styled header similar to CategoryHeader
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 pos = ImGui::GetCursorScreenPos();
			float availableWidth = ImGui::GetContentRegionAvail().x;
			ImVec2 textSize = ImGui::CalcTextSize(sectionName);

			// Calculate line positions
			float lineY = pos.y + textSize.y * 0.5f;
			float lineLength = (availableWidth - textSize.x - 20.0f) * 0.5f;  // 20px for padding

			// Left line
			if (lineLength > 0) {
				drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), headerColor, 1.0f);
			}

			// Right line
			float rightLineStart = pos.x + lineLength + 10.0f + textSize.x + 10.0f;
			if (rightLineStart < pos.x + availableWidth) {
				drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), headerColor, 1.0f);
			}

			// Center text
			ImVec2 textPos = ImVec2(pos.x + lineLength + 10.0f, pos.y + 2.0f);
			drawList->AddText(textPos, headerColor, sectionName);

			// Move cursor to next line
			ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + C::SECTION_HEADER_GAP * GetUIScale()));
			ImGui::Dummy(ImVec2(availableWidth, 0.0f));
		}

		return stateChanged;
	}

	// ColorCodedValueConfig static helper implementations
	ColorCodedValueConfig ColorCodedValueConfig::HighIsBad(float low, float med, float high)
	{
		ColorCodedValueConfig config;
		const auto& theme = globals::menu->GetTheme().StatusPalette;
		config.thresholds = {
			{ low, theme.Disable },    // Very low - gray
			{ med, theme.InfoColor },  // Low - blue
			{ high, theme.Warning },   // Medium - orange
			{ FLT_MAX, theme.Error }   // High - red (bad)
		};
		return config;
	}

	ColorCodedValueConfig ColorCodedValueConfig::HighIsGood(float low, float med, float high)
	{
		ColorCodedValueConfig config;
		const auto& theme = globals::menu->GetTheme().StatusPalette;
		config.thresholds = {
			{ low, theme.Disable },          // Very low - gray
			{ med, theme.InfoColor },        // Low - blue
			{ high, theme.Warning },         // Medium - orange
			{ FLT_MAX, theme.SuccessColor }  // High - green (good)
		};
		return config;
	}

	void DrawColorCodedValue(
		const std::string& label,
		float valueToCheck,
		const std::string& valueStr,
		const ColorCodedValueConfig& config,
		bool useBullet)
	{
		// Display label
		if (useBullet) {
			ImGui::BulletText("%s", label.c_str());
		} else {
			ImGui::Text("%s", label.c_str());
		}
		if (config.sameLine) {
			ImGui::SameLine();
		}

		// Determine color based on thresholds
		ImVec4 valueColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // Default white
		for (const auto& tc : config.thresholds) {
			if (valueToCheck < tc.threshold) {
				valueColor = tc.color;
				break;
			}
		}

		// Display colored value (arbitrary string)
		ImGui::TextColored(valueColor, "%s", valueStr.c_str());

		// Add tooltip if provided
		if (config.tooltipText) {
			Util::HoverTip(config.tooltipText);
		}
	}

	void DrawMultiLineTooltip(const std::vector<std::string>& lines, const std::vector<ImVec4>& colors)
	{
		for (size_t i = 0; i < lines.size(); ++i) {
			const char* lineCStr = lines[i].c_str();
			if (!colors.empty() && i < colors.size()) {
				// Use provided color for this line
				ImGui::TextColored(colors[i], "%s", lineCStr);
			} else {
				// Use default color
				ImGui::Text("%s", lineCStr);
			}
		}
	}

	void DrawColoredMultiLineTooltip(const ColoredTextLines& lines)
	{
		for (const auto& line : lines) {
			ImGui::TextColored(line.color, "%s", line.text.c_str());
		}
	}

	void SortTableRowsByColumn(std::vector<std::vector<std::string>>& rows, size_t column, bool ascending)
	{
		std::sort(rows.begin(), rows.end(), [column, ascending](const auto& a, const auto& b) {
			if (column >= a.size() || column >= b.size())
				return false;
			return ascending ? (a[column] < b[column]) : (a[column] > b[column]);
		});
	}

	bool VersionStringLess(const std::string& a, const std::string& b, bool ascending)
	{
		const auto parsePart = [](const std::string& part) {
			try {
				return std::stoi(part);
			} catch (...) {
				return 0;
			}
		};
		const auto split = [&](const std::string& s) {
			std::vector<int> parts;
			size_t start = 0, end = 0;
			while ((end = s.find('.', start)) != std::string::npos) {
				parts.push_back(parsePart(s.substr(start, end - start)));
				start = end + 1;
			}
			if (start < s.size())
				parts.push_back(parsePart(s.substr(start)));
			return parts;
		};
		auto va = split(a), vb = split(b);
		for (size_t i = 0; i < std::max(va.size(), vb.size()); ++i) {
			int ai = i < va.size() ? va[i] : 0;
			int bi = i < vb.size() ? vb[i] : 0;
			if (ai != bi)
				return ascending ? (ai < bi) : (ai > bi);
		}
		return false;
	}

	bool VersionSortComparator(const std::string& a, const std::string& b, bool asc)
	{
		return VersionStringLess(a, b, asc);
	}

	bool StringSortComparator(const std::string& a, const std::string& b, bool ascending)
	{
		return ascending ? (a < b) : (b < a);
	}

	void RenderTextWithHighlights(const std::string& text, const std::string& searchTerm, ImVec4 highlightColor)
	{
		if (searchTerm.empty()) {
			ImGui::TextUnformatted(text.c_str());
			return;
		}

		std::string lowerText = text;
		std::string lowerSearch = searchTerm;
		std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });
		std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

		size_t pos = 0;
		size_t lastPos = 0;

		while ((pos = lowerText.find(lowerSearch, lastPos)) != std::string::npos) {
			// Render text before highlight
			if (pos > lastPos) {
				ImGui::TextUnformatted(text.substr(lastPos, pos - lastPos).c_str());
				ImGui::SameLine(0, 0);
			}

			// Render highlighted text
			ImGui::PushStyleColor(ImGuiCol_Text, highlightColor);
			ImGui::TextUnformatted(text.substr(pos, searchTerm.length()).c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(0, 0);

			lastPos = pos + searchTerm.length();
		}

		// Render remaining text
		if (lastPos < text.length()) {
			ImGui::TextUnformatted(text.substr(lastPos).c_str());
		}
	}

	ImVec4 GetThresholdColor(float value, float good, float warn, ImVec4 goodColor, ImVec4 warnColor, ImVec4 badColor)
	{
		if (value < good)
			return goodColor;
		else if (value < warn)
			return warnColor;
		else
			return badColor;
	}

	bool FeatureMatchesSearch(Feature* feat, const std::string& searchQuery)
	{
		if (searchQuery.empty())
			return true;

		// ASCII-only folding: UTF-8 continuation bytes are left untouched, so localized names still match byte-for-byte
		const auto lowerAscii = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
		const auto contains = [&](std::string_view text) {
			return std::ranges::search(text, searchQuery, {}, lowerAscii, lowerAscii).begin() != text.end();
		};

		// The list shows the (possibly translated) display name and category label, so match those as well as the English names
		return contains(feat->GetShortName()) ||
		       contains(feat->GetName()) ||
		       contains(feat->GetDisplayName()) ||
		       contains(FeatureListRenderer::TranslateCategory(feat->GetCategory()));
	}

	bool StringMatchesSearch(const std::string& text, const std::string& searchQuery)
	{
		if (searchQuery.empty())
			return true;

		// Case-insensitive in every language, not only ASCII
		return FoldForSearch(text).find(FoldForSearch(searchQuery)) != std::string::npos;
	}

	void DrawBreathingText(const char* text, std::optional<ImVec4> color, float speed, float minAlpha, float maxAlpha)
	{
		float alphaRange = maxAlpha - minAlpha;
		float breathe = minAlpha + alphaRange * 0.5f * (1.0f + sinf((float)ImGui::GetTime() * speed));
		const ImVec4 base = color.value_or(globals::menu->GetTheme().Palette.Text);
		ImGui::TextColored(ImVec4(base.x, base.y, base.z, breathe), "%s", text);
	}

	ImVec4 GetPulsingColor(const ImVec4& baseColor, float speed, float minBrightness, float maxBrightness)
	{
		float brightnessRange = maxBrightness - minBrightness;
		float pulse = minBrightness + brightnessRange * 0.5f * (1.0f + sinf((float)ImGui::GetTime() * speed));
		return ImVec4(
			baseColor.x * pulse,
			baseColor.y * pulse,
			baseColor.z * pulse,
			baseColor.w);
	}

	void PushTintedFrameStyle(const ImVec4& color)
	{
		using Constants = ThemeManager::Constants;
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, Constants::TINTED_FRAME_BG_ALPHA));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, Constants::TINTED_FRAME_BG_HOVERED_ALPHA));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, Constants::TINTED_FRAME_BG_ACTIVE_ALPHA));
		ImGui::PushStyleColor(ImGuiCol_Border, color);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Constants::TINTED_FRAME_BORDER_SIZE);
	}

	void PopTintedFrameStyle()
	{
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar();
	}

	namespace detail
	{
		struct ComboSearchState
		{
			char buffer[256] = {};
			bool needsFocus = true;
		};

		static std::unordered_map<std::string, ComboSearchState>& GetComboSearchStates()
		{
			static std::unordered_map<std::string, ComboSearchState> states;
			return states;
		}
	}

	std::string DrawComboSearchInput(const char* id)
	{
		auto& state = detail::GetComboSearchStates()[id];

		if (state.needsFocus) {
			ImGui::SetKeyboardFocusHere();
			state.needsFocus = false;
		}

		const float scale = GetSearchUIScale();
		const float iconSize = ThemeManager::Constants::COMBO_SEARCH_ICON_SIZE * scale;
		constexpr float iconAlpha = ThemeManager::Constants::COMBO_SEARCH_ICON_ALPHA;
		const float iconOffsetX = ThemeManager::Constants::COMBO_SEARCH_ICON_OFFSET_X * scale;
		const float paddingLeft = ThemeManager::Constants::COMBO_SEARCH_PADDING_LEFT * scale;

		char widgetId[128];
		snprintf(widgetId, sizeof(widgetId), "##%s_search", id);

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(paddingLeft, ImGui::GetStyle().FramePadding.y));
		ImGui::InputTextWithHint(widgetId, T("ui.search", "Search..."), state.buffer, IM_ARRAYSIZE(state.buffer));
		ImGui::PopStyleVar();

		ImVec4 iconColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		iconColor.w *= iconAlpha;
		Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), ImVec2(ImGui::GetItemRectMin().x + iconOffsetX, ImGui::GetItemRectMin().y),
			ImVec2(iconSize, ImGui::GetItemRectSize().y), Icons::TI(MenuIcons::kSearch), ImGui::GetColorU32(iconColor));

		ImGui::Separator();

		return state.buffer;
	}

	void ClearComboSearch(const char* id)
	{
		auto& state = detail::GetComboSearchStates()[id];
		state.buffer[0] = '\0';
		state.needsFocus = true;
	}

	void DrawFeatureSearchBar(std::string& searchString, float availableWidth)
	{
		ImGui::PushID("FeatureSearchBar");

		const float scale = GetSearchUIScale();
		const float iconSize = ThemeManager::Constants::SEARCH_ICON_SIZE * scale;
		const float iconSpace = iconSize + ThemeManager::Constants::SEARCH_INPUT_PADDING_EXTRA * scale;

		// Get the current cursor position and available width
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		if (availableWidth <= 0.0f) {
			availableWidth = ImGui::GetContentRegionAvail().x;
		}
		float frameHeight = ImGui::GetFrameHeight();

		// Custom style - always transparent background to avoid click blocking
		ImVec4 bgColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		ImVec4 bgColorActive = C::FEATURE_TOGGLE_ACTIVE_BG;
		// Use theme text color instead of hardcoded color
		auto& palette = globals::menu->GetTheme().Palette;
		ImVec4 textColor = palette.Text;

		ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, bgColor);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, bgColorActive);
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(iconSpace, ThemeManager::Constants::SEARCH_INPUT_FRAME_PADDING_Y * scale));

		// Draw the input field
		ImGui::SetNextItemWidth(availableWidth);
		char buffer[256];
		strncpy_s(buffer, searchString.c_str(), sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';

		if (ImGui::InputTextWithHint("##feature_search", T("ui.search_features", "Search Features..."), buffer, sizeof(buffer))) {
			searchString = buffer;
		}

		// Draw search icon using the reusable function
		ImVec4 iconColor = textColor;
		iconColor.w *= ThemeManager::Constants::SEARCH_ICON_ALPHA;
		Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), ImVec2(cursorPos.x + ThemeManager::Constants::SEARCH_ICON_OFFSET_X * scale, cursorPos.y),
			ImVec2(iconSize, frameHeight), Icons::TI(MenuIcons::kSearch), ImGui::GetColorU32(iconColor));

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
		ImGui::PopID();
	}

	TableSortSpec ReadTableSortSpec(int defaultColumn, bool defaultAscending)
	{
		TableSortSpec spec{ defaultColumn, defaultAscending };
		if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs && sortSpecs->SpecsCount > 0) {
			spec.column = sortSpecs->Specs->ColumnIndex;
			spec.ascending = sortSpecs->Specs->SortDirection == ImGuiSortDirection_Ascending;
		}
		return spec;
	}

	void ShowSortedStringTableStrings(
		const char* table_id,
		const std::vector<std::string>& headers,
		const std::vector<std::vector<std::string>>& rows,
		size_t sortColumn,
		bool ascending,
		const std::vector<TableSortFunc>& customSorts,
		TableCellRenderFunc cellRender)
	{
		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable;
		if (ImGui::BeginTable(table_id, static_cast<int>(headers.size()), flags)) {
			for (const auto& header : headers)
				ImGui::TableSetupColumn(header.c_str());
			ImGui::TableHeadersRow();

			const TableSortSpec spec = ReadTableSortSpec(static_cast<int>(sortColumn), ascending);

			// Make a copy if sorting is needed
			std::vector<std::vector<std::string>> sortedRows = rows;
			if (spec.column >= 0 && static_cast<size_t>(spec.column) < headers.size()) {
				// Fallback to default string sort if no custom sort is provided
				auto cmp = (spec.column < static_cast<int>(customSorts.size()) && customSorts[spec.column]) ? customSorts[spec.column] : StringSortComparator;
				const size_t sortCol = static_cast<size_t>(spec.column);
				SortTableRowsWith<std::vector<std::string>>(sortedRows, spec, [sortCol, &cmp](const std::vector<std::string>& a, const std::vector<std::string>& b, bool sortAsc) {
					const std::string& aVal = (sortCol < a.size()) ? a[sortCol] : std::string();
					const std::string& bVal = (sortCol < b.size()) ? b[sortCol] : std::string();
					return cmp(aVal, bVal, sortAsc);
				});
			}

			// Render rows
			for (size_t rowIdx = 0; rowIdx < sortedRows.size(); ++rowIdx) {
				const auto& row = sortedRows[rowIdx];
				ImGui::TableNextRow();
				for (size_t col = 0; col < headers.size(); ++col) {
					ImGui::TableSetColumnIndex(static_cast<int>(col));
					if (cellRender) {
						const std::string& value = (col < row.size()) ? row[col] : std::string();
						cellRender(static_cast<int>(rowIdx), static_cast<int>(col), value);
					} else {
						if (col < row.size())
							ImGui::TextUnformatted(row[col].c_str());
					}
				}
			}
			ImGui::EndTable();
		}
	}

	void DrawDllVersionTable(
		const char* label,
		const wchar_t* pluginDir,
		const std::vector<std::pair<std::string, std::string>>& dllVersions,
		const char* tableId)
	{
		if (ImGui::Selectable(label)) {
			auto realPath = Util::PathHelpers::GetRealPathFromDataRelative(pluginDir);
			ShellExecuteW(nullptr, L"open", realPath.empty() ? pluginDir : realPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
		std::vector<std::string> headers = { T("ui.dll_table.name", "DLL Name"), T("ui.dll_table.version", "Version") };
		std::vector<std::vector<std::string>> rows;
		rows.reserve(dllVersions.size());
		for (const auto& [name, version] : dllVersions)
			rows.push_back({ name, version });
		std::vector<TableSortFunc> sorters = { nullptr, VersionSortComparator };
		ShowSortedStringTableStrings(tableId, headers, rows, 0, true, sorters);
	}

	void DropdownArrowButton(const char* popupId, const char* tooltip, bool disabled, const char* disabledTooltip)
	{
		const ImVec2 fieldMin = ImGui::GetItemRectMin();
		ImGui::SameLine(0.0f, 0.0f);
		{
			const DisableGuard disableGuard(disabled);
			if (ImGui::ArrowButton(std::format("##Arrow{}", popupId).c_str(), ImGuiDir_Down))
				ImGui::OpenPopup(popupId);
		}
		AddTooltip(disabled && disabledTooltip ? disabledTooltip : tooltip, kTooltipWhenDisabled);

		const ImVec2 buttonMax = ImGui::GetItemRectMax();
		ImGui::SetNextWindowPos(ImVec2(fieldMin.x, buttonMax.y + ImGui::GetStyle().ItemInnerSpacing.y));
		ImGui::SetNextWindowSizeConstraints(ImVec2(buttonMax.x - fieldMin.x, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
	}

	// Theme-aware color accessor functions
	namespace Colors
	{
		ImVec4 GetDefault()
		{
			return globals::menu->GetTheme().Palette.Text;
		}

		ImVec4 GetSuccess()
		{
			return globals::menu->GetTheme().StatusPalette.SuccessColor;
		}

		ImVec4 GetWarning()
		{
			return globals::menu->GetTheme().StatusPalette.Warning;
		}

		ImVec4 GetError()
		{
			return globals::menu->GetTheme().StatusPalette.Error;
		}

		ImVec4 GetUnsaved()
		{
			auto color = GetError();
			color.w = C::UNSAVED_ALPHA;  // muted: a dirty save affordance without a separate label
			return color;
		}

		ImVec4 GetInfo()
		{
			return globals::menu->GetTheme().StatusPalette.InfoColor;
		}

		ImVec4 GetDisabled()
		{
			return globals::menu->GetTheme().StatusPalette.Disable;
		}

		ImVec4 GetSecondary()
		{
			// Keeps well above AA contrast on the theme's dark panels while staying clearly below primary.
			constexpr float kSecondaryTextAlpha = 0.72f;
			auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			color.w *= kSecondaryTextAlpha;
			return color;
		}

		ImVec4 GetAccent()
		{
			auto color = ImGui::GetStyleColorVec4(ImGuiCol_Header);
			color.w = 1.0f;
			return color;
		}

	}

	namespace Text
	{
		static void ColoredTextV(ImVec4 color, const char* fmt, va_list args)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextV(fmt, args);
			ImGui::PopStyleColor();
		}

		static void ColoredTextWrappedV(ImVec4 color, const char* fmt, va_list args)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextWrappedV(fmt, args);
			ImGui::PopStyleColor();
		}

#define UTIL_TEXT(Name, ColorFn)                    \
	void Name(const char* fmt, ...)                 \
	{                                               \
		va_list args;                               \
		va_start(args, fmt);                        \
		ColoredTextV(Colors::ColorFn(), fmt, args); \
		va_end(args);                               \
	}
#define UTIL_TEXT_WRAPPED(Name, ColorFn)                   \
	void Name(const char* fmt, ...)                        \
	{                                                      \
		va_list args;                                      \
		va_start(args, fmt);                               \
		ColoredTextWrappedV(Colors::ColorFn(), fmt, args); \
		va_end(args);                                      \
	}

		UTIL_TEXT(Warning, GetWarning)
		UTIL_TEXT_WRAPPED(WrappedWarning, GetWarning)
		UTIL_TEXT(Error, GetError)
		UTIL_TEXT_WRAPPED(WrappedError, GetError)
		UTIL_TEXT(Success, GetSuccess)
		UTIL_TEXT(Info, GetInfo)
		UTIL_TEXT_WRAPPED(WrappedInfo, GetInfo)
		UTIL_TEXT(Disabled, GetDisabled)
		UTIL_TEXT_WRAPPED(WrappedDisabled, GetDisabled)
		UTIL_TEXT(Secondary, GetSecondary)
		UTIL_TEXT_WRAPPED(WrappedSecondary, GetSecondary)

#undef UTIL_TEXT
#undef UTIL_TEXT_WRAPPED
	}

	namespace Input
	{
#define IM_VK_KEYPAD_ENTER (VK_RETURN + 256)

		ImGuiKey VirtualKeyToImGuiKey(WPARAM vkKey)
		{
			switch (vkKey) {
			case VK_TAB:
				return ImGuiKey_Tab;
			case VK_LEFT:
				return ImGuiKey_LeftArrow;
			case VK_RIGHT:
				return ImGuiKey_RightArrow;
			case VK_UP:
				return ImGuiKey_UpArrow;
			case VK_DOWN:
				return ImGuiKey_DownArrow;
			case VK_PRIOR:
				return ImGuiKey_PageUp;
			case VK_NEXT:
				return ImGuiKey_PageDown;
			case VK_HOME:
				return ImGuiKey_Home;
			case VK_END:
				return ImGuiKey_End;
			case VK_INSERT:
				return ImGuiKey_Insert;
			case VK_DELETE:
				return ImGuiKey_Delete;
			case VK_BACK:
				return ImGuiKey_Backspace;
			case VK_SPACE:
				return ImGuiKey_Space;
			case VK_RETURN:
				return ImGuiKey_Enter;
			case VK_ESCAPE:
				return ImGuiKey_Escape;
			case VK_OEM_7:
				return ImGuiKey_Apostrophe;
			case VK_OEM_COMMA:
				return ImGuiKey_Comma;
			case VK_OEM_MINUS:
				return ImGuiKey_Minus;
			case VK_OEM_PERIOD:
				return ImGuiKey_Period;
			case VK_OEM_2:
				return ImGuiKey_Slash;
			case VK_OEM_1:
				return ImGuiKey_Semicolon;
			case VK_OEM_PLUS:
				return ImGuiKey_Equal;
			case VK_OEM_4:
				return ImGuiKey_LeftBracket;
			case VK_OEM_5:
				return ImGuiKey_Backslash;
			case VK_OEM_6:
				return ImGuiKey_RightBracket;
			case VK_OEM_3:
				return ImGuiKey_GraveAccent;
			case VK_CAPITAL:
				return ImGuiKey_CapsLock;
			case VK_SCROLL:
				return ImGuiKey_ScrollLock;
			case VK_NUMLOCK:
				return ImGuiKey_NumLock;
			case VK_SNAPSHOT:
				return ImGuiKey_PrintScreen;
			case VK_PAUSE:
				return ImGuiKey_Pause;
			case VK_NUMPAD0:
				return ImGuiKey_Keypad0;
			case VK_NUMPAD1:
				return ImGuiKey_Keypad1;
			case VK_NUMPAD2:
				return ImGuiKey_Keypad2;
			case VK_NUMPAD3:
				return ImGuiKey_Keypad3;
			case VK_NUMPAD4:
				return ImGuiKey_Keypad4;
			case VK_NUMPAD5:
				return ImGuiKey_Keypad5;
			case VK_NUMPAD6:
				return ImGuiKey_Keypad6;
			case VK_NUMPAD7:
				return ImGuiKey_Keypad7;
			case VK_NUMPAD8:
				return ImGuiKey_Keypad8;
			case VK_NUMPAD9:
				return ImGuiKey_Keypad9;
			case VK_DECIMAL:
				return ImGuiKey_KeypadDecimal;
			case VK_DIVIDE:
				return ImGuiKey_KeypadDivide;
			case VK_MULTIPLY:
				return ImGuiKey_KeypadMultiply;
			case VK_SUBTRACT:
				return ImGuiKey_KeypadSubtract;
			case VK_ADD:
				return ImGuiKey_KeypadAdd;
			case IM_VK_KEYPAD_ENTER:
				return ImGuiKey_KeypadEnter;
			case VK_LSHIFT:
				return ImGuiKey_LeftShift;
			case VK_LCONTROL:
				return ImGuiKey_LeftCtrl;
			case VK_LMENU:
				return ImGuiKey_LeftAlt;
			case VK_LWIN:
				return ImGuiKey_LeftSuper;
			case VK_RSHIFT:
				return ImGuiKey_RightShift;
			case VK_RCONTROL:
				return ImGuiKey_RightCtrl;
			case VK_RMENU:
				return ImGuiKey_RightAlt;
			case VK_RWIN:
				return ImGuiKey_RightSuper;
			case VK_APPS:
				return ImGuiKey_Menu;
			case '0':
				return ImGuiKey_0;
			case '1':
				return ImGuiKey_1;
			case '2':
				return ImGuiKey_2;
			case '3':
				return ImGuiKey_3;
			case '4':
				return ImGuiKey_4;
			case '5':
				return ImGuiKey_5;
			case '6':
				return ImGuiKey_6;
			case '7':
				return ImGuiKey_7;
			case '8':
				return ImGuiKey_8;
			case '9':
				return ImGuiKey_9;
			case 'A':
				return ImGuiKey_A;
			case 'B':
				return ImGuiKey_B;
			case 'C':
				return ImGuiKey_C;
			case 'D':
				return ImGuiKey_D;
			case 'E':
				return ImGuiKey_E;
			case 'F':
				return ImGuiKey_F;
			case 'G':
				return ImGuiKey_G;
			case 'H':
				return ImGuiKey_H;
			case 'I':
				return ImGuiKey_I;
			case 'J':
				return ImGuiKey_J;
			case 'K':
				return ImGuiKey_K;
			case 'L':
				return ImGuiKey_L;
			case 'M':
				return ImGuiKey_M;
			case 'N':
				return ImGuiKey_N;
			case 'O':
				return ImGuiKey_O;
			case 'P':
				return ImGuiKey_P;
			case 'Q':
				return ImGuiKey_Q;
			case 'R':
				return ImGuiKey_R;
			case 'S':
				return ImGuiKey_S;
			case 'T':
				return ImGuiKey_T;
			case 'U':
				return ImGuiKey_U;
			case 'V':
				return ImGuiKey_V;
			case 'W':
				return ImGuiKey_W;
			case 'X':
				return ImGuiKey_X;
			case 'Y':
				return ImGuiKey_Y;
			case 'Z':
				return ImGuiKey_Z;
			case VK_F1:
				return ImGuiKey_F1;
			case VK_F2:
				return ImGuiKey_F2;
			case VK_F3:
				return ImGuiKey_F3;
			case VK_F4:
				return ImGuiKey_F4;
			case VK_F5:
				return ImGuiKey_F5;
			case VK_F6:
				return ImGuiKey_F6;
			case VK_F7:
				return ImGuiKey_F7;
			case VK_F8:
				return ImGuiKey_F8;
			case VK_F9:
				return ImGuiKey_F9;
			case VK_F10:
				return ImGuiKey_F10;
			case VK_F11:
				return ImGuiKey_F11;
			case VK_F12:
				return ImGuiKey_F12;
			default:
				return ImGuiKey_None;
			};
		}

		bool IsRejectedByNumericTextEntry(uint32_t character)
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();
			if (context.TempInputId == 0 || context.ActiveId != context.TempInputId)
				return false;
			return !(character >= '0' && character <= '9') && character != '.' && character != '-';
		}

		uint32_t DIKToVK(uint32_t dikKey)
		{
			switch (dikKey) {
			case DIK_LEFTARROW:
				return VK_LEFT;
			case DIK_RIGHTARROW:
				return VK_RIGHT;
			case DIK_UPARROW:
				return VK_UP;
			case DIK_DOWNARROW:
				return VK_DOWN;
			case DIK_DELETE:
				return VK_DELETE;
			case DIK_END:
				return VK_END;
			case DIK_HOME:
				return VK_HOME;  // pos1
			case DIK_PRIOR:
				return VK_PRIOR;  // page up
			case DIK_NEXT:
				return VK_NEXT;  // page down
			case DIK_INSERT:
				return VK_INSERT;
			case DIK_NUMPAD0:
				return VK_NUMPAD0;
			case DIK_NUMPAD1:
				return VK_NUMPAD1;
			case DIK_NUMPAD2:
				return VK_NUMPAD2;
			case DIK_NUMPAD3:
				return VK_NUMPAD3;
			case DIK_NUMPAD4:
				return VK_NUMPAD4;
			case DIK_NUMPAD5:
				return VK_NUMPAD5;
			case DIK_NUMPAD6:
				return VK_NUMPAD6;
			case DIK_NUMPAD7:
				return VK_NUMPAD7;
			case DIK_NUMPAD8:
				return VK_NUMPAD8;
			case DIK_NUMPAD9:
				return VK_NUMPAD9;
			case DIK_DECIMAL:
				return VK_DECIMAL;
			case DIK_NUMPADENTER:
				return IM_VK_KEYPAD_ENTER;
			case DIK_RMENU:
				return VK_RMENU;  // right alt
			case DIK_RCONTROL:
				return VK_RCONTROL;  // right control
			case DIK_LWIN:
				return VK_LWIN;  // left win
			case DIK_RWIN:
				return VK_RWIN;  // right win
			case DIK_APPS:
				return VK_APPS;
			case DIK_SYSRQ:
				return VK_SNAPSHOT;
			default:
				return dikKey;
			}
		}

		const char* KeyIdToString(uint32_t key)
		{
			if (key >= 256)
				return "";

			static const char* keyboard_keys_international[256] = {
				"", "Left Mouse", "Right Mouse", "Cancel", "Middle Mouse", "X1 Mouse", "X2 Mouse", "", "Backspace", "Tab", "", "", "Clear", "Enter", "", "",
				"Shift", "Control", "Alt", "Pause", "Caps Lock", "", "", "", "", "", "", "Escape", "", "", "", "",
				"Space", "Page Up", "Page Down", "End", "Home", "Left Arrow", "Up Arrow", "Right Arrow", "Down Arrow", "Select", "", "", "Print Screen", "Insert", "Delete", "Help",
				"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
				"", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
				"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Windows", "Right Windows", "Apps", "", "Sleep",
				"Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad *", "Numpad +", "", "Numpad -", "Numpad Decimal", "Numpad /",
				"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
				"F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "", "", "", "", "", "", "", "",
				"Num Lock", "Scroll Lock", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"Left Shift", "Right Shift", "Left Control", "Right Control", "Left Menu", "Right Menu", "Browser Back", "Browser Forward", "Browser Refresh", "Browser Stop", "Browser Search", "Browser Favorites", "Browser Home", "Volume Mute", "Volume Down", "Volume Up",
				"Next Track", "Previous Track", "Media Stop", "Media Play/Pause", "Mail", "Media Select", "Launch App 1", "Launch App 2", "", "", "OEM ;", "OEM +", "OEM ,", "OEM -", "OEM .", "OEM /",
				"OEM ~", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"", "", "", "", "", "", "", "", "", "", "", "OEM [", "OEM \\", "OEM ]", "OEM '", "OEM 8",
				"", "", "OEM <", "", "", "", "", "", "", "", "", "", "", "", "", "",
				"", "", "", "", "", "", "Attn", "CrSel", "ExSel", "Erase EOF", "Play", "Zoom", "", "PA1", "OEM Clear", ""
			};

			return keyboard_keys_international[key];
		}

		std::string KeyIdToString(const std::vector<InputCombo>& combo)
		{
			if (combo.empty())
				return "None";

			std::string result;
			for (size_t i = 0; i < combo.size(); ++i) {
				if (i > 0)
					result += " + ";
				result += KeyIdToString(combo[i].GetKey());
			}
			return result;
		}
	}  // namespace Input

	bool ButtonWithFlash(const char* label, const ImVec2& size, int flashDurationMs)
	{
		static std::unordered_map<std::string, std::chrono::steady_clock::time_point> flashTimers;
		static std::mutex flashTimersMutex;

		std::string buttonId = std::string(label);
		auto now = std::chrono::steady_clock::now();

		// Check if this button has active flash (thread-safe)
		bool hasActiveFlash = false;
		{
			std::lock_guard<std::mutex> lock(flashTimersMutex);
			auto it = flashTimers.find(buttonId);
			if (it != flashTimers.end()) {
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
				if (elapsed.count() < flashDurationMs) {
					hasActiveFlash = true;
				} else {
					// Flash expired, remove it
					flashTimers.erase(it);
				}
			}
		}

		// Style the button with flash effect if active.
		bool styleChanged = false;
		if (hasActiveFlash) {
			// Use subtle white overlay similar to action icon hover effect
			ImVec4 normalButton = ImGui::GetStyleColorVec4(ImGuiCol_Button);
			ImVec4 flashColor = ImVec4(
				normalButton.x + C::FLASH_BRIGHTEN,
				normalButton.y + C::FLASH_BRIGHTEN,
				normalButton.z + C::FLASH_BRIGHTEN,
				normalButton.w);
			ImVec4 flashHovered = ImVec4(flashColor.x * C::FLASH_HOVER_SCALE, flashColor.y * C::FLASH_HOVER_SCALE, flashColor.z * C::FLASH_HOVER_SCALE, flashColor.w);
			ImVec4 flashActive = ImVec4(flashColor.x * C::FLASH_ACTIVE_SCALE, flashColor.y * C::FLASH_ACTIVE_SCALE, flashColor.z * C::FLASH_ACTIVE_SCALE, flashColor.w);

			ImGui::PushStyleColor(ImGuiCol_Button, flashColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, flashHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, flashActive);
			styleChanged = true;
		}

		bool clicked = ImGui::Button(label, size);

		if (styleChanged) {
			ImGui::PopStyleColor(3);
		}

		// If clicked, start the flash timer (thread-safe)
		if (clicked) {
			std::lock_guard<std::mutex> lock(flashTimersMutex);
			flashTimers[buttonId] = now;
		}

		return clicked;
	}

	bool LoadDDSTextureFromFile(ID3D11Device* device,
		const char* filename,
		ID3D11ShaderResourceView** out_srv,
		ImVec2& out_size)
	{
		if (!device || !out_srv) {
			logger::warn("LoadDDSTextureFromFile: Invalid parameters");
			return false;
		}

		*out_srv = nullptr;

		// Try to load from BSA using Skyrim's resource system
		RE::BSResourceNiBinaryStream bsaStream(filename);
		if (!bsaStream.good()) {
			logger::warn("LoadDDSTextureFromFile: Failed to open resource: {}", filename);
			return false;
		}

		// Read entire DDS file into memory
		std::vector<uint8_t> ddsData;
		auto size = bsaStream.stream->totalSize;
		if (size == 0) {
			logger::warn("LoadDDSTextureFromFile: Resource has zero size: {}", filename);
			return false;
		}

		ddsData.resize(size);
		bsaStream.read(reinterpret_cast<char*>(ddsData.data()), size);

		// Load DDS from memory
		DirectX::ScratchImage image;
		try {
			DX::ThrowIfFailed(DirectX::LoadFromDDSMemory(
				ddsData.data(),
				ddsData.size(),
				DirectX::DDS_FLAGS_NONE,
				nullptr,
				image));
		} catch (const DX::com_exception& e) {
			logger::warn("LoadDDSTextureFromFile: Failed to load DDS data from {}: {}", filename, e.what());
			return false;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(DirectX::CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::warn("LoadDDSTextureFromFile: Failed to create texture: {}", e.what());
			return false;
		}

		ID3D11Texture2D* pTexture = reinterpret_cast<ID3D11Texture2D*>(pResource);
		D3D11_TEXTURE2D_DESC desc;
		pTexture->GetDesc(&desc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = desc.MipLevels }
		};

		HRESULT hr = device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
		if (SUCCEEDED(hr) && *out_srv)
			Util::SetResourceName(*out_srv, "UI::DDS:%s", filename);
		pTexture->Release();

		if (FAILED(hr) || !*out_srv) {
			logger::warn("LoadDDSTextureFromFile: Failed to create SRV, HRESULT: 0x{:08X}", static_cast<uint32_t>(hr));
			return false;
		}

		out_size = ImVec2((float)desc.Width, (float)desc.Height);
		logger::debug("LoadDDSTextureFromFile: Successfully loaded {} ({}x{})", filename, desc.Width, desc.Height);
		return true;
	}

	bool FeatureToggle(const char* label, bool* enabled, const ImVec2& size)
	{
		if (!enabled)
			return false;

		using Constants = ThemeManager::Constants;
		ImVec2 toggleSize = size;
		if (toggleSize.x <= 0)
			toggleSize.x = ImGui::GetFrameHeight() * C::FEATURE_TOGGLE_WIDTH_RATIO;
		if (toggleSize.y <= 0)
			toggleSize.y = ImGui::GetFrameHeight() * C::FEATURE_TOGGLE_HEIGHT_RATIO;

		ImGui::PushID(label);
		const ImGuiID id = ImGui::GetID("toggle");
		const bool clicked = ImGui::InvisibleButton("##toggle", toggleSize);
		const bool hovered = ImGui::IsItemHovered();
		if (clicked)
			*enabled = !*enabled;
		ImGui::PopID();

		// Material style: a thin pill track with a larger round knob that overhangs both its ends. The knob slides
		// (eased, and snapped while UI animations are off), and its colours follow the theme's accent.
		const float on = Anim::Approach(id, *enabled ? 1.0f : 0.0f, Constants::ANIM_TOGGLE_RATE, *enabled ? 1.0f : 0.0f);
		const float hover = Anim::Approach(id + 1, hovered ? 1.0f : 0.0f, Constants::ANIM_HOVER_RATE, 0.0f);

		const ImVec2 min = ImGui::GetItemRectMin();
		const float knobRadius = toggleSize.y * 0.5f;
		const float trackHeight = toggleSize.y * Constants::TOGGLE_TRACK_HEIGHT_RATIO;
		const float trackOverhang = knobRadius * Constants::TOGGLE_TRACK_INSET_RATIO;
		const float centerY = min.y + toggleSize.y * 0.5f;
		const ImVec2 trackMin(min.x + trackOverhang, centerY - trackHeight * 0.5f);
		const ImVec2 trackMax(min.x + toggleSize.x - trackOverhang, centerY + trackHeight * 0.5f);
		const float knobX = min.x + knobRadius + (toggleSize.x - knobRadius * 2.0f) * on;

		const ImVec4 accent = Colors::GetAccent();
		const ImVec4 trackOff = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
		const ImVec4 trackOn = Anim::ScaleAlpha(accent, Constants::TOGGLE_TRACK_ON_ALPHA);
		const ImVec4 knobOff = Anim::Lerp(Colors::GetSecondary(), ImGui::GetStyleColorVec4(ImGuiCol_Text), hover);
		const ImVec4 knobOn = Anim::Lerp(accent, ImVec4(1.0f, 1.0f, 1.0f, accent.w), hover * Constants::TOGGLE_KNOB_HOVER_LIFT);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(Anim::Lerp(trackOff, trackOn, on)), trackHeight * 0.5f);
		// A soft shadow lifts the knob off the track, as in the Material design
		drawList->AddCircleFilled(ImVec2(knobX, centerY + Constants::TOGGLE_SHADOW_OFFSET_RATIO * knobRadius), knobRadius,
			ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, Constants::TOGGLE_SHADOW_ALPHA)));
		drawList->AddCircleFilled(ImVec2(knobX, centerY), knobRadius, ImGui::GetColorU32(Anim::Lerp(knobOff, knobOn, on)));

		return clicked;
	}

	bool InputComboWidget(
		const char* label,
		std::vector<InputCombo>& combo,
		bool& isRecording,
		const char* recordingLabel)
	{
		bool changed = false;
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s", label);
		ImGui::SameLine();

		// Use theme colors for consistent styling
		auto& theme = globals::menu->GetTheme().StatusPalette;

		if (isRecording) {
			// Recording state visual
			ImGui::PushStyleColor(ImGuiCol_Button, theme.CurrentHotkey);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.CurrentHotkey);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.CurrentHotkey);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));  // Black text on recording color

			// Show current pending combo if available, otherwise prompt
			std::string buttonText;
			if (!combo.empty()) {
				buttonText = Util::Input::KeyIdToString(combo) + "...";  // Indicate it's still capturing
			} else {
				buttonText = T("ui.keybind.recording", "Recording... (Esc to cancel)");
			}

			if (ImGui::Button(buttonText.c_str(), ImVec2(0, 0))) {
				isRecording = false;
			}

			ImGui::PopStyleColor(4);

			// Add tooltip explaining how to record
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", T("ui.keybind.record_tooltip", "Press any key combination.\nModifiers (Ctrl, Shift, Alt) are supported.\nPress Escape to cancel."));
			}
		} else {
			// Display current binding with unique button ID
			std::string keyString = Util::Input::KeyIdToString(combo);
			std::string btnLabel = keyString + "##" + recordingLabel;
			if (ImGui::Button(btnLabel.c_str(), ImVec2(0, 0))) {
				isRecording = true;
			}

			// Context menu for clearing
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::Selectable(T("ui.keybind.clear", "Clear Binding"))) {
					combo.clear();
					changed = true;
				}
				ImGui::EndPopup();
			}

			// First run / empty state hint
			if (combo.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", T("ui.keybind.click_to_bind", "(Click to bind)"));
			}
		}

		return changed;
	}

	namespace ConstrainedUI
	{
		namespace
		{
			// Helper to render constraint tooltip
			void RenderConstraintTooltip(const FeatureConstraints::ConstraintResult& constraint)
			{
				if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					return;

				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * ThemeManager::Constants::TOOLTIP_WRAP_FONT_SIZES);
				Util::Text::Warning("%s", T("ui.constraint.title", "Setting Constrained"));
				ImGui::Text("%s", T("ui.constraint.constrained_by", "This setting is constrained by:"));
				ImGui::Spacing();
				for (const auto& src : constraint.sources) {
					ImGui::BulletText("%s", src.featureName.c_str());
					ImGui::Indent();
					ImGui::TextWrapped("%s", src.reason.c_str());
					if (src.recommendDisableAtBoot) {
						Util::Text::WrappedError("%s", T("ui.constraint.disable_at_boot", "Consider disabling this feature at boot for best compatibility."));
					}
					ImGui::Unindent();
				}
				ImGui::Separator();
				ImGui::Text("%s: %s", T("ui.constraint.forced_value", "Forced value"), FeatureConstraints::FormatConstraintValue(constraint.forcedValue).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

		bool Checkbox(const char* label, bool* value, const FeatureConstraints::SettingId& settingId)
		{
			auto constraint = FeatureConstraints::GetConstraints(settingId);

			if (constraint.isConstrained) {
				// Display the forced value instead of the stored value
				if (auto* forcedBool = std::get_if<bool>(&constraint.forcedValue)) {
					bool displayValue = *forcedBool;
					ImGui::BeginDisabled();
					ImGui::Checkbox(label, &displayValue);
					ImGui::EndDisabled();
				} else {
					// Fallback: wrong type, show disabled with stored value
					ImGui::BeginDisabled();
					ImGui::Checkbox(label, value);
					ImGui::EndDisabled();
				}
				RenderConstraintTooltip(constraint);
				return false;
			}

			return ImGui::Checkbox(label, value);
		}

		bool SliderFloat(const char* label, float* value, float min, float max,
			const FeatureConstraints::SettingId& settingId, const char* format)
		{
			auto constraint = FeatureConstraints::GetConstraints(settingId);

			if (constraint.isConstrained) {
				// Display the forced value instead of the stored value
				if (auto* forcedFloat = std::get_if<float>(&constraint.forcedValue)) {
					float displayValue = *forcedFloat;
					ImGui::BeginDisabled();
					ImGui::SliderFloat(label, &displayValue, min, max, format);
					ImGui::EndDisabled();
				} else {
					// Fallback: wrong type, show disabled with stored value
					ImGui::BeginDisabled();
					ImGui::SliderFloat(label, value, min, max, format);
					ImGui::EndDisabled();
				}
				RenderConstraintTooltip(constraint);
				return false;
			}

			return ImGui::SliderFloat(label, value, min, max, format);
		}

		bool SliderInt(const char* label, int* value, int min, int max,
			const FeatureConstraints::SettingId& settingId, const char* format)
		{
			auto constraint = FeatureConstraints::GetConstraints(settingId);

			if (constraint.isConstrained) {
				// Display the forced value instead of the stored value
				if (auto* forcedInt = std::get_if<int>(&constraint.forcedValue)) {
					int displayValue = *forcedInt;
					ImGui::BeginDisabled();
					ImGui::SliderInt(label, &displayValue, min, max, format);
					ImGui::EndDisabled();
				} else {
					// Fallback: wrong type, show disabled with stored value
					ImGui::BeginDisabled();
					ImGui::SliderInt(label, value, min, max, format);
					ImGui::EndDisabled();
				}
				RenderConstraintTooltip(constraint);
				return false;
			}

			return ImGui::SliderInt(label, value, min, max, format);
		}
	}
}  // namespace Util
