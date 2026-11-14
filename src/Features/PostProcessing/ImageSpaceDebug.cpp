#include "Features/PostProcessing.h"
#include "I18n/I18n.h"
#include "Utils/DebugSection.h"

void PostProcessing::DrawDebugSection()
{
	Util::DebugSection section;
	if (!section)
		return;

	ImGui::Text(T("feature.post_processing.base_amount", "Base Amount: %.3f"), imageSpaceManager->gameISData.baseAmount);
	ImGui::Text("%s", T("feature.post_processing.base_data", "Base Data:"));
	ImGui::Text("%s", T("feature.post_processing.cinematic_values", "Cinematic Values:"));
	ImGui::Text(T("feature.post_processing.saturation_brightness_contrast_values", "Saturation: %.3f\nBrightness: %.3f\nContrast: %.3f"),
		imageSpaceManager->gameISData.baseData.cinematic.saturation,
		imageSpaceManager->gameISData.baseData.cinematic.brightness,
		imageSpaceManager->gameISData.baseData.cinematic.contrast);

	ImGui::Text("%s", T("feature.post_processing.hdr_values", "HDR Values:"));
	ImGui::Text(T("feature.post_processing.hdr_values_detail", "Eye Adapt Speed: %.3f\nBloom Blur Radius: %.3f\nBloom Threshold: %.3f\nBloom Scale: %.3f\nReceive Bloom Threshold: %.3f\nWhite: %.3f\nSunlight Scale: %.3f\nSky Scale: %.3f\nEye Adapt Strength: %.3f"),
		imageSpaceManager->gameISData.baseData.hdr.eyeAdaptSpeed,
		imageSpaceManager->gameISData.baseData.hdr.bloomBlurRadius,
		imageSpaceManager->gameISData.baseData.hdr.bloomThreshold,
		imageSpaceManager->gameISData.baseData.hdr.bloomScale,
		imageSpaceManager->gameISData.baseData.hdr.receiveBloomThreshold,
		imageSpaceManager->gameISData.baseData.hdr.white,
		imageSpaceManager->gameISData.baseData.hdr.sunlightScale,
		imageSpaceManager->gameISData.baseData.hdr.skyScale,
		imageSpaceManager->gameISData.baseData.hdr.eyeAdaptStrength);

	ImGui::Text("%s", T("feature.post_processing.tint_values", "Tint Values:"));
	ImGui::Text(T("feature.post_processing.tint_values_detail", "Tint Amount: %.3f\nTint Color: (%.3f, %.3f, %.3f)"),
		imageSpaceManager->gameISData.baseData.tint.amount,
		imageSpaceManager->gameISData.baseData.tint.color.red,
		imageSpaceManager->gameISData.baseData.tint.color.green,
		imageSpaceManager->gameISData.baseData.tint.color.blue);

	ImGui::Text("%s", T("feature.post_processing.depth_of_field_values", "Depth of Field Values:"));
	ImGui::Text(T("feature.post_processing.depth_of_field_values_detail", "DOF Strength: %.3f\nDOF Distance: %.3f\nDOF Range: %.3f\nDOF Flags: %d\nDOF Sky Blur Radius: %d"),
		imageSpaceManager->gameISData.baseData.depthOfField.strength,
		imageSpaceManager->gameISData.baseData.depthOfField.distance,
		imageSpaceManager->gameISData.baseData.depthOfField.range,
		imageSpaceManager->gameISData.baseData.depthOfField.flags,
		static_cast<int>(imageSpaceManager->gameISData.baseData.depthOfField.skyBlurRadius.get()));

	ImGui::Text(T("feature.post_processing.mod_amount", "Mod Amount: %.3f"), imageSpaceManager->gameISData.modAmount);
	ImGui::Text("%s", T("feature.post_processing.mod_data", "Mod Data:"));
	ImGui::Text(T("feature.post_processing.mod_fade_values_detail", "Fade Amount: %.3f\nFade Color: (%.3f, %.3f, %.3f)\nBlur Radius: %.3f\nDouble Vision Strength: %.3f\n"),
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeAmount],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeR],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeG],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kFadeB],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kBlurRadius],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDoubleVisionStrength]);
	ImGui::Text(T("feature.post_processing.radial_blur_values_detail", "Radial Blur Strength: %.3f\nRadial Blur Rampup: %.3f\nRadial Blur Start: %.3f\nRadial Blur Rampdown: %.3f\nRadial Blur Down Start: %.3f\nRadial Blur Center: (%.3f, %.3f)"),
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurStrength],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurRampup],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurStart],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurRampdown],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurDownStart],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurCenterX],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kRadialBlurCenterY]);
	ImGui::Text(T("feature.post_processing.mod_dof_values_detail", "DOF Strength: %.3f\nDOF Distance: %.3f\nDOF Range: %.3f\nDOF Mode: %d"),
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFStrength],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFDistance],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFRange],
		imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kDOFMode]);
	ImGui::Text(T("feature.post_processing.motion_blur_strength", "Motion Blur Strength: %.3f"), imageSpaceManager->gameISData.modData.data[RE::ImageSpaceModData::kMotionBlurStrength]);
}
