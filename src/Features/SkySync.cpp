#include "SkySync.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SkySync::Settings,
	Enabled,
	UseAlternateSunPath,
	MoonLightSource,
	SunPath,
	CustomAngle,
	MinShadowElevation,
	ShadowTransitionDuration,
	MoonPhaseDirLight,
	MoonPhaseDirLightAmount,
	MoonColorDirLight,
	MoonColorDirLightAmount,
	NewMoonIntensity,
	CrescentMoonIntensity,
	FullMoonIntensity,
	MasserColor,
	SecundaColor)

void SkySync::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Enable or disable Sky Sync features.");
	}

	ImGui::Checkbox("Use alternate sun path", &settings.UseAlternateSunPath);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Calculate sun position based on time of day and season instead of vanilla movement.");
	}

	if (settings.UseAlternateSunPath) {
		if (ImGui::SliderInt("Sun path", &settings.SunPath, 0, static_cast<uint8_t>(SunPath::Count) - 1, SunPathNames[settings.SunPath], ImGuiSliderFlags_AlwaysClamp))
			SetSunAngle();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Choose the trajectory the sun takes across the sky.");
		}

		if (settings.SunPath == static_cast<int32_t>(SunPath::Custom)) {
			if (ImGui::SliderFloat("Custom angle", &settings.CustomAngle, -90.0f, 90.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
				SetSunAngle();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Set a custom angle for the sun's trajectory.");
			}
		}
	}

	ImGui::SliderInt("Moon light source", &settings.MoonLightSource, 0, static_cast<uint8_t>(MoonLightSource::Count) - 1, MoonLightSourceNames[settings.MoonLightSource], ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Select which moon casts shadows during the night.");
	}

	ImGui::SliderFloat("Min Shadow Elevation", &settings.MinShadowElevation, 0.0f, 45.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("The minimum angle sunlight will set to. Caps shadow length. Higher = shorter shadows at sunset/sunrise.");
	}

	ImGui::SliderFloat("Shadow Transition Duration", &settings.ShadowTransitionDuration, 0.0f, 500.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("How long (in game-time units) the shadow direction takes to fade between sources. 100 = ~5 seconds at timescale 20.");
	}

	ImGui::Checkbox("Moon Phase Dir Light", &settings.MoonPhaseDirLight);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Dim directional light based on the active moon's phase.");
	}
	if (settings.MoonPhaseDirLight) {
		ImGui::SliderFloat("Phase Amount", &settings.MoonPhaseDirLightAmount, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Blend between original brightness (0) and phase-dimmed brightness (1).");
		}
	}

	ImGui::Checkbox("Moon Color Dir Light", &settings.MoonColorDirLight);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Tint directional light with the active moon's base color.");
	}
	if (settings.MoonColorDirLight) {
		ImGui::SliderFloat("Color Amount", &settings.MoonColorDirLightAmount, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Blend between original color (0) and moon-tinted color (1).");
		}
	}

	if (ImGui::TreeNodeEx("Moon Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Masser Base Color", settings.MasserColor.data());
		ImGui::ColorEdit3("Secunda Base Color", settings.SecundaColor.data());
		ImGui::SliderFloat("New Moon Intensity", &settings.NewMoonIntensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Crescent Intensity", &settings.CrescentMoonIntensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Full Moon Intensity", &settings.FullMoonIntensity, 0.0f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_None)) {
		static constexpr const char* CasterNames[] = { "Sun", "Masser", "Secunda", "None" };
		static constexpr const char* PhaseNames[] = { "Full", "Waning Gibbous", "Waning Quarter", "Waning Crescent", "New", "Waxing Crescent", "Waxing Quarter", "Waxing Gibbous" };

		auto drawMoonEntry = [&](const char* label, Caster caster, const char* phase) {
			const int idx = static_cast<int>(caster);
			auto& dir = directions[idx];
			auto& color = colors[idx];
			float intensity = intensities[idx];

			ImVec4 swatch = { color.x, color.y, color.z, 1.0f };
			ImGui::ColorButton(label, swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, { ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight() });
			ImGui::SameLine();
			ImGui::Text("%s  [%s]  intensity %.4f  dir (%.2f, %.2f, %.2f)  color (%.3f, %.3f, %.3f)", label, phase, intensity, dir.x, dir.y, dir.z, color.x, color.y, color.z);
		};

		auto getPhase = [](const RE::Moon* moon) -> const char* {
			if (!moon || !moon->moonMesh)
				return "Unknown";
			if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().shaderProperty.get())) {
				if (auto tex = prop->GetBaseTexture())
					return PhaseNames[static_cast<int>(Util::Moon::GetPhaseFromTexture(tex->name.c_str()))];
			}
			return "Unknown";
		};

		auto& sunDir = directions[static_cast<int>(Caster::Sun)];
		ImGui::Text("Sun  visibility %.4f  dir (%.2f, %.2f, %.2f)", intensities[static_cast<int>(Caster::Sun)], sunDir.x, sunDir.y, sunDir.z);

		const auto sky = globals::game::sky;
		drawMoonEntry("Masser", Caster::Masser, sky ? getPhase(sky->masser) : "Unknown");
		drawMoonEntry("Secunda", Caster::Secunda, sky ? getPhase(sky->secunda) : "Unknown");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::Text("Shadow target: %s", CasterNames[static_cast<int>(shadowFader.target)]);
		ImGui::Text("Shadow dir:    (%.2f, %.2f, %.2f)", shadowFader.currentDir.x, shadowFader.currentDir.y, shadowFader.currentDir.z);
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

	if (!settings.MoonPhaseDirLight && !settings.MoonColorDirLight)
		return;

	// Only modify dir light during full nighttime (after sunset end, before sunrise begin)
	if (!IsNight(sky))
		return;

	struct MoonData
	{
		float phaseFactor;
		float3 normalizedColor;
		bool valid;
	};

	auto getMoonData = [&](Caster source) -> MoonData {
		if (source != Caster::Masser && source != Caster::Secunda)
			return { 0.0f, {}, false };
		const int idx = static_cast<int>(source);
		auto& c = colors[idx];
		float intensity = (c.x + c.y + c.z) * (1.0f / 3.0f);
		if (intensity <= 0.0f)
			return { phaseFactors[idx], { 1.0f, 1.0f, 1.0f }, true };
		return { phaseFactors[idx], { c.x / intensity, c.y / intensity, c.z / intensity }, true };
	};

	float influence = 0.0f;
	float phaseFactor = 1.0f;
	float3 normalizedColor = { 1.0f, 1.0f, 1.0f };

	if (shadowFader.transitioning) {
		float t = settings.ShadowTransitionDuration > 0.0f ? shadowFader.fadeTimer / settings.ShadowTransitionDuration : 1.0f;

		auto prev = getMoonData(shadowFader.previousTarget);
		auto cur = getMoonData(shadowFader.target);

		if (prev.valid && cur.valid) {
			phaseFactor = std::lerp(prev.phaseFactor, cur.phaseFactor, t);
			normalizedColor = { std::lerp(prev.normalizedColor.x, cur.normalizedColor.x, t), std::lerp(prev.normalizedColor.y, cur.normalizedColor.y, t), std::lerp(prev.normalizedColor.z, cur.normalizedColor.z, t) };
			influence = 1.0f;
		} else if (cur.valid) {
			phaseFactor = cur.phaseFactor;
			normalizedColor = cur.normalizedColor;
			influence = t;
		} else if (prev.valid) {
			phaseFactor = prev.phaseFactor;
			normalizedColor = prev.normalizedColor;
			influence = 1.0f - t;
		}
	} else {
		auto data = getMoonData(shadowFader.target);
		if (data.valid) {
			phaseFactor = data.phaseFactor;
			normalizedColor = data.normalizedColor;
			influence = 1.0f;
		}
	}

	if (influence <= 0.0f)
		return;

	auto& dirLight = sky->skyColor[static_cast<uint>(RE::TESWeather::ColorTypes::kSunlight)];

	// Phase dimming: scale brightness by the phase factor (1.0 = full moon, 0.05 = new moon)
	if (settings.MoonPhaseDirLight) {
		float amount = settings.MoonPhaseDirLightAmount * influence;
		dirLight.red = std::lerp(dirLight.red, dirLight.red * phaseFactor, amount);
		dirLight.green = std::lerp(dirLight.green, dirLight.green * phaseFactor, amount);
		dirLight.blue = std::lerp(dirLight.blue, dirLight.blue * phaseFactor, amount);
	}

	// Color tinting: shift dir light toward the moon's normalized color
	if (settings.MoonColorDirLight) {
		float amount = settings.MoonColorDirLightAmount * influence;
		dirLight.red = std::lerp(dirLight.red, dirLight.red * normalizedColor.x, amount);
		dirLight.green = std::lerp(dirLight.green, dirLight.green * normalizedColor.y, amount);
		dirLight.blue = std::lerp(dirLight.blue, dirLight.blue * normalizedColor.z, amount);
	}
}

void SkySync::Sky_Update::thunk(RE::Sky* sky)
{
	func(sky);
	globals::features::skySync.Update(sky);
}

void SkySync::Update(const RE::Sky* sky)
{
	if (!settings.Enabled)
		return;

	const auto sun = sky->sun;
	const auto climate = sky->currentClimate;
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!sun || !climate || !player)
		return;

	const auto cell = player->GetParentCell();

	if (cell != currentCell) {
		const auto prevCell = currentCell;
		if (cell)
			SetSkyRotation(sky, cell);
		if (cell && prevCell && (cell->IsInteriorCell() != prevCell->IsInteriorCell() || cell->GetRuntimeData().worldSpace != prevCell->GetRuntimeData().worldSpace))
			shadowFader.Reset();
	}

	// Exterior worldspaces always run; interior cells require the sunlight-shadows flag.
	if (cell && cell->IsInteriorCell() && !cell->cellFlags.all(static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows))) {
		return;
	}

	ProcessSun(sky);
	ProcessMoon(sky, Caster::Masser);
	ProcessMoon(sky, Caster::Secunda);

	shadowFader.Update(sun, directions, intensities, settings.ShadowTransitionDuration);
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

void SkySync::ProcessSun(const RE::Sky* sky)
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

	directions[static_cast<int>(Caster::Sun)] = dir;

	float sunAlpha = 0.0f;
	if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(sun->sunBase->GetGeometryRuntimeData().shaderProperty.get()))
		sunAlpha = prop->kBlendColor.alpha;

	intensities[static_cast<int>(Caster::Sun)] = sunAlpha;
}

void SkySync::ProcessMoon(const RE::Sky* sky, const Caster type)
{
	const int idx = static_cast<int>(type);
	intensities[idx] = 0.0f;
	colors[idx] = {};
	phaseFactors[idx] = 1.0f;
	directions[idx] = { 0.0f, 0.0f, 1.0f };

	const auto moon = type == Caster::Masser ? sky->masser : sky->secunda;
	if (!moon)
		return;

	auto dir = moon->root->local.rotate.GetVectorY();

	if (moonAndStarsLoaded)
		dir = { dir.y, -dir.x, dir.z };

	directions[idx] = dir;

	const bool isMasser = type == Caster::Masser;
	auto& c = isMasser ? settings.MasserColor : settings.SecundaColor;
	float4 baseColor = { c[0], c[1], c[2], 1.0f };
	float4 color = Util::Moon::CalculateColor(sky, isMasser, baseColor, settings.NewMoonIntensity, settings.CrescentMoonIntensity, settings.FullMoonIntensity);

	float fade = 0.0f;
	if (moon->moonMesh) {
		if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().shaderProperty.get())) {
			fade = prop->kBlendColor.alpha;
			if (auto tex = prop->GetBaseTexture()) {
				auto phase = Util::Moon::GetPhaseFromTexture(tex->name.c_str());
				phaseFactors[idx] = Util::Moon::GetPhaseIntensityFactor(phase, settings.NewMoonIntensity, settings.CrescentMoonIntensity, settings.FullMoonIntensity);
			}
		}
	}

	colors[idx] = { color.x * fade, color.y * fade, color.z * fade, fade };

	// Only allow moons to become shadow casters during full nighttime and when fully faded in
	if (!IsNight(sky) || fade < 1.0f)
		return;

	const auto src = static_cast<MoonLightSource>(settings.MoonLightSource);
	const bool isValidSource = src == MoonLightSource::Brightest || (src == MoonLightSource::Masser && type == Caster::Masser) || (src == MoonLightSource::Secunda && type == Caster::Secunda);
	if (!isValidSource)
		return;

	intensities[idx] = (color.x + color.y + color.z) * (1.0f / 3.0f);
}

bool SkySync::IsNight(const RE::Sky* sky)
{
	if (!sky || !sky->currentClimate)
		return false;
	const auto& timing = sky->currentClimate->timing;
	const float sunsetEnd = timing.sunset.end / 6.0f;
	const float sunriseBegin = timing.sunrise.begin / 6.0f;
	const float hour = sky->currentGameHour;
	return hour >= sunsetEnd || hour < sunriseBegin;
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
	target = Caster::None;
	previousTarget = Caster::None;
	fadeTimer = 0.0f;
	transitioning = false;
}

void SkySync::ShadowFader::Update(const RE::Sun* sun, RE::NiPoint3 dirs[], float intensities[], float fadeDuration)
{
	// Pick brightest source
	float bestIntensity = 0.0f;
	auto best = Caster::None;
	for (int i = 0; i < 3; ++i) {
		if (intensities[i] > bestIntensity) {
			bestIntensity = intensities[i];
			best = static_cast<Caster>(i);
		}
	}

	// If brightest source changed, begin a new transition
	if (best != target) {
		previousTarget = target;
		target = best;
		startDir = currentDir;
		fadeTimer = 0.0f;
		transitioning = true;
	}

	if (best == Caster::None) {
		SetLighting(sun, currentDir);
		return;
	}

	if (!transitioning) {
		currentDir = dirs[static_cast<int>(target)];
		SetLighting(sun, currentDir);
		return;
	}

	float timeScale = 20.0f;
	if (const auto calendar = globals::game::calendar)
		timeScale = calendar->GetTimescale();
	fadeTimer = std::min(fadeTimer + *globals::game::deltaTime * timeScale, fadeDuration);
	const float t = fadeDuration > 0.0f ? fadeTimer / fadeDuration : 1.0f;

	RE::NiPoint3 targetDir = dirs[static_cast<int>(target)];
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

	SetLighting(sun, currentDir);
}

void SkySync::ShadowFader::SetLighting(const RE::Sun* sun, RE::NiPoint3 dir)
{
	ClampDirection(dir);

	RE::NiMatrix3& m = sun->light->local.rotate;
	m.entry[0][0] = -dir.x;
	m.entry[1][0] = -dir.y;
	m.entry[2][0] = -dir.z;

	RE::NiUpdateData updateData;
	sun->light->Update(updateData);
}

inline void SkySync::ShadowFader::ClampDirection(RE::NiPoint3& dir)
{
	const float minDegrees = globals::features::skySync.settings.MinShadowElevation;
	const float minElev = DirectX::XMConvertToRadians(minDegrees);
	const float elev = DirectX::XMScalarASinEst(dir.z);
	if (elev >= minElev)
		return;

	const float heading = std::atan2(dir.y, dir.x);
	float sinElev, cosElev, sinHeading, cosHeading;
	DirectX::XMScalarSinCosEst(&sinElev, &cosElev, minElev);
	DirectX::XMScalarSinCosEst(&sinHeading, &cosHeading, heading);

	dir.x = cosElev * cosHeading;
	dir.y = cosElev * sinHeading;
	dir.z = sinElev;
}



