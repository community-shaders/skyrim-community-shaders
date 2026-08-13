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
		ImGui::SliderFloat(T(TKEY("refill_time"), "Snow Refill Time"), &settings.RefillTime, 0.0f, 3600.0f, "%.0f s");
		if (auto _ttRefill = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("refill_time_tooltip"), "Time for compressed snow to fully recover. 0 disables refilling."));

		if (ImGui::TreeNodeEx(T(TKEY("render_distance"), "Render Distance"), ImGuiTreeNodeFlags_Framed)) {
			if (auto _ttRd = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("render_distance_tooltip"), "How far each snow system reaches. Higher = more VRAM and GPU cost."));
			ImGui::SliderFloat(T(TKEY("range_trenches"), "Trenches"), &settings.RangeTrenchesM, 29.0f, 200.0f, "%.0f m");
			if (ImGui::IsItemDeactivatedAfterEdit())
				trenchRangeDirty = true;
			if (auto _ttRt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("range_trenches_tooltip"), "Deformation window radius (also the actor stamping cutoff). Applying a change CLEARS existing trenches; trench detail coarsens with range."));
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx(T(TKEY("debug_options"), "Debugging Options"), ImGuiTreeNodeFlags_Framed)) {
			ImGui::Checkbox(T(TKEY("show_debug"), "Show Deformation Map"), &settings.ShowDebugTexture);
			if (settings.ShowDebugTexture) {
				ImGui::Text("%s", T(TKEY("debug_hint"), "White = compressed snow. The map follows the camera."));
				ImGui::Image(GetDeformationSRV(), { 512.0f, 512.0f });
			}

			if (ImGui::Button(T(TKEY("clear"), "Clear Deformation Map")))
				clearRequested = true;

			ImGui::Checkbox(T(TKEY("debug_overlay"), "Debug Terrain Overlay"), &debugTerrainOverlay);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("debug_overlay_tooltip"), "Paints diagnostics on terrain: red = outside deformation window, green = deformation, blue = detected snow."));

			ImGui::TreePop();
		}

		ImGui::Text("Snow mask cache: %zu entries, %llu hits, %llu misses",
			snowMasksSizeForUI(),
			(unsigned long long)landMaskHits.load(std::memory_order_relaxed),
			(unsigned long long)landMaskMisses.load(std::memory_order_relaxed));

		ImGui::TreePop();
	}
}
