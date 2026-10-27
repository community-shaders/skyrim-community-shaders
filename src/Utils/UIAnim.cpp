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
		return current + (target - current) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * rate));
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
		return EaseOutCubic(elapsed / duration);
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

	float BeginFadeIn(ImGuiID key)
	{
		const float t = EnterProgress(key, 0.0f, Constants::ANIM_POPUP_SECONDS);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * t);
		return t;
	}

	void EndFadeIn()
	{
		ImGui::PopStyleVar();
	}
}
