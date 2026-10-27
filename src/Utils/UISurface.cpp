#include "Utils/UISurface.h"

#include "Menu/ThemeManager.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"

namespace Util::Surface
{
	using Constants = ThemeManager::Constants;

	ImU32 FillColor(float alpha)
	{
		const ImVec4 window = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		const ImVec4 lifted = Anim::Lerp(window, ImVec4(1.0f, 1.0f, 1.0f, window.w), Constants::SURFACE_FILL_LIFT);
		return ImGui::GetColorU32(ImVec4(lifted.x, lifted.y, lifted.z, window.w * alpha));
	}

	void Draw(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const Style& style)
	{
		const float rounding = style.rounding >= 0.0f ? style.rounding : ImGui::GetStyle().ChildRounding;

		if (style.shadow) {
			// The shadow spills outside the window that owns the list, so lift its clip rect for it
			const float scale = GetUIScale();
			const float grow = Constants::SURFACE_SHADOW_SPREAD * scale;
			const float dropY = Constants::SURFACE_SHADOW_OFFSET_Y * scale;
			drawList->PushClipRectFullScreen();
			drawList->AddRectFilled(ImVec2(min.x - grow, min.y - grow + dropY), ImVec2(max.x + grow, max.y + grow + dropY),
				IM_COL32(0, 0, 0, static_cast<int>(255.0f * Constants::SURFACE_SHADOW_ALPHA * style.alpha)), rounding + grow);
			drawList->PopClipRect();
		}

		drawList->AddRectFilled(min, max, FillColor(style.alpha), rounding);
		if (style.border)
			drawList->AddRect(min, max,
				style.borderColor ? style.borderColor : IM_COL32(255, 255, 255, static_cast<int>(255.0f * Constants::SURFACE_BORDER_ALPHA * style.alpha)),
				rounding);
	}
}
