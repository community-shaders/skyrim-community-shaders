#include "CloudRelight.h"

#include "CloudShadows.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudRelight::Settings,
	enabled,
	cloudRelightMix,
	cloudOriginalMix,
	silverLiningMix,
	silverLiningSpread)

void CloudRelight::DataLoaded()
{
	if (!globals::features::cloudShadows.loaded) {
		failedLoadedMessage = "Cloud Shadows is required for Cloud Relight to function.";
		loaded = false;
	}
}

void CloudRelight::RestoreDefaultSettings()
{
	settings = {};
}

void CloudRelight::LoadSettings(json& o_json)
{
	settings = o_json;
}

void CloudRelight::SaveSettings(json& o_json)
{
	o_json = settings;
}

void CloudRelight::DrawSettings()
{
	bool isEnabled = settings.enabled != 0;
	if (ImGui::Checkbox("Enabled", &isEnabled))
		settings.enabled = isEnabled ? 1 : 0;

	ImGui::SeparatorText("Cloud Relighting");

	ImGui::SliderFloat("Vanilla Mix", &settings.cloudOriginalMix, 0.f, 2.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multiplier on the original (vanilla) cloud color before relighting is applied.");

	ImGui::SliderFloat("Relight Mix", &settings.cloudRelightMix, 0.f, 2.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multiplier on the physically-based directional light contribution added to clouds.");

	ImGui::SeparatorText("Silver Lining");

	ImGui::SliderFloat("Silver Lining Accent", &settings.silverLiningMix, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Blend between flat isotropic phase (0) and the sharp silver-lining phase function (1).");

	ImGui::SliderFloat("Silver Lining Spread", &settings.silverLiningSpread, -0.99f, 0.99f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"Positive: silver-lining appears only on thin cloud edges.\n"
			"Negative: silver-lining bleeds into thick cloud areas.");
}
