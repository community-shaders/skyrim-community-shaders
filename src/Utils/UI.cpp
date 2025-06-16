#include "UI.h"
#include <imgui.h>

namespace Util
{
	PerformanceOverlay performanceOverlay;

	HoverTooltipWrapper::HoverTooltipWrapper()
	{
		hovered = ImGui::IsItemHovered();
		if (hovered) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		}
	}

	HoverTooltipWrapper::~HoverTooltipWrapper()
	{
		if (hovered) {
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	DisableGuard::DisableGuard(bool disable) :
		disable(disable)
	{
		if (disable)
			ImGui::BeginDisabled();
	}
	DisableGuard::~DisableGuard()
	{
		if (disable)
			ImGui::EndDisabled();
	}

	bool PercentageSlider(const char* label, float* data, float lb, float ub, const char* format)
	{
		float percentageData = (*data) * 1e2f;
		bool retval = ImGui::SliderFloat(label, &percentageData, lb, ub, format);
		(*data) = percentageData * 1e-2f;
		return retval;
	}

	ImVec2 GetNativeViewportSizeScaled(float scale)
	{
		const auto Size = ImGui::GetMainViewport()->Size;
		return { Size.x * scale, Size.y * scale };
	}

	// StyledButtonWrapper implementation
	StyledButtonWrapper::StyledButtonWrapper(const ImVec4& normalColor, const ImVec4& hoveredColor, const ImVec4& activeColor) :
		m_pushedStyles(0)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, normalColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
		m_pushedStyles = 3;
	}

	StyledButtonWrapper::~StyledButtonWrapper()
	{
		if (m_pushedStyles > 0) {
			ImGui::PopStyleColor(m_pushedStyles);
		}
	}

	// SectionWrapper implementation
	SectionWrapper::SectionWrapper(const char* title, const char* description, const ImVec4& titleColor, bool isVisible) :
		m_shouldDraw(isVisible),
		m_treeNodeOpened(false)
	{
		if (!m_shouldDraw) {
			return;
		}

		ImGui::TextColored(titleColor, "%s", title);
		ImGui::Spacing();

		if (description && strlen(description) > 0) {
			ImGui::TextWrapped("%s", description);
			ImGui::Spacing();
		}

		// Note: For this simplified version, we don't use TreeNode
		// The sections are always expanded in FeatureIssues UI
	}

	SectionWrapper::~SectionWrapper()
	{
		if (m_shouldDraw) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
	}

	SectionWrapper::operator bool() const
	{
		return m_shouldDraw;
	}

	bool DrawCategoryHeader(const char* categoryName, bool& isExpanded)
	{
		// Draw category header with custom styling
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float availableWidth = ImGui::GetContentRegionAvail().x;
		ImVec2 textSize = ImGui::CalcTextSize(categoryName);

		// Calculate line positions
		float lineY = pos.y + textSize.y * 0.5f;
		float lineLength = (availableWidth - textSize.x - 20.0f) * 0.5f;  // 20px for padding

		// Create selectable area for the entire header
		ImGui::PushID(categoryName);
		bool hovered = false;
		bool clicked = false;

		// Invisible button for hover detection and clicking
		ImGui::SetCursorScreenPos(pos);
		if (ImGui::InvisibleButton("##CategoryHeader", ImVec2(availableWidth, textSize.y + 4.0f))) {
			clicked = true;
		}
		hovered = ImGui::IsItemHovered();

		// Draw the lines and text
		ImU32 lineColor = hovered ? IM_COL32(100, 100, 100, 255) : IM_COL32(120, 120, 120, 255);
		ImU32 textColor = hovered ? IM_COL32(140, 140, 140, 255) : IM_COL32(180, 180, 180, 255);

		// Left line
		if (lineLength > 0) {
			drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), lineColor, 1.0f);
		}

		// Right line
		float rightLineStart = pos.x + lineLength + 10.0f + textSize.x + 10.0f;
		if (rightLineStart < pos.x + availableWidth) {
			drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), lineColor, 1.0f);
		}

		// Center text
		ImVec2 textPos = ImVec2(pos.x + lineLength + 10.0f, pos.y + 2.0f);
		drawList->AddText(textPos, textColor, categoryName);

		// Handle click to toggle expansion
		if (clicked) {
			isExpanded = !isExpanded;
		}

		ImGui::PopID();

		// Move cursor to next line
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));

		return clicked;
	}

	void DrawSectionHeader(const char* sectionName, bool useWhiteText)
	{
		// Draw custom styled header similar to CategoryHeader but non-collapsible
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float availableWidth = ImGui::GetContentRegionAvail().x;
		ImVec2 textSize = ImGui::CalcTextSize(sectionName);

		// Calculate line positions
		float lineY = pos.y + textSize.y * 0.5f;
		float lineLength = (availableWidth - textSize.x - 20.0f) * 0.5f;  // 20px for padding

		// Use normal text color (white) to differentiate from loaded features
		ImU32 lineColor = IM_COL32(120, 120, 120, 255);
		ImU32 textColor = useWhiteText ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255);

		// Left line
		if (lineLength > 0) {
			drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + lineLength, lineY), lineColor, 1.0f);
		}

		// Right line
		float rightLineStart = pos.x + lineLength + 10.0f + textSize.x + 10.0f;
		if (rightLineStart < pos.x + availableWidth) {
			drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(pos.x + availableWidth, lineY), lineColor, 1.0f);
		}

		// Center text
		ImVec2 textPos = ImVec2(pos.x + lineLength + 10.0f, pos.y + 2.0f);
		drawList->AddText(textPos, textColor, sectionName);

		// Move cursor to next line
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + textSize.y + 8.0f));
	}
}  // namespace Util
