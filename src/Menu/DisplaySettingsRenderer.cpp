#include "DisplaySettingsRenderer.h"

#include <imgui.h>

#include "Menu.h"
#include "Util.h"

void DisplaySettingsRenderer::RenderDisplaySettings(
	const std::function<bool(const std::string&)>& isFeatureDisabled,
	const std::function<void()>& drawUpscalingSettings)
{
	const std::vector<std::pair<std::string, std::function<void()>>> features = {
		{ "Upscaling", drawUpscalingSettings }
	};

	for (const auto& [featureName, drawFunc] : features) {
		bool isDisabled = isFeatureDisabled(featureName);
		RenderFeatureSection(featureName, drawFunc, isDisabled, false);
	}
}

void DisplaySettingsRenderer::RenderFeatureSection(
	const std::string& featureName,
	const std::function<void()>& drawFunc,
	bool isDisabled,
	bool isVRMode)
{
	auto& themeSettings = Menu::GetSingleton()->GetTheme();

	// Frame Generation is disabled in VR mode
	if (featureName == "Frame Generation" && isVRMode) {
		isDisabled = true;
	}

	if (!isDisabled) {
		if (ImGui::CollapsingHeader(featureName.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
			drawFunc();
		}
	} else {
		ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Disable);
		ImGui::CollapsingHeader(featureName.c_str(), ImGuiTreeNodeFlags_NoTreePushOnOpen);
		ImGui::PopStyleColor();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s has been disabled at boot. "
				"Reenable in the Advanced -> Disable at Boot Menu.",
				featureName.c_str());
		}
	}
}