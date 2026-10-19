#include "SkySync.h"
#include "../I18n/I18n.h"
#include "RE/B/BSVolumetricLightingRenderData.h"

#include "LinearLighting.h"
#include "Utils/Climate.h"
#include "Utils/Game.h"

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
	HorizonFadeElevation,
	HorizonElevation,
	NewMoonIntensity,
	CrescentMoonIntensity,
	FullMoonIntensity,
	DisabledWorldspaces)

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
		ImGui::Text("%s", T(TKEY("shadow_transition_duration_tooltip"), "How long (in game-time units) the shadow direction takes to fade between sources. 300 = ~15 seconds at timescale 20."));
	}

	ImGui::SliderFloat(T(TKEY("horizon_elevation"), "Effective Horizon"), &settings.HorizonElevation, MinHorizonElevation, 0.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T(TKEY("horizon_elevation_tooltip"), "Elevation treated as the horizon, where light fades to zero and the sun and moons hand over. Lower it to let the light fade out slightly below the visible horizon."));
	}

	ImGui::Checkbox(T(TKEY("dim_sunlight_under_horizon"), "Dim Sunlight Under Horizon"), &settings.DimSunlightUnderHorizon);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T(TKEY("dim_sunlight_under_horizon_tooltip"), "Fade directional light to zero as the sun or moon approaches the horizon."));
	}

	ImGui::Checkbox(T(TKEY("fade_volumetric_lighting"), "Fade Volumetric Lighting"), &settings.DimVolumetricLighting);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T(TKEY("fade_volumetric_lighting_tooltip"), "Also fade volumetric lighting with the directional dim around dawn and dusk."));
	}

	if (settings.DimSunlightUnderHorizon || settings.DimVolumetricLighting) {
		ImGui::SliderFloat(T(TKEY("horizon_fade_duration"), "Moonlight Fade Duration"), &settings.HorizonFadeHours, 0.0f, MaxHorizonFadeHours, "%.1f h", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("horizon_fade_duration_tooltip"), "How long (in game hours) moonlight takes to fade in once a moon casts shadows."));
		}

		ImGui::SliderFloat(T(TKEY("horizon_fade_elevation"), "Horizon Fade Elevation"), &settings.HorizonFadeElevation, 0.0f, MaxHorizonFadeElevation, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("horizon_fade_elevation_tooltip"), "Height above the horizon at which the sun or moon light starts fading out. Higher = longer fade."));
		}
	}

	ImGui::SliderFloat(T(TKEY("new_moon_intensity"), "New Moon Intensity"), &settings.NewMoonIntensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("crescent_intensity"), "Crescent Intensity"), &settings.CrescentMoonIntensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("full_moon_intensity"), "Full Moon Intensity"), &settings.FullMoonIntensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

	if (ImGui::TreeNodeEx(T(TKEY("disabled_worldspaces"), "Disabled Worldspaces"), ImGuiTreeNodeFlags_None)) {
		Util::TextUnformattedDisabled(T(TKEY("disabled_worldspaces_hint"), "Sky Sync leaves lighting to vanilla in these worldspaces."));

		auto& worldspaces = settings.DisabledWorldspaces;
		std::optional<size_t> removed;
		if (ImGui::BeginTable("##DisabledWorldspaces", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableSetupColumn(T(TKEY("worldspace_editor_id"), "Worldspace Editor ID"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			for (size_t i = 0; i < worldspaces.size(); ++i) {
				// By index, since a hand-edited list may hold duplicates
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(worldspaces[i].c_str());
				ImGui::TableNextColumn();
				if (ImGui::SmallButton("X"))
					removed = i;
				Util::AddTooltip(T(TKEY("remove_worldspace_tooltip"), "Remove from the list"));
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (removed) {
			worldspaces.erase(worldspaces.begin() + *removed);
			UpdateWorldspaceDisabled();
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto worldspace = player ? player->GetWorldspace() : nullptr;
		const std::string editorID = worldspace && worldspace->GetFormEditorID() ? worldspace->GetFormEditorID() : "";
		{
			auto addGuard = Util::DisableGuard(editorID.empty() || IsWorldspaceDisabled(worldspace));
			const auto addLabel = I18n::GetSingleton()->Format(TKEY("add_current_worldspace"), { { "name", editorID.empty() ? std::string("-") : editorID } }, "Add Current Worldspace ({name})");
			if (ImGui::Button(addLabel.c_str())) {
				worldspaces.push_back(editorID);
				UpdateWorldspaceDisabled();
			}
		}

		ImGui::TreePop();
	}

	if (Util::ShowDebugSections() && ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_None)) {
		static constexpr const char* CasterNames[] = { "Sun", "Masser", "Secunda", "Zenith" };
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
		ImGui::Text("Transition intensity factor: %.3f", shadowFader.intensityFactor);
		ImGui::Text("VL fade: %.3f", shadowFader.vlFade);
		if (shadowFader.transitioning) {
			const float t = shadowFader.Progress(settings.ShadowTransitionDuration);
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
	settings.HorizonFadeElevation = std::clamp(settings.HorizonFadeElevation, 0.0f, MaxHorizonFadeElevation);
	settings.HorizonElevation = std::clamp(settings.HorizonElevation, MinHorizonElevation, 0.0f);
	SetSunAngle();
	UpdateWorldspaceDisabled();
}

void SkySync::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SkySync::RestoreDefaultSettings()
{
	settings = {};
	SetSunAngle();
	UpdateWorldspaceDisabled();
}

void SkySync::PostPostLoad()
{
	if (Util::Moon::IsMoonAndStarsLoaded())
		logger::info("[Sky Sync] Moon and Stars detected, compatibility enabled");

	if (GetModuleHandle(L"EVLaS.dll")) {
		DisableOnConflict("EVLaS");
		return;
	}

	stl::detour_thunk<Sky_Update>(REL::RelocationID(25682, 26229));
	Util::SetCelestialTransitionHandlerAvailable(true);

	gSunPosition = reinterpret_cast<RE::NiPoint3*>(REL::RelocationID(527924, 414871).address());

	gVolumetricLighting = reinterpret_cast<RE::BSVolumetricLightingRenderData*>(
		REL::RelocationID(527719, 414629).address() - offsetof(RE::BSVolumetricLightingRenderData, color));

	logger::info("[Sky Sync] Installed hooks");
}

void SkySync::DataLoaded()
{
	const auto data = RE::TESDataHandler::GetSingleton();
	if (data && (data->LookupLoadedModByName("DVLaSS.esp"sv) || data->LookupLoadedLightModByName("DVLaSS.esp"sv)))
		DisableOnConflict("DVLaSS");

	if (const auto collection = globals::game::gameSettingCollection) {
		gSunAlphaTransTime = collection->GetSetting("fSunAlphaTransTime");
		gWeatherFlashDirectional = collection->GetSetting("fWeatherFlashDirectional");
	}
}

void SkySync::GameLoaded()
{
	Util::RequestGameLoadTransition();
}

void SkySync::DisableOnConflict(std::string_view conflictName)
{
	failedLoadedMessage = fmt::format("Disabled as {} has been detected, both cannot be used together", conflictName);
	loaded = false;
	settings.Enabled = false;
	Util::SetCelestialTransitionHandlerAvailable(false);
	logger::warn("[Sky Sync] {}", failedLoadedMessage);
}

void SkySync::OnSkyUpdateColors(RE::Sky* sky)
{
	if (!settings.Enabled || !active || !sky) {
		flashWeight = 0.0f;
		return;
	}

	auto lightFactor = [&](bool dimmed) { return dimmed ? shadowFader.intensityFactor * currentDim : shadowFader.intensityFactor; };

	auto& dirLight = sky->skyColor[static_cast<uint>(RE::TESWeather::ColorTypes::kSunlight)];
	// Lightning strikes from overhead rather than the caster, so only the light under the flash is dimmed
	const float flash = GetDirectionalFlash(sky);
	const RE::NiColor flashLight{ std::min(flash, dirLight.red), std::min(flash, dirLight.green), std::min(flash, dirLight.blue) };
	dirLight = (dirLight - flashLight) * lightFactor(settings.DimSunlightUnderHorizon) + flashLight;
	const float totalLight = dirLight.red + dirLight.green + dirLight.blue;
	flashWeight = totalLight > 0.0f ? (flashLight.red + flashLight.green + flashLight.blue) / totalLight : 0.0f;

	if (gVolumetricLighting)
		gVolumetricLighting->intensity *= lightFactor(settings.DimVolumetricLighting) * shadowFader.vlFade;
}

void SkySync::ApplyMoonlightColors(RE::Sky* sky)
{
	moonNightWeight = 0.0f;
	lightNightWeight = 0.0f;
	// Only where vanilla lights from weather colours; lighting templates and rooms are left alone
	if (!settings.Enabled || !active || !sky || !sunBelowHorizon || !sky->currentWeather || sky->mode.get() != RE::Sky::Mode::kFull ||
		sky->extLightingOverride || sky->currentRoom || sky->previousRoom)
		return;

	// Vanilla blends sunset into night on the climate's colour timings, which outlast the sun, so drop the time blend
	const float currentWeight = sky->currentWeatherPct;
	const float lastWeight = sky->lastWeather ? 1.0f - currentWeight : 0.0f;

	auto nightBlend = [&](RE::TESWeather::ColorType type) {
		auto nightColor = [type](const RE::TESWeather* weather) {
			return weather ? weather->colorData[type][RE::TESWeather::ColorTime::kNight] : RE::Color{};
		};
		return RE::Sky::COLOR_BLEND{ { nightColor(sky->currentWeather), {}, nightColor(sky->lastWeather), {} }, { currentWeight, 0.0f, lastWeight, 0.0f } };
	};

	// Full night at once, since the light is dimmed out at the handover; undimmed, vanilla's blend avoids a pop
	if (settings.DimSunlightUnderHorizon) {
		auto sunlightBlend = nightBlend(RE::TESWeather::ColorTypes::kSunlight);
		sky->SetColor(sky->skyColor[RE::TESWeather::ColorTypes::kSunlight], &sunlightBlend, GetDirectionalFlash(sky));
		lightNightWeight = 1.0f;
	}

	// Moon::Update tints the discs with this; ease it in with the moonlight, as the moons stay visible through the handover
	auto moonGlareBlend = nightBlend(RE::TESWeather::ColorTypes::kMoonGlare);
	RE::NiColor nightMoonGlare;
	sky->SetColor(nightMoonGlare, &moonGlareBlend, 0.0f);
	auto& moonGlare = sky->skyColor[RE::TESWeather::ColorTypes::kMoonGlare];
	moonNightWeight = twilightCap;
	moonGlare += (nightMoonGlare - moonGlare) * moonNightWeight;

	if (gVolumetricLighting && settings.DimVolumetricLighting)
		*gVolumetricLighting = BlendNightVolumetricLighting(sky, currentWeight, lastWeight);
}

float SkySync::GetDirectionalFlash(const RE::Sky* sky)
{
	// Sky::SetColor caps the added flash at 1
	return !sky->lastWeather && gWeatherFlashDirectional ? std::min(gWeatherFlashDirectional->GetFloat() * sky->flash, 1.0f) : 0.0f;
}

RE::BSVolumetricLightingRenderData SkySync::GetDefaultVolumetricLighting()
{
	// Resolve once but read live, as the console can change them
	static const auto intensity = RE::GetINISetting("fVolumetricLightingIntensity:Display");
	static const auto customColorContribution = RE::GetINISetting("fVolumetricLightingCustomColorContribution:Display");
	static const auto densityContribution = RE::GetINISetting("fVolumetricLightingDensityContribution:Display");
	static const auto densityScale = RE::GetINISetting("fVolumetricLightingDensityScale:Display");
	static const auto windSpeedScale = RE::GetINISetting("fVolumetricLightingWindSpeedScale:Display");
	static const auto windFallingSpeed = RE::GetINISetting("fVolumetricLightingWindFallingSpeed:Display");
	static const auto phaseContribution = RE::GetINISetting("fVolumetricLightingPhaseContribution:Display");
	static const auto phaseScattering = RE::GetINISetting("fVolumetricLightingPhaseScattering:Display");
	static const auto rangeFactor = RE::GetINISetting("fVolumetricLightingRangeFactor:Display");

	auto value = [](const RE::Setting* setting) { return setting ? setting->GetFloat() : 0.0f; };
	return {
		value(intensity),
		{ value(customColorContribution) },
		{ 1.0f, 1.0f, 1.0f },
		{ value(densityContribution), value(densityScale), value(windSpeedScale), value(windFallingSpeed) },
		{ value(phaseContribution), value(phaseScattering) },
		{ value(rangeFactor) }
	};
}

RE::BSVolumetricLightingRenderData SkySync::BlendNightVolumetricLighting(const RE::Sky* sky, float currentWeight, float lastWeight)
{
	auto nightVolumetricLighting = [](const RE::TESWeather* weather) {
		const auto form = weather ? weather->volumetricLighting[RE::TESWeather::ColorTime::kNight] : nullptr;
		return form ? static_cast<const RE::BSVolumetricLightingRenderData&>(*form) : GetDefaultVolumetricLighting();
	};

	// Vanilla accumulates the render data as a flat array of floats
	constexpr size_t FloatCount = sizeof(RE::BSVolumetricLightingRenderData) / sizeof(float);
	static_assert(FloatCount * sizeof(float) == sizeof(RE::BSVolumetricLightingRenderData));
	RE::BSVolumetricLightingRenderData result{};
	auto accumulate = [&](const RE::BSVolumetricLightingRenderData& data, float weight) {
		const auto source = reinterpret_cast<const float*>(&data);
		const auto target = reinterpret_cast<float*>(&result);
		for (size_t i = 0; i < FloatCount; ++i)
			target[i] += source[i] * weight;
	};

	accumulate(nightVolumetricLighting(sky->currentWeather), currentWeight);
	if (lastWeight > 0.0f)
		accumulate(nightVolumetricLighting(sky->lastWeather), lastWeight);
	return result;
}

void SkySync::Sky_Update::thunk(RE::Sky* sky, float deltaTime)
{
	func(sky, deltaTime);
	auto& skySync = globals::features::skySync;
	if (sky && sky->sun)
		globals::features::linearLighting.ClearSunlightColor(sky->sun->light.get());
	skySync.PreparePendingTransitions();
	if (skySync.Update(sky))
		Util::CompleteCelestialTransition();
}

void SkySync::PreparePendingTransitions()
{
	const auto request = Util::ConsumeCelestialTransitionRequest();
	if (!request.timeJump && !request.gameLoad)
		return;

	if (request.gameLoad) {
		shadowFader.Reset();
		currentCell = nullptr;
		currentCellInterior = false;
		currentCellWorldspace = nullptr;
		currentSkyRotation = D3D11_FLOAT32_MAX;
	}

	immediateTransitionReady = true;
}

bool SkySync::Update(const RE::Sky* sky)
{
	std::fill(std::begin(rawDirections), std::end(rawDirections), RE::NiPoint3{});
	active = false;
	currentDim = 1.0f;

	if (!settings.Enabled)
		return std::exchange(immediateTransitionReady, false);

	if (!sky)
		return false;

	const auto sun = sky->sun;
	const auto climate = sky->currentClimate;
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!sun || !climate || !player)
		return false;

	const auto cell = player->GetParentCell();
	bool resetTransition = false;

	if (cell != currentCell) {
		resetTransition = true;
		const auto prevCell = currentCell;
		const bool resetFaderForCellChange = cell && prevCell &&
		                                     (cell->IsInteriorCell() != currentCellInterior ||
												 cell->GetRuntimeData().worldSpace != currentCellWorldspace);
		if (cell)
			SetSkyRotation(sky, cell);
		else
			currentCell = nullptr;  // keep the cache in sync so a cell-less frame doesn't reset every frame
		if (resetFaderForCellChange)
			shadowFader.Reset();
		UpdateWorldspaceDisabled();
	}

	// Exterior worldspaces run unless listed as disabled; interior cells require the sunlight-shadows flag.
	if (worldspaceDisabled || (cell && cell->IsInteriorCell() && !cell->cellFlags.all(static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows)))) {
		// Vanilla drives the sun in a disabled worldspace, so a pending transition has nothing to wait for
		return worldspaceDisabled && std::exchange(immediateTransitionReady, false);
	}
	active = true;

	const float hour = sky->currentGameHour;

	RE::NiPoint3 directions[BodyCount] = {};
	float intensities[BodyCount] = {};

	const RE::NiPoint3 sunDir = GetSunDirectionAtHour(climate->timing, hour);
	const float sunHeight = GetHeightAboveHorizon(sunDir);

	ProcessSun(sky, sunDir, sunHeight, directions, intensities);
	ProcessMoon(sky, Caster::Masser, directions, intensities);
	ProcessMoon(sky, Caster::Secunda, directions, intensities);
	std::copy(std::begin(directions), std::end(directions), std::begin(rawDirections));

	sunBelowHorizon = sunHeight < 0.0f;
	// A sun below the horizon never casts, so skip the search
	if (!sunBelowHorizon)
		HoldLowSunDirection(climate->timing, hour, sunDir, directions[static_cast<int>(Caster::Sun)]);

	const auto calendar = globals::game::calendar;
	const auto deltaTime = globals::game::deltaTime;
	float fadeAdvance = calendar && deltaTime ? std::max(*deltaTime * calendar->GetTimescale(), 0.0f) : 0.0f;

	// The clock can outrun real time (waiting, fast travel) or move while frames are paused
	// (console, scrubbing), so advance by whichever of the two elapsed more.
	if (lastGameHour >= 0.0f) {
		const float hourDelta = std::remainder(hour - lastGameHour, HoursPerDay);  // shortest way round the clock
		fadeAdvance = std::max(fadeAdvance, std::abs(hourDelta) * SecondsPerGameHour);
	}
	lastGameHour = hour;

	const bool transitionCompleted = std::exchange(immediateTransitionReady, false);
	shadowFader.Update(sky, directions, intensities, workingLightColors, settings.ShadowTransitionDuration, fadeAdvance, transitionCompleted || resetTransition);

	twilightCap = GetTwilightCap(climate->timing, hour, sunHeight);
	UpdateMoonlightFade(fadeAdvance / SecondsPerGameHour, transitionCompleted);
	// At night follow the light's own direction, so the dim stays continuous while it swings between moons
	currentDim = sunBelowHorizon ? moonlightFade * GetHorizonDim(GetHeightAboveHorizon(shadowFader.currentDir)) : GetHorizonDim(sunHeight);

	return transitionCompleted;
}

float SkySync::GetHeightAboveHorizon(const RE::NiPoint3& dir) const
{
	return DirectX::XMScalarASinEst(dir.z) - DirectX::XMConvertToRadians(settings.HorizonElevation);
}

float SkySync::GetHorizonDim(float heightAboveHorizon) const
{
	// Elevation-based so every path fades over the same arc, however long it lingers near the horizon
	const float fadeRadians = std::max(DirectX::XMConvertToRadians(settings.HorizonFadeElevation), FLT_EPSILON);
	return std::clamp(heightAboveHorizon / fadeRadians, 0.0f, 1.0f);
}

void SkySync::UpdateMoonlightFade(float advanceHours, bool immediate)
{
	const float target = sunBelowHorizon ? 1.0f : 0.0f;
	const float step = GetMoonlightFadeProgress(advanceHours);
	moonlightFade = std::min(immediate ? target : std::clamp(target, moonlightFade - step, moonlightFade + step), twilightCap);
}

float SkySync::GetTwilightCap(const RE::TESClimate::Timing& timing, float hour, float sunHeight) const
{
	if (sunHeight >= 0.0f)
		return 0.0f;

	// Derived from the clock rather than accumulated, so scrubbing either way gives the same light
	const float sunriseHorizon = GetSunHorizonHours(timing).first;
	const float hoursToSunrise = hour < sunriseHorizon ? sunriseHorizon - hour : sunriseHorizon + HoursPerDay - hour;
	float hoursSinceHandover = hour - GetSunCrossingHour(timing, DirectX::XMConvertToRadians(settings.HorizonElevation), false);
	if (hoursSinceHandover < 0.0f)
		hoursSinceHandover += HoursPerDay;
	if (hoursToSunrise < hoursSinceHandover)
		return GetHorizonDim(-sunHeight);
	return std::min(GetMoonlightFadeProgress(hoursSinceHandover), 1.0f);
}

float SkySync::GetSunCrossingHour(const RE::TESClimate::Timing& timing, float elevation, bool rising) const
{
	// Both paths climb steadily from the middle of the night to midday and sink back after, so bisect within one half
	const auto [sunrise, sunset] = GetSunHorizonHours(timing);
	const float halfNight = (HoursPerDay - (sunset - sunrise)) * 0.5f;
	float above = (sunrise + sunset) * 0.5f;
	float below = rising ? sunrise - halfNight : sunset + halfNight;
	for (int i = 0; i < SunCrossingSearchIterations; ++i) {
		const float middle = (above + below) * 0.5f;
		(DirectX::XMScalarASinEst(GetSunDirectionAtHour(timing, middle).z) >= elevation ? above : below) = middle;
	}
	return below;
}

void SkySync::HoldLowSunDirection(const RE::TESClimate::Timing& timing, float hour, const RE::NiPoint3& sunDir, RE::NiPoint3& shadowDir) const
{
	const float minElevation = DirectX::XMConvertToRadians(settings.MinShadowElevation);
	if (DirectX::XMScalarASinEst(sunDir.z) >= minElevation)
		return;

	// Hold where the sun crossed the minimum, so a low sun moves the shadow neither up nor sideways
	const auto [sunrise, sunset] = GetSunHorizonHours(timing);
	const bool rising = hour < (sunrise + sunset) * 0.5f;
	shadowDir = GetSunDirectionAtHour(timing, GetSunCrossingHour(timing, minElevation, rising));
}

RE::NiPoint3 SkySync::GetSunDirectionAtHour(const RE::TESClimate::Timing& timing, float hour) const
{
	const auto [sunrise, sunset] = GetSunHorizonHours(timing);
	return settings.UseAlternateSunPath ? CalculateAlternateSunDirection(hour, sunrise, sunset, sunAngle) : CalculateVanillaSunDirection(hour, sunrise, sunset);
}

RE::NiPoint3 SkySync::CalculateVanillaSunDirection(float hour, float sunrise, float sunset)
{
	const float dayLength = sunset - sunrise;
	const float hoursSinceSunrise = hour >= sunrise ? hour - sunrise : hour + HoursPerDay - sunrise;
	const bool day = hoursSinceSunrise < dayLength;
	const float x = day ? (1.0f - 2.0f * hoursSinceSunrise / dayLength) * SunPeakDistance :
	                      (2.0f * (hoursSinceSunrise - dayLength) / (HoursPerDay - dayLength) - 1.0f) * SunPeakDistance;
	const float z = day ? SunPeakDistance - std::abs(x) : std::abs(x) - SunPeakDistance;
	RE::NiPoint3 dir{ x, VanillaSunOffsetY, z };
	dir.Unitize();
	return dir;
}

RE::NiPoint3 SkySync::CalculateAlternateSunDirection(float hour, float sunrise, float sunset, float sunAngle)
{
	float sinPhi, cosPhi, sinTilt, cosTilt;
	DirectX::XMScalarSinCosEst(&sinPhi, &cosPhi, GetAlternateSunPhase(hour, sunrise, sunset));
	DirectX::XMScalarSinCosEst(&sinTilt, &cosTilt, DirectX::XMConvertToRadians(sunAngle));

	RE::NiPoint3 dir{ cosPhi, -sinPhi * cosTilt, sinPhi * sinTilt };
	dir.Unitize();  // unit by construction, up to the estimates' error
	return dir;
}

float SkySync::GetSunDistance(const RE::Sky* sky) const
{
	if (settings.UseAlternateSunPath) {
		const auto [sunrise, sunset] = GetSunHorizonHours(sky->currentClimate->timing);
		const float sinPhi = DirectX::XMScalarSinEst(GetAlternateSunPhase(sky->currentGameHour, sunrise, sunset));
		return std::lerp(SunHorizonDistance, SunPeakDistance, std::max(sinPhi, 0.0f));
	}

	const float distance = sky->sun->root->local.translate.Length();
	return distance < FLT_EPSILON ? SunPeakDistance : distance;
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
	currentCellInterior = cell->IsInteriorCell();
	currentCellWorldspace = cell->GetRuntimeData().worldSpace;
	const float rotation = cell->GetNorthRotation();
	if (rotation == currentSkyRotation)
		return;

	currentSkyRotation = rotation;
	sky->root->local.rotate = RE::NiMatrix3{ RE::NiPoint3{ 0.0f, 0.0f, -rotation } };
	RE::NiUpdateData updateData;
	sky->root->Update(updateData);
}

bool SkySync::IsWorldspaceDisabled(const RE::TESWorldSpace* worldspace) const
{
	const char* editorID = worldspace ? worldspace->GetFormEditorID() : nullptr;
	// Editor IDs are case-insensitive in the engine
	return editorID && std::ranges::any_of(settings.DisabledWorldspaces, [editorID](const std::string& listed) { return !_stricmp(listed.c_str(), editorID); });
}

std::pair<float, float> SkySync::GetSunHorizonHours(const RE::TESClimate::Timing& timing) const
{
	// Vanilla's sun touches the horizon where its alpha fade ends: the interval midpoint +- half fSunAlphaTransTime
	const float halfTransition = settings.UseAlternateSunPath ? AlternateSunHorizonOffsetHours :
	                                                            (gSunAlphaTransTime ? gSunAlphaTransTime->GetFloat() : DefaultSunAlphaTransTime) * 0.5f;
	return { Util::Climate::GetMiddleHour(timing.sunrise) - halfTransition, Util::Climate::GetMiddleHour(timing.sunset) + halfTransition };
}

void SkySync::ProcessSun(const RE::Sky* sky, const RE::NiPoint3& sunDir, float sunHeight, RE::NiPoint3 dirs[], float intensities[])
{
	const auto sun = sky->sun;
	// Vanilla's direction is replaced too: Sun::Update mirrors its night arc above the horizon, where a lowered effective horizon would reveal the disc
	SetSunPosition(sun, sunDir, GetSunDistance(sky));
	FadeSunAtHorizon(sky, sunHeight);

	dirs[static_cast<int>(Caster::Sun)] = sunDir;

	if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(sun->sunBase->GetGeometryRuntimeData().shaderProperty.get()))
		intensities[static_cast<int>(Caster::Sun)] = prop->kBlendColor.alpha;
}

void SkySync::FadeSunAtHorizon(const RE::Sky* sky, float sunHeight)
{
	const float fade = std::clamp((sunHeight - DirectX::XMConvertToRadians(SunDiscFadeStartElevation)) / DirectX::XMConvertToRadians(SunDiscFadeElevation), 0.0f, 1.0f);
	// Rebuilt the way Sun::Update blends it, since its own glare alpha already carries the clock-based fade
	const auto weatherGlare = [](const RE::TESWeather* weather, float weight) {
		return weather ? weather->data.sunGlare * SunGlareScale * weight : 0.0f;
	};
	const float glare = weatherGlare(sky->currentWeather, sky->currentWeatherPct) + weatherGlare(sky->lastWeather, 1.0f - sky->currentWeatherPct);

	const std::pair<RE::BSTriShape*, float> alphas[] = { { sky->sun->sunBase.get(), fade }, { sky->sun->sunGlare.get(), glare * fade } };
	for (const auto& [geometry, alpha] : alphas) {
		if (const auto prop = geometry ? skyrim_cast<RE::BSSkyShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get()) : nullptr)
			prop->kBlendColor.alpha = alpha;
	}
}

void SkySync::ProcessMoon(const RE::Sky* sky, const Caster type, RE::NiPoint3 dirs[], float intensities[])
{
	const int idx = static_cast<int>(type);
	colors[idx] = {};

	const auto moon = type == Caster::Masser ? sky->masser : sky->secunda;
	if (!moon || moon->root->GetFlags().any(RE::NiAVObject::Flag::kHidden))
		return;

	dirs[idx] = Util::Moon::GetFacingAxis(moon->root->local.rotate);

	const auto& baseColor = type == Caster::Masser ? Util::Moon::MasserBaseColor : Util::Moon::SecundaBaseColor;
	float4 color = Util::Moon::GetLightColor(moon, baseColor, settings.NewMoonIntensity, settings.CrescentMoonIntensity, settings.FullMoonIntensity).GetColor();
	colors[idx] = color;

	const auto src = static_cast<MoonLightSource>(settings.MoonLightSource);
	const bool isValidSource = src == MoonLightSource::Brightest || (src == MoonLightSource::Masser && type == Caster::Masser) || (src == MoonLightSource::Secunda && type == Caster::Secunda);
	if (!isValidSource)
		return;

	// A moon sinking toward the horizon loses out to one that is still up
	intensities[idx] = color.w * GetHorizonDim(GetHeightAboveHorizon(dirs[idx]));
}

RE::NiPoint3 SkySync::GetCelestialDirection(const RE::Sky* sky, const Caster caster) const
{
	const auto idx = static_cast<size_t>(caster);
	assert(idx < std::size(rawDirections));
	if (!sky || !sky->root)
		return ZenithDirection;

	RE::NiPoint3 dir = rawDirections[idx];
	if (dir.SqrLength() > 0.0f)
		dir = sky->root->world.rotate * dir;
	else if (caster == Caster::Sun)  // SetSunPosition writes only the local translate, so the world one may be stale
		dir = sky->sun && sky->sun->root ? sky->sun->root->world.translate - sky->root->world.translate : RE::NiPoint3{};
	else
		dir = Util::Moon::GetDirection(caster == Caster::Masser ? sky->masser : sky->secunda);

	if (dir.Unitize() <= FLT_EPSILON)
		return ZenithDirection;
	return dir;
}

SkySync::Caster SkySync::GetMoonLightCaster(const RE::Sky* sky) const
{
	if (!sky)
		return Caster::Zenith;

	auto visibility = [](const RE::Moon* moon) {
		if (!moon || !moon->root || !moon->moonMesh || moon->root->GetFlags().any(RE::NiAVObject::Flag::kHidden))
			return 0.0f;
		const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().shaderProperty.get());
		return prop ? prop->kBlendColor.alpha : 0.0f;
	};

	const float masser = visibility(sky->masser);
	const float secunda = visibility(sky->secunda);

	switch (static_cast<MoonLightSource>(settings.MoonLightSource)) {
	case MoonLightSource::Masser:
		return masser > 0.0f ? Caster::Masser : Caster::Zenith;
	case MoonLightSource::Secunda:
		return secunda > 0.0f ? Caster::Secunda : Caster::Zenith;
	default:
		if (masser <= 0.0f && secunda <= 0.0f)
			return Caster::Zenith;
		return secunda > masser ? Caster::Secunda : Caster::Masser;
	}
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
	fadeTimer = 0.0f;
	immediateTransitionRemaining = 0.0f;
	transitioning = false;
	intensityFactor = startIntensityFactor = 1.0f;
	vlFade = startVlFade = 1.0f;
}

void SkySync::ShadowFader::Update(const RE::Sky* sky, RE::NiPoint3 dirs[], float intensities[], std::optional<std::array<RE::NiColor, 3>> colors, float fadeDuration, float fadeAdvance, bool a_immediateTransition)
{
	auto isValidDir = [](const RE::NiPoint3& d) { return d.x != 0.0f || d.y != 0.0f || d.z != 0.0f; };

	if (fadeDuration > 0.0f && a_immediateTransition)
		immediateTransitionRemaining = fadeDuration;
	else
		immediateTransitionRemaining = std::max(immediateTransitionRemaining - fadeAdvance, 0.0f);

	Caster best;

	if (globals::features::skySync.sunBelowHorizon) {
		bool masserValid = isValidDir(dirs[static_cast<int>(Caster::Masser)]);
		bool secundaValid = isValidDir(dirs[static_cast<int>(Caster::Secunda)]);

		if (!masserValid && !secundaValid)
			best = Caster::Zenith;
		else if (!masserValid)
			best = Caster::Secunda;
		else if (!secundaValid || intensities[static_cast<int>(Caster::Secunda)] <= intensities[static_cast<int>(Caster::Masser)])
			best = Caster::Masser;
		else
			best = Caster::Secunda;
	} else {
		best = Caster::Sun;
	}

	auto casterDir = [&](Caster c) {
		return c == Caster::Zenith ? ZenithDirection : dirs[static_cast<int>(c)];
	};

	// If best source changed, begin a new transition
	if (best != target) {
		target = best;
		startDir = currentDir;
		startIntensityFactor = intensityFactor;
		startVlFade = vlFade;
		fadeTimer = 0.0f;
		transitioning = true;
	}

	const RE::NiPoint3 targetDir = casterDir(target);
	auto applyLighting = [&]() {
		const bool hasCaster = target != Caster::Zenith;
		const int targetIdx = static_cast<int>(target);
		const float intensity = hasCaster ? intensities[targetIdx] : 0.0f;
		std::optional<RE::NiColor> color = std::nullopt;
		if (colors)
			color = hasCaster ? (*colors)[targetIdx] : RE::NiColor{ 0.0f, 0.0f, 0.0f };
		SetLighting(sky, currentDir, intensity * intensityFactor, color);
	};
	const float targetVlFade = target == Caster::Zenith ? 0.0f : 1.0f;

	if (!transitioning) {
		currentDir = targetDir;
		intensityFactor = 1.0f;
		vlFade = targetVlFade;
		immediateTransitionRemaining = 0.0f;
		applyLighting();
		return;
	}

	const float effectiveFadeAdvance = immediateTransitionRemaining > 0.0f ? fadeDuration : fadeAdvance;
	fadeTimer = std::min(fadeTimer + effectiveFadeAdvance, fadeDuration);
	const float t = Progress(fadeDuration);

	currentDir = {
		std::lerp(startDir.x, targetDir.x, t),
		std::lerp(startDir.y, targetDir.y, t),
		std::lerp(startDir.z, targetDir.z, t)
	};
	// Opposite start and target directions cancel at the midpoint; pass through straight up instead
	if (currentDir.Unitize() <= FLT_EPSILON)
		currentDir = ZenithDirection;

	if (t >= 1.0f) {
		currentDir = targetDir;
		transitioning = false;
		immediateTransitionRemaining = 0.0f;
	}

	// Fade out as the direction leaves the old caster and back in as it reaches the new one
	const float fadeOut = startIntensityFactor * ComputeAlignmentFactor(currentDir, startDir);
	intensityFactor = std::max(fadeOut, ComputeAlignmentFactor(currentDir, targetDir));
	vlFade = std::lerp(startVlFade, targetVlFade, t);
	applyLighting();
}

void SkySync::ShadowFader::SetLighting(const RE::Sky* sky, RE::NiPoint3 dir, float intensity, std::optional<RE::NiColor> color)
{
	// Steer towards the strike for as long as the flash outshines the caster
	if (const float flashWeight = globals::features::skySync.flashWeight; flashWeight > 0.0f) {
		dir = dir + (ZenithDirection - dir) * flashWeight;
		if (dir.Unitize() <= FLT_EPSILON)
			dir = ZenithDirection;
	}
	ClampDirection(dir);

	RE::NiMatrix3& m = sky->sun->light->local.rotate;
	m.entry[0][0] = -dir.x;
	m.entry[1][0] = -dir.y;
	m.entry[2][0] = -dir.z;

	if (color.has_value()) {
		*color *= intensity;
		globals::features::linearLighting.SetSunlightColor(sky->sun->light.get(), *color);
	} else {
		globals::features::linearLighting.ClearSunlightColor(sky->sun->light.get());
	}

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

float SkySync::ShadowFader::ComputeAlignmentFactor(const RE::NiPoint3& current, const RE::NiPoint3& target)
{
	const float dot = std::clamp(current.Dot(target), -1.0f, 1.0f);
	const float angle = DirectX::XMConvertToDegrees(DirectX::XMScalarACosEst(dot));

	return std::clamp((AlignmentFadeEndAngle - angle) / (AlignmentFadeEndAngle - AlignmentFadeStartAngle), 0.0f, 1.0f);
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
