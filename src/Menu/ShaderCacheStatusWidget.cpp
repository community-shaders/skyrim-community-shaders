#include "Menu/ShaderCacheStatusWidget.h"

#include <imgui.h>

#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu/ThemeManager.h"
#include "ShaderCache.h"
#include "Util.h"

namespace ShaderCacheStatusWidget
{
	void DrawBreakdownBar()
	{
		auto* shaderCache = globals::shaderCache;
		const uint64_t total = shaderCache->GetTotalTasks();
		if (total == 0)
			return;

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

		struct Segment
		{
			uint64_t count;
			ImU32 color;
			const char* label;
		};
		const Segment segments[] = {
			{ cacheHits, IM_COL32(120, 120, 120, 255), T("menu.settings.shader_deduplicated", "Deduplicated") },
			{ diskHits, IM_COL32(70, 130, 200, 255), T("menu.settings.shader_disk_cache", "Disk cache") },
			{ fast, IM_COL32(80, 180, 80, 255), T("menu.settings.shader_fast", "Fast (<2s)") },
			{ medium, IM_COL32(220, 180, 50, 255), T("menu.settings.shader_slow", "Slow (2-8s)") },
			{ verySlow, IM_COL32(220, 60, 60, 255), T("menu.settings.shader_very_slow", "Very slow (>=8s)") },
			{ failed, IM_COL32(160, 30, 30, 255), T("menu.settings.shader_failed", "Failed") },
		};

		const float barHeight = ThemeManager::Constants::SHADER_BREAKDOWN_BAR_HEIGHT * Util::GetUIScale();
		const float barWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Background
		drawList->AddRectFilled(cursor, ImVec2(cursor.x + barWidth, cursor.y + barHeight), IM_COL32(40, 40, 40, 255));

		// Draw segments
		float x = cursor.x;
		for (const auto& seg : segments) {
			if (seg.count == 0)
				continue;
			float segWidth = (static_cast<float>(seg.count) / static_cast<float>(total)) * barWidth;
			if (segWidth < 1.0f)
				segWidth = 1.0f;
			drawList->AddRectFilled(ImVec2(x, cursor.y), ImVec2(x + segWidth, cursor.y + barHeight), seg.color);
			x += segWidth;
		}

		// Reserve space and handle tooltip
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
}
