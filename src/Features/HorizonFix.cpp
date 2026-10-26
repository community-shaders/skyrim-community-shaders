#include "HorizonFix.h"

#include <imgui.h>

#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "Effects11.h"
#include "ExponentialHeightFog.h"
#include "Globals.h"
#include "I18n/I18n.h"

#define I18N_KEY_PREFIX "feature.horizon_fix."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HorizonFix::UserSettings,
	OverrideHorizonFixSettings,
	HorizonBlendBand,
	HorizonBlendDegrees,
	WaterSkirtRadius,
	WaterSkirtRimQuality)

namespace
{
	// Slider ceiling; HorizonFix itself accepts up to 45, and its INI advises against going past 3
	constexpr float kMaxBlendDegrees = 5.0f;
	// HorizonFix raises any smaller radius to four LOD32 blocks (4 x 131072 units)
	constexpr int kMinSkirtRadius = 524288;
	// Slider ceiling; the plugin's tile count grows with the square of the radius
	constexpr int kMaxSkirtRadius = 5000000;
	constexpr int kMaxRimQuality = 6;
}

void HorizonFix::DrawSettings()
{
	if (!setHorizonBandEnabledFn && !setHorizonBlendDegreesFn && !setWaterSkirtRadiusFn && !setWaterSkirtRimQualityFn)
		return;

	ImGui::Checkbox(T(TKEY("override_horizonfix_settings"), "Override HorizonFix Settings"), &userSettings.OverrideHorizonFixSettings);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("override_horizonfix_settings_tooltip"),
							  "Use the options below instead of HorizonFix.ini. Unchecked, HorizonFix runs on its own INI settings."));
	}

	// A scene page replays this panel bound to its own entries, where the override is one more
	// per-scene value, so the options stay editable there whatever the live override says
	const bool optionsLocked = !userSettings.OverrideHorizonFixSettings && !SceneWidgetInterceptor::IsArmed();

	// The fog hides the band whatever is set here, so both band options grey out with it
	const bool fogActive = IsHeightFogActive();

	if (setHorizonBandEnabledFn) {
		{
			auto _ = Util::DisableGuard(optionsLocked || fogActive);
			ImGui::Checkbox(T(TKEY("horizon_blend_band"), "Horizon Blend Band"), &userSettings.HorizonBlendBand);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("horizon_blend_band_tooltip"), "HorizonFix's haze band that blends the far water into the sky at the horizon. Auto-disabled by exponential height fog"));
		}
	}

	if (setHorizonBlendDegreesFn) {
		{
			auto _ = Util::DisableGuard(optionsLocked || fogActive);
			ImGui::SliderFloat(T(TKEY("horizon_blend_degrees"), "Horizon Blend Degrees"), &userSettings.HorizonBlendDegrees, 0.0f, kMaxBlendDegrees, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("horizon_blend_degrees_tooltip"),
								  "How far the band reaches above and below the horizon line, in degrees. 0 draws no band. "
								  "Replaces HorizonFix.ini's fHorizonBlendDegrees while overriding."));
		}
	}

	if (setWaterSkirtRadiusFn) {
		{
			auto _ = Util::DisableGuard(optionsLocked);
			ImGui::SliderInt(T(TKEY("water_skirt_radius"), "Water Skirt Radius"), &userSettings.WaterSkirtRadius, kMinSkirtRadius, kMaxSkirtRadius, "%d", ImGuiSliderFlags_AlwaysClamp);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("water_skirt_radius_tooltip"),
								  "How far the water horizon reaches, in game units. Larger costs more. "
								  "Replaces HorizonFix.ini's fWaterSkirtRadius while overriding."));
		}
	}

	if (setWaterSkirtRimQualityFn) {
		{
			auto _ = Util::DisableGuard(optionsLocked);
			ImGui::SliderInt(T(TKEY("water_skirt_rim_quality"), "Water Skirt Rim Quality"), &userSettings.WaterSkirtRimQuality, 0, kMaxRimQuality, "%d", ImGuiSliderFlags_AlwaysClamp);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("water_skirt_rim_quality_tooltip"),
								  "How finely the water skirt's circular outer edge is tessellated. Each step halves the edge's stair steps "
								  "and roughly doubles its draw calls. Useful only if blend band is disabled. "
								  "Replaces HorizonFix.ini's iWaterSkirtRimQuality while overriding."));
		}
	}

	// Apply edits right away, including outside the world where Prepass does not run. Not from a
	// scene page: the settings it shows are the scene's entries, not the live ones to hand the plugin.
	if (!SceneWidgetInterceptor::IsArmed())
		SyncPlugin();
}

void HorizonFix::PostPostLoad()
{
	// The shader-side far-water support is only wanted while the HorizonFix plugin is
	// actually installed; without it water keeps the vanilla far-clip look. Checked here
	// because every SKSE plugin has loaded by now and the shader disk cache has not been
	// validated yet, so installing or removing the plugin invalidates the cache through
	// regular feature validation.
	const auto companionModule = GetModuleHandleW(L"HorizonFix.dll");
	if (loaded && companionModule == nullptr) {
		loaded = false;
		failedLoadedMessage = "HorizonFix is not installed, compatibility is disabled.";
		logger::info("[Horizon Fix] HorizonFix plugin not detected, compatibility disabled");
	} else {
		logger::info("[Horizon Fix] HorizonFix plugin detected, compatibility enabled");
	}
	if (loaded) {
		// Exponential Height Fog fogs the sky out to the far water's horizon (ISSAOComposite.hlsl,
		// Sky.hlsl); the plugin reports how far that is, and 0 wherever it draws no far water.
		farWaterDistanceFn = reinterpret_cast<FarWaterDistanceFn>(GetProcAddress(companionModule, "HorizonFix_GetFarWaterDistance"));
		if (!farWaterDistanceFn)
			logger::info("[Horizon Fix] HorizonFix plugin does not export its far water distance, sky fog stays at the far plane");

		// Runtime options. A setter is only taken together with its reset, which puts the plugin
		// back on its INI value whenever no override applies, and an INI-backed setter also needs
		// its getter: the INI value is what an option no CS config holds starts from.
		const auto resolve = [companionModule](const char* name) { return GetProcAddress(companionModule, name); };
		if (const auto resetBand = reinterpret_cast<ResetHorizonBandFn>(resolve("HorizonFix_ResetHorizonBand"))) {
			setHorizonBandEnabledFn = reinterpret_cast<SetHorizonBandEnabledFn>(resolve("HorizonFix_SetHorizonBandEnabled"));
			resetHorizonBandFn = resetBand;
		}
		const auto getDegrees = reinterpret_cast<GetHorizonBlendDegreesFn>(resolve("HorizonFix_GetHorizonBlendDegrees"));
		const auto resetDegrees = reinterpret_cast<ResetHorizonBlendDegreesFn>(resolve("HorizonFix_ResetHorizonBlendDegrees"));
		if (getDegrees && resetDegrees) {
			setHorizonBlendDegreesFn = reinterpret_cast<SetHorizonBlendDegreesFn>(resolve("HorizonFix_SetHorizonBlendDegrees"));
			resetHorizonBlendDegreesFn = resetDegrees;
			iniBlendDegrees = getDegrees();
		}
		const auto getRadius = reinterpret_cast<GetWaterSkirtRadiusFn>(resolve("HorizonFix_GetWaterSkirtRadius"));
		const auto resetRadius = reinterpret_cast<ResetWaterSkirtRadiusFn>(resolve("HorizonFix_ResetWaterSkirtRadius"));
		if (getRadius && resetRadius) {
			setWaterSkirtRadiusFn = reinterpret_cast<SetWaterSkirtRadiusFn>(resolve("HorizonFix_SetWaterSkirtRadius"));
			resetWaterSkirtRadiusFn = resetRadius;
			iniSkirtRadius = static_cast<int>(std::lround(getRadius()));
		}
		const auto getRimQuality = reinterpret_cast<GetWaterSkirtRimQualityFn>(resolve("HorizonFix_GetWaterSkirtRimQuality"));
		const auto resetRimQuality = reinterpret_cast<ResetWaterSkirtRimQualityFn>(resolve("HorizonFix_ResetWaterSkirtRimQuality"));
		if (getRimQuality && resetRimQuality) {
			setWaterSkirtRimQualityFn = reinterpret_cast<SetWaterSkirtRimQualityFn>(resolve("HorizonFix_SetWaterSkirtRimQuality"));
			resetWaterSkirtRimQualityFn = resetRimQuality;
			iniRimQuality = getRimQuality();
		}
		if (!setHorizonBandEnabledFn || !setHorizonBlendDegreesFn || !setWaterSkirtRadiusFn || !setWaterSkirtRimQualityFn)
			logger::info("[Horizon Fix] HorizonFix plugin predates some runtime settings exports, those settings stay INI-only");

		// The CS config was loaded before the plugin could be asked for its INI values
		ApplyIniFallbacks();

		// A saved override reaches the plugin before it builds its first skirt
		SyncPlugin();
	}
}

void HorizonFix::Prepass()
{
	// Every frame, so the band follows Exponential Height Fog being toggled and settings applied
	// from anywhere (presets, a scene starting or ending its override, restore defaults) reach
	// the plugin
	SyncPlugin();
}

void HorizonFix::SaveSettings(json& o_json)
{
	o_json = userSettings;
}

void HorizonFix::LoadSettings(json& o_json)
{
	userSettings = o_json;
	blendDegreesFromConfig = o_json.contains("HorizonBlendDegrees");
	skirtRadiusFromConfig = o_json.contains("WaterSkirtRadius");
	rimQualityFromConfig = o_json.contains("WaterSkirtRimQuality");
	ApplyIniFallbacks();
}

void HorizonFix::RestoreDefaultSettings()
{
	userSettings = {};
	blendDegreesFromConfig = false;
	skirtRadiusFromConfig = false;
	rimQualityFromConfig = false;
	ApplyIniFallbacks();
}

void HorizonFix::ApplyIniFallbacks()
{
	// Only once PostPostLoad has asked the plugin; until then the struct defaults stand in
	if (setHorizonBlendDegreesFn && !blendDegreesFromConfig)
		userSettings.HorizonBlendDegrees = iniBlendDegrees;
	if (setWaterSkirtRadiusFn && !skirtRadiusFromConfig)
		userSettings.WaterSkirtRadius = iniSkirtRadius;
	if (setWaterSkirtRimQualityFn && !rimQualityFromConfig)
		userSettings.WaterSkirtRimQuality = iniRimQuality;
}

bool HorizonFix::IsHeightFogActive()
{
	// The fog's own shader-side switch, minus the flat world map (HorizonFix hides its far water
	// there anyway, so following it would only toggle the band back and forth)
	const auto& fog = globals::features::exponentialHeightFog;
	return fog.loaded && fog.settings.enabled && !globals::features::effects11.OwnsFeature(E11Handoff::Feature::HeightFog);
}

void HorizonFix::SyncPlugin()
{
	if (!loaded)
		return;

	const bool overriding = userSettings.OverrideHorizonFixSettings;

	if (setHorizonBandEnabledFn) {
		// Hidden while the fog does the band's job or an override turns it off; otherwise the
		// plugin's own behavior
		const bool hideBand = IsHeightFogActive() || (overriding && !userSettings.HorizonBlendBand);
		if (appliedBandHidden != hideBand) {
			if (hideBand)
				setHorizonBandEnabledFn(false);
			else
				resetHorizonBandFn();
			appliedBandHidden = hideBand;
		}
	}

	if (setHorizonBlendDegreesFn) {
		// Empty: the plugin's INI value. An override is held to the slider range; the INI's own
		// value passes as it is.
		std::optional<float> degrees;
		if (overriding)
			degrees = userSettings.HorizonBlendDegrees == iniBlendDegrees ? iniBlendDegrees : std::clamp(userSettings.HorizonBlendDegrees, 0.0f, kMaxBlendDegrees);
		if (degrees != appliedBlendDegrees) {
			if (degrees)
				setHorizonBlendDegreesFn(*degrees);
			else
				resetHorizonBlendDegreesFn();
			appliedBlendDegrees = degrees;
		}
	}

	if (setWaterSkirtRadiusFn) {
		// Empty: the plugin's INI value. A settings file or scene can carry anything, so an
		// override is held to the slider range, since the plugin's tile count grows with the
		// square of the radius; the INI's own value passes as it is.
		std::optional<int> radius;
		if (overriding)
			radius = userSettings.WaterSkirtRadius == iniSkirtRadius ? iniSkirtRadius : std::clamp(userSettings.WaterSkirtRadius, kMinSkirtRadius, kMaxSkirtRadius);
		if (radius != appliedSkirtRadius) {
			if (radius)
				setWaterSkirtRadiusFn(static_cast<float>(*radius));
			else
				resetWaterSkirtRadiusFn();
			appliedSkirtRadius = radius;
		}
	}

	if (setWaterSkirtRimQualityFn) {
		std::optional<int> quality;
		if (overriding)
			quality = std::clamp(userSettings.WaterSkirtRimQuality, 0, kMaxRimQuality);
		if (quality != appliedRimQuality) {
			if (quality)
				setWaterSkirtRimQualityFn(*quality);
			else
				resetWaterSkirtRimQualityFn();
			appliedRimQuality = quality;
		}
	}
}

HorizonFix::Settings HorizonFix::GetCommonBufferData() const
{
	Settings data;
	if (loaded && farWaterDistanceFn)
		data.farWaterDistance = std::max(farWaterDistanceFn(), 0.0f);
	return data;
}
