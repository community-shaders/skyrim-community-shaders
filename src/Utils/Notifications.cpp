#include "Utils/Notifications.h"

#include "Menu/ThemeManager.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"
#include "Utils/UISurface.h"

#include <algorithm>
#include <format>
#include <vector>

namespace
{
	struct Notification
	{
		std::string message;
		ImVec4 color;
		float startTime;
		float duration;
	};

	std::vector<Notification> notifications;
	/// ImGui frame the cards were last drawn in; a second host in the same frame skips them.
	int lastDrawnFrame = -1;
}

namespace Util::Notifications
{
	void Show(const std::string& message, const ImVec4& color, float duration)
	{
		// Guard against calls before ImGui is initialized
		if (!ImGui::GetCurrentContext()) {
			logger::warn("ShowNotification called before ImGui initialization: {}", message);
			return;
		}

		Notification notif;
		notif.message = message;
		notif.color = color;
		notif.startTime = static_cast<float>(ImGui::GetTime());
		notif.duration = duration;
		notifications.push_back(notif);
	}

	void ShowOnce(const std::string& message, const ImVec4& color, float duration)
	{
		if (std::ranges::any_of(notifications, [&message](const Notification& notification) { return notification.message == message; }))
			return;
		Show(message, color, duration);
	}

	bool HasAny()
	{
		return !notifications.empty();
	}

	void Draw()
	{
		// Guard against calls before ImGui is initialized
		if (!ImGui::GetCurrentContext()) {
			return;
		}

		const int frame = ImGui::GetFrameCount();
		if (frame == lastDrawnFrame)
			return;
		lastDrawnFrame = frame;

		using Constants = ThemeManager::Constants;
		const float currentTime = static_cast<float>(ImGui::GetTime());
		const float scale = Util::GetUIScale();

		notifications.erase(
			std::remove_if(notifications.begin(), notifications.end(),
				[currentTime](const Notification& n) { return currentTime - n.startTime > n.duration; }),
			notifications.end());

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float maxTextWidth = std::min(Constants::TOAST_MAX_TEXT_WIDTH_FONTS * ImGui::GetFontSize(), viewport->WorkSize.x * Constants::TOAST_MAX_TEXT_WIDTH_VIEWPORT_RATIO);
		float stackUp = 0.0f;

		for (auto& notif : notifications) {
			const float elapsed = currentTime - notif.startTime;
			const float remaining = notif.duration - elapsed;
			float alpha = 1.0f;
			if (elapsed < Constants::TOAST_FADE_IN_SECONDS)
				alpha = Util::Anim::EaseOutCubic(elapsed / Constants::TOAST_FADE_IN_SECONDS);
			else if (remaining < Constants::TOAST_FADE_OUT_SECONDS)
				alpha = Util::Anim::EaseInOut(remaining / Constants::TOAST_FADE_OUT_SECONDS);

			const ImVec2 textSize = ImGui::CalcTextSize(notif.message.c_str(), nullptr, false, maxTextWidth);
			const ImVec2 pad{ Constants::TOAST_PADDING_X * scale, Constants::TOAST_PADDING_Y * scale };
			const ImVec2 cardSize{ textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f };
			const float x = viewport->WorkPos.x + (viewport->WorkSize.x - cardSize.x) * 0.5f;
			const float y = viewport->WorkPos.y + viewport->WorkSize.y - cardSize.y - (Constants::TOAST_BOTTOM_MARGIN * scale + stackUp);

			ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
			ImGui::SetNextWindowSize(cardSize, ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.0f);

			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetStyle().FrameRounding * Constants::TOAST_ROUNDING_SCALE);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

			if (ImGui::Begin(std::format("##Notification{}", (void*)&notif).c_str(), nullptr,
					ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
						ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
						ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar)) {
				ImDrawList* draw = ImGui::GetWindowDrawList();
				const ImVec2 p0 = ImGui::GetWindowPos();
				const ImVec2 p1{ p0.x + cardSize.x, p0.y + cardSize.y };
				const float rounding = ImGui::GetStyle().WindowRounding;
				Util::Surface::Options surface;
				surface.rounding = rounding;
				surface.alpha = alpha;
				Util::Surface::Draw(draw, p0, p1, Util::Surface::Role::Toast, surface);
				const ImU32 accent = ImGui::ColorConvertFloat4ToU32(
					ImVec4(notif.color.x, notif.color.y, notif.color.z, Constants::TOAST_ACCENT_ALPHA * alpha));
				Util::Surface::DrawAccentFrame(draw, p0, p1, accent, rounding, Constants::TOAST_BORDER_THICKNESS * scale,
					Constants::TOAST_STRIPE_WIDTH * scale);

				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxTextWidth);
				ImVec4 colorWithAlpha = notif.color;
				colorWithAlpha.w *= alpha;
				ImGui::PushStyleColor(ImGuiCol_Text, colorWithAlpha);
				ImGui::TextWrapped("%s", notif.message.c_str());
				ImGui::PopStyleColor();
				ImGui::PopTextWrapPos();
			}
			ImGui::End();
			ImGui::PopStyleVar(3);

			stackUp += cardSize.y + Constants::TOAST_STACK_SPACING * scale;
		}
	}
}
