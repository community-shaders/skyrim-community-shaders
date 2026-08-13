#include "Features/SnowDeformation.h"

#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.snow_deformation."

void SnowDeformation::DrawSettings()
{
	if (ImGui::TreeNodeEx(T(TKEY("snow_deformation"), "Snow Deformation"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("enable"), "Enable Snow Deformation"), &settings.EnableSnowDeformation);

		ImGui::SliderFloat(T(TKEY("stamp_radius"), "Stamp Radius"), &settings.StampRadius, 4.0f, 128.0f, "%.0f");
		if (auto _ttStamp = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("stamp_radius_tooltip"), "Scales the Havok collision-shape radii used for stamping (20 = the shapes' actual size). Stamps come from actors' real collision shapes — feet and legs carve individually."));

		if (ImGui::TreeNodeEx(T(TKEY("debug_options"), "Debugging Options"), ImGuiTreeNodeFlags_Framed)) {
			ImGui::Checkbox(T(TKEY("show_debug"), "Show Deformation Map"), &settings.ShowDebugTexture);
			if (settings.ShowDebugTexture) {
				ImGui::Text("%s", T(TKEY("debug_hint"), "White = compressed snow. The map follows the camera."));
				ImGui::Image(GetDeformationSRV(), { 512.0f, 512.0f });
			}

			if (ImGui::Button(T(TKEY("clear"), "Clear Deformation Map")))
				clearRequested = true;

			ImGui::TreePop();
		}

		ImGui::TreePop();
	}
}
