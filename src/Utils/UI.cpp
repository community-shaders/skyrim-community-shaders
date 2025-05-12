#include "UI.h"

namespace Util
{
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

	void DisplayVersionInfo(const std::string& version)
	{
		// Add spacing and separator before version display
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Push gray text color for version info
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

		// Position the text at the bottom of the current window
		ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() - 5.0f);

		// Display the version info
		ImGui::Text("v%s", version.c_str());

		// Pop the color style
		ImGui::PopStyleColor();
	}
	
	void DisplayFeatureDescription(const std::string& description)
	{
		if (description.empty())
			return;
			
		// Style the description as an info box
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.18f, 0.6f));
		
		// Create a child window for the description
		ImGui::BeginChild("FeatureDescription", ImVec2(ImGui::GetContentRegionAvail().x, 0), true);
		
		// Display the description text
		ImGui::TextWrapped("%s", description.c_str());
		
		ImGui::EndChild();
		
		ImGui::PopStyleColor();
		
		// Add spacing after the description
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}
}  // namespace Util
