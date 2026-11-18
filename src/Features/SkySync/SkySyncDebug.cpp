#include "Features/SkySync.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Utils/DebugSection.h"

#define I18N_KEY_PREFIX "feature.sky_sync."

namespace
{
	const char* PhaseName(RE::Moon::Phases::Phase phase)
	{
		switch (phase) {
		case RE::Moon::Phases::Phase::kFull:
			return T(TKEY("debug_phase_full"), "Full");
		case RE::Moon::Phases::Phase::kWaningGibbous:
			return T(TKEY("debug_phase_waning_gibbous"), "Waning Gibbous");
		case RE::Moon::Phases::Phase::kWaningQuarter:
			return T(TKEY("debug_phase_waning_quarter"), "Waning Quarter");
		case RE::Moon::Phases::Phase::kWaningCrescent:
			return T(TKEY("debug_phase_waning_crescent"), "Waning Crescent");
		case RE::Moon::Phases::Phase::kNewMoon:
			return T(TKEY("debug_phase_new"), "New");
		case RE::Moon::Phases::Phase::kWaxingCrescent:
			return T(TKEY("debug_phase_waxing_crescent"), "Waxing Crescent");
		case RE::Moon::Phases::Phase::kWaxingQuarter:
			return T(TKEY("debug_phase_waxing_quarter"), "Waxing Quarter");
		case RE::Moon::Phases::Phase::kWaxingGibbous:
			return T(TKEY("debug_phase_waxing_gibbous"), "Waxing Gibbous");
		default:
			return T(TKEY("debug_unknown"), "Unknown");
		}
	}
}

void SkySync::DrawDebugSection()
{
	Util::DebugSection section;
	if (!section)
		return;

	auto casterName = [](Caster caster) -> const char* {
		switch (caster) {
		case Caster::Sun:
			return T(TKEY("debug_caster_sun"), "Sun");
		case Caster::Masser:
			return "Masser";
		case Caster::Secunda:
			return "Secunda";
		default:
			return T(TKEY("debug_caster_zenith"), "Zenith");
		}
	};

	auto getPhase = [](const RE::Moon* moon) -> const char* {
		if (!moon || !moon->moonMesh)
			return T(TKEY("debug_unknown"), "Unknown");
		if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().shaderProperty.get())) {
			if (auto tex = prop->GetBaseTexture())
				return PhaseName(Util::Moon::GetPhaseFromTexture(tex->name.c_str()));
		}
		return T(TKEY("debug_unknown"), "Unknown");
	};

	auto drawMoonEntry = [&](const char* label, Caster caster, const char* phase) {
		auto& color = colors[static_cast<int>(caster)];
		ImVec4 swatch = { color.x, color.y, color.z, 1.0f };
		ImGui::ColorButton(label, swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, { ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight() });
		ImGui::SameLine();
		ImGui::Text(T(TKEY("debug_moon_entry"), "%s  [%s]  color (%.3f, %.3f, %.3f, %.3f)"), label, phase, color.x, color.y, color.z, color.w);
	};

	const auto sky = globals::game::sky;
	const char* unknown = T(TKEY("debug_unknown"), "Unknown");
	drawMoonEntry("Masser", Caster::Masser, sky ? getPhase(sky->masser) : unknown);
	drawMoonEntry("Secunda", Caster::Secunda, sky ? getPhase(sky->secunda) : unknown);

	ImGui::Text(T(TKEY("debug_dim"), "Dim: %.3f"), currentDim);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text(T(TKEY("debug_shadow_target"), "Shadow target: %s"), casterName(shadowFader.target));
	ImGui::Text(T(TKEY("debug_shadow_dir"), "Shadow dir: (%.2f, %.2f, %.2f)"), shadowFader.currentDir.x, shadowFader.currentDir.y, shadowFader.currentDir.z);
	ImGui::Text(T(TKEY("debug_transition_intensity_factor"), "Transition intensity factor: %.3f"), shadowFader.intensityFactor);
	ImGui::Text(T(TKEY("debug_caster_fade"), "Caster fade: %.3f"), shadowFader.casterFade);
	if (shadowFader.transitioning) {
		const float t = shadowFader.Progress(settings.ShadowTransitionDuration);
		ImGui::ProgressBar(t, { -1.0f, 0.0f }, "");
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::Text(T(TKEY("debug_transitioning"), "Transitioning %.0f%%"), t * 100.0f);
	} else {
		ImGui::TextDisabled("%s", T(TKEY("debug_no_transition"), "No transition"));
	}
}

#undef I18N_KEY_PREFIX
