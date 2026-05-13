#include "CloudRelight.h"

#include "CloudShadows.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudRelight::Settings,
	enabled,
	cloudRelightMix,
	cloudOriginalMix,
	silverLiningMix,
	silverLiningSpread)

namespace
{
	void ClampSettings(CloudRelight::Settings& settings)
	{
		settings.cloudRelightMix = std::clamp(settings.cloudRelightMix, 0.0f, 2.0f);
		settings.cloudOriginalMix = std::clamp(settings.cloudOriginalMix, 0.0f, 2.0f);
		settings.silverLiningMix = std::clamp(settings.silverLiningMix, 0.0f, 1.0f);
		settings.silverLiningSpread = std::clamp(settings.silverLiningSpread, -0.99f, 0.99f);
	}
}

void CloudRelight::DrawSettings()
{
	bool enable = settings.enabled != 0;
	if (ImGui::Checkbox("Enabled", &enable))
		settings.enabled = enable;

	ImGui::SeparatorText("Cloud Relighting");

	ImGui::SliderFloat("Vanilla Mix", &settings.cloudOriginalMix, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multiplier on the original vanilla cloud color before relighting is applied.");

	ImGui::SliderFloat("Relight Mix", &settings.cloudRelightMix, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multiplier on the directional light contribution added to clouds.");

	ImGui::SeparatorText("Silver Lining");

	ImGui::SliderFloat("Silver Lining Accent", &settings.silverLiningMix, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Blend between flat isotropic phase and sharp silver-lining phase lighting.");

	ImGui::SliderFloat("Silver Lining Spread", &settings.silverLiningSpread, -0.99f, 0.99f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"Positive: silver lining appears only on thin cloud edges.\n"
			"Negative: silver lining bleeds into thick cloud areas.");

	if (!globals::features::cloudShadows.loaded) {
		ImGui::Spacing();
		ImGui::TextWrapped("Cloud self-shadowing requires Cloud Shadows to be installed and enabled.");
	}
}

void CloudRelight::LoadSettings(json& o_json)
{
	settings = o_json;
	ClampSettings(settings);
}

void CloudRelight::SaveSettings(json& o_json)
{
	o_json = settings;
}

void CloudRelight::RestoreDefaultSettings()
{
	settings = {};
}

CloudRelight::Settings CloudRelight::GetCommonBufferData() const
{
	return settings;
}
