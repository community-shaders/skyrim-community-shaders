#include "WetnessEffects.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WetnessEffects::Settings,
	EnableWetnessEffects,
	MaxRainWetness,
	MaxPuddleWetness,
	MaxShoreWetness,
	ShoreRange,
	PuddleRadius,
	PuddleMaxAngle,
	PuddleMinWetness,
	MinRainWetness,
	SkinWetness,
	WeatherTransitionSpeed,
	EnableRaindropFx,
	EnableSplashes,
	EnableRipples,
	RaindropGridSize,
	RaindropInterval,
	RaindropChance,
	SplashesLifetime,
	SplashesStrength,
	SplashesMinRadius,
	SplashesMaxRadius,
	RippleStrength,
	RippleRadius,
	RippleBreadth,
	RippleLifetime)

void WetnessEffects::DrawSettings()
{
	if (ImGui::TreeNodeEx("Wetness Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Wetness", (bool*)&settings.EnableWetnessEffects);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables a wetness effect near water and when it is raining.");
		}

		ImGui::SliderFloat("Rain Wetness", &settings.MaxRainWetness, 0.0f, 1.0f);
		ImGui::SliderFloat("Puddle Wetness", &settings.MaxPuddleWetness, 0.0f, 4.0f);
		ImGui::SliderFloat("Shore Wetness", &settings.MaxShoreWetness, 0.0f, 1.0f);
		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Raindrop Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Raindrop Effects", (bool*)&settings.EnableRaindropFx);

		ImGui::BeginDisabled(!settings.EnableRaindropFx);

		ImGui::Checkbox("Enable Splashes", (bool*)&settings.EnableSplashes);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Enables small splashes of wetness on dry surfaces.");
		ImGui::Checkbox("Enable Ripples", (bool*)&settings.EnableRipples);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Enables circular ripples on puddles, and to a less extent other wet surfaces");

		if (ImGui::TreeNodeEx("Raindrops")) {
			ImGui::BulletText(
				"At every interval, a raindrop is placed within each grid cell.\n"
				"Only a set portion of raindrops will actually trigger splashes and ripples.\n");

			ImGui::SliderFloat("Grid Size", &settings.RaindropGridSize, 1.f, 10.f, "%.1f game unit(s)");
			ImGui::SliderFloat("Interval", &settings.RaindropInterval, 0.1f, 2.f, "%.1f sec");
			ImGui::SliderFloat("Chance", &settings.RaindropChance, 0.0f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Portion of raindrops that will actually cause splashes and ripples.");
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Splashes")) {
			ImGui::SliderFloat("Strength", &settings.SplashesStrength, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat("Min Radius", &settings.SplashesMinRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("As portion of grid size.");
			ImGui::SliderFloat("Max Radius", &settings.SplashesMaxRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("As portion of grid size.");
			ImGui::SliderFloat("Lifetime", &settings.SplashesLifetime, 0.1f, 20.f, "%.1f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Ripples")) {
			ImGui::SliderFloat("Strength", &settings.RippleStrength, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat("Radius", &settings.RippleRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("As portion of grid size.");
			ImGui::SliderFloat("Breadth", &settings.RippleBreadth, 0.f, 1.f, "%.2f");
			ImGui::SliderFloat("Lifetime", &settings.RippleLifetime, 0.f, settings.RaindropInterval, "%.2f sec", ImGuiSliderFlags_AlwaysClamp);
			ImGui::TreePop();
		}

		ImGui::EndDisabled();

		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Weather transition speed", &settings.WeatherTransitionSpeed, 0.5f, 5.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"How fast wetness appears when raining and how quickly it dries "
				"after rain has stopped. ");
		}

		ImGui::SliderFloat("Min Rain Wetness", &settings.MinRainWetness, 0.0f, 0.9f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"The minimum amount an object gets wet from rain. ");
		}

		ImGui::SliderFloat("Skin Wetness", &settings.SkinWetness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"How wet character skin and hair get during rain. ");
		}

		ImGui::SliderInt("Shore Range", (int*)&settings.ShoreRange, 1, 64);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"The maximum distance from a body of water that Shore Wetness affects. ");
		}

		ImGui::SliderFloat("Puddle Radius", &settings.PuddleRadius, 0.3f, 3.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"The radius that is used to determine puddle size and location. ");
		}

		ImGui::SliderFloat("Puddle Max Angle", &settings.PuddleMaxAngle, 0.6f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"How flat a surface needs to be for puddles to form on it. ");
		}

		ImGui::SliderFloat("Puddle Min Wetness", &settings.PuddleMinWetness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"The wetness value at which puddles start to form. ");
		}

		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();
}

WetnessEffects::PerFrame WetnessEffects::GetCommonBufferData()
{
	PerFrame data{};

	data.Raining = 0.0f;
	data.Wetness = 0.0f;
	data.PuddleWetness = 0.0f;

	if (settings.EnableWetnessEffects) {
		if (auto sky = globals::game::sky) {
			if (sky->mode.get() == RE::Sky::Mode::kFull) {
				if (auto precip = sky->precip) {
					float currentRaining = 0.0f;
					float lastRaining = 0.0f;

					{
						auto precipObject = precip->currentPrecip;
						if (!precipObject) {
							precipObject = precip->lastPrecip;
						}
						if (precipObject) {
							auto& effect = precipObject->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect];
							auto shaderProp = netimmerse_cast<RE::BSShaderProperty*>(effect.get());
							auto particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProp);
							auto rain = (RE::BSParticleShaderRainEmitter*)(particleShaderProperty->particleEmitter);
							data.OcclusionViewProj = rain->occlusionProjection;
						}
					}

					if (precip->currentPrecip && sky->currentWeather && sky->currentWeather->precipitationData) {
						auto& precipObject = precip->currentPrecip;
						auto weather = sky->currentWeather;

						auto& effect = precipObject->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect];
						auto shaderProp = netimmerse_cast<RE::BSShaderProperty*>(effect.get());
						auto particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProp);

						auto rain = (RE::BSParticleShaderRainEmitter*)(particleShaderProperty->particleEmitter);
						if (rain->emitterType.any(RE::BSParticleShaderEmitter::EMITTER_TYPE::kRain)) {
							auto maxDensity = weather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
							if (maxDensity > 0.0f)
								currentRaining = rain->density / maxDensity;
						}
					}

					if (precip->lastPrecip && sky->lastWeather && sky->lastWeather->precipitationData) {
						auto& precipObject = precip->lastPrecip;
						auto weather = sky->lastWeather;

						auto& effect = precipObject->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect];
						auto shaderProp = netimmerse_cast<RE::BSShaderProperty*>(effect.get());
						auto particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProp);
						auto rain = (RE::BSParticleShaderRainEmitter*)(particleShaderProperty->particleEmitter);
						if (rain->emitterType.any(RE::BSParticleShaderEmitter::EMITTER_TYPE::kRain)) {
							auto maxDensity = weather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
							if (maxDensity > 0.0f)
								lastRaining = rain->density / maxDensity;
						}
					}

					data.Raining = std::min(1.0f, currentRaining + lastRaining);
				}

				auto linearstep = [](float edge0, float edge1, float x) {
					return std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
				};

				float wetnessCurrentWeather = 0.0f;
				float puddleCurrentWeather = 0.0f;

				if (sky->currentWeather && sky->currentWeather->precipitationData && sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
					wetnessCurrentWeather = linearstep(255.0f + (float)sky->currentWeather->data.precipitationBeginFadeIn, 255, sky->currentWeatherPct * 255);
					puddleCurrentWeather = pow(wetnessCurrentWeather, 2.0f);
				}

				float wetnessLastWeather = 0.0f;
				float puddleLastWeather = 0.0f;

				if (sky->lastWeather && sky->lastWeather->precipitationData && sky->lastWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
					wetnessLastWeather = 1.0f - linearstep((float)sky->lastWeather->data.precipitationEndFadeOut, 255, sky->currentWeatherPct * 255);
					puddleLastWeather = pow(std::max(wetnessLastWeather, 1.0f - sky->currentWeatherPct), 0.25f);
				}

				float wetness = std::min(1.0f, wetnessCurrentWeather + wetnessLastWeather);
				float puddleWetness = std::min(1.0f, puddleCurrentWeather + puddleLastWeather);

				data.Wetness = wetness;
				data.PuddleWetness = puddleWetness;
			}
		}
	}

	static size_t rainTimer = 0;  // size_t for precision
	if (!globals::game::ui->GameIsPaused())
		rainTimer += (size_t)(RE::GetSecondsSinceLastFrame() * 1000);  // BSTimer::delta is always 0 for some reason
	data.Time = rainTimer / 1000.f;

	data.settings = settings;
	// Disable Shore Wetness if Wetness Effects are Disabled
	data.settings.MaxShoreWetness = settings.EnableWetnessEffects ? settings.MaxShoreWetness : 0.0f;

	// Calculating some parameters on cpu
	data.settings.RaindropChance *= data.Raining * data.Raining;
	data.settings.RaindropGridSize = 1.0f / settings.RaindropGridSize;
	data.settings.RaindropInterval = 1.0f / settings.RaindropInterval;
	data.settings.RippleLifetime = settings.RaindropInterval / settings.RippleLifetime;

	return data;
}

void WetnessEffects::Prepass()
{
	static auto renderer = globals::game::renderer;
	static auto& precipOcclusionTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];

	auto context = globals::d3d::context;

	context->PSSetShaderResources(70, 1, &precipOcclusionTexture.depthSRV);
}

void WetnessEffects::LoadSettings(json& o_json)
{
	settings = o_json;
}

void WetnessEffects::SaveSettings(json& o_json)
{
	o_json = settings;
}

void WetnessEffects::RestoreDefaultSettings()
{
	settings = {};
}