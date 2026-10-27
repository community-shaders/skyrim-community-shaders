#include "Utils/UIAnim.h"

#include "Globals.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"

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
