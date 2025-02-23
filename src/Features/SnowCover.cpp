#include "SnowCover.h"

#include "Util.h"
#include <DDSTextureLoader.h>

const float MIN_START_PERCENTAGE = 0.05f;
const float DEFAULT_TRANSITION_PERCENTAGE = 1.0f;
const float TRANSITION_CURVE_MULTIPLIER = 2.0f;
const float TRANSITION_DENOMINATOR = 256.0f;
const float DRY_WETNESS = 0.0f;
const float RAIN_DELTA_PER_SECOND = -2.0f / 3600.0f;
const float SNOWY_DAY_DELTA_PER_SECOND = 2.0f / 3600.0f;
const float CLOUDY_DAY_DELTA_PER_SECOND = -0.735f / 3600.0f;
const float CLEAR_DAY_DELTA_PER_SECOND = -1.518f / 3600.0f;
const float WETNESS_SCALE = 2.0;  // Speed at which wetness builds up and drys.
const float PUDDLE_SCALE = 1.0;   // Speed at which puddles build up and dry
const float MAX_PUDDLE_DEPTH = 3.0f;
const float MAX_WETNESS_DEPTH = 2.0f;
const float MAX_PUDDLE_WETNESS = 1.0f;
const float MAX_WETNESS = 1.0f;
const float SECONDS_IN_A_DAY = 86400;
const float MAX_TIME_DELTA = SECONDS_IN_A_DAY - 30;
const float MIN_WEATHER_TRANSITION_SPEED = 0.0f;
const float MAX_WEATHER_TRANSITION_SPEED = 500.0f;
const float AVERAGE_RAIN_VOLUME = 4000.0f;
const float MIN_RAINDROP_CHANCE_MULTIPLIER = 0.1f;
const float MAX_RAINDROP_CHANCE_MULTIPLIER = 2.0f;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowCover::Settings,
	EnableSnowCover,
	AffectFoliageColor,
	SnowHeightOffset,
	FoliageHeightOffset,
	MaxSummerMonth,
	MaxWinterMonth, SummerHeightOffset, WinterHeightOffset, UVScale, ParallaxScale, screenSpaceScale, logMicrofacetDensity, microfacetRoughness, densityRandomization)

void SnowCover::DrawSettings()
{
	if (ImGui::TreeNodeEx("Snow Cover", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Snow Cover", (bool*)&settings.EnableSnowCover);
		ImGui::Checkbox("Affect Foliage Color", (bool*)&settings.AffectFoliageColor);
		ImGui::SliderFloat("Snow Line Height Offset", &settings.SnowHeightOffset, -10000.0f, 10000.0f);
		ImGui::SliderFloat("Foliage Color Height Offset", &settings.FoliageHeightOffset, -10000.0f, 10000.0f);
		ImGui::SliderInt("Maximum Summer Month", (int*)&settings.MaxSummerMonth, 0, 11);
		ImGui::SliderInt("Maximum Winter Month", (int*)&settings.MaxWinterMonth, 0, 11);
		ImGui::SliderFloat("Summer Height Offset", &settings.SummerHeightOffset, -10000.0f, 10000.0f);
		ImGui::SliderFloat("Winter Height Offset", &settings.WinterHeightOffset, -10000.0f, 10000.0f);

		if (ImGui::TreeNodeEx("Snow Material", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("UV Scale", &settings.UVScale, 0.1f, 10.f, "%.1f");
			ImGui::SliderFloat("Parallax Scale", &settings.ParallaxScale, 0.f, 2.f, "%.1f");
			ImGui::SliderFloat("Screenspace Scale", &settings.screenSpaceScale, 0.f, 3.f, "%.3f");
			ImGui::SliderFloat("Log Microfacet Density", &settings.logMicrofacetDensity, 0.f, 40.f, "%.3f");
			ImGui::SliderFloat("Microfacet Roughness", &settings.microfacetRoughness, 0.f, 1.f, "%.3f");
			ImGui::SliderFloat("Density Randomization", &settings.densityRandomization, 0.f, 5.f, "%.3f");
			ImGui::TreePop();
		}
		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();
}

float SnowCover::CalculateWeatherTransitionPercentage(float skyCurrentWeatherPct, float beginFade, bool fadeIn)
{
	float weatherTransitionPercentage = DEFAULT_TRANSITION_PERCENTAGE;
	// Correct if beginFade is zero or negative
	beginFade = beginFade > 0 ? beginFade : beginFade + TRANSITION_DENOMINATOR;
	// Wait to start transition until precipitation begins/ends
	float startPercentage = 1 - ((TRANSITION_DENOMINATOR - beginFade) * (1.0f / TRANSITION_DENOMINATOR));

	if (fadeIn) {
		float currentPercentage = (skyCurrentWeatherPct - startPercentage) / (1 - startPercentage);
		weatherTransitionPercentage = std::clamp(currentPercentage, 0.0f, 1.0f);
	} else {
		float currentPercentage = (startPercentage - skyCurrentWeatherPct) / (startPercentage);
		weatherTransitionPercentage = 1 - std::clamp(currentPercentage, 0.0f, 1.0f);
	}
	return weatherTransitionPercentage;
}

void SnowCover::CalculateWetness(RE::TESWeather* weather, RE::Sky* sky, float seconds, float& weatherWetnessDepth)
{
	float wetnessDepthDelta = CLEAR_DAY_DELTA_PER_SECOND * WETNESS_SCALE * seconds;
	float puddleDepthDelta = CLEAR_DAY_DELTA_PER_SECOND * PUDDLE_SCALE * seconds;
	if (weather && sky) {
		// Figure out the weather type and set the wetness
		if (weather->precipitationData && weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
			// Raining
			wetnessDepthDelta = RAIN_DELTA_PER_SECOND * WETNESS_SCALE * seconds;
			puddleDepthDelta = RAIN_DELTA_PER_SECOND * PUDDLE_SCALE * seconds;
		} else if (weather->precipitationData && weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
			wetnessDepthDelta = SNOWY_DAY_DELTA_PER_SECOND * WETNESS_SCALE * seconds;
			puddleDepthDelta = SNOWY_DAY_DELTA_PER_SECOND * PUDDLE_SCALE * seconds;
		} else if (weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kCloudy)) {
			wetnessDepthDelta = CLOUDY_DAY_DELTA_PER_SECOND * WETNESS_SCALE * seconds;
			puddleDepthDelta = CLOUDY_DAY_DELTA_PER_SECOND * PUDDLE_SCALE * seconds;
		}
	}

	weatherWetnessDepth = wetnessDepthDelta > 0 ? std::min(weatherWetnessDepth + wetnessDepthDelta, MAX_WETNESS_DEPTH) : std::max(weatherWetnessDepth + wetnessDepthDelta, 0.0f);
}

void SnowCover::Draw(const RE::BSShader*, const uint32_t)
{
}

SnowCover::PerFrame SnowCover::GetCommonBufferData()
{
	PerFrame data{};
	data.SnowAmount = 0;
	currentWeatherID = 0;
	uint32_t previousLastWeatherID = lastWeatherID;
	lastWeatherID = 0;
	float currentWeatherSnowing = 0.0f;
	float lastWeatherSnowing = 0.0f;
	float weatherTransitionPercentage = previousWeatherTransitionPercentage;

	if (settings.EnableSnowCover) {
		if (auto sky = RE::Sky::GetSingleton()) {
			data.Sky = static_cast<uint>(sky->mode.get());
			if (sky->mode.get() == RE::Sky::Mode::kFull) {
				if (auto currentWeather = sky->currentWeather) {
					if (currentWeather->precipitationData && currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
						float particleDensity = currentWeather->precipitationData->data[static_cast<int>(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity)].f;
						float particleGravity = currentWeather->precipitationData->data[static_cast<int>(RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity)].f;
						currentWeatherSnowing = std::clamp(((particleDensity * particleGravity) / AVERAGE_RAIN_VOLUME), MIN_RAINDROP_CHANCE_MULTIPLIER, MAX_RAINDROP_CHANCE_MULTIPLIER);
					}
					currentWeatherID = currentWeather->GetFormID();
					if (auto calendar = RE::Calendar::GetSingleton()) {
						auto time = calendar->GetTime();
						data.Month = static_cast<float>(time.tm_mon + (time.tm_mday + (time.tm_hour + (time.tm_min + time.tm_sec / 61.0) / 60.0) / 24.0) / 32.0);
						float currentWeatherWetnessDepth = snowDepth;
						float currentGameTime = calendar->GetCurrentGameTime() * SECONDS_IN_A_DAY;
						lastGameTimeValue = lastGameTimeValue == 0 ? currentGameTime : lastGameTimeValue;
						float seconds = currentGameTime - lastGameTimeValue;
						lastGameTimeValue = currentGameTime;

						if (abs(seconds) >= MAX_TIME_DELTA) {
							// If too much time has passed, snap wetness depths to the current weather.
							seconds = 0.0f;
							currentWeatherWetnessDepth = 0.0f;
							weatherTransitionPercentage = DEFAULT_TRANSITION_PERCENTAGE;
							CalculateWetness(currentWeather, sky, 1.0f, currentWeatherWetnessDepth);
							snowDepth = currentWeatherWetnessDepth > 0 ? MAX_WETNESS_DEPTH : 0.0f;
						}

						if (seconds > 0 || (seconds < 0 && snowDepth > 0)) {
							weatherTransitionPercentage = DEFAULT_TRANSITION_PERCENTAGE;
							float lastWeatherWetnessDepth = snowDepth;
							seconds *= MIN_WEATHER_TRANSITION_SPEED + (MAX_WEATHER_TRANSITION_SPEED - MIN_WEATHER_TRANSITION_SPEED) / 2.0f;
							CalculateWetness(currentWeather, sky, seconds, currentWeatherWetnessDepth);
							// If there is a lastWeather, figure out what type it is and set the wetness
							if (auto lastWeather = sky->lastWeather) {
								lastWeatherID = lastWeather->GetFormID();
								CalculateWetness(lastWeather, sky, seconds, lastWeatherWetnessDepth);
								// If it was raining, wait to transition until precipitation ends, otherwise use the current weather's fade in
								if (lastWeather->precipitationData && lastWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
									float particleDensity = lastWeather->precipitationData->data[static_cast<int>(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity)].f;
									float particleGravity = lastWeather->precipitationData->data[static_cast<int>(RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity)].f;
									lastWeatherSnowing = std::clamp(((particleDensity * particleGravity) / AVERAGE_RAIN_VOLUME), MIN_RAINDROP_CHANCE_MULTIPLIER, MAX_RAINDROP_CHANCE_MULTIPLIER);
									weatherTransitionPercentage = CalculateWeatherTransitionPercentage(sky->currentWeatherPct, lastWeather->data.precipitationEndFadeOut, false);
								} else {
									weatherTransitionPercentage = CalculateWeatherTransitionPercentage(sky->currentWeatherPct, currentWeather->data.precipitationBeginFadeIn, true);
								}
							}

							// Transition between CurrentWeather and LastWeather depth values
							snowDepth = std::lerp(lastWeatherWetnessDepth, currentWeatherWetnessDepth, weatherTransitionPercentage);
						} else {
							lastWeatherID = previousLastWeatherID;
						}

						// Calculate the wetness value from the water depth
						data.SnowAmount = std::min(snowDepth, MAX_WETNESS);
						previousWeatherTransitionPercentage = weatherTransitionPercentage;
					}
				}
			}
		}
	}

	static size_t snowTimer = 0;                                       // size_t for precision
	if (!RE::UI::GetSingleton()->GameIsPaused())                       // from lightlimitfix
		snowTimer += (size_t)(RE::GetSecondsSinceLastFrame() * 1000);  // BSTimer::delta is always 0 for some reason
	data.TimeSnowing = snowTimer / 1000.f;

	data.settings = settings;

	return data;
}

void SnowCover::SetupResources()
{
	auto& device = State::GetSingleton()->device;
	auto& context = State::GetSingleton()->context;
	HRESULT hr = DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\SnowCover\\snow.dds", nullptr, &views.at(0));
	if (hr != S_OK)
		logger::warn("Snow Cover: Error loading diffuse texture: {}", hr);
	hr = DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\SnowCover\\snow_n.dds", nullptr, &views.at(1));
	if (hr != S_OK)
		logger::warn("Snow Cover: Error loading normal texture: {}", hr);
	hr = DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\SnowCover\\snow_rmaos.dds", nullptr, &views.at(2));
	if (hr != S_OK)
		logger::warn("Snow Cover: Error loading RMAOS texture: {}", hr);
	hr = DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\SnowCover\\snow_p.dds", nullptr, &views.at(3));
	if (hr != S_OK)
		logger::warn("Snow Cover: Error loading parallax texture: {}", hr);
}

void SnowCover::Prepass()
{
	auto& context = State::GetSingleton()->context;
	context->PSSetShaderResources(73, (uint)views.size(), views.data());
}

void SnowCover::Reset()
{
	requiresUpdate = true;
}

void SnowCover::Load(json& o_json)
{
	settings = o_json;
}

void SnowCover::Save(json& o_json)
{
	o_json = settings;
}

void SnowCover::RestoreDefaultSettings()
{
	settings = {};
}