#include "Utils/UIAnim.h"

#include "Globals.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"
#include "Utils/UISurface.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace Util::Anim
{
	using Constants = ThemeManager::Constants;

	bool ReducedMotion()
	{
		return globals::menu && globals::menu->GetSettings().ReduceUIAnimations;
	}

	float Speed()
	{
		const float speed = globals::menu ? globals::menu->GetSettings().AnimationSpeed : 1.0f;
		return std::isfinite(speed) ? std::clamp(speed, Constants::ANIM_SPEED_MIN, Constants::ANIM_SPEED_MAX) : 1.0f;
	}

	float EaseOutCubic(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		const float inv = 1.0f - t;
		return 1.0f - inv * inv * inv;
	}

	float EaseInOut(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	float Smooth(float current, float target, float rate)
	{
		if (ReducedMotion() || std::abs(target - current) < Constants::ANIM_EPSILON)
			return target;
		return current + (target - current) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * rate * Speed()));
	}

	float Approach(ImGuiID id, float target, float rate, float initial)
	{
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const float value = Smooth(storage->GetFloat(id, std::isnan(initial) ? target : initial), target, rate);
		storage->SetFloat(id, value);
		return value;
	}

	float Toggle(ImGuiID id, bool on, float rate)
	{
		return Approach(id, on ? 1.0f : 0.0f, rate > 0.0f ? rate : Constants::ANIM_HOVER_RATE);
	}

	Blend ItemBlend(ImGuiID id, bool hovered, bool held, bool selected)
	{
		Blend blend;
		blend.hover = Approach(ImGui::GetID("hv") + id, hovered ? 1.0f : 0.0f, Constants::ANIM_HOVER_RATE);
		blend.press = Approach(ImGui::GetID("pr") + id, held ? 1.0f : 0.0f, Constants::ANIM_HOVER_RATE);
		blend.selected = Approach(ImGui::GetID("sl") + id, selected ? 1.0f : 0.0f, Constants::ANIM_SELECT_RATE);
		return blend;
	}

	float EnterProgress(ImGuiID key, float delay, float duration)
	{
		if (ReducedMotion())
			return 1.0f;
		if (duration <= 0.0f)
			duration = Constants::ANIM_PAGE_ENTER_SECONDS;
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID frameKey = key + 1;
		const ImGuiID startKey = key + 2;
		const int frame = ImGui::GetFrameCount();
		// Not asked for last frame: a new visit
		if (storage->GetInt(frameKey, -10) < frame - 1)
			storage->SetFloat(startKey, static_cast<float>(ImGui::GetTime()));
		storage->SetInt(frameKey, frame);
		const float elapsed = static_cast<float>(ImGui::GetTime()) - storage->GetFloat(startKey, 0.0f) - delay;
		return EaseOutCubic(elapsed * Speed() / duration);
	}

	GlideRect SelectionGlide(ImGuiID listId, const ImVec2& targetMin, const ImVec2& targetMax, float rate)
	{
		if (rate <= 0.0f)
			rate = Constants::ANIM_GLIDE_RATE;
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImVec2 origin(ImGui::GetWindowPos().x - ImGui::GetScrollX(), ImGui::GetWindowPos().y - ImGui::GetScrollY());
		const float target[4] = { targetMin.x - origin.x, targetMin.y - origin.y, targetMax.x - origin.x, targetMax.y - origin.y };
		const ImGuiID frameKey = listId + 5;
		const int frame = ImGui::GetFrameCount();
		const bool fresh = storage->GetInt(frameKey, -10) < frame - 1;
		storage->SetInt(frameKey, frame);

		float value[4];
		for (ImGuiID n = 0; n < 4; ++n) {
			value[n] = Smooth(fresh ? target[n] : storage->GetFloat(listId + 1 + n, target[n]), target[n], rate);
			storage->SetFloat(listId + 1 + n, value[n]);
		}
		return { ImVec2(value[0] + origin.x, value[1] + origin.y), ImVec2(value[2] + origin.x, value[3] + origin.y) };
	}

	EnterGroup::EnterGroup(ImGuiID groupId, float stagger) :
		groupId(groupId),
		stagger(stagger > 0.0f ? stagger : Constants::ANIM_CARD_STAGGER_SECONDS)
	{}

	float EnterGroup::Next(ImGuiID memberId)
	{
		return EnterProgress(groupId + memberId, stagger * static_cast<float>(index++));
	}

	float Pulse(float periodSeconds)
	{
		if (ReducedMotion())
			return 0.0f;
		if (periodSeconds <= 0.0f)
			periodSeconds = Constants::ANIM_PULSE_PERIOD;
		constexpr float twoPi = 6.28318530718f;
		return 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * twoPi / periodSeconds);
	}

	ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t)
	{
		return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
	}

	ImU32 LerpU32(ImU32 a, ImU32 b, float t)
	{
		return ImGui::ColorConvertFloat4ToU32(Lerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), t));
	}

	ImVec4 ScaleAlpha(const ImVec4& color, float alpha)
	{
		return ImVec4(color.x, color.y, color.z, color.w * alpha);
	}

	ImU32 ScaleAlphaU32(ImU32 color, float alpha)
	{
		ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
		c.w *= alpha;
		return ImGui::ColorConvertFloat4ToU32(c);
	}

	float BeginFadeIn(ImGuiID key, float duration, float fromAlpha)
	{
		const float t = EnterProgress(key, 0.0f, duration > 0.0f ? duration : Constants::ANIM_POPUP_SECONDS);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (fromAlpha + (1.0f - fromAlpha) * t));
		return t;
	}

	void EndFadeIn()
	{
		ImGui::PopStyleVar();
	}

	namespace
	{
		std::uint32_t g_pageEpoch = 0;
		ImGuiID g_lastPageId = 0;
	}

	std::uint32_t PageEpoch()
	{
		return g_pageEpoch;
	}

	bool EasedSliderScalar(SliderScalarFn real, const char* label, ImGuiDataType dataType, void* data, const void* min,
		const void* max, const char* format, ImGuiSliderFlags flags)
	{
		const bool isFloat = dataType == ImGuiDataType_Float;
		const bool isInt = dataType == ImGuiDataType_S32;
		if (ReducedMotion() || (!isFloat && !isInt))
			return real(label, dataType, data, min, max, format, flags);

		const auto read = [&](const void* value) {
			return isFloat ? *static_cast<const float*>(value) : static_cast<float>(*static_cast<const int*>(value));
		};
		const ImGuiID id = ImGui::GetID(label);
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID shownKey = id ^ 0xA5A5A5A5u;
		const ImGuiID frameKey = id ^ 0x3C3C3C3Cu;
		const ImGuiID epochKey = id ^ 0x7E7E7E7Eu;

		const float target = read(data);
		// A slider that was not drawn last frame, or that belongs to a page that was just opened, starts at rest
		const int frame = ImGui::GetFrameCount();
		const int epoch = static_cast<int>(g_pageEpoch);
		const bool fresh = storage->GetInt(frameKey, -10) < frame - 1 || storage->GetInt(epochKey, -1) != epoch;
		storage->SetInt(frameKey, frame);
		storage->SetInt(epochKey, epoch);

		float shown = target;
		if (!fresh && ImGui::GetActiveID() != id) {
			shown = Smooth(storage->GetFloat(shownKey, target), target, Constants::ANIM_SLIDER_RATE);
			const float snap = std::max(isInt ? 0.5f : 0.0f, std::abs(read(max) - read(min)) * Constants::ANIM_SLIDER_SNAP_FRACTION);
			if (std::abs(shown - target) <= snap)
				shown = target;
		}

		if (shown == target) {
			storage->SetFloat(shownKey, target);
			return real(label, dataType, data, min, max, format, flags);
		}

		// Easing: the widget draws a copy, and only a real edit is written back
		float easedFloat = shown;
		int easedInt = static_cast<int>(std::lround(shown));
		void* eased = isFloat ? static_cast<void*>(&easedFloat) : static_cast<void*>(&easedInt);
		const bool changed = real(label, dataType, eased, min, max, format, flags);
		if (changed) {
			if (isFloat)
				*static_cast<float*>(data) = easedFloat;
			else
				*static_cast<int*>(data) = easedInt;
			storage->SetFloat(shownKey, read(data));
		} else {
			storage->SetFloat(shownKey, shown);
		}
		return changed;
	}

	PageFade::PageFade(ImGuiID pageId)
	{
		if (pageId != g_lastPageId) {
			g_lastPageId = pageId;
			++g_pageEpoch;
		}
		BeginFadeIn(pageId, Constants::ANIM_PAGE_ENTER_SECONDS, Constants::ANIM_PAGE_FADE_FROM);
	}

	PageFade::~PageFade()
	{
		EndFadeIn();
	}

	namespace
	{
		int g_noListHighlight = 0;

		// Salts that separate one list's state keys; the list key is its window's id
		constexpr ImGuiID kHoverIdSalt = 0x1B873593u;
		constexpr ImGuiID kHoverFrameSalt = 0x2C4F8E15u;
		constexpr ImGuiID kSelectedFrameSalt = 0x3D91A7C3u;
		constexpr ImGuiID kDrawnFrameSalt = 0x4E27B5D9u;
		constexpr ImGuiID kLastDrawSalt = 0x5F6A93E1u;
		constexpr ImGuiID kVisSalt = 0x6A09E667u;
		constexpr ImGuiID kSelSalt = 0x7B3C1D8Fu;
		constexpr ImGuiID kPressSalt = 0x8C5E2A47u;
		constexpr ImGuiID kRectSalt = 0x9D7F4B63u;  // + 0..3

		/// The area ImGui::Selectable fills for an item: its rect grown by half the item spacing.
		ImRect SelectableFillRect(ImGuiSelectableFlags flags)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float spacingX = (flags & ImGuiSelectableFlags_SpanAllColumns) ? 0.0f : style.ItemSpacing.x;
			const float left = std::floor(spacingX * 0.5f);
			const float top = std::floor(style.ItemSpacing.y * 0.5f);
			return ImRect(ImVec2(ImGui::GetItemRectMin().x - left, ImGui::GetItemRectMin().y - top),
				ImVec2(ImGui::GetItemRectMax().x + (spacingX - left), ImGui::GetItemRectMax().y + (style.ItemSpacing.y - top)));
		}

		/// The highlight's look: the accent at the NavSelected surface's strength, fainter on hover, darker once selected
		/// or pressed, with the accent bar fading in on a selected row.
		void DrawListHighlight(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float visibility, float selectedness, float press)
		{
			const auto& surface = Surface::Get(Surface::Role::NavSelected);
			if (!surface.enabled || visibility <= 0.0f)
				return;
			const ImVec4 accent = Colors::GetAccent();
			const float darken = (1.0f - (1.0f - Constants::LIST_HIGHLIGHT_SELECTED_DARKEN) * selectedness) *
			                     (1.0f - (1.0f - Constants::LIST_HIGHLIGHT_PRESS_DARKEN) * press);
			const float strength = (Constants::LIST_HIGHLIGHT_HOVER_RATIO + (1.0f - Constants::LIST_HIGHLIGHT_HOVER_RATIO) * selectedness) *
			                       (1.0f + (Constants::LIST_HIGHLIGHT_PRESS_BOOST - 1.0f) * press);
			const float rounding = ImGui::GetStyle().FrameRounding;
			drawList->AddRectFilled(min, max,
				ImGui::GetColorU32(ImVec4(accent.x * darken, accent.y * darken, accent.z * darken, std::min(1.0f, surface.fillAlpha * strength * visibility))),
				rounding);
			const float barWidth = surface.accentBarWidth * GetUIScale();
			if (surface.accentBar && barWidth > 0.0f && selectedness > 0.0f) {
				const float inset = (max.y - min.y) * Constants::NAV_SELECTED_BAR_INSET;
				drawList->AddRectFilled(ImVec2(min.x, min.y + inset), ImVec2(min.x + barWidth, max.y - inset),
					ImGui::GetColorU32(ScaleAlpha(accent, selectedness * visibility)), barWidth * 0.5f);
			}
		}
	}

	NoListHighlight::NoListHighlight()
	{
		++g_noListHighlight;
	}

	NoListHighlight::~NoListHighlight()
	{
		--g_noListHighlight;
	}

	bool GlideSelectable(SelectableFn real, const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (g_noListHighlight > 0 || ReducedMotion() || window->SkipItems)
			return real(label, selected, flags, size);

		const ImGuiID id = window->GetID(label);
		const ImGuiID list = window->ID;
		ImGuiStorage* storage = ImGui::GetStateStorage();

		// ImGui's own fills are hidden; the list highlight below replaces them
		UnderlayScope underlay;
		ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32_BLACK_TRANS);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32_BLACK_TRANS);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32_BLACK_TRANS);
		const bool clicked = real(label, selected, flags, size);
		ImGui::PopStyleColor(3);
		if (!ImGui::IsItemVisible())
			return clicked;

		const int frame = ImGui::GetFrameCount();
		const bool disabled = (flags & ImGuiSelectableFlags_Disabled) != 0;
		const bool hovered = !disabled && ImGui::IsItemHovered();
		const bool held = hovered && ImGui::IsItemActive();
		const auto key = [list](ImGuiID salt) { return list ^ salt; };

		if (hovered) {
			storage->SetInt(key(kHoverIdSalt), static_cast<int>(id));
			storage->SetInt(key(kHoverFrameSalt), frame);
		}
		if (selected && !disabled)
			storage->SetInt(key(kSelectedFrameSalt), frame);

		// Who the highlight is on: the row the mouse was over a moment ago, else the selected row
		const bool hoverRecent = storage->GetInt(key(kHoverFrameSalt), -10) >= frame - 1;
		const ImGuiID hoverId = hoverRecent ? static_cast<ImGuiID>(storage->GetInt(key(kHoverIdSalt), 0)) : 0;
		const bool isTarget = !disabled && (hoverId != 0 ? hoverId == id : selected);
		const bool expectTarget = hoverId != 0 || storage->GetInt(key(kSelectedFrameSalt), -10) >= frame - 1;
		const bool drawnThisFrame = storage->GetInt(key(kDrawnFrameSalt), -1) == frame;
		const int lastDraw = storage->GetInt(key(kLastDrawSalt), -10);

		// The rect is kept relative to the window's scrolled origin, so it stays put while the list scrolls
		const ImVec2 origin(window->Pos.x - window->Scroll.x, window->Pos.y - window->Scroll.y);

		if (isTarget && !drawnThisFrame) {
			storage->SetInt(key(kDrawnFrameSalt), frame);
			ImRect rect = SelectableFillRect(flags);
			// The fill reaches half an item spacing past the row; keep it inside the window so a pane's edge never cuts the bar
			rect.Min.x = std::max(rect.Min.x, window->InnerClipRect.Min.x);
			rect.Max.x = std::min(rect.Max.x, window->InnerClipRect.Max.x);
			const float targetRel[4] = { rect.Min.x - origin.x, rect.Min.y - origin.y, rect.Max.x - origin.x, rect.Max.y - origin.y };
			const bool fresh = lastDraw < frame - 1;
			float rel[4];
			float stored[4];
			for (ImGuiID n = 0; n < 4; ++n)
				stored[n] = storage->GetFloat(key(kRectSalt + n), targetRel[n]);
			// A jump of many rows is another list in the same window, so it appears there instead of sweeping across
			const bool snap = fresh || std::abs(targetRel[1] - stored[1]) > rect.GetHeight() * Constants::LIST_HIGHLIGHT_SNAP_ROWS;
			for (ImGuiID n = 0; n < 4; ++n) {
				rel[n] = snap ? targetRel[n] : Smooth(stored[n], targetRel[n], Constants::ANIM_GLIDE_RATE);
				storage->SetFloat(key(kRectSalt + n), rel[n]);
			}
			const float visibility = Smooth(fresh ? 0.0f : storage->GetFloat(key(kVisSalt), 0.0f), 1.0f, Constants::ANIM_HOVER_RATE);
			const float selectedTarget = selected ? 1.0f : 0.0f;
			const float selectedness = Smooth(fresh ? selectedTarget : storage->GetFloat(key(kSelSalt), selectedTarget), selectedTarget, Constants::ANIM_SELECT_RATE);
			const float press = Smooth(fresh ? 0.0f : storage->GetFloat(key(kPressSalt), 0.0f), held ? 1.0f : 0.0f, Constants::ANIM_HOVER_RATE);
			storage->SetFloat(key(kVisSalt), visibility);
			storage->SetFloat(key(kSelSalt), selectedness);
			storage->SetFloat(key(kPressSalt), press);
			storage->SetInt(key(kLastDrawSalt), frame);
			DrawListHighlight(underlay.Background(), ImVec2(rel[0] + origin.x, rel[1] + origin.y), ImVec2(rel[2] + origin.x, rel[3] + origin.y),
				visibility, selectedness, press);
		} else if (!expectTarget && !drawnThisFrame && lastDraw >= frame - 1) {
			// Nothing to rest on: the highlight fades out where it was
			const float visibility = Smooth(storage->GetFloat(key(kVisSalt), 0.0f), 0.0f, Constants::ANIM_HOVER_RATE);
			storage->SetFloat(key(kVisSalt), visibility);
			if (visibility > Constants::ANIM_EPSILON) {
				storage->SetInt(key(kDrawnFrameSalt), frame);
				storage->SetInt(key(kLastDrawSalt), frame);
				const float selectedness = storage->GetFloat(key(kSelSalt), 0.0f);
				const float press = storage->GetFloat(key(kPressSalt), 0.0f);
				const ImVec2 min(storage->GetFloat(key(kRectSalt), 0.0f) + origin.x, storage->GetFloat(key(kRectSalt + 1), 0.0f) + origin.y);
				const ImVec2 max(storage->GetFloat(key(kRectSalt + 2), 0.0f) + origin.x, storage->GetFloat(key(kRectSalt + 3), 0.0f) + origin.y);
				DrawListHighlight(underlay.Background(), min, max, visibility, selectedness, press);
			}
		}
		return clicked;
	}

	namespace
	{
		/// What a popup drew while it was open, and when it closed, so it can fade out over the screen.
		struct PopupSnapshot
		{
			ImDrawList* list = nullptr;
			bool wasOpen = false;
			double lastOpenTime = 0.0;
			double closedAt = -1.0;
		};

		std::unordered_map<ImGuiID, PopupSnapshot> g_popupSnapshots;

		/// Replays a snapshot into the foreground list with every vertex alpha scaled; vertices are written unindexed,
		/// which is fine for a popup that only lives on screen for a moment.
		void DrawSnapshot(const ImDrawList& src, float alpha)
		{
			ImDrawList* dst = ImGui::GetForegroundDrawList();
			for (const ImDrawCmd& cmd : src.CmdBuffer) {
				if (cmd.UserCallback || cmd.ElemCount == 0 || cmd.ElemCount > 60000)
					continue;
				dst->PushClipRect(ImVec2(cmd.ClipRect.x, cmd.ClipRect.y), ImVec2(cmd.ClipRect.z, cmd.ClipRect.w), false);
				dst->PushTexture(cmd.TexRef);
				dst->PrimReserve(static_cast<int>(cmd.ElemCount), static_cast<int>(cmd.ElemCount));
				const ImDrawIdx base = static_cast<ImDrawIdx>(dst->_VtxCurrentIdx);
				for (unsigned int i = 0; i < cmd.ElemCount; ++i) {
					const ImDrawVert& vertex = src.VtxBuffer[cmd.VtxOffset + src.IdxBuffer[cmd.IdxOffset + i]];
					const ImU32 faded = (vertex.col & ~IM_COL32_A_MASK) |
					                    (static_cast<ImU32>(static_cast<float>(vertex.col >> IM_COL32_A_SHIFT) * alpha) << IM_COL32_A_SHIFT);
					dst->PrimWriteVtx(vertex.pos, vertex.uv, faded);
					dst->PrimWriteIdx(static_cast<ImDrawIdx>(base + i));
				}
				dst->PopTexture();
				dst->PopClipRect();
			}
		}
	}

	PopupGhost::PopupGhost(const char* popupId, bool modal) :
		id(ImGui::GetID(popupId)),
		modal(modal),
		popupId(popupId)
	{
		if (ReducedMotion())
			return;
		PopupSnapshot& snapshot = g_popupSnapshots[id];
		const double now = ImGui::GetTime();
		if (ImGui::IsPopupOpen(popupId)) {
			snapshot.wasOpen = true;
			snapshot.lastOpenTime = now;
			snapshot.closedAt = -1.0;
			return;
		}
		// Closed since last frame: start the fade, unless the menu itself was hidden in between
		if (snapshot.wasOpen) {
			snapshot.wasOpen = false;
			if (now - snapshot.lastOpenTime < Constants::ANIM_POPUP_GHOST_MAX_GAP_SECONDS)
				snapshot.closedAt = now;
		}
		if (snapshot.closedAt >= 0.0 && snapshot.list) {
			const float t = static_cast<float>(now - snapshot.closedAt) * Speed() / Constants::ANIM_POPUP_SECONDS;
			if (t >= 1.0f)
				snapshot.closedAt = -1.0;
			else
				DrawSnapshot(*snapshot.list, 1.0f - EaseOutCubic(t));
		}
	}

	PopupGhost::~PopupGhost()
	{
		if (ReducedMotion() || !ImGui::IsPopupOpen(popupId))
			return;
		// Still open after its contents were drawn: remember them for when it closes
		char name[64];
		if (modal)
			ImFormatString(name, IM_ARRAYSIZE(name), "%s", popupId);
		else
			ImFormatString(name, IM_ARRAYSIZE(name), "##Popup_%08x", id);
		const ImGuiWindow* window = ImGui::FindWindowByName(name);
		if (!window || !window->WasActive)
			return;
		PopupSnapshot& snapshot = g_popupSnapshots[id];
		if (!snapshot.list)
			snapshot.list = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
		snapshot.list->CmdBuffer = window->DrawList->CmdBuffer;
		snapshot.list->IdxBuffer = window->DrawList->IdxBuffer;
		snapshot.list->VtxBuffer = window->DrawList->VtxBuffer;
	}

	PopupFade::PopupFade(const char* popupId) :
		ghost(popupId)
	{
		const ImGuiID id = ImGui::GetID(popupId);
		pushed = ImGui::IsPopupOpen(popupId);
		if (pushed)
			BeginFadeIn(id, Constants::ANIM_POPUP_SECONDS, Constants::ANIM_PAGE_FADE_FROM);
	}

	PopupFade::~PopupFade()
	{
		if (pushed)
			EndFadeIn();
	}
}
