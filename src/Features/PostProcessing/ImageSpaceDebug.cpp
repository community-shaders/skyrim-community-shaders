#include "Features/PostProcessing.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/MenuIcons.h"
#include "Utils/DebugSection.h"
#include "Utils/UI.h"

#include <format>
#include <initializer_list>
#include <optional>
#include <string>

namespace
{
	/** @brief One readout: a dim label on the left, the value right-aligned, and an optional colour swatch before it. */
	struct ValueRow
	{
		const char* label;
		std::string value;
		std::optional<ImVec4> swatch = std::nullopt;
	};

	std::string Num(float v)
	{
		return std::format("{:.3f}", v);
	}

	std::string Triple(float a, float b, float c)
	{
		return std::format("({:.3f}, {:.3f}, {:.3f})", a, b, c);
	}

	std::string Pair(float a, float b)
	{
		return std::format("({:.3f}, {:.3f})", a, b);
	}

	/** @brief Draws the rows as a two-column label/value list. */
	void DrawValueRows(const char* id, std::initializer_list<ValueRow> rows)
	{
		if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
			return;
		ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 1.0f);  // i18n-ignore: table column id
		ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f);  // i18n-ignore: table column id

		const float swatchSize = ImGui::GetFontSize();
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		int index = 0;
		for (const auto& row : rows) {
			ImGui::PushID(index++);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", row.label);

			ImGui::TableNextColumn();
			const float textWidth = ImGui::CalcTextSize(row.value.c_str()).x;
			const float width = textWidth + (row.swatch ? swatchSize + spacing : 0.0f);
			const float offset = ImGui::GetContentRegionAvail().x - width;
			if (offset > 0.0f)
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
			if (row.swatch) {
				ImGui::ColorButton("##swatch", *row.swatch,  // i18n-ignore: widget id
					ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoAlpha,
					ImVec2(swatchSize, swatchSize));
				ImGui::SameLine(0.0f, spacing);
			}
			ImGui::TextUnformatted(row.value.c_str());
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}

void PostProcessing::DrawDebugSection()
{
	Util::DebugSection section;
	if (!section)
		return;

	section.Hint(T("feature.post_processing.debug.hint", "Live values of the game's current image space, before Post Processing modifies them."));
	ImGui::Spacing();

	const auto& data = imageSpaceManager->gameISData;
	const auto& base = data.baseData;
	const auto& mod = data.modData.data;

	{
		Util::SectionCard card("##ImageSpaceOverview", ICON_TI_EYE, T("feature.post_processing.debug.overview", "Overview"));
		DrawValueRows("##OverviewRows", {
											{ T("feature.post_processing.base_amount", "Base Amount"), Num(data.baseAmount) },
											{ T("feature.post_processing.mod_amount", "Mod Amount"), Num(data.modAmount) },
										});
	}

	Util::SeparatorTextWithFont(T("feature.post_processing.base_data", "Base Data"), Menu::FontRole::Subheading);

	{
		Util::SectionCard card("##ImageSpaceCinematic", ICON_TI_ADJUSTMENTS_HORIZONTAL, T("feature.post_processing.cinematic_values", "Cinematic"));
		DrawValueRows("##CinematicRows", {
											 { T("feature.post_processing.debug.saturation", "Saturation"), Num(base.cinematic.saturation) },
											 { T("feature.post_processing.debug.brightness", "Brightness"), Num(base.cinematic.brightness) },
											 { T("feature.post_processing.debug.contrast", "Contrast"), Num(base.cinematic.contrast) },
										 });
	}

	{
		Util::SectionCard card("##ImageSpaceHdr", ICON_TI_SUN, T("feature.post_processing.hdr_values", "HDR"));
		DrawValueRows("##HdrRows", {
									   { T("feature.post_processing.debug.eye_adapt_speed", "Eye Adapt Speed"), Num(base.hdr.eyeAdaptSpeed) },
									   { T("feature.post_processing.debug.eye_adapt_strength", "Eye Adapt Strength"), Num(base.hdr.eyeAdaptStrength) },
									   { T("feature.post_processing.debug.bloom_blur_radius", "Bloom Blur Radius"), Num(base.hdr.bloomBlurRadius) },
									   { T("feature.post_processing.debug.bloom_threshold", "Bloom Threshold"), Num(base.hdr.bloomThreshold) },
									   { T("feature.post_processing.debug.bloom_scale", "Bloom Scale"), Num(base.hdr.bloomScale) },
									   { T("feature.post_processing.debug.receive_bloom_threshold", "Receive Bloom Threshold"), Num(base.hdr.receiveBloomThreshold) },
									   { T("feature.post_processing.debug.white", "White"), Num(base.hdr.white) },
									   { T("feature.post_processing.debug.sunlight_scale", "Sunlight Scale"), Num(base.hdr.sunlightScale) },
									   { T("feature.post_processing.debug.sky_scale", "Sky Scale"), Num(base.hdr.skyScale) },
								   });
	}

	{
		Util::SectionCard card("##ImageSpaceTint", ICON_TI_PALETTE, T("feature.post_processing.tint_values", "Tint"));
		DrawValueRows("##TintRows", {
										{ T("feature.post_processing.debug.tint_amount", "Amount"), Num(base.tint.amount) },
										{ T("feature.post_processing.debug.tint_color", "Color"),
											Triple(base.tint.color.red, base.tint.color.green, base.tint.color.blue),
											ImVec4(base.tint.color.red, base.tint.color.green, base.tint.color.blue, 1.0f) },
									});
	}

	{
		Util::SectionCard card("##ImageSpaceDof", ICON_TI_APERTURE, T("feature.post_processing.depth_of_field_values", "Depth of Field"));
		DrawValueRows("##DofRows", {
									   { T("feature.post_processing.debug.dof_strength", "Strength"), Num(base.depthOfField.strength) },
									   { T("feature.post_processing.debug.dof_distance", "Distance"), Num(base.depthOfField.distance) },
									   { T("feature.post_processing.debug.dof_range", "Range"), Num(base.depthOfField.range) },
									   { T("feature.post_processing.debug.dof_flags", "Flags"), std::format("{}", base.depthOfField.flags) },
									   { T("feature.post_processing.debug.dof_sky_blur_radius", "Sky Blur Radius"), std::format("{}", static_cast<int>(base.depthOfField.skyBlurRadius.get())) },
								   });
	}

	Util::SeparatorTextWithFont(T("feature.post_processing.mod_data", "Mod Data"), Menu::FontRole::Subheading);

	{
		Util::SectionCard card("##ImageSpaceFade", ICON_TI_PALETTE, T("feature.post_processing.debug.fade", "Fade"));
		DrawValueRows("##FadeRows", {
										{ T("feature.post_processing.debug.fade_amount", "Amount"), Num(mod[RE::ImageSpaceModData::kFadeAmount]) },
										{ T("feature.post_processing.debug.fade_color", "Color"),
											Triple(mod[RE::ImageSpaceModData::kFadeR], mod[RE::ImageSpaceModData::kFadeG], mod[RE::ImageSpaceModData::kFadeB]),
											ImVec4(mod[RE::ImageSpaceModData::kFadeR], mod[RE::ImageSpaceModData::kFadeG], mod[RE::ImageSpaceModData::kFadeB], 1.0f) },
										{ T("feature.post_processing.debug.fade_blur_radius", "Blur Radius"), Num(mod[RE::ImageSpaceModData::kBlurRadius]) },
										{ T("feature.post_processing.debug.double_vision", "Double Vision Strength"), Num(mod[RE::ImageSpaceModData::kDoubleVisionStrength]) },
									});
	}

	{
		Util::SectionCard card("##ImageSpaceRadial", ICON_TI_REFRESH, T("feature.post_processing.debug.radial_blur", "Radial Blur"));
		DrawValueRows("##RadialRows", {
										  { T("feature.post_processing.debug.radial_strength", "Strength"), Num(mod[RE::ImageSpaceModData::kRadialBlurStrength]) },
										  { T("feature.post_processing.debug.radial_rampup", "Rampup"), Num(mod[RE::ImageSpaceModData::kRadialBlurRampup]) },
										  { T("feature.post_processing.debug.radial_start", "Start"), Num(mod[RE::ImageSpaceModData::kRadialBlurStart]) },
										  { T("feature.post_processing.debug.radial_rampdown", "Rampdown"), Num(mod[RE::ImageSpaceModData::kRadialBlurRampdown]) },
										  { T("feature.post_processing.debug.radial_down_start", "Down Start"), Num(mod[RE::ImageSpaceModData::kRadialBlurDownStart]) },
										  { T("feature.post_processing.debug.radial_center", "Center"), Pair(mod[RE::ImageSpaceModData::kRadialBlurCenterX], mod[RE::ImageSpaceModData::kRadialBlurCenterY]) },
									  });
	}

	{
		Util::SectionCard card("##ImageSpaceModDof", ICON_TI_APERTURE, T("feature.post_processing.debug.mod_dof", "Depth of Field"));
		DrawValueRows("##ModDofRows", {
										  { T("feature.post_processing.debug.dof_strength", "Strength"), Num(mod[RE::ImageSpaceModData::kDOFStrength]) },
										  { T("feature.post_processing.debug.dof_distance", "Distance"), Num(mod[RE::ImageSpaceModData::kDOFDistance]) },
										  { T("feature.post_processing.debug.dof_range", "Range"), Num(mod[RE::ImageSpaceModData::kDOFRange]) },
										  { T("feature.post_processing.debug.dof_mode", "Mode"), std::format("{}", static_cast<int>(mod[RE::ImageSpaceModData::kDOFMode])) },
									  });
	}

	{
		Util::SectionCard card("##ImageSpaceMotionBlur", ICON_TI_SPARKLES, T("feature.post_processing.debug.motion_blur", "Motion Blur"));
		DrawValueRows("##MotionBlurRows", {
											  { T("feature.post_processing.debug.motion_blur_strength", "Strength"), Num(mod[RE::ImageSpaceModData::kMotionBlurStrength]) },
										  });
	}
}
