#include "DebugView.h"

#include <format>

namespace Util
{
	std::string DebugNum(float value)
	{
		return std::format("{:.3f}", value);
	}

	void DrawDebugValueRows(const char* id, std::initializer_list<DebugValueRow> rows)
	{
		if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
			return;
		ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 1.0f);  // i18n-ignore: table column id
		ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f);  // i18n-ignore: table column id

		const float swatchSize = ImGui::GetFontSize();
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		int index = 0;
		for (const auto& row : rows) {
			ImGui::PushID(index++);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", row.label);

			ImGui::TableNextColumn();
			const float textWidth = ImGui::CalcTextSize(row.value.c_str()).x;
			const float width = textWidth + (row.swatch ? swatchSize + spacing : 0.0f);
			const float offset = ImGui::GetContentRegionAvail().x - width;
			if (offset > 0.0f)
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
			if (row.swatch) {
				ImGui::ColorButton("##swatch", *row.swatch,  // i18n-ignore: widget id
					ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoAlpha,
					ImVec2(swatchSize, swatchSize));
				ImGui::SameLine(0.0f, spacing);
			}
			ImGui::TextUnformatted(row.value.c_str());
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}
