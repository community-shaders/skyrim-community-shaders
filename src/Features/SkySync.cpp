#include "SkySync.h"
#include "../I18n/I18n.h"
#include "RE/B/BSVolumetricLightingRenderData.h"

#define I18N_KEY_PREFIX "feature.sky_sync."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SkySync::Settings,
	Enabled,
	UseAlternateSunPath,
	MoonLightSource,
	SunPath,
	CustomAngle,
	MinShadowElevation,
	ShadowTransitionDuration,
	DimSunlightUnderHorizon,
	DimVolumetricLighting,
	HorizonFadeHours,
	NewMoonIntensity,
	CrescentMoonIntensity,
	FullMoonIntensity)

void SkySync::DrawSettings()
{
	const char* sunPathNames[] = {
		T(TKEY("sun_path_southern"), "Southern Sky"),
		T(TKEY("sun_path_northern"), "Northern Sky"),
		T(TKEY("sun_path_vanilla"), "Vanilla"),
		T(TKEY("sun_path_custom"), "Custom")
	};
	const char* moonLightSourceNames[] = {
		T(TKEY("moon_light_source_brightest"), "Brightest"),
		T(TKEY("moon_light_source_masser"), "Masser"),
		T(TKEY("moon_light_source_secunda"), "Secunda")
	};

	ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T(TKEY("enabled_tooltip"), "Enable or disable Sky Sync features."));
	}

	ImGui::Checkbox(T(TKEY("use_alternate_sun_path"), "Use alternate sun path"), &settings.UseAlternateSunPath);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T(TKEY("use_alternate_sun_path_tooltip"), "Calculate sun position based on time of day and season instead of vanilla movement."));
	}

	if (settings.UseAlternateSunPath) {
		if (ImGui::SliderInt(T(TKEY("sun_path"), "Sun path"), &settings.SunPath, 0, static_cast<uint8_t>(SunPath::Count) - 1, sunPathNames[settings.SunPath], ImGuiSliderFlags_AlwaysClamp))
			SetSunAngle();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("sun_path_tooltip"), "Choose the trajectory the sun takes across the sky."));
		}

		if (settings.SunPath == static_cast<int32_t>(SunPath::Custom)) {
			if (ImGui::SliderFloat(T(TKEY("custom_angle"), "Custom angle"), &settings.CustomAngle, -90.0f, 90.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
				SetSunAngle();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T(TKEY("custom_angle_tooltip"), "Set a custom angle for the sun's trajectory."));
			}
		}
	}

	ImGui::SliderInt(T(TKEY("moon_light_source"), "Moon light source"), &settings.MoonLightSource, 0, static_cast<uint8_t>(MoonLightSource::Count) - 1, moonLightSourceNames[settings.MoonLightSource], ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T(TKEY("moon_light_source_tooltip"), "Select which moon casts shadows during the night."));
	}

	ImGui::SliderFloat(T(TKEY("min_shadow_elevation"), "Min Shadow Elevation"), &settings.MinShadowElevation, 0.0f, 45.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("min_shadow_elevation_tooltip"), "The minimum angle sunlight will set to. Caps shadow length. Higher = shorter shadows at sunset/sunrise."));
	}

	ImGui::SliderFloat(T(TKEY("shadow_transition_duration"), "Shadow Transition Duration"), &settings.ShadowTransitionDuration, 0.0f, 500.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("shadow_transition_duration_tooltip"), "How long (in game-time units) the shadow direction takes to fade between sources. 100 = ~5 seconds at timescale 20."));
	}

	ImGui::Checkbox(T(TKEY("dim_sunlight_under_horizon"), "Dim Sunlight Under Horizon"), &settings.DimSunlightUnderHorizon);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T(TKEY("dim_sunlight_under_horizon_tooltip"), "Fade directional light to zero as the sun goes below the horizon."));
	}

	ImGui::Checkbox(T(TKEY("fade_volumetric_lighting"), "Fade Volumetric Lighting"), &settings.DimVolumetricLighting);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T(TKEY("fade_volumetric_lighting_tooltip"), "Also fade volumetric lighting with the directional dim around dawn and dusk."));
	}

	if (settings.DimSunlightUnderHorizon || settings.DimVolumetricLighting) {
		ImGui::SliderFloat(T(TKEY("horizon_fade_duration"), "Horizon Fade Duration"), &settings.HorizonFadeHours, 0.0f, MaxHorizonFadeHours, "%.1f h", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("horizon_fade_duration_tooltip"), "How long (in game hours) the dim eases out after sunset and back in before sunrise."));
		}
	}

	ImGui::SliderFloat(T(TKEY("new_moon_intensity"), "New Moon Intensity"), &settings.NewMoonIntensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("crescent_intensity"), "Crescent Intensity"), &settings.CrescentMoonIntensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("full_moon_intensity"), "Full Moon Intensity"), &settings.FullMoonIntensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

	if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_None)) {
		static constexpr const char* CasterNames[] = { "Sun", "Masser", "Secunda", "None" };
		static constexpr const char* PhaseNames[] = { "Full", "Waning Gibbous", "Waning Quarter", "Waning Crescent", "New", "Waxing Crescent", "Waxing Quarter", "Waxing Gibbous" };

		auto getPhase = [](const RE::Moon* moon) -> const char* {
			if (!moon || !moon->moonMesh)
				return "Unknown";
			if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().shaderProperty.get())) {
				if (auto tex = prop->GetBaseTexture())
					return PhaseNames[static_cast<int>(Util::Moon::GetPhaseFromTexture(tex->name.c_str()))];
			}
			return "Unknown";
		};

		auto drawMoonEntry = [&](const char* label, Caster caster, const char* phase) {
			auto& color = colors[static_cast<int>(caster)];
			ImVec4 swatch = { color.x, color.y, color.z, 1.0f };
			ImGui::ColorButton(label, swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, { ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight() });
			ImGui::SameLine();
			ImGui::Text("%s  [%s]  color (%.3f, %.3f, %.3f, %.3f)", label, phase, color.x, color.y, color.z, color.w);
		};

		const auto sky = globals::game::sky;
		drawMoonEntry("Masser", Caster::Masser, sky ? getPhase(sky->masser) : "Unknown");
		drawMoonEntry("Secunda", Caster::Secunda, sky ? getPhase(sky->secunda) : "Unknown");

		ImGui::Text("Dim: %.3f", currentDim);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::Text("Shadow target: %s", CasterNames[static_cast<int>(shadowFader.target)]);
		ImGui::Text("Shadow dir:    (%.2f, %.2f, %.2f)", shadowFader.currentDir.x, shadowFader.currentDir.y, shadowFader.currentDir.z);
		ImGui::Text("VL intensity factor: %.3f", shadowFader.vlIntensityFactor);
		if (shadowFader.transitioning) {
			const float t = settings.ShadowTransitionDuration > 0.0f ? shadowFader.fadeTimer / settings.ShadowTransitionDuration : 1.0f;
			ImGui::ProgressBar(t, { -1.0f, 0.0f }, "");
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::Text("Transitioning %.0f%%", t * 100.0f);
		} else {
			ImGui::TextDisabled("No transition");
		}

		ImGui::TreePop();
	}
}

void SkySync::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.MoonLightSource = std::clamp(settings.MoonLightSource, static_cast<int32_t>(MoonLightSource::Brightest), static_cast<int32_t>(MoonLightSource::Secunda));
	settings.SunPath = std::clamp(settings.SunPath, static_cast<int32_t>(SunPath::Southern), static_cast<int32_t>(SunPath::Custom));
	settings.CustomAngle = std::clamp(settings.CustomAngle, -90.0f, 90.0f);
	settings.MinShadowElevation = std::clamp(settings.MinShadowElevation, 0.0f, 45.0f);
	settings.HorizonFadeHours = std::clamp(settings.HorizonFadeHours, 0.0f, MaxHorizonFadeHours);
	SetSunAngle();
}

void SkySync::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SkySync::RestoreDefaultSettings()
{
	settings = {};
	SetSunAngle();
}

void SkySync::PostPostLoad()
{
	moonAndStarsLoaded = GetModuleHandle(L"po3_MoonMod.dll");
	if (moonAndStarsLoaded)
		logger::info("[Sky Sync] Moon and Stars detected, compatibility enabled");

	if (GetModuleHandle(L"EVLaS.dll")) {
		DisableOnConflict("EVLaS");
		return;
	}

	stl::detour_thunk<Sky_Update>(REL::RelocationID(25682, 26229));

	gSunPosition = reinterpret_cast<RE::NiPoint3*>(REL::RelocationID(527924, 414871).address());

	gVolumetricLighting = reinterpret_cast<RE::BSVolumetricLightingRenderData*>(
		REL::RelocationID(527719, 414629).address() - offsetof(RE::BSVolumetricLightingRenderData, red));

	logger::info("[Sky Sync] Installed hooks");
}

void SkySync::DataLoaded()
{
	const auto data = RE::TESDataHandler::GetSingleton();
	if (data && (data->LookupLoadedModByName("DVLaSS.esp"sv) || data->LookupLoadedLightModByName("DVLaSS.esp"sv)))
		DisableOnConflict("DVLaSS");
}

void SkySync::DisableOnConflict(std::string_view conflictName)
{
	failedLoadedMessage = fmt::format("Disabled as {} has been detected, both cannot be used together", conflictName);
	loaded = false;
	settings.Enabled = false;
	logger::warn("[Sky Sync] {}", failedLoadedMessage);
}

void SkySync::OnSkyUpdateColors(RE::Sky* sky)
{
	if (!settings.Enabled || !sky)
		return;

	if (settings.DimSunlightUnderHorizon && currentDim > 0.0f && currentDim < 1.0f) {
		auto& dirLight = sky->skyColor[static_cast<uint>(RE::TESWeather::ColorTypes::kSunlight)];
		dirLight.red *= currentDim;
		dirLight.green *= currentDim;
		dirLight.blue *= currentDim;
	}

	if (gVolumetricLighting) {
		float vlFactor = shadowFader.vlIntensityFactor;
		if (settings.DimVolumetricLighting)
			vlFactor *= currentDim;
		gVolumetricLighting->intensity *= vlFactor;
	}
}

void SkySync::Sky_Update::thunk(RE::Sky* sky)
{
	func(sky);
	globals::features::skySync.Update(sky);
}

void SkySync::Update(const RE::Sky* sky)
{
	if (!settings.Enabled) {
		currentDim = 1.0f;
		return;
	}

	const auto sun = sky->sun;
	const auto climate = sky->currentClimate;
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!sun || !climate || !player) {
		currentDim = 1.0f;
		return;
	}

	const auto cell = player->GetParentCell();

	if (cell != currentCell) {
		const auto prevCell = currentCell;
		if (cell)
			SetSkyRotation(sky, cell);
		if (cell && prevCell && (cell->IsInteriorCell() != prevCell->IsInteriorCell() || cell->GetRuntimeData().worldSpace != prevCell->GetRuntimeData().worldSpace))
			shadowFader.Reset();
			lastGameHour = -1.0f;
	}

	// Exterior worldspaces always run; interior cells require the sunlight-shadows flag.
	if (cell && cell->IsInteriorCell() && !cell->cellFlags.all(static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows))) {
		currentDim = 1.0f;
		return;
	}

	// Compute dim once per frame — used by OnSkyUpdateColors (if option on) and ShadowFader (always)
	if (sky->currentClimate) {
		const auto& timing = sky->currentClimate->timing;
		const float hour = sky->currentGameHour;
		const float sunriseBegin = timing.sunrise.begin / 6.0f;
		const float sunriseMiddle = (timing.sunrise.begin + timing.sunrise.end) / 12.0f;
		const float sunsetMiddle = (timing.sunset.begin + timing.sunset.end) / 12.0f;
		const float sunsetEnd = timing.sunset.end / 6.0f;
		const float fadeHours = settings.HorizonFadeHours;

		// Hours elapsed from a to b, wrapping across midnight so gap windows survive past 24h.
		auto hoursBetween = [](float from, float to) {
			float d = to - from;
			return d < 0.0f ? d + 24.0f : d;
		};

		sunSetting = hour >= sunsetMiddle && hour < sunsetEnd;
		sunRising = hour >= sunriseBegin && hour < sunriseMiddle;
		sunBelowHorizon = hour >= sunsetEnd || hour < sunriseBegin;

		if (hour >= sunsetMiddle && hour < sunsetEnd) {
			// Dusk: sun dipping under the horizon, fade the directional light out.
			float range = sunsetEnd - sunsetMiddle;
			float t = range > 0.0f ? (hour - sunsetMiddle) / range : 1.0f;
			currentDim = std::sqrt(1.0f - t);
		} else if (fadeHours > 0.0f && hoursBetween(sunsetEnd, hour) < fadeHours) {
			// Caster has swapped to the moon but the colour is still dusk-bright; ease the dim back out.
			currentDim = hoursBetween(sunsetEnd, hour) / fadeHours;
		} else if (fadeHours > 0.0f && hoursBetween(hour, sunriseBegin) > 0.0f && hoursBetween(hour, sunriseBegin) <= fadeHours) {
			// Still on the moon but the colour is brightening toward dawn; ease the dim back in.
			currentDim = hoursBetween(hour, sunriseBegin) / fadeHours;
		} else if (hour >= sunriseBegin && hour < sunriseMiddle) {
			// Dawn: sun rising above the horizon, fade the directional light in.
			float range = sunriseMiddle - sunriseBegin;
			float t = range > 0.0f ? (hour - sunriseBegin) / range : 1.0f;
			currentDim = std::sqrt(t);
		} else {
			currentDim = 1.0f;
		}
	} else {
		currentDim = 1.0f;
		sunSetting = false;
		sunRising = false;
		sunBelowHorizon = false;
	}

	RE::NiPoint3 directions[3] = {};
	float intensities[3] = {};

	ProcessSun(sky, directions, intensities);
	ProcessMoon(sky, Caster::Masser, directions, intensities);
	ProcessMoon(sky, Caster::Secunda, directions, intensities);

	// Advance the shadow fade by elapsed game time so the transition stays smooth during
	// normal play but snaps when time jumps (scrubbing, waiting, fast travel).
	const float gameHour = sky->currentGameHour;
	float fadeAdvance = settings.ShadowTransitionDuration;  // first frame: snap
	if (lastGameHour >= 0.0f) {
		float hourDelta = gameHour - lastGameHour;
		if (hourDelta > 12.0f)
			hourDelta -= 24.0f;
		else if (hourDelta < -12.0f)
			hourDelta += 24.0f;
		fadeAdvance = std::abs(hourDelta) * SecondsPerGameHour;
	}
	lastGameHour = gameHour;

	shadowFader.Update(sky, directions, intensities, settings.ShadowTransitionDuration, fadeAdvance);
}
void SkySync::SetSunAngle()
{
	switch (static_cast<SunPath>(settings.SunPath)) {
	case SunPath::Southern:
		sunAngle = SouthernSunAngle;
		break;
	case SunPath::Northern:
		sunAngle = NorthernSunAngle;
		break;
	case SunPath::Vanilla:
		sunAngle = VanillaSunAngle;
		break;
	case SunPath::Custom:
		sunAngle = 90.0f + settings.CustomAngle;
		break;
	default:;
	}
}

void SkySync::SetSkyRotation(const RE::Sky* sky, RE::TESObjectCELL* cell)
{
	// If the interior cell isn't initialised it won't have the north rotation extra data ready, skip for a frame
	if (cell->IsInteriorCell() && cell->cellState == static_cast<RE::TESObjectCELL::CellState>(0))
		return;

	currentCell = cell;
	const float rotation = cell->GetNorthRotation();
	if (rotation == currentSkyRotation)
		return;

	currentSkyRotation = rotation;
	sky->root->local.rotate = RE::NiMatrix3{ RE::NiPoint3{ 0.0f, 0.0f, -rotation } };
	RE::NiUpdateData updateData;
	sky->root->Update(updateData);
}

void SkySync::ProcessSun(const RE::Sky* sky, RE::NiPoint3 dirs[], float intensities[])
{
	const auto sun = sky->sun;
	RE::NiPoint3 dir;
	float dist;

	if (settings.UseAlternateSunPath) {
		const auto climate = sky->currentClimate;
		const float sunrise = (climate->timing.sunrise.begin / 6.0f + climate->timing.sunrise.end / 6.0f) * 0.5f - 0.25f;
		const float sunset = (climate->timing.sunset.begin / 6.0f + climate->timing.sunset.end / 6.0f) * 0.5f + 0.25f;
		CalculateAlternateSunDirectionAndDistance(dir, dist, sky->currentGameHour, sunrise, sunset, sunAngle);
	} else
		CalculateSunDirectionAndDistance(sun, dir, dist);

	SetSunPosition(sun, dir, dist);

	dirs[static_cast<int>(Caster::Sun)] = dir;

	if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(sun->sunBase->GetGeometryRuntimeData().shaderProperty.get()))
		intensities[static_cast<int>(Caster::Sun)] = prop->kBlendColor.alpha;
}

void SkySync::ProcessMoon(const RE::Sky* sky, const Caster type, RE::NiPoint3 dirs[], float intensities[])
{
	const int idx = static_cast<int>(type);
	colors[idx] = {};

	const auto moon = type == Caster::Masser ? sky->masser : sky->secunda;
	if (!moon || moon->root->GetFlags().any(RE::NiAVObject::Flag::kHidden))
		return;

	auto dir = moon->root->local.rotate.GetVectorY();

	if (moonAndStarsLoaded)
		dir = { dir.y, -dir.x, dir.z };

	dirs[idx] = dir;

	const float4& baseColor = type == Caster::Masser ? Util::Moon::MasserBaseColor : Util::Moon::SecundaBaseColor;
	float4 color = Util::Moon::GetBlendColor(moon, baseColor, settings.NewMoonIntensity, settings.CrescentMoonIntensity, settings.FullMoonIntensity);
	colors[idx] = color;

	const auto src = static_cast<MoonLightSource>(settings.MoonLightSource);
	const bool isValidSource = src == MoonLightSource::Brightest || (src == MoonLightSource::Masser && type == Caster::Masser) || (src == MoonLightSource::Secunda && type == Caster::Secunda);
	if (!isValidSource)
		return;

	intensities[idx] = color.w;
}

inline void SkySync::CalculateSunDirectionAndDistance(const RE::Sun* sun, RE::NiPoint3& outDir, float& outDistance)
{
	outDir = sun->root->local.translate;
	if (outDistance = outDir.Unitize(); outDistance < FLT_EPSILON) {
		outDir = { 0.0f, 0.0f, 1.0f };
		outDistance = SunPeakDistance;
	}
}

inline void SkySync::CalculateAlternateSunDirectionAndDistance(RE::NiPoint3& outDir, float& outDist, const float time, const float sunrise, const float sunset, const float sunAngle)
{
	const float phi = DirectX::XM_PI * ((time - sunrise) / (sunset - sunrise));
	float sinPhi, cosPhi;
	DirectX::XMScalarSinCosEst(&sinPhi, &cosPhi, phi);

	float tiltRadians = DirectX::XMConvertToRadians(sunAngle);
	float cosTilt, sinTilt;
	DirectX::XMScalarSinCosEst(&sinTilt, &cosTilt, tiltRadians);

	outDir = { cosPhi, -sinPhi * cosTilt, sinPhi * sinTilt };

	if (const float length = outDir.Unitize(); length < FLT_EPSILON)
		outDir = { 0.0f, 0.0f, 1.0f };

	const float elevationRatio = std::max(sinPhi, 0.0f);
	outDist = std::lerp(SunHorizonDistance, SunPeakDistance, elevationRatio);
}

inline void SkySync::SetSunPosition(const RE::Sun* sun, const RE::NiPoint3& dir, const float distance)
{
	const auto position = dir * distance;
	sun->root->local.translate = position;
	sun->sunGlareNode->local.translate = position;
	*gSunPosition = position;
}

void SkySync::ShadowFader::Reset()
{
	target = Caster::Sun;
	previousTarget = Caster::Sun;
	fadeTimer = 0.0f;
	transitioning = false;
	sunriseReleased = false;
	frozenHeading = 0.0f;
	sunsetHeadingLocked = false;
}

void SkySync::ShadowFader::Update(const RE::Sky* sky, RE::NiPoint3 dirs[], float intensities[], float fadeDuration, float fadeAdvance)
{
	auto isValidDir = [](const RE::NiPoint3& d) { return d.x != 0.0f || d.y != 0.0f || d.z != 0.0f; };

	Caster best;

	if (globals::features::skySync.sunBelowHorizon) {
		bool masserValid = isValidDir(dirs[static_cast<int>(Caster::Masser)]);
		bool secundaValid = isValidDir(dirs[static_cast<int>(Caster::Secunda)]);

		if (!masserValid && !secundaValid)
			best = Caster::None;
		else if (!masserValid)
			best = Caster::Secunda;
		else if (!secundaValid || intensities[static_cast<int>(Caster::Secunda)] <= intensities[static_cast<int>(Caster::Masser)])
			best = Caster::Masser;
		else
			best = Caster::Secunda;
	} else {
		best = Caster::Sun;
	}

	LockSunElevation(dirs);

	// No valid caster points straight up so shadows fall directly down.
	auto casterDir = [&](Caster c) {
		return c == Caster::None ? RE::NiPoint3{ 0.0f, 0.0f, 1.0f } : dirs[static_cast<int>(c)];
	};

	// If best source changed, begin a new transition
	if (best != target) {
		previousTarget = target;
		target = best;
		startDir = currentDir;
		fadeTimer = 0.0f;
		transitioning = true;
	}

	const RE::NiPoint3 targetDir = casterDir(target);

	if (!transitioning) {
		currentDir = targetDir;
		vlIntensityFactor = target == Caster::None ? 0.0f : 1.0f;
		SetLighting(sky, currentDir);
		return;
	}

	fadeTimer = std::min(fadeTimer + fadeAdvance, fadeDuration);
	const float t = fadeDuration > 0.0f ? fadeTimer / fadeDuration : 1.0f;

	currentDir = {
		std::lerp(startDir.x, targetDir.x, t),
		std::lerp(startDir.y, targetDir.y, t),
		std::lerp(startDir.z, targetDir.z, t)
	};
	currentDir.Unitize();

	if (t >= 1.0f) {
		currentDir = targetDir;
		transitioning = false;
	}

	// Fade VL out as it settles into the no-caster fallback, otherwise fade with shadow alignment.
	vlIntensityFactor = target == Caster::None ? 1.0f - t : ComputeVLFactor(currentDir, targetDir);
	SetLighting(sky, currentDir);
}

void SkySync::ShadowFader::LockSunElevation(RE::NiPoint3 dirs[])
{
	// Dusk: lock elevation to the minimum so the shadow can't tilt back up as the sun goes under,
	// and once dimming passes the threshold lock heading too so it stops sweeping while the VL fades.
	// Dawn: lock at the minimum until the sun naturally rises above it, then follow it.
	const auto& skySync = globals::features::skySync;
	const int sunIdx = static_cast<int>(Caster::Sun);
	const float minElev = DirectX::XMConvertToRadians(skySync.settings.MinShadowElevation);
	if (skySync.sunSetting) {
		if (skySync.currentDim <= SunsetHeadingLockThreshold) {
			if (!sunsetHeadingLocked) {
				frozenHeading = std::atan2(dirs[sunIdx].y, dirs[sunIdx].x);
				sunsetHeadingLocked = true;
			}
			SetDirection(dirs[sunIdx], frozenHeading, minElev);
		} else {
			SetElevation(dirs[sunIdx], minElev);
		}
	} else if (skySync.sunRising) {
		if (!sunriseReleased) {
			if (DirectX::XMScalarASinEst(dirs[sunIdx].z) >= minElev)
				sunriseReleased = true;
			else
				SetElevation(dirs[sunIdx], minElev);
		}
	} else {
		sunriseReleased = false;
		sunsetHeadingLocked = false;
	}
}

void SkySync::ShadowFader::SetLighting(const RE::Sky* sky, RE::NiPoint3 dir)
{
	ClampDirection(dir);

	RE::NiMatrix3& m = sky->sun->light->local.rotate;
	m.entry[0][0] = -dir.x;
	m.entry[1][0] = -dir.y;
	m.entry[2][0] = -dir.z;

	RE::NiUpdateData updateData;
	sky->sun->light->Update(updateData);
}

inline void SkySync::ShadowFader::SetDirection(RE::NiPoint3& dir, float headingRadians, float elevRadians)
{
	float sinElev, cosElev, sinHeading, cosHeading;
	DirectX::XMScalarSinCosEst(&sinElev, &cosElev, elevRadians);
	DirectX::XMScalarSinCosEst(&sinHeading, &cosHeading, headingRadians);

	dir.x = cosElev * cosHeading;
	dir.y = cosElev * sinHeading;
	dir.z = sinElev;
}

inline void SkySync::ShadowFader::SetElevation(RE::NiPoint3& dir, float elevRadians)
{
	SetDirection(dir, std::atan2(dir.y, dir.x), elevRadians);
}

float SkySync::ShadowFader::ComputeVLFactor(const RE::NiPoint3& current, const RE::NiPoint3& target)
{
	const float dot = std::clamp(current.Dot(target), -1.0f, 1.0f);
	const float angle = DirectX::XMConvertToDegrees(DirectX::XMScalarACosEst(dot));

	return std::clamp((VLFadeEndAngle - angle) / (VLFadeEndAngle - VLFadeStartAngle), 0.0f, 1.0f);
}

inline void SkySync::ShadowFader::ClampDirection(RE::NiPoint3& dir)
{
	const float minDegrees = globals::features::skySync.settings.MinShadowElevation;
	const float minElev = DirectX::XMConvertToRadians(minDegrees);
	const float elev = DirectX::XMScalarASinEst(dir.z);
	if (elev >= minElev)
		return;

	SetElevation(dir, minElev);
}



#undef I18N_KEY_PREFIX
