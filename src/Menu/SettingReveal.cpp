#include "Menu/SettingReveal.h"

#include "Feature.h"
#include "I18n/I18n.h"
#include "Menu/ThemeManager.h"
#include "Utils/Notifications.h"
#include "Utils/SettingsCatalog.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"

#include <imgui.h>
#include <string>

namespace SettingReveal
{
	namespace
	{
		struct Pending
		{
			bool active = false;
			std::string feature;
			std::string serializedPath;
			std::string serializedKey;
			std::string lookUnder;     ///< Display path for the toast
			double requestedAt = 0.0;  ///< ImGui time of the request
			bool scanning = false;     ///< The pending feature's body is drawing now
			bool found = false;        ///< The control was drawn
			double highlightUntil = 0.0;
		};
		Pending g_pending;

		bool Matches(const SceneSettingsCatalog::SettingMetadata& setting)
		{
			return setting.featureShortName == g_pending.feature && setting.serializedKey == g_pending.serializedKey &&
			       setting.serializedPath == g_pending.serializedPath;
		}
	}

	void Request(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		g_pending = {};
		g_pending.active = true;
		g_pending.feature = std::string(setting.featureShortName);
		g_pending.serializedPath = std::string(setting.serializedPath);
		g_pending.serializedKey = std::string(setting.serializedKey);
		g_pending.requestedAt = ImGui::GetTime();

		std::string path;
		for (const auto& part : Util::Settings::GetCatalogDisplayPath(setting)) {
			if (!path.empty())
				path += " > ";
			path += part;
		}
		g_pending.lookUnder = std::move(path);
	}

	void BeginFeatureBody(const Feature* feature)
	{
		if (!g_pending.active || !feature)
			return;
		if (!g_pending.found && ImGui::GetTime() - g_pending.requestedAt > ThemeManager::Constants::REVEAL_EXPIRY_SECONDS) {
			g_pending = {};  // the page never drew: the request is stale
			return;
		}
		g_pending.scanning = const_cast<Feature*>(feature)->GetShortName() == g_pending.feature;
	}

	void EndFeatureBody()
	{
		if (!g_pending.scanning)
			return;
		g_pending.scanning = false;

		if (!g_pending.found) {
			const auto message = g_pending.lookUnder.empty() ?
			                         std::string(T("menu.palette.not_found", "That setting is not visible on the page.")) :
			                         I18n::GetSingleton()->Format("menu.palette.look_under", { { "path", g_pending.lookUnder } }, "Look under: {path}");
			Util::Notifications::Show(message, Util::Colors::GetInfo(), ThemeManager::Constants::PALETTE_TOAST_SECONDS);
			g_pending = {};
		} else if (ImGui::GetTime() >= g_pending.highlightUntil) {
			g_pending = {};
		}
	}

	void NotifyControl(const SceneSettingsCatalog::SettingMetadata& setting)
	{
		if (!g_pending.scanning || !Matches(setting))
			return;

		const double now = ImGui::GetTime();
		if (!g_pending.found) {
			g_pending.found = true;
			g_pending.highlightUntil = now + ThemeManager::Constants::REVEAL_HIGHLIGHT_SECONDS;
			ImGui::SetScrollHereY(ThemeManager::Constants::REVEAL_SCROLL_RATIO);
		}
		if (now >= g_pending.highlightUntil)
			return;

		// Fades out over the highlight; drawn on the window list so the child clips it
		const float remaining = static_cast<float>((g_pending.highlightUntil - now) / ThemeManager::Constants::REVEAL_HIGHLIGHT_SECONDS);
		const ImVec4 color = Util::Anim::ScaleAlpha(Util::Colors::GetAccent(), Util::Anim::EaseOutCubic(remaining * ThemeManager::Constants::REVEAL_FADE_HOLD));
		const float pad = ThemeManager::Constants::REVEAL_OUTLINE_PADDING * Util::GetUIScale();
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - pad, min.y - pad), ImVec2(max.x + pad, max.y + pad),
			ImGui::GetColorU32(color), ImGui::GetStyle().FrameRounding, 0, ThemeManager::Constants::REVEAL_OUTLINE_THICKNESS * Util::GetUIScale());
	}
}
