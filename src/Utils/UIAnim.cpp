#include "Utils/UIAnim.h"

#include "Globals.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"

#include <algorithm>
#include <cmath>

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

	PageFade::PageFade(ImGuiID pageId)
	{
		BeginFadeIn(pageId, Constants::ANIM_PAGE_FADE_SECONDS, Constants::ANIM_PAGE_FADE_FROM);
	}

	PageFade::~PageFade()
	{
		EndFadeIn();
	}

	PopupFade::PopupFade(const char* popupId)
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
