#include "ProceduralSun.h"

#include <algorithm>
#include <cmath>

#include "Effects11.h"
#include "Effects11/SettingManager.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.procedural_sun."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ProceduralSun::Settings,
	enabled,
	sunDiskAngularRadius,
	diskIntensity,
	edgeSoftness,
	haloEnabled,
	haloAngularWidth,
	haloIntensity,
	haloFalloff,
	cloudOcclusionStrength)

namespace
{
	constexpr float kMinimumAngularRadius = DirectX::XMConvertToRadians(0.05f);
	constexpr float kMaximumAngularRadius = DirectX::XMConvertToRadians(5.0f);
	constexpr float kMaximumDiskIntensity = 20.0f;
	constexpr float kMinimumEdgeSoftness = 0.01f;
	constexpr float kMaximumEdgeSoftness = 1.0f;
	constexpr float kMinimumHaloAngularWidth = DirectX::XMConvertToRadians(0.05f);
	constexpr float kMaximumHaloAngularWidth = DirectX::XMConvertToRadians(5.0f);
	constexpr float kMaximumHaloIntensity = 20.0f;
	constexpr float kMinimumHaloFalloff = 1.0f;
	constexpr float kMaximumHaloFalloff = 100.0f;
	constexpr float kMaximumCloudOcclusionStrength = 4.0f;

	void ClampSettings(ProceduralSun::Settings& settings)
	{
		const ProceduralSun::Settings defaults;
		settings.enabled = settings.enabled != 0;
		settings.haloEnabled = settings.haloEnabled != 0;
		if (!std::isfinite(settings.sunDiskAngularRadius))
			settings.sunDiskAngularRadius = defaults.sunDiskAngularRadius;
		if (!std::isfinite(settings.diskIntensity))
			settings.diskIntensity = defaults.diskIntensity;
		if (!std::isfinite(settings.edgeSoftness))
			settings.edgeSoftness = defaults.edgeSoftness;
		if (!std::isfinite(settings.haloAngularWidth))
			settings.haloAngularWidth = defaults.haloAngularWidth;
		if (!std::isfinite(settings.haloIntensity))
			settings.haloIntensity = defaults.haloIntensity;
		if (!std::isfinite(settings.haloFalloff))
			settings.haloFalloff = defaults.haloFalloff;
		if (!std::isfinite(settings.cloudOcclusionStrength))
			settings.cloudOcclusionStrength = defaults.cloudOcclusionStrength;

		settings.sunDiskAngularRadius = std::clamp(settings.sunDiskAngularRadius, kMinimumAngularRadius, kMaximumAngularRadius);
		settings.diskIntensity = std::clamp(settings.diskIntensity, 0.0f, kMaximumDiskIntensity);
		settings.edgeSoftness = std::clamp(settings.edgeSoftness, kMinimumEdgeSoftness, kMaximumEdgeSoftness);
		settings.haloAngularWidth = std::clamp(settings.haloAngularWidth, kMinimumHaloAngularWidth, kMaximumHaloAngularWidth);
		settings.haloIntensity = std::clamp(settings.haloIntensity, 0.0f, kMaximumHaloIntensity);
		settings.haloFalloff = std::clamp(settings.haloFalloff, kMinimumHaloFalloff, kMaximumHaloFalloff);
		settings.cloudOcclusionStrength = std::clamp(settings.cloudOcclusionStrength, 0.0f, kMaximumCloudOcclusionStrength);
	}
}

void ProceduralSun::DrawSettings()
{
	const Util::LockedSection enbLock(IsManagedByENB(),
		T("common.settings_managed_by_enb", "This setting is managed by Effects 11."));

	bool enabled = settings.enabled != 0;
	if (ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &enabled))
		settings.enabled = enabled;

	ImGui::SliderAngle(T(TKEY("angular_radius"), "Angular Radius"), &settings.sunDiskAngularRadius, 0.05f, 5.0f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", T(TKEY("angular_radius_tooltip"), "The real sun's angular radius is about 0.27 degrees. The 0.53 degree default preserves Jiaye's established procedural-sun scale."));

	ImGui::SliderFloat(T(TKEY("disk_intensity"), "Disk Intensity"), &settings.diskIntensity, 0.0f, kMaximumDiskIntensity, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", T(TKEY("disk_intensity_tooltip"), "Brightness of the procedural disc before the existing sky brightness and HDR adjustments."));

	ImGui::SliderFloat(T(TKEY("edge_softness"), "Edge Softness"), &settings.edgeSoftness, kMinimumEdgeSoftness, kMaximumEdgeSoftness, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", T(TKEY("edge_softness_tooltip"), "Width of the anti-aliased transition at the edge of the sun disc."));

	ImGui::SliderFloat(T(TKEY("cloud_occlusion_strength"), "Cloud Occlusion Strength"), &settings.cloudOcclusionStrength, 0.0f, kMaximumCloudOcclusionStrength, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", T(TKEY("cloud_occlusion_strength_tooltip"), "Additional fading of the disc and halo behind clouds. 0 preserves normal cloud blending; higher values hide the sun more strongly. Clear sky is unchanged."));

	bool haloEnabled = settings.haloEnabled != 0;
	if (ImGui::Checkbox(T(TKEY("halo_enabled"), "Enable Halo"), &haloEnabled))
		settings.haloEnabled = haloEnabled;

	ImGui::BeginDisabled(!haloEnabled);
	ImGui::SliderAngle(T(TKEY("halo_width"), "Halo Width"), &settings.haloAngularWidth, 0.05f, 5.0f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", T(TKEY("halo_width_tooltip"), "Angular distance the halo extends beyond the edge of the sun disc."));

	ImGui::SliderFloat(T(TKEY("halo_intensity"), "Halo Intensity"), &settings.haloIntensity, 0.0f, kMaximumHaloIntensity, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", T(TKEY("halo_intensity_tooltip"), "Brightness of the halo before the normal sun tint, HDR, and bloom adjustments."));

	ImGui::SliderFloat(T(TKEY("halo_falloff"), "Halo Falloff"), &settings.haloFalloff, kMinimumHaloFalloff, kMaximumHaloFalloff, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", T(TKEY("halo_falloff_tooltip"), "How quickly the halo fades with angle. Higher values concentrate it near the sun disc."));
	ImGui::EndDisabled();
}

#undef I18N_KEY_PREFIX

void ProceduralSun::LoadSettings(json& o_json)
{
	settings = o_json;
	ClampSettings(settings);
}

void ProceduralSun::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ProceduralSun::RestoreDefaultSettings()
{
	settings = {};
}

bool ProceduralSun::IsManagedByENB() const
{
	const auto& effects11 = globals::features::effects11;
	return effects11.loaded && effects11.enableEffect && SettingManager::GetSingleton().GetValue<bool>("EnableProceduralSun", "EFFECT");
}

ProceduralSun::PerFrameData ProceduralSun::GetCommonBufferData() const
{
	PerFrameData data{
		.enabled = settings.enabled,
		.sunDiskCos = std::cos(settings.sunDiskAngularRadius),
		.diskIntensity = settings.diskIntensity,
		.edgeSoftness = settings.edgeSoftness,
		.haloEnabled = settings.haloEnabled,
		.sunHaloCos = std::cos(settings.sunDiskAngularRadius + settings.haloAngularWidth),
		.haloIntensity = settings.haloIntensity,
		.haloFalloff = settings.haloFalloff,
		.cloudOcclusionStrength = settings.cloudOcclusionStrength
	};
	const auto* sky = globals::game::sky;
	if (settings.enabled && sky && sky->sun && sky->sun->sunBase) {
		const float radius = sky->sun->sunBase->GetModelData().modelBound.radius;
		if (std::isfinite(radius) && radius > 0.0f)
			data.sunQuadModelRadius = radius;
	}
	return data;
}
