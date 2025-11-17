#include "SkySync.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SkySync::Settings,
	Enabled,
	UseAlternateSunPath,
	MoonLightSource,
	SunPath,
	CustomAngle)

void SkySync::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);

	ImGui::Checkbox("Use alternate sun path", &settings.UseAlternateSunPath);

	if (settings.UseAlternateSunPath) {
		if (ImGui::SliderInt("Sun path", &settings.SunPath, 0, static_cast<uint8_t>(SunPath::Count) - 1, SunPathNames[settings.SunPath], ImGuiSliderFlags_AlwaysClamp))
			SetSunAngle();

		if (settings.SunPath == static_cast<int32_t>(SunPath::Custom)) {
			if (ImGui::SliderFloat("Custom angle", &settings.CustomAngle, -90.0f, 90.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
				SetSunAngle();
		}
	}

	ImGui::SliderInt("Moon light source", &settings.MoonLightSource, 0, static_cast<uint8_t>(MoonLightSource::Count) - 1, MoonLightSourceNames[settings.MoonLightSource], ImGuiSliderFlags_AlwaysClamp);
}

void SkySync::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.MoonLightSource = std::clamp(settings.MoonLightSource, static_cast<int32_t>(MoonLightSource::Brightest), static_cast<int32_t>(MoonLightSource::Secunda));
	settings.SunPath = std::clamp(settings.SunPath, static_cast<int32_t>(SunPath::Southern), static_cast<int32_t>(SunPath::Custom));
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

	stl::detour_thunk<Moon_Update>(REL::RelocationID(25626, 26169));
	stl::detour_thunk<Sky_Update>(REL::RelocationID(25682, 26229));
	stl::detour_thunk<Sky_OnNewClimate>(REL::RelocationID(25695, 26242));
	stl::write_thunk_call<ApplyVolumetricLighting_VolumetricLightingDescriptor_Get>(REL::RelocationID(100475, 107193).address() + 0x354);

	gSunPosition = reinterpret_cast<RE::NiPoint3*>(REL::RelocationID(527924, 414871).address());
	gSunGlareSize = reinterpret_cast<float*>(REL::RelocationID(502611, 370235).address());
	gMasserSize = reinterpret_cast<uint32_t*>(REL::RelocationID(502558, 370155).address());
	gSecundaSize = reinterpret_cast<uint32_t*>(REL::RelocationID(502570, 370173).address());

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

	if (const auto cell = player->GetParentCell(); cell != currentCell) {
		SetSkyRotation(sky, cell);
		if (currentCell && (cell->IsInteriorCell() != currentCell->IsInteriorCell() || cell->GetRuntimeData().worldSpace != currentCell->GetRuntimeData().worldSpace))
			shadowFader.Reset();
	}

	const float time = sky->currentGameHour;
	const bool isDayTime = time > timings.sunriseFadeOutMoonEnd && time < timings.sunsetFadeInMoonStart;

	const auto worldSpace = player->GetWorldspace();
	const float altitude = worldSpace ? player->GetPositionZ() - worldSpace->GetDefaultWaterHeight() : 0.0f;

	ProcessSun(sun, time, altitude, isDayTime);
	ProcessMoon(sky->masser, time, Caster::Masser, altitude, isDayTime);
	ProcessMoon(sky->secunda, time, Caster::Secunda, altitude, isDayTime);

	shadowFader.Update(sun, directions, intensities, lightColors, isDayTime);
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

void SkySync::ProcessSun(const RE::Sun* sun, const float time, const float altitude, const bool isDayTime)
{
	RE::NiPoint3 dir;
	float dist;

	if (settings.UseAlternateSunPath) {
		CalculateAlternateSunDirectionAndDistance(dir, dist, time, timings.sunrise, timings.sunset, sunAngle);
	} else
		CalculateSunDirectionAndDistance(sun, dir, dist);

	rawDirections[static_cast<int>(Caster::Sun)] = dir;

	const RE::NiPoint3 apparentDir = GetApparentDirection(dir, altitude);
	SetSunPosition(sun, apparentDir, dist);

	directions[static_cast<int>(Caster::Sun)] = apparentDir;

	SetSunBaseVisibility(sun, isDayTime ? 1.0f : 0.0f);

	intensities[static_cast<int>(Caster::Sun)] = isDayTime ? CalculateVisibility(dir, dist, *gSunGlareSize * SunScaleFactor) : 0.0f;
}

void SkySync::ProcessMoon(const RE::Moon* moon, const float time, const Caster type, const float altitude, const bool isDayTime)
{
	intensities[static_cast<int>(type)] = 0.0f;
	directions[static_cast<int>(type)] = { 0.0f, 0.0f, 1.0f };
	rawDirections[static_cast<int>(type)] = { 0.0f, 0.0f, -1.0f };

	if (!moon)
		return;

	const auto dir = moon->root->local.rotate.GetVectorY();

	rawDirections[static_cast<int>(type)] = dir;

	auto apparentDir = GetApparentDirection(dir, altitude);
	SetMoonDirection(moon, apparentDir);

	// Moon and Stars adjusts some intermediary rotation matrices for the moon
	// Directly changing the directions here avoids 3 matrix multiplications and a vector rotation
	if (moonAndStarsLoaded)
		apparentDir = { apparentDir.y, -apparentDir.x, apparentDir.z };

	directions[static_cast<int>(type)] = apparentDir;

	if (isDayTime)
		return;

	const auto src = static_cast<MoonLightSource>(settings.MoonLightSource);
	const bool isValidSource = src == MoonLightSource::Brightest || (src == MoonLightSource::Masser && type == Caster::Masser) || (src == MoonLightSource::Secunda && type == Caster::Secunda);
	if (!isValidSource)
		return;

	const float moonRadius = type == Caster::Masser ? static_cast<float>(*gMasserSize) : static_cast<float>(*gSecundaSize);
	float intensity = CalculateVisibility(dir, moon->moonMesh->local.translate.y, moonRadius);

	if (type == Caster::Masser)
		intensity *= masserPhaseIntensityFactor;
	else if (type == Caster::Secunda)
		intensity *= secundaPhaseIntensityFactor * SecundaIntensityFactor;

	if (time >= timings.sunriseFadeOutMoonStart && time <= timings.sunriseFadeOutMoonEnd)
		intensity *= SmoothStep(timings.sunriseFadeOutMoonEnd, timings.sunriseFadeOutMoonStart, time);
	else if (time >= timings.sunsetFadeInMoonStart && time <= timings.sunsetFadeInMoonEnd)
		intensity *= SmoothStep(timings.sunsetFadeInMoonStart, timings.sunsetFadeInMoonEnd, time);

	intensities[static_cast<int>(type)] = intensity;
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

RE::NiPoint3 SkySync::GetApparentDirection(const RE::NiPoint3& dir, const float altitude)
{
	const float dipAngle = -std::atan(altitude / RenderDistance);
	float sinPhi, cosPhi;
	DirectX::XMScalarSinCosEst(&sinPhi, &cosPhi, dipAngle);

	const auto rotationAxis = dir.UnitCross({ 0.0f, 0.0f, 1.0f });
	const float axisDotDir = rotationAxis.Dot(dir);
	const auto axisCrossDir = rotationAxis.Cross(dir);
	const float oneMinusCosPhi = 1.0f - cosPhi;

	const float x = dir.x * cosPhi + axisCrossDir.x * sinPhi + rotationAxis.x * (axisDotDir * oneMinusCosPhi);
	const float y = dir.y * cosPhi + axisCrossDir.y * sinPhi + rotationAxis.y * (axisDotDir * oneMinusCosPhi);
	const float z = dir.z * cosPhi + axisCrossDir.z * sinPhi + rotationAxis.z * (axisDotDir * oneMinusCosPhi);

	RE::NiPoint3 rotated = { x, y, z };
	rotated.Unitize();
	return rotated;
}

inline void SkySync::SetSunPosition(const RE::Sun* sun, const RE::NiPoint3& dir, const float distance)
{
	const auto position = dir * distance;
	sun->root->local.translate = position;
	sun->sunGlareNode->local.translate = position;
	*gSunPosition = position;
}

inline void SkySync::SetMoonDirection(const RE::Moon* moon, const RE::NiPoint3& dir)
{
	auto& m = moon->root->local.rotate;
	m.entry[0][1] = dir.x;
	m.entry[1][1] = dir.y;
	m.entry[2][1] = dir.z;
}

inline float SkySync::CalculateVisibility(const RE::NiPoint3& dir, const float dist, const float radius)
{
	const float height = dir.Dot({ 0.0f, 0.0f, 1.0f }) * dist;
	return SmoothStep(-radius, radius, height);
}

inline void SkySync::SetSunBaseVisibility(const RE::Sun* sun, const float visibility)
{
	if (const auto property = skyrim_cast<RE::BSSkyShaderProperty*>(sun->sunBase->GetGeometryRuntimeData().properties[1].get()))
		property->kBlendColor.alpha = visibility;
}

void SkySync::ShadowFader::Reset()
{
	fadePhase = Phase::None;
	current = Caster::None;
	target = Caster::None;
	fadeTimer = 0.0f;
}

void SkySync::ShadowFader::Update(const RE::Sun* sun, RE::NiPoint3 dirs[3], float intensities[3], std::optional<std::array<RE::NiColor, 3>> colors, const bool isDayTime)
{
	const float masserIntensity = intensities[static_cast<int>(Caster::Masser)];
	const float secundaIntensity = intensities[static_cast<int>(Caster::Secunda)];

	auto desired = Caster::None;
	if (isDayTime)
		desired = Caster::Sun;
	else if (masserIntensity > 0.0f && masserIntensity >= secundaIntensity)
		desired = Caster::Masser;
	else if (secundaIntensity > 0.0f)
		desired = Caster::Secunda;

	if (desired != target) {
		target = desired;
		fadeTimer = 0.0f;

		if (current == Caster::None) {
			fadePhase = Phase::FadeIn;
			current = target;
		} else
			fadePhase = Phase::FadeOut;
	}

	const auto calendar = RE::Calendar::GetSingleton();
	const float currentHoursPassed = calendar->GetHoursPassed();
	const float timeScale = calendar->GetTimescale();
	const float hoursPassedDiff = abs(currentHoursPassed - previousHoursPassed);
	previousHoursPassed = currentHoursPassed;
	if (timeScale <= 0.0f || hoursPassedDiff >= 0.01f) {
		fadePhase = Phase::None;
		current = target;
	}

	std::optional<RE::NiColor> color = std::nullopt;
	if (colors.has_value())
		color = { 0.f, 0.f, 0.f };

	if (current == Caster::None) {
		fadePhase = Phase::None;
		SetLighting(sun, { 0.0f, 0.0f, 1.0f }, 0.0f, color);
		return;
	}

	const auto& dir = dirs[static_cast<int>(current)];
	const auto intensity = intensities[static_cast<int>(current)];
	if (colors.has_value())
		color = (*colors)[static_cast<int>(current)];

	if (fadePhase == Phase::None) {
		SetLighting(sun, dir, intensity, color);
		return;
	}

	fadeTimer = std::min(fadeTimer + *globals::game::deltaTime * timeScale, FadeTime);

	const float t = fadeTimer / FadeTime;
	const float fade = fadePhase == Phase::FadeIn ? t : 1.0f - t;
	SetLighting(sun, dir, intensity * fade, color);

	if (fadePhase == Phase::FadeOut) {
		if (t >= 1.0f || intensity <= 0.0f) {
			current = target;
			fadePhase = Phase::FadeIn;
			fadeTimer = 0.0f;
		}
	} else if (fadePhase == Phase::FadeIn) {
		if (t >= 1.0f)
			fadePhase = Phase::None;
	}
}

void SkySync::ShadowFader::SetLighting(const RE::Sun* sun, RE::NiPoint3 dir, float intensity, std::optional<RE::NiColor> color)
{
	ClampDirection(dir);

	RE::NiMatrix3& m = sun->light->local.rotate;
	m.entry[0][0] = -dir.x;
	m.entry[1][0] = -dir.y;
	m.entry[2][0] = -dir.z;

	if (color.has_value())
		sun->light->GetLightRuntimeData().diffuse = *color;

	RE::NiUpdateData updateData;
	sun->light->Update(updateData);

	intensity = std::clamp(intensity, 0.0f, 1.0f);
	sun->light->GetLightRuntimeData().fade = intensity;
	volumetricLightingIntensityFactor = intensity;
}

inline void SkySync::ShadowFader::ClampDirection(RE::NiPoint3& dir)
{
	constexpr float minElev = DirectX::XMConvertToRadians(MinElevation);
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

SkySync::VolumetricLightingDescriptor* SkySync::ApplyVolumetricLighting_VolumetricLightingDescriptor_Get::thunk()
{
	const auto volumetricLightingDescriptor = func();
	if (globals::features::skySync.settings.Enabled)
		volumetricLightingDescriptor->lightingIntensity *= volumetricLightingIntensityFactor;
	return volumetricLightingDescriptor;
}

void SkySync::ClimateTimings::Update(const RE::TESClimate* climate)
{
	sunriseBegin = climate->timing.sunrise.begin / 6.0f;
	sunriseEnd = climate->timing.sunrise.end / 6.0f;
	sunsetBegin = climate->timing.sunset.begin / 6.0f;
	sunsetEnd = climate->timing.sunset.end / 6.0f;
	sunrise = (sunriseBegin + sunriseEnd) * 0.5f - 0.25f;
	sunset = (sunsetBegin + sunsetEnd) * 0.5f + 0.25f;
	sunriseFadeOutMoonStart = sunriseBegin - 0.5f;
	sunriseFadeOutMoonEnd = sunriseBegin + 1.0f;
	sunsetFadeInMoonStart = sunsetEnd - 1.0f;
	sunsetFadeInMoonEnd = sunsetEnd + 0.5f;
}

void SkySync::Sky_OnNewClimate::thunk(RE::Sky* sky)
{
	if (auto& singleton = globals::features::skySync; singleton.settings.Enabled && sky && sky->currentClimate)
		singleton.timings.Update(sky->currentClimate);
	func(sky);
}

void SkySync::Moon_Update::thunk(RE::Moon* moon, RE::Sky* sky)
{
	const auto updateMoonTexture = moon->updateMoonTexture;

	func(moon, sky);

	if (auto& singleton = globals::features::skySync; singleton.settings.Enabled && updateMoonTexture != moon->updateMoonTexture) {
		// Gets the texture name of the current moon phase when it changes rather than reading direct global variables
		// Allows for compatability with other mods that don't directly update the in-game phase values
		const auto moonShaderProperty = skyrim_cast<RE::BSSkyShaderProperty*>(moon->moonMesh->GetGeometryRuntimeData().properties[1].get());

		const auto name = moonShaderProperty->GetBaseTexture()->name.c_str();
		const size_t len = std::strlen(name);
		std::string lower;
		lower.reserve(len);
		for (size_t i = 0; i < len; ++i) {
			lower.push_back(static_cast<char>(std::tolower(name[i])));
		}

		static constexpr std::array<std::pair<std::string_view, RE::Moon::Phases::Phase>, 8> Lookup{
			{ { "full", RE::Moon::Phases::Phase::kFull },
				{ "three_wan", RE::Moon::Phases::Phase::kWaningGibbous },
				{ "half_wan", RE::Moon::Phases::Phase::kWaningQuarter },
				{ "one_wan", RE::Moon::Phases::Phase::kWaningCrescent },
				{ "new", RE::Moon::Phases::Phase::kNewMoon },
				{ "one_wax", RE::Moon::Phases::Phase::kWaxingCrescent },
				{ "half_wax", RE::Moon::Phases::Phase::kWaxingQuarter },
				{ "three_wax", RE::Moon::Phases::Phase::kWaxingGibbous } }
		};

		RE::Moon::Phases::Phase phase = RE::Moon::Phases::Phase::kFull;
		for (auto& [suffix, id] : Lookup) {
			if (lower.find(suffix) != std::string::npos) {
				phase = id;
				break;
			}
		}

		float* intensityFactor = moon == sky->masser ? &singleton.masserPhaseIntensityFactor : &singleton.secundaPhaseIntensityFactor;
		if (phase == RE::Moon::Phases::Phase::kNewMoon) {
			*intensityFactor = NewMoonIntensityFactor;
		} else {
			const float t = (abs(static_cast<float>(phase) - static_cast<float>(RE::Moon::Phases::Phase::kNewMoon)) - 1.0f) / 3.0f;
			*intensityFactor = std::lerp(CrescentMoonIntensityFactor, FullMoonIntensityFactor, t);
		}
	}
}

inline float SkySync::SmoothStep(const float start, const float end, const float x)
{
	const float t = std::clamp((x - start) / (end - start), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}