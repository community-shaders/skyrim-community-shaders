#include "Utils/UIGlass.h"

#include "Menu/ThemeManager.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"

#include <algorithm>

namespace Util::Glass
{
	using Constants = ThemeManager::Constants;

	ImU32 FillColor(float alpha)
	{
		const ImVec4 window = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		const ImVec4 lifted = Anim::Lerp(window, ImVec4(1.0f, 1.0f, 1.0f, window.w), Constants::GLASS_FILL_LIFT);
		return ImGui::GetColorU32(ImVec4(lifted.x, lifted.y, lifted.z, Constants::GLASS_FILL_ALPHA * alpha));
	}

	void DrawShadow(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding, float alpha)
	{
		const float scale = GetUIScale();
		const int layers = std::clamp(Constants::GLASS_SHADOW_LAYERS, 0, 4);
		const float dropY = Constants::GLASS_SHADOW_OFFSET_Y * scale;
		for (int i = layers; i >= 1; --i) {
			const float grow = Constants::GLASS_SHADOW_SPREAD * scale * static_cast<float>(i);
			drawList->AddRectFilled(ImVec2(min.x - grow, min.y - grow + dropY), ImVec2(max.x + grow, max.y + grow + dropY),
				IM_COL32(0, 0, 0, static_cast<int>(255.0f * Constants::GLASS_SHADOW_ALPHA * alpha)), rounding + grow);
		}
	}

	void Draw(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const Style& style)
	{
		const float rounding = style.rounding >= 0.0f ? style.rounding : ImGui::GetStyle().FrameRounding;
		const float scale = GetUIScale();

		if (style.shadow) {
			// The rings spill outside the window that owns the list, so lift its clip rect for them
			drawList->PushClipRectFullScreen();
			DrawShadow(drawList, min, max, rounding, style.alpha);
			drawList->PopClipRect();
		}

		ImU32 fill = FillColor(style.alpha);
		if (style.tint && style.tintAmount > 0.0f)
			fill = Anim::LerpU32(fill, Anim::ScaleAlphaU32(style.tint, Constants::GLASS_FILL_ALPHA * style.alpha), style.tintAmount);
		drawList->AddRectFilled(min, max, fill, rounding);

		if (style.sheen) {
			// Sheen: a white gradient fading down from the top edge, clipped to the rounded top by the rect corners
			const float height = std::min((max.y - min.y) * Constants::GLASS_HIGHLIGHT_HEIGHT_RATIO, Constants::GLASS_HIGHLIGHT_MAX_HEIGHT * scale);
			const ImU32 top = IM_COL32(255, 255, 255, static_cast<int>(255.0f * Constants::GLASS_HIGHLIGHT_ALPHA * style.alpha));
			const ImU32 none = IM_COL32(255, 255, 255, 0);
			const float inset = rounding * 0.5f;
			drawList->AddRectFilledMultiColor(ImVec2(min.x + inset, min.y + 1.0f), ImVec2(max.x - inset, min.y + height), top, top, none, none);
			drawList->AddLine(ImVec2(min.x + rounding, min.y + 0.5f), ImVec2(max.x - rounding, min.y + 0.5f),
				IM_COL32(255, 255, 255, static_cast<int>(255.0f * Constants::GLASS_EDGE_LINE_ALPHA * style.alpha)));
		}

		drawList->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f),
			IM_COL32(255, 255, 255, static_cast<int>(255.0f * Constants::GLASS_BORDER_ALPHA * style.alpha)), rounding);
	}
}
