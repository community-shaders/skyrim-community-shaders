#include "ProceduralAurora.h"

#include "I18n/I18n.h"
#include "State.h"

#include <numbers>

#define I18N_KEY_PREFIX "feature.procedural_aurora."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ProceduralAurora::Settings,
	Enabled,
	Brightness,
	Speed,
	RayDetail,
	RayStrength,
	FoldStrength,
	Softness,
	Haze,
	Nebula,
	FringeAmount,
	CrownStart,
	NewPatternEachNight,
	GreatStormChance,
	ColorVariety,
	CurtainCount,
	CurtainSize,
	FringeColor,
	BodyColor,
	CrownColor)

namespace
{
	struct ColorPreset
	{
		std::array<float, 3> fringe;
		std::array<float, 3> body;
		std::array<float, 3> crown;
		float fringeAmount;
		float crownStart;
	};

	// Lower edge, middle and top colors. Order matches PresetName.
	constexpr std::array<ColorPreset, 7> kColorPresets = { {
		{ { 0.70f, 1.00f, 0.50f }, { 0.15f, 1.00f, 0.45f }, { 1.00f, 0.28f, 0.42f }, 0.75f, 0.22f },  // Classic
		{ { 0.60f, 1.00f, 0.75f }, { 0.05f, 0.95f, 0.40f }, { 0.05f, 0.55f, 0.90f }, 0.60f, 0.25f },  // Emerald
		{ { 0.90f, 0.98f, 1.00f }, { 0.10f, 0.85f, 1.00f }, { 0.22f, 0.25f, 1.00f }, 0.65f, 0.22f },  // Glacier
		{ { 1.00f, 0.30f, 0.80f }, { 0.25f, 0.45f, 1.00f }, { 0.95f, 0.15f, 0.30f }, 0.80f, 0.20f },  // Nebula
		{ { 1.00f, 0.40f, 0.70f }, { 1.00f, 0.20f, 0.30f }, { 0.50f, 0.12f, 0.85f }, 0.75f, 0.22f },  // Crimson Storm
		{ { 1.00f, 0.45f, 0.75f }, { 0.60f, 0.30f, 1.00f }, { 0.15f, 0.40f, 1.00f }, 0.75f, 0.22f },  // Twilight
		{ { 1.00f, 0.75f, 0.30f }, { 1.00f, 0.30f, 0.55f }, { 0.50f, 0.20f, 0.95f }, 0.80f, 0.22f },  // Ember Veil
	} };

	const char* PresetName(int index)
	{
		switch (index) {
		case 0:
			return T(TKEY("preset_classic"), "Classic (lime / green / red)");
		case 1:
			return T(TKEY("preset_emerald"), "Emerald (mint / emerald / blue)");
		case 2:
			return T(TKEY("preset_glacier"), "Glacier (ice white / cyan / deep blue)");
		case 3:
			return T(TKEY("preset_nebula"), "Nebula (pink / blue / red)");
		case 4:
			return T(TKEY("preset_crimson_storm"), "Crimson Storm (pink / red / deep purple)");
		case 5:
			return T(TKEY("preset_twilight"), "Twilight (pink / violet / blue)");
		case 6:
			return T(TKEY("preset_ember_veil"), "Ember Veil (gold / pink / violet)");
		default:
			return T(TKEY("preset_custom"), "Custom");
		}
	}

	constexpr int kMaxCurtains = 7;  // RibbonCount + DistantCount in ProceduralAurora.hlsli

	bool SameColor(const std::array<float, 3>& a, const std::array<float, 3>& b)
	{
		for (size_t i = 0; i < 3; i++)
			if (std::abs(a[i] - b[i]) > 0.002f)
				return false;
		return true;
	}

	// OKLCh (lightness, chroma, hue in radians) of a display-space color, with dim colors lifted toward an even
	// lightness so blues and purples glow as strongly as greens
	std::array<float, 3> ToAuroraLch(const std::array<float, 3>& color)
	{
		const float r = std::pow(std::clamp(color[0], 0.0f, 1.0f), 2.2f);
		const float g = std::pow(std::clamp(color[1], 0.0f, 1.0f), 2.2f);
		const float b = std::pow(std::clamp(color[2], 0.0f, 1.0f), 2.2f);
		const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
		const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
		const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
		float lightness = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
		const float labA = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
		const float labB = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
		constexpr float evenLightness = 0.72f;
		if (lightness < evenLightness)
			lightness += (evenLightness - lightness) * 0.5f;
		return { lightness, std::hypot(labA, labB), std::atan2(labB, labA) };
	}

	// Unwraps hue relative to reference along the shorter way round, unless that crosses yellow while neither end is
	// warm; then the other way, through cyan and blue, as real auroras shade
	float RouteHue(float reference, float hue)
	{
		constexpr float pi = std::numbers::pi_v<float>;
		constexpr float yellow = 95.0f * pi / 180.0f;
		auto isWarm = [=](float h) {
			const float wrapped = std::fmod(std::fmod(h, 2.0f * pi) + 2.0f * pi, 2.0f * pi);
			return wrapped >= 50.0f * pi / 180.0f && wrapped <= 125.0f * pi / 180.0f;
		};
		while (hue - reference > pi)
			hue -= 2.0f * pi;
		while (hue - reference < -pi)
			hue += 2.0f * pi;
		const float low = std::min(reference, hue);
		const float high = std::max(reference, hue);
		bool crossesYellow = false;
		for (int k = -2; k <= 2; k++) {
			const float centre = yellow + 2.0f * pi * static_cast<float>(k);
			crossesYellow = crossesYellow || (low < centre && centre < high);
		}
		if (crossesYellow && !isWarm(reference) && !isWarm(hue))
			hue += hue > reference ? -2.0f * pi : 2.0f * pi;
		return hue;
	}
}

int ProceduralAurora::MatchPreset() const
{
	for (size_t i = 0; i < kColorPresets.size(); i++) {
		const auto& preset = kColorPresets[i];
		if (SameColor(settings.FringeColor, preset.fringe) && SameColor(settings.BodyColor, preset.body) && SameColor(settings.CrownColor, preset.crown))
			return static_cast<int>(i);
	}
	return -1;
}

void ProceduralAurora::ApplyPreset(int index)
{
	if (index < 0 || index >= static_cast<int>(kColorPresets.size()))
		return;
	const auto& preset = kColorPresets[static_cast<size_t>(index)];
	settings.FringeColor = preset.fringe;
	settings.BodyColor = preset.body;
	settings.CrownColor = preset.crown;
	settings.FringeAmount = preset.fringeAmount;
	settings.CrownStart = preset.crownStart;
}

void ProceduralAurora::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &settings.Enabled);
	{
		const auto night = GetNight();
		const char* strength = night.storm >= 0.8f ? T(TKEY("night_storm"), "Great Storm") : (night.storm >= 0.45f ? T(TKEY("night_active"), "Active") : T(TKEY("night_quiet"), "Quiet"));
		const auto tonight = I18n::GetSingleton()->Format(TKEY("tonight"), { { "strength", strength }, { "preset", PresetName(night.colorPreset >= 0 ? night.colorPreset : MatchPreset()) } }, "Tonight: {strength}, {preset}");
		ImGui::SameLine();
		ImGui::TextDisabled("%s", tonight.c_str());
		ImGui::TextDisabled("%s %.0f%%", T(TKEY("showing_now"), "Aurora in the sky now:"), auroraVisibility * 100.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("showing_now_tooltip"), "Follows the vanilla aurora, which depends on the weather and time of day."));
		}
	}
	ImGui::Spacing();

	const int current = MatchPreset();
	if (ImGui::BeginCombo(T(TKEY("preset"), "Preset"), PresetName(current))) {
		for (int i = 0; i < static_cast<int>(kColorPresets.size()); i++) {
			const bool selected = i == current;
			if (ImGui::Selectable(PresetName(i), selected))
				ApplyPreset(i);
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	constexpr ImGuiColorEditFlags swatch = ImGuiColorEditFlags_NoInputs;
	ImGui::ColorEdit3(T(TKEY("crown_color"), "Top"), settings.CrownColor.data(), swatch);
	ImGui::SameLine();
	ImGui::ColorEdit3(T(TKEY("body_color"), "Middle"), settings.BodyColor.data(), swatch);
	ImGui::SameLine();
	ImGui::ColorEdit3(T(TKEY("fringe_color"), "Lower Edge"), settings.FringeColor.data(), swatch);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("colors_tooltip"), "Changing a color switches to a custom preset."));
	}

	ImGui::SliderFloat(T(TKEY("brightness"), "Brightness"), &settings.Brightness, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("softness"), "Softness"), &settings.Softness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("softness_tooltip"), "Higher makes the edges softer and more see-through."));
	}
	ImGui::SliderFloat(T(TKEY("haze"), "Haze"), &settings.Haze, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("haze_tooltip"), "A soft glow around the curtains."));
	}
	ImGui::SliderFloat(T(TKEY("nebula"), "Nebula Glow"), &settings.Nebula, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("nebula_tooltip"), "A glow in the top color above the curtains."));
	}
	ImGui::SliderInt(T(TKEY("curtain_count"), "Curtains"), &settings.CurtainCount, 1, kMaxCurtains, "%d", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("curtain_count_tooltip"), "How many curtains fill the sky. The faint ones near the horizon go first."));
	}
	ImGui::SliderFloat(T(TKEY("curtain_size"), "Curtain Size"), &settings.CurtainSize, 0.5f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("curtain_size_tooltip"), "Higher makes each curtain taller and wider, with fewer, larger folds."));
	}

	ImGui::Checkbox(T(TKEY("new_pattern_each_night"), "Different Aurora Each Night"), &settings.NewPatternEachNight);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("new_pattern_each_night_tooltip"), "Each night gets its own shape, strength and sometimes colors."));
	}

	ImGui::Spacing();
	if (ImGui::CollapsingHeader(T(TKEY("section_advanced"), "Advanced"))) {
		ImGui::SliderFloat(T(TKEY("ray_detail"), "Ray Detail"), &settings.RayDetail, 0.25f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("ray_strength"), "Ray Strength"), &settings.RayStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("fold_strength"), "Curtain Folds"), &settings.FoldStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("speed"), "Speed"), &settings.Speed, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("crown_start"), "Top Color Height"), &settings.CrownStart, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("fringe_amount"), "Lower Edge Amount"), &settings.FringeAmount, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (settings.NewPatternEachNight) {
			ImGui::SliderFloat(T(TKEY("great_storm_chance"), "Great Storm Chance"), &settings.GreatStormChance, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("color_variety"), "Random Preset Chance"), &settings.ColorVariety, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		}
	}
}

ProceduralAurora::CommonBufferData ProceduralAurora::GetCommonBufferData()
{
	if (!loaded || !settings.Enabled) {
		auroraVisibility = 0.0f;
		lastClockUpdate = {};
		return {};
	}

	const auto night = GetNight();
	CommonBufferData data{};
	// Colors go to the shader as OKLCh, hues unwrapped relative to the middle color so the shader can blend them
	const auto middle = ToAuroraLch(night.body);
	auto lower = ToAuroraLch(night.fringe);
	auto top = ToAuroraLch(night.crown);
	lower[2] = RouteHue(middle[2], lower[2]);
	top[2] = RouteHue(middle[2], top[2]);
	data.FringeColor = { lower[0], lower[1], lower[2], night.fringeAmount };
	data.BodyColor = { middle[0], middle[1], middle[2], night.storm };
	data.CrownColor = { top[0], top[1], top[2], night.crownStart };
	data.Brightness = settings.Brightness;

	// Advance the animation clock by real time, paused while the game is paused; it restarts each in-game night
	const auto now = std::chrono::steady_clock::now();
	const bool firstFrame = lastClockUpdate.time_since_epoch().count() == 0;
	double delta = firstFrame ? 0.0 : std::chrono::duration<double>(now - lastClockUpdate).count();
	lastClockUpdate = now;
	// Curtains fade in or out at one per second when the setting changes, also while the game is paused
	const float targetCurtains = static_cast<float>(std::clamp(settings.CurtainCount, 1, kMaxCurtains));
	const float curtainStep = static_cast<float>(std::clamp(delta, 0.0, 0.1));
	curtainCount = firstFrame ? targetCurtains : curtainCount + std::clamp(targetCurtains - curtainCount, -curtainStep, curtainStep);
	const auto ui = RE::UI::GetSingleton();
	if (ui && ui->GameIsPaused())
		delta = 0.0;
	const uint32_t nightIndex = GetNightIndex();
	if (nightIndex != clockNight) {
		clockNight = nightIndex;
		animationTime = 0.0;
	}
	animationTime += std::clamp(delta, 0.0, 0.1) * settings.Speed * std::lerp(0.6, 1.7, static_cast<double>(night.storm));
	data.Time = static_cast<float>(animationTime);
	data.RayDetail = settings.RayDetail;
	data.Softness = settings.Softness;
	// Ease toward the vanilla visibility so the aurora never pops, even where vanilla only switches it on or off.
	// On the first frame after enabling, start at the vanilla value, as the vanilla ribbons are culled from then on.
	const float vanilla = vanillaVisibility.load(std::memory_order_relaxed);
	const float visibilityStep = static_cast<float>(std::clamp(delta, 0.0, 0.1)) * 0.5f;
	auroraVisibility = firstFrame ? vanilla : auroraVisibility + std::clamp(vanilla - auroraVisibility, -visibilityStep, visibilityStep);
	data.Atmosphere = { auroraVisibility, settings.Haze, settings.Nebula };
	data.CurtainSize = std::clamp(settings.CurtainSize, 0.5f, 2.0f);
	data.CurtainCount = curtainCount;
	data.FoldStrength = settings.FoldStrength;
	data.RayStrength = settings.RayStrength;
	data.Enabled = 1;

	// One seed per night: the day count rounds over at noon, so a night keeps its pattern past midnight
	float seed = 0.0f;
	if (settings.NewPatternEachNight)
		seed = std::fmod(static_cast<float>(nightIndex) * 0.618034f, 1.0f) * 97.0f;
	data.Seed = seed;
	return data;
}

uint32_t ProceduralAurora::GetNightIndex()
{
	if (!globals::game::calendar)
		return 0;
	return static_cast<uint32_t>(std::max(std::floor(globals::game::calendar->GetDaysPassed() + 0.5f), 0.0f));
}

void ProceduralAurora::OnSkyUpdate(RE::Sky* a_sky)
{
	ZoneScoped;
	const bool hideRibbons = loaded && settings.Enabled;
	if (!hideRibbons && !ribbonsCulled)
		return;

	// The game fades its aurora in and out through the material alpha of its ribbons; culling them leaves that alpha
	// untouched, so it can still be read while the ribbons are hidden.
	float visibility = 0.0f;
	const auto root = a_sky ? a_sky->auroraRoot.get() : nullptr;
	if (root) {
		const bool showing = a_sky->mode.get() == RE::Sky::Mode::kFull && !root->GetAppCulled();
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
			if (IsVanillaAuroraRibbon(a_geometry->name.c_str())) {
				a_geometry->SetAppCulled(hideRibbons);
				if (showing) {
					if (auto effect = netimmerse_cast<RE::BSEffectShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get()))
						visibility = std::max(visibility, effect->QMaterialAlpha());
				}
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}
	ribbonsCulled = hideRibbons;
	vanillaVisibility.store(std::clamp(visibility, 0.0f, 1.0f), std::memory_order_relaxed);
}

ProceduralAurora::Night ProceduralAurora::GetNight() const
{
	Night night;
	night.fringe = settings.FringeColor;
	night.body = settings.BodyColor;
	night.crown = settings.CrownColor;
	night.fringeAmount = settings.FringeAmount;
	night.crownStart = settings.CrownStart;

	if (settings.NewPatternEachNight && globals::game::calendar) {
		const uint32_t nightIndex = GetNightIndex();
		auto roll = [nightIndex](uint32_t salt) {
			uint32_t x = nightIndex * 747796405u + salt * 2891336453u;
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
		};

		night.storm = roll(1) < settings.GreatStormChance ? 0.85f + 0.15f * roll(2) : 0.15f + 0.6f * roll(2);

		if (roll(3) < settings.ColorVariety * 0.4f) {
			const auto index = static_cast<size_t>(roll(4) * static_cast<float>(kColorPresets.size())) % kColorPresets.size();
			const auto& preset = kColorPresets[index];
			night.fringe = preset.fringe;
			night.body = preset.body;
			night.crown = preset.crown;
			night.fringeAmount = preset.fringeAmount;
			night.crownStart = preset.crownStart;
			night.colorPreset = static_cast<int>(index);
		}
	}
	return night;
}

void ProceduralAurora::LoadSettings(json& o_json)
{
	settings = o_json;

	// Keep hand-edited values inside the slider ranges; a non-finite value (NaN, inf) falls back to its default
	const Settings defaults{};
	auto sanitize = [](float& value, float fallback, float minValue, float maxValue) {
		value = std::isfinite(value) ? std::clamp(value, minValue, maxValue) : fallback;
	};
	auto& s = settings;
	sanitize(s.Brightness, defaults.Brightness, 0.0f, 3.0f);
	sanitize(s.Speed, defaults.Speed, 0.0f, 4.0f);
	sanitize(s.RayDetail, defaults.RayDetail, 0.25f, 3.0f);
	sanitize(s.RayStrength, defaults.RayStrength, 0.0f, 2.0f);
	sanitize(s.FoldStrength, defaults.FoldStrength, 0.0f, 1.5f);
	sanitize(s.Softness, defaults.Softness, 0.0f, 1.0f);
	sanitize(s.Haze, defaults.Haze, 0.0f, 1.0f);
	sanitize(s.Nebula, defaults.Nebula, 0.0f, 1.0f);
	sanitize(s.FringeAmount, defaults.FringeAmount, 0.0f, 1.0f);
	sanitize(s.CrownStart, defaults.CrownStart, 0.0f, 1.0f);
	sanitize(s.GreatStormChance, defaults.GreatStormChance, 0.0f, 1.0f);
	sanitize(s.ColorVariety, defaults.ColorVariety, 0.0f, 1.0f);
	sanitize(s.CurtainSize, defaults.CurtainSize, 0.5f, 2.0f);
	s.CurtainCount = std::clamp(s.CurtainCount, 1, kMaxCurtains);
	const std::array colors{ std::pair{ &s.FringeColor, &defaults.FringeColor }, std::pair{ &s.BodyColor, &defaults.BodyColor }, std::pair{ &s.CrownColor, &defaults.CrownColor } };
	for (const auto& [color, fallback] : colors)
		for (size_t i = 0; i < color->size(); ++i)
			sanitize((*color)[i], (*fallback)[i], 0.0f, 1.0f);
}

void ProceduralAurora::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ProceduralAurora::RestoreDefaultSettings()
{
	settings = {};
}

#undef I18N_KEY_PREFIX
