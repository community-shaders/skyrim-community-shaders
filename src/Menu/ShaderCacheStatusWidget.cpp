#include "Menu/ShaderCacheStatusWidget.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

#include <imgui.h>

#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu/ThemeManager.h"
#include "ShaderCache.h"
#include "Util.h"

namespace ShaderCacheStatusWidget
{
	namespace
	{
		struct Segment
		{
			uint64_t count;
			ImU32 color;
			const char* label;
		};

		/** @brief The last build's tasks split by outcome, in bar order. */
		std::array<Segment, 6> GetSegments()
		{
			auto* shaderCache = globals::shaderCache;
			const uint64_t completed = shaderCache->GetCompletedTasks();
			const uint64_t failed = shaderCache->GetFailedTasks();
			const uint64_t cacheHits = shaderCache->GetCachedHitTasks();
			const uint64_t diskHits = shaderCache->GetDiskHitTasks();
			const uint64_t slow = shaderCache->GetSlowTasks();
			const uint64_t verySlow = shaderCache->GetVerySlowTasks();
			// Compiled = tasks that actually went through compilation (excluding disk hits).
			// Cache hits are separate (returned early without queueing).
			const uint64_t compiled = completed > diskHits ? completed - diskHits : 0;
			const uint64_t fast = compiled > slow ? compiled - slow : 0;
			const uint64_t medium = slow > verySlow ? slow - verySlow : 0;  // 2-8s
			return { {
				{ cacheHits, ThemeManager::Constants::SHADER_BAR_DEDUPLICATED, T("menu.settings.shader_deduplicated", "Deduplicated") },
				{ diskHits, ThemeManager::Constants::SHADER_BAR_DISK_CACHE, T("menu.settings.shader_disk_cache", "Disk cache") },
				{ fast, ThemeManager::Constants::SHADER_BAR_FAST, T("menu.settings.shader_fast", "Fast (<2s)") },
				{ medium, ThemeManager::Constants::SHADER_BAR_SLOW, T("menu.settings.shader_slow", "Slow (2-8s)") },
				{ verySlow, ThemeManager::Constants::SHADER_BAR_VERY_SLOW, T("menu.settings.shader_very_slow", "Very slow (>=8s)") },
				{ failed, ThemeManager::Constants::SHADER_BAR_FAILED, T("menu.settings.shader_failed", "Failed") },
			} };
		}
	}

	void DrawBreakdownBar()
	{
		auto* shaderCache = globals::shaderCache;
		const uint64_t total = shaderCache->GetTotalTasks();
		if (total == 0)
			return;
		const auto segments = GetSegments();

		const float barHeight = ThemeManager::Constants::SHADER_BREAKDOWN_BAR_HEIGHT * Util::GetUIScale();
		const float barWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->AddRectFilled(cursor, ImVec2(cursor.x + barWidth, cursor.y + barHeight), ImGui::GetColorU32(ThemeManager::Constants::SHADER_BAR_BACKGROUND));

		float x = cursor.x;
		for (const auto& seg : segments) {
			if (seg.count == 0)
				continue;
			const float segWidth = std::max(1.0f, (static_cast<float>(seg.count) / static_cast<float>(total)) * barWidth);
			drawList->AddRectFilled(ImVec2(x, cursor.y), ImVec2(x + segWidth, cursor.y + barHeight), ImGui::GetColorU32(seg.color));
			x += segWidth;
		}

		ImGui::Dummy(ImVec2(barWidth, barHeight));
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			for (const auto& seg : segments) {
				if (seg.count == 0)
					continue;
				const float pct = 100.0f * static_cast<float>(seg.count) / static_cast<float>(total);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(seg.color), "%s: %llu (%.1f%%)", seg.label, seg.count, pct);
			}
			ImGui::EndTooltip();
		}
	}

	void DrawBreakdownLegend()
	{
		const uint64_t total = globals::shaderCache->GetTotalTasks();
		if (total == 0)
			return;
		const ImGuiStyle& style = ImGui::GetStyle();
		MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
		const float swatch = ImGui::GetTextLineHeight() * 0.6f;
		const float startX = ImGui::GetCursorScreenPos().x;
		const float rightEdge = startX + ImGui::GetContentRegionAvail().x;
		bool first = true;
		for (const auto& seg : GetSegments()) {
			if (seg.count == 0)
				continue;
			const std::string text = std::format("{} {}", seg.label, seg.count);
			const float width = swatch + style.ItemInnerSpacing.x + ImGui::CalcTextSize(text.c_str()).x;
			// Entries flow left to right and wrap at the content edge
			if (!first && ImGui::GetItemRectMax().x + style.ItemSpacing.x * 2.0f + width <= rightEdge)
				ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
			first = false;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float y = pos.y + (ImGui::GetTextLineHeight() - swatch) * 0.5f;
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + swatch, y + swatch), ImGui::GetColorU32(seg.color), swatch * 0.25f);
			ImGui::Dummy(ImVec2(swatch, ImGui::GetTextLineHeight()));
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::TextDisabled("%s", text.c_str());
		}
	}
}
