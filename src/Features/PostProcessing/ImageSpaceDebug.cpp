#include "Features/PostProcessing.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/MenuIcons.h"
#include "Utils/DebugSection.h"
#include "Utils/DebugView.h"
#include "Utils/UI.h"

#include <format>
#include <string>

namespace
{
	std::string Triple(float a, float b, float c)
	{
		return std::format("({:.3f}, {:.3f}, {:.3f})", a, b, c);
	}

	std::string Pair(float a, float b)
	{
		return std::format("({:.3f}, {:.3f})", a, b);
	}
}

/** @brief The game's current image space values, as cards. */
void PostProcessing::DrawImageSpaceDebug()
{
	Util::SettingHint(T("feature.post_processing.debug.hint", "Live values of the game's current image space, before Post Processing modifies them."), false);
	ImGui::Spacing();

	const auto& data = imageSpaceManager->gameISData;
	const auto& base = data.baseData;
	const auto& mod = data.modData.data;

	{
		Util::SectionCard card("##ImageSpaceOverview", ICON_TI_EYE, T("feature.post_processing.debug.overview", "Overview"));
		Util::DrawDebugValueRows("##OverviewRows", {
													   { T("feature.post_processing.base_amount", "Base Amount"), Util::DebugNum(data.baseAmount) },
													   { T("feature.post_processing.mod_amount", "Mod Amount"), Util::DebugNum(data.modAmount) },
												   });
	}

	Util::SeparatorTextWithFont(T("feature.post_processing.base_data", "Base Data"), Menu::FontRole::Subheading);

	{
		Util::SectionCard card("##ImageSpaceCinematic", ICON_TI_ADJUSTMENTS_HORIZONTAL, T("feature.post_processing.cinematic_values", "Cinematic"));
		Util::DrawDebugValueRows("##CinematicRows", {
														{ T("feature.post_processing.debug.saturation", "Saturation"), Util::DebugNum(base.cinematic.saturation) },
														{ T("feature.post_processing.debug.brightness", "Brightness"), Util::DebugNum(base.cinematic.brightness) },
														{ T("feature.post_processing.debug.contrast", "Contrast"), Util::DebugNum(base.cinematic.contrast) },
													});
	}

	{
		Util::SectionCard card("##ImageSpaceHdr", ICON_TI_SUN, T("feature.post_processing.hdr_values", "HDR"));
		Util::DrawDebugValueRows("##HdrRows", {
												  { T("feature.post_processing.debug.eye_adapt_speed", "Eye Adapt Speed"), Util::DebugNum(base.hdr.eyeAdaptSpeed) },
												  { T("feature.post_processing.debug.eye_adapt_strength", "Eye Adapt Strength"), Util::DebugNum(base.hdr.eyeAdaptStrength) },
												  { T("feature.post_processing.debug.bloom_blur_radius", "Bloom Blur Radius"), Util::DebugNum(base.hdr.bloomBlurRadius) },
												  { T("feature.post_processing.debug.bloom_threshold", "Bloom Threshold"), Util::DebugNum(base.hdr.bloomThreshold) },
												  { T("feature.post_processing.debug.bloom_scale", "Bloom Scale"), Util::DebugNum(base.hdr.bloomScale) },
												  { T("feature.post_processing.debug.receive_bloom_threshold", "Receive Bloom Threshold"), Util::DebugNum(base.hdr.receiveBloomThreshold) },
												  { T("feature.post_processing.debug.white", "White"), Util::DebugNum(base.hdr.white) },
												  { T("feature.post_processing.debug.sunlight_scale", "Sunlight Scale"), Util::DebugNum(base.hdr.sunlightScale) },
												  { T("feature.post_processing.debug.sky_scale", "Sky Scale"), Util::DebugNum(base.hdr.skyScale) },
											  });
	}

	{
		Util::SectionCard card("##ImageSpaceTint", ICON_TI_PALETTE, T("feature.post_processing.tint_values", "Tint"));
		Util::DrawDebugValueRows("##TintRows", {
												   { T("feature.post_processing.debug.tint_amount", "Amount"), Util::DebugNum(base.tint.amount) },
												   { T("feature.post_processing.debug.tint_color", "Color"),
													   Triple(base.tint.color.red, base.tint.color.green, base.tint.color.blue),
													   ImVec4(base.tint.color.red, base.tint.color.green, base.tint.color.blue, 1.0f) },
											   });
	}

	{
		Util::SectionCard card("##ImageSpaceDof", ICON_TI_APERTURE, T("feature.post_processing.depth_of_field_values", "Depth of Field"));
		Util::DrawDebugValueRows("##DofRows", {
												  { T("feature.post_processing.debug.dof_strength", "Strength"), Util::DebugNum(base.depthOfField.strength) },
												  { T("feature.post_processing.debug.dof_distance", "Distance"), Util::DebugNum(base.depthOfField.distance) },
												  { T("feature.post_processing.debug.dof_range", "Range"), Util::DebugNum(base.depthOfField.range) },
												  { T("feature.post_processing.debug.dof_flags", "Flags"), std::format("{}", base.depthOfField.flags) },
												  { T("feature.post_processing.debug.dof_sky_blur_radius", "Sky Blur Radius"), std::format("{}", static_cast<int>(base.depthOfField.skyBlurRadius.get())) },
											  });
	}

	Util::SeparatorTextWithFont(T("feature.post_processing.mod_data", "Mod Data"), Menu::FontRole::Subheading);

	{
		Util::SectionCard card("##ImageSpaceFade", ICON_TI_PALETTE, T("feature.post_processing.debug.fade", "Fade"));
		Util::DrawDebugValueRows("##FadeRows", {
												   { T("feature.post_processing.debug.fade_amount", "Amount"), Util::DebugNum(mod[RE::ImageSpaceModData::kFadeAmount]) },
												   { T("feature.post_processing.debug.fade_color", "Color"),
													   Triple(mod[RE::ImageSpaceModData::kFadeR], mod[RE::ImageSpaceModData::kFadeG], mod[RE::ImageSpaceModData::kFadeB]),
													   ImVec4(mod[RE::ImageSpaceModData::kFadeR], mod[RE::ImageSpaceModData::kFadeG], mod[RE::ImageSpaceModData::kFadeB], 1.0f) },
												   { T("feature.post_processing.debug.fade_blur_radius", "Blur Radius"), Util::DebugNum(mod[RE::ImageSpaceModData::kBlurRadius]) },
												   { T("feature.post_processing.debug.double_vision", "Double Vision Strength"), Util::DebugNum(mod[RE::ImageSpaceModData::kDoubleVisionStrength]) },
											   });
	}

	{
		Util::SectionCard card("##ImageSpaceRadial", ICON_TI_REFRESH, T("feature.post_processing.debug.radial_blur", "Radial Blur"));
		Util::DrawDebugValueRows("##RadialRows", {
													 { T("feature.post_processing.debug.radial_strength", "Strength"), Util::DebugNum(mod[RE::ImageSpaceModData::kRadialBlurStrength]) },
													 { T("feature.post_processing.debug.radial_rampup", "Rampup"), Util::DebugNum(mod[RE::ImageSpaceModData::kRadialBlurRampup]) },
													 { T("feature.post_processing.debug.radial_start", "Start"), Util::DebugNum(mod[RE::ImageSpaceModData::kRadialBlurStart]) },
													 { T("feature.post_processing.debug.radial_rampdown", "Rampdown"), Util::DebugNum(mod[RE::ImageSpaceModData::kRadialBlurRampdown]) },
													 { T("feature.post_processing.debug.radial_down_start", "Down Start"), Util::DebugNum(mod[RE::ImageSpaceModData::kRadialBlurDownStart]) },
													 { T("feature.post_processing.debug.radial_center", "Center"), Pair(mod[RE::ImageSpaceModData::kRadialBlurCenterX], mod[RE::ImageSpaceModData::kRadialBlurCenterY]) },
												 });
	}

	{
		Util::SectionCard card("##ImageSpaceModDof", ICON_TI_APERTURE, T("feature.post_processing.debug.mod_dof", "Depth of Field"));
		Util::DrawDebugValueRows("##ModDofRows", {
													 { T("feature.post_processing.debug.dof_strength", "Strength"), Util::DebugNum(mod[RE::ImageSpaceModData::kDOFStrength]) },
													 { T("feature.post_processing.debug.dof_distance", "Distance"), Util::DebugNum(mod[RE::ImageSpaceModData::kDOFDistance]) },
													 { T("feature.post_processing.debug.dof_range", "Range"), Util::DebugNum(mod[RE::ImageSpaceModData::kDOFRange]) },
													 { T("feature.post_processing.debug.dof_mode", "Mode"), std::format("{}", static_cast<int>(mod[RE::ImageSpaceModData::kDOFMode])) },
												 });
	}

	{
		Util::SectionCard card("##ImageSpaceMotionBlur", ICON_TI_SPARKLES, T("feature.post_processing.debug.motion_blur", "Motion Blur"));
		Util::DrawDebugValueRows("##MotionBlurRows", {
														 { T("feature.post_processing.debug.motion_blur_strength", "Strength"), Util::DebugNum(mod[RE::ImageSpaceModData::kMotionBlurStrength]) },
													 });
	}
}

void PostProcessing::DrawDebugSection()
{
	Util::DebugSection section;
	if (!section)
		return;

	// With an effect's settings page open, its debug views and the image space readouts share the section as tabs.
	auto* effect = GetOpenEffectWithDebug();
	if (!effect) {
		DrawImageSpaceDebug();
		return;
	}

	if (Util::BeginPillTabBar("##PostProcessingDebugTabs", true)) {
		if (Util::BeginPillTabItem(effect->GetDisplayName().c_str())) {
			ImGui::Spacing();
			effect->DrawDebugSection();
			Util::EndPillTabItem();
		}
		if (Util::BeginPillTabItem(GetDisplayName().c_str())) {
			ImGui::Spacing();
			DrawImageSpaceDebug();
			Util::EndPillTabItem();
		}
		Util::EndPillTabBar();
	}
}
