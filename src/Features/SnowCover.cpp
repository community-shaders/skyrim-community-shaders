#include "SnowCover.h"

#include "Util.h"
#include <DDSTextureLoader.h>
#include <cstdlib>

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
	SnowCover::UserSettings,
	AffectFoliageColor,
	SnowHeightOffset)

void SnowCover::DrawSettings()
{
	if (ImGui::TreeNodeEx("Snow Cover", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button("Reload configs")) {
			last_worldspace = "";
		}

		ImGui::Checkbox("Enable Snow Cover", (bool*)&wsettings.EnableSnowCover);
		ImGui::Checkbox("Affect Foliage Color", (bool*)&settings.AffectFoliageColor);
		ImGui::SliderFloat("Snow Line Height Offset", &settings.SnowHeightOffset, -10000.0f, 10000.0f);
		ImGui::SliderFloat("Foliage Color Height Offset", &wsettings.FoliageHeightOffset, -10000.0f, 10000.0f);
		ImGui::SliderInt("Maximum Summer Month", (int*)&wsettings.MaxSummerMonth, 0, 11);
		ImGui::SliderInt("Maximum Winter Month", (int*)&wsettings.MaxWinterMonth, 0, 11);
		ImGui::SliderFloat("Summer Height Offset", &wsettings.SummerHeightOffset, -10000.0f, 10000.0f);
		ImGui::SliderFloat("Winter Height Offset", &wsettings.WinterHeightOffset, -10000.0f, 10000.0f);

		if (ImGui::TreeNodeEx("Snow Material", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("UV Scale", &wsettings.UVScale, 0.1f, 10.f, "%.1f");
			ImGui::SliderFloat("Screenspace Scale", &wsettings.ScreenSpaceScale, 0.f, 3.f, "%.3f");
			ImGui::SliderFloat("Log Microfacet Density", &wsettings.LogMicrofacetDensity, 0.f, 40.f, "%.3f");
			ImGui::SliderFloat("Microfacet Roughness", &wsettings.MicrofacetRoughness, 0.f, 1.f, "%.3f");
			ImGui::SliderFloat("Density Randomization", &wsettings.DensityRandomization, 0.f, 5.f, "%.3f");
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
	Reload();

	PerFrame data{};
	data.SnowAmount = 0;
	currentWeatherID = 0;
	uint32_t previousLastWeatherID = lastWeatherID;
	lastWeatherID = 0;
	float currentWeatherSnowing = 0.0f;
	float lastWeatherSnowing = 0.0f;
	float weatherTransitionPercentage = previousWeatherTransitionPercentage;

	if (wsettings.EnableSnowCover) {
		if (auto sky = RE::Sky::GetSingleton()) {
			data.Sky = static_cast<uint>(sky->mode.get());
			if (sky->mode.get() == RE::Sky::Mode::kFull) {
				if (auto currentWeather = sky->currentWeather) {
					if (currentWeather->precipitationData && currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
						float particleDensity = currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
						float particleGravity = currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity).f;
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
									float particleDensity = currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
									float particleGravity = currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity).f;
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
	data.wsettings = wsettings;

	return data;
}

void SnowCover::SetupResources()
{
	Reload();
}

std::string wstrtostr(std::wstring& wide)
{
	std::string str(wide.length(), 0);
	std::transform(wide.begin(), wide.end(), str.begin(), [](wchar_t c) {
		return (char)c;
	});
	return str;
}


std::wstring strtowstr(std::string& wide)
{
	std::wstring str(wide.length(), 0);
	std::transform(wide.begin(), wide.end(), str.begin(), [](char c) {
		return (wchar_t)c;
	});
	return str;
}

void SnowCover::Reload()
{
	std::string curr_worldspace = "none";
	auto tes = globals::game::tes;
	if (tes) {
		auto worldspace = tes->GetRuntimeData2().worldSpace;
		if (worldspace) {
			curr_worldspace = worldspace->GetFormEditorID();
		}
	}
	if (curr_worldspace == last_worldspace)
		return;
	last_worldspace = curr_worldspace;
	auto path = std::string("Data\\Shaders\\SnowCover\\") + curr_worldspace + ".json";
	if (!std::filesystem::exists(path)) {
		wsettings.EnableSnowCover = false;
		return;
	}
	std::ifstream fileStream(path);
	if (!fileStream.is_open()) {
		logger::error("[Snow Cover] Cannot open config at  {}", path);
		return;
	}
	json config;
	try {
		fileStream >> config;
		wsettings.EnableSnowCover = true;
		wsettings.FoliageHeightOffset = config["FoliageHeightOffset"];
		wsettings.UVScale = config["UVScale"];
		wsettings.MaxSummerMonth = config["MaxSummerMonth"];
		wsettings.MaxWinterMonth = config["MaxWinterMonth"];
		wsettings.SummerHeightOffset = config["SummerHeightOffset"];
		wsettings.WinterHeightOffset = config["WinterHeightOffset"];
		for (auto i = 0; i < 9; ++i)
			wsettings.Equation[i] = config["Equation"][i];
		wsettings.ScreenSpaceScale = config["ScreenSpaceScale"];
		wsettings.LogMicrofacetDensity = config["LogMicrofacetDensity"];
		wsettings.MicrofacetRoughness = config["MicrofacetRoughness"];
		wsettings.DensityRandomization = config["DensityRandomization"];
		wsettings.MainTint = float4(config["MainTint"][0], config["MainTint"][1], config["MainTint"][2], config["MainTint"][3]);
		wsettings.AltTint = float4(config["AltTint"][0], config["AltTint"][1], config["AltTint"][2], config["AltTint"][3]);

		std::string str_tname = config["MainTexture"];
		std::wstring tname = strtowstr(str_tname);

		auto device = globals::d3d::device;
		auto context = globals::d3d::context;
		if (views[0])
			views[0]->Release();
		if (views[1])
			views[1]->Release();
		HRESULT hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\Shaders\\SnowCover\\") + tname + L"_rdao.dds").c_str(), nullptr, &views.at(0));
		if (hr != S_OK)
			logger::warn("Snow Cover: Error loading {}_rdao.dds texture: {}", str_tname, hr);
		hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\Shaders\\SnowCover\\") + tname + L"_n.dds").c_str(), nullptr, &views.at(1));
		if (hr != S_OK)
			logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", str_tname, hr);
		if (config.contains("AltTexture")) {
			if (views[2])
				views[2]->Release();
			if (views[3])
				views[3]->Release();
			std::string str_altname = config["AltTexture"];
			std::wstring altname = strtowstr(str_altname);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\Shaders\\SnowCover\\") + altname + L"_rdao.dds").c_str(), nullptr, &views.at(2));
			if (hr != S_OK)
				logger::warn("Snow Cover: Error loading {}_rdao.dds texture: {}", str_altname, hr);
			hr = DirectX::CreateDDSTextureFromFile(device, context, (std::wstring(L"Data\\Shaders\\SnowCover\\") + altname + L"_n.dds").c_str(), nullptr, &views.at(3));
			if (hr != S_OK)
				logger::warn("Snow Cover: Error loading {}_n.dds texture: {}", str_altname, hr);

		} else {
			views[2] = views[0];
			views[3] = views[1];
		}

	} catch (const nlohmann::json::parse_error& e) {
		logger::error("[Snow Cover] failed to parse {} : {}", path, e.what());
		wsettings = WorldSettings{};
		wsettings.EnableSnowCover = false;
		return;
	} catch (const nlohmann::json::exception e) {
		logger::error("[Snow Cover] failed to parse {} : {}", path, e.what());
		wsettings = WorldSettings{};
		wsettings.EnableSnowCover = false;
		return;
	}
}

void SnowCover::Prepass()
{
	if (globals::features::snowCover->wsettings.EnableSnowCover) {
		auto context = globals::d3d::context;
		context->PSSetShaderResources(73, (uint)views.size(), views.data());
	}
}

void SnowCover::Reset()
{
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

void SnowCover::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	if (globals::features::snowCover->wsettings.EnableSnowCover) {
		globals::features::snowCover->BSLightingShader_Setup(Pass);
	}
	func(This, Pass, RenderFlags);
}

void SnowCover::BSLightingShader_Setup(RE::BSRenderPass* a_pass)
{
	auto state = globals::state;
	auto userData = a_pass->geometry->GetUserData();
	if (!userData)
		return;
	if (userData && userData->CanBeMoved() && !userData->As<RE::Actor>())
		state->currentExtraDescriptor |= (uint)State::ExtraShaderDescriptors::IsMobile;
}