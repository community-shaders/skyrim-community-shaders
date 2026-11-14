#include "Features/PhysicalSky.h"

#include "Util.h"

#include <numbers>

namespace
{
	// The engine's SkySound is a sound handle followed by its weather and SNAM data.
	// AE 1.6.1170 layout; the play entry is resolved for both SE and AE.
	struct WeatherSound
	{
		RE::BSSoundHandle handle;
		uint32_t padding;
		RE::TESWeather* weather;
		uint32_t type;
		RE::FormID descriptor;
		uint32_t nextTick, flags;
	};
	static_assert(offsetof(WeatherSound, type) == 0x18 && sizeof(WeatherSound) == 0x28);

	struct PlayWeatherSound
	{
		static void thunk(WeatherSound* sound, float volume)
		{
			if (sound && sound->type == 3 && globals::features::physicalSky.lightning.OwnsWeather()) {
				if (sound->handle.IsPlaying())
					sound->handle.Stop();
				return;
			}
			func(sound, volume);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void PhysicalSky::Settings::SanitizeLightning()
{
	const auto finiteClamp = [](float value, float low, float high, float fallback) {
		return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
	};
	lightningIntensity = finiteClamp(lightningIntensity, 0.f, 8.f, 0.f);
	lightning.rate = finiteClamp(lightning.rate, 0.f, 600.f, 3.f);
	lightning.distanceRange.x = finiteClamp(lightning.distanceRange.x, 0.05f, 50.f, 0.35f);
	lightning.distanceRange.y = finiteClamp(lightning.distanceRange.y, lightning.distanceRange.x, 50.f, 7.f);
	lightning.distanceRange.y = std::max(lightning.distanceRange.x, lightning.distanceRange.y);
	lightning.radius = finiteClamp(lightning.radius, 10.f, 5000.f, 600.f);
	lightning.cloudLuminance = finiteClamp(lightning.cloudLuminance, 0.f, 1.0e9f, 1000.f);
	lightning.channelLuminance = finiteClamp(lightning.channelLuminance, 0.f, 1.0e9f, 1.0e7f);
	lightning.channelRadius = finiteClamp(lightning.channelRadius, 0.001f, 5.f, 0.12f);
	lightning.strokeCount = std::clamp(lightning.strokeCount, 1, 1024);
	lightning.strokeDuration = finiteClamp(lightning.strokeDuration, 1.f, 60000.f, 20.f);
	lightning.strokeInterval = finiteClamp(lightning.strokeInterval, lightning.strokeDuration, 60000.f, 90.f);
	lightning.strokeInterval = std::max(lightning.strokeDuration, lightning.strokeInterval);
	lightning.strokeDecay = finiteClamp(lightning.strokeDecay, 0.f, 1.f, 0.65f);
	lightning.groundChance = finiteClamp(lightning.groundChance, 0.f, 100.f, 35.f);
	lightning.cloudBaseChance = finiteClamp(lightning.cloudBaseChance, 0.f, 100.f - lightning.groundChance, 0.f);
	lightning.color.x = finiteClamp(lightning.color.x, 0.f, 1.f, 0.86f);
	lightning.color.y = finiteClamp(lightning.color.y, 0.f, 1.f, 0.9f);
	lightning.color.z = finiteClamp(lightning.color.z, 0.f, 1.f, 1.f);
}

void SkyLightning::InstallHooks()
{
	stl::detour_thunk<PlayWeatherSound>(REL::RelocationID(25673, 26217));
}

void SkyLightning::SetupResources()
{
	candidates = eastl::make_unique<Buffer>(StructuredBufferDesc<float4>(kCandidates, true, false), nullptr, "PhysicalSky::LightningCandidates");
	D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
	view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	view.Buffer.NumElements = kCandidates;
	candidates->CreateUAV(view);
	segments = eastl::make_unique<StructuredBuffer>(StructuredBufferDesc<Segment>(kMaxSegments, false, true), kMaxSegments, "PhysicalSky::LightningSegments");
	segments->CreateSRV();
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = sizeof(float4) * kCandidates;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	readback = nullptr;
	DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&desc, nullptr, readback.put()));
	Util::SetResourceName(readback.get(), "PhysicalSky::LightningCandidateReadback");
	Cancel();
	channel.clear();
	sourceHeight = 0.f;
	radius = 0.f;
}

float SkyLightning::Random()
{
	randomState ^= randomState << 13;
	randomState ^= randomState >> 17;
	randomState ^= randomState << 5;
	return (randomState >> 8) * (1.f / 16777216.f);
}

void SkyLightning::Cancel()
{
	for (auto& sound : thunder)
		if (sound.played && sound.sound.IsValid())
			sound.sound.Stop();
	thunder.clear();
	phase = Phase::Idle;
	pendingPhase = Phase::Idle;
	pendingPositionRadius = {};
	pendingFrames = 0;
	lastPulse = 0;
	preparedFrames = 0;
	replay = false;
	nextEvent = time;
	eventHazard = 0;
	targetHazard = -std::log(std::max(1.0 - Random(), 1.0e-7));
}

void SkyLightning::Replay()
{
	Cancel();
	replay = true;
	nextEvent = time;
	if (!channel.empty() && sourceHeight > 0.f && OwnsWeather()) {
		++generation;
		phase = Phase::Preparing;
	}
}

void SkyLightning::Trigger()
{
	Cancel();
	eventSeed = 0;
	replay = true;
	nextEvent = time;
}

void SkyLightning::SuppressVanilla(RE::Sky* sky) const
{
	if (!OwnsWeather() || !sky)
		return;
	sky->flash = 0.f;
	if (sky->skySoundList)
		for (auto* entry : *sky->skySoundList) {
			auto* sound = reinterpret_cast<WeatherSound*>(entry);
			if (sound && sound->type == 3 && sound->handle.IsPlaying())
				sound->handle.Stop();
		}
}

void SkyLightning::QuerySource(ID3D11ComputeShader* shader)
{
	if (!OwnsWeather() || pendingPhase != Phase::Query || !shader)
		return;
	auto* context = globals::d3d::context;
	auto* output = candidates->uav.get();
	context->CSSetUnorderedAccessViews(4, 1, &output, nullptr);
	context->CSSetShader(shader, nullptr, 0);
	context->Dispatch(1, 1, 1);
	output = nullptr;
	context->CSSetUnorderedAccessViews(4, 1, &output, nullptr);
	context->CopyResource(readback.get(), candidates->resource.get());
	pendingPhase = Phase::Readback;
	queryStart = time;
}

void SkyLightning::ReadSource(PhysicalSky& sky)
{
	D3D11_MAPPED_SUBRESOURCE mapped{};
	auto* context = globals::d3d::context;
	const HRESULT result = context->Map(readback.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
	if (result == DXGI_ERROR_WAS_STILL_DRAWING)
		return;
	if (FAILED(result)) {
		pendingPhase = Phase::Idle;
		nextEvent = time + 0.1;
		return;
	}
	const auto* positions = static_cast<const float4*>(mapped.pData);
	float4 selected{};
	for (uint32_t i = 0; i < kCandidates; ++i)
		if (positions[i].w > 0.f) {
			selected = positions[i];
			break;
		}
	context->Unmap(readback.get(), 0);
	if (selected.w <= 0.f) {
		pendingPhase = Phase::Idle;
		nextEvent = time + std::clamp(60.0 / std::max(sky.settings.lightning.rate, 1.f), 0.1, 3.0);
		return;
	}
	pendingPositionRadius.x = selected.x;
	pendingPositionRadius.y = selected.y;
	pendingPositionRadius.z = selected.z + sky.cbData.zBottom;
	pendingHeight = selected.w;
	pendingFrames = 0;
	pendingPhase = Phase::Preparing;
}

void SkyLightning::PromoteSource(PhysicalSky& sky)
{
	source = { pendingPositionRadius.x, pendingPositionRadius.y, pendingPositionRadius.z };
	radius = pendingPositionRadius.w;
	sourceHeight = pendingHeight;
	channelRadius = pendingChannelRadius;
	distanceRange = pendingDistanceRange;
	grounded = pendingGrounded;
	cloudBase = pendingCloudBase;
	eventSeed = pendingSeed;
	randomState = eventSeed;
	BuildChannel(sky.cbData.zBottom);
	std::array<Segment, kMaxSegments> upload{};
	std::copy(channel.begin(), channel.end(), upload.begin());
	segments->Update(upload.data(), sizeof(upload));
	activeLane ^= 1u;
	preparedFrames = pendingFrames;
	pendingPhase = Phase::Idle;
	pendingPositionRadius = {};
	pendingFrames = 0;
	phase = Phase::Preparing;
}

void SkyLightning::BuildChannel(float bottomZ)
{
	const float units = 1.f / Util::Units::GAME_UNIT_TO_M;
	ground = source;
	ground.z = std::max(bottomZ, source.z - sourceHeight * 0.6f * units);
	if (grounded) {
		grounded = false;
		if (auto* tes = RE::TES::GetSingleton()) {
			for (uint32_t attempt = 0; attempt < 8 && !grounded; ++attempt) {
				ground.x = source.x + (Random() - 0.5f) * radius;
				ground.y = source.y + (Random() - 0.5f) * radius;
				RE::bhkPickData pick{};
				const float havok = RE::bhkWorld::GetWorldScale();
				pick.rayInput.from = RE::hkVector4(ground.x * havok, ground.y * havok, source.z * havok, 0.f);
				pick.rayInput.to = RE::hkVector4(ground.x * havok, ground.y * havok, (bottomZ - 500.f * units) * havok, 0.f);
				tes->Pick(pick);
				float terrainHeight = 0.f;
				if (pick.rayOutput.rootCollidable) {
					ground.z = std::lerp(source.z, bottomZ - 500.f * units, pick.rayOutput.hitFraction);
					grounded = true;
				} else if (tes->GetLandHeight({ ground.x, ground.y, source.z }, terrainHeight) && std::isfinite(terrainHeight) && terrainHeight < source.z) {
					ground.z = terrainHeight;
					grounded = true;
				}
			}
		}
	}
	if (!grounded) {
		const float angle = Random() * 2.f * std::numbers::pi_v<float>;
		ground = source + float3(std::cos(angle), std::sin(angle), (Random() - 0.5f) * 0.2f) * radius * 1.8f;
	}
	channel.clear();
	const float3 direction = ground - source;
	const float3 axis = direction / std::max(direction.Length(), 1.f);
	float3 side = axis.Cross(std::abs(axis.z) < 0.9f ? float3(0, 0, 1) : float3(1, 0, 0));
	side /= std::max(side.Length(), 1.0e-6f);
	const float3 up = axis.Cross(side);
	// Correlated bends at several scales, with fixed attachment endpoints.
	constexpr uint32_t count = 64;
	std::array<float3, count + 1> path{};
	path.front() = source;
	path.back() = ground;
	for (uint32_t span = count; span >= 2; span /= 2) {
		const float amplitude = radius * 0.18f * std::pow(static_cast<float>(span) / count, 0.65f);
		for (uint32_t first = 0; first < count; first += span)
			path[first + span / 2] = (path[first] + path[first + span]) * 0.5f +
			                         side * ((Random() - 0.5f) * amplitude) + up * ((Random() - 0.5f) * amplitude);
	}
	float3 emitterCenter{};
	float projectedArea = 0.f;
	float3 previous = source;
	uint32_t branches = 0;
	for (uint32_t i = 1; i <= count; ++i) {
		const float3 position = path[i];
		channel.push_back({ float4(previous.x, previous.y, previous.z, channelRadius * units), float4(position.x, position.y, position.z, 1.f) });
		// Broadside projected area of the luminous main channel; branches are visual detail.
		const float area = 2.f * channelRadius * (position - previous).Length() * Util::Units::GAME_UNIT_TO_M;
		emitterCenter += (position + previous) * (0.5f * area);
		projectedArea += area;
		if (i > 3 && i < 50 && branches < 8 && Random() < 0.16f) {
			++branches;
			float3 branch = position;
			const float3 branchDirection = (axis * (0.3f + Random() * 0.5f) + side * (Random() - 0.5f) + up * (Random() - 0.5f)) * radius;
			for (uint32_t j = 1; j <= 7; ++j) {
				const float f = static_cast<float>(j) / 7;
				float3 end = position + branchDirection * f + (side * (Random() - 0.5f) + up * (Random() - 0.5f)) * radius * 0.035f;
				if (grounded)
					end.z = std::max(end.z, ground.z);
				channel.push_back({ float4(branch.x, branch.y, branch.z, channelRadius * 0.5f * units), float4(end.x, end.y, end.z, 0.35f * (1.f - f * 0.7f)) });
				branch = end;
			}
		}
		previous = position;
	}
	const auto center = projectedArea > 0.f ? emitterCenter / projectedArea : source;
	channelEmitter = { center.x, center.y, center.z, std::sqrt(projectedArea / std::numbers::pi_v<float>) * units };
}

void SkyLightning::StartFlash(const PhysicalSky& physicalSky)
{
	const auto& settings = physicalSky.settings;
	pulseParameters = { static_cast<unsigned>(settings.lightning.strokeCount), settings.lightning.strokeDuration * 0.001,
		settings.lightning.strokeInterval * 0.001, settings.lightning.strokeDecay };
	flashStart = time;
	phase = Phase::Flashing;
	replay = false;
	eventHazard = 0;
	targetHazard = -std::log(std::max(1.0 - Random(), 1.0e-7));
	Thunder sound;
	// Reuse the generic thunder descriptor without consulting vanilla weather.
	sound.descriptor = 0xE7522u;
	const float3 point = grounded ? ground : source;
	sound.position = { point.x, point.y, point.z };
	const auto eye = globals::game::frameBufferCached.GetCameraPosAdjust();
	const float distance = (point - float3(eye.x, eye.y, eye.z)).Length() * Util::Units::GAME_UNIT_TO_M;
	sound.time = time + distance / 343.0;
	sound.volume = std::clamp(std::sqrt(settings.lightningIntensity), 0.f, 1.f);
	thunder.push_back(sound);
}

void SkyLightning::CloudFrameComplete(bool historyValid)
{
	if (HasSource())
		preparedFrames = historyValid ? preparedFrames + 1u : 1u;
	if (pendingPhase == Phase::Preparing)
		pendingFrames = historyValid ? pendingFrames + 1u : 1u;
}

void SkyLightning::Update(PhysicalSky& physicalSky, const std::string& currentWorldspace, bool available)
{
	physicalSky.settings.SanitizeLightning();
	const auto& settings = physicalSky.settings;
	const float intensity = settings.lightningIntensity;
	// Ownership follows the configured override, not source/shader readiness.
	// A zero rate or a missing cloud must not bring vanilla lightning back.
	const bool overrideWeather = physicalSky.loaded && settings.enabled && intensity > 0.f;
	available &= overrideWeather && physicalSky.settings.enableVolumetricClouds && candidates && segments && readback && PlayWeatherSound::func.address() != 0;
	if (!available || worldspace != currentWorldspace) {
		Cancel();
		channel.clear();
		sourceHeight = 0.f;
		radius = 0.f;
		worldspace = currentWorldspace;
	}
	ownsWeather.store(overrideWeather, std::memory_order_relaxed);
	auto& data = physicalSky.cbData;
	data.lightningPositionRadius = {};
	data.lightningEndPosition = {};
	data.lightningColorIntensity = {};
	data.lightningChannelEmitter = {};
	data.lightningPreparationPositionRadius = {};
	data.lightningPreparationControl = { 0.f, 0.f, static_cast<float>(activeLane), 0.f };
	data.lightningPhotometry = { settings.lightning.cloudLuminance, settings.lightning.channelLuminance, channelRadius, cloudBase ? 1.f : 0.f };
	data.lightningPlacement = { pendingDistanceRange.x, pendingDistanceRange.y, static_cast<float>(generation), pendingCloudBase ? 1.f : 0.f };
	data.lightningControl = { static_cast<float>(generation), 0.f, 0.f, overrideWeather ? 1.f : 0.f };
	if (!available)
		return;
	previousTime = time;
	if (auto* ui = globals::game::ui; ui && !ui->GameIsPaused()) {
		const double elapsed = RE::GetSecondsSinceLastFrame();
		if (std::isfinite(elapsed))
			time += std::clamp(elapsed, 0.0, 0.25);
	}
	color = settings.lightning.color;
	if (pendingPhase == Phase::Readback) {
		ReadSource(physicalSky);
		if (pendingPhase == Phase::Readback && time - queryStart > 2.0) {
			pendingPhase = Phase::Idle;
			nextEvent = time + 3.0;
		}
	}
	// Count storm activity during a discharge too, so high rates can run back to back.
	if (time >= nextEvent)
		eventHazard += (time - previousTime) * settings.lightning.rate / 60.0;
	// Estimate the mask lead time from the current frame interval. Preparing only
	// near the end of a long burst avoids holding a source while its cloud moves away.
	const double leadTime = std::clamp((time - previousTime) * 22.0, 0.1, 5.5);
	const double remaining = phase == Phase::Flashing ? std::max(0.0, LightningPulse::Duration(pulseParameters) - (time - flashStart)) : 0.0;
	const double rate = settings.lightning.rate / 60.0;
	const bool automatic = settings.lightning.rate > 0.f;
	if (!automatic && !replay) {
		pendingPhase = Phase::Idle;
		pendingPositionRadius = {};
		pendingFrames = 0;
	}
	if (time > previousTime && pendingPhase == Phase::Idle && time >= nextEvent &&
		(phase == Phase::Idle || phase == Phase::Flashing) && remaining <= leadTime &&
		(replay || (automatic && eventHazard + rate * leadTime >= targetHazard))) {
		const float kind = Random() * 100.f;
		pendingGrounded = kind < settings.lightning.groundChance;
		pendingCloudBase = kind < settings.lightning.groundChance + settings.lightning.cloudBaseChance;
		pendingPositionRadius = { 0.f, 0.f, 0.f, settings.lightning.radius / Util::Units::GAME_UNIT_TO_M };
		pendingChannelRadius = settings.lightning.channelRadius;
		pendingDistanceRange = settings.lightning.distanceRange / Util::Units::GAME_UNIT_TO_KM;
		pendingSeed = randomState;
		pendingFrames = 0;
		++generation;
		pendingPhase = Phase::Query;
	}
	if (time > previousTime && phase == Phase::Idle && pendingPhase == Phase::Preparing && pendingFrames >= 20u &&
		(replay || (automatic && eventHazard >= targetHazard)))
		PromoteSource(physicalSky);
	if (time > previousTime && phase == Phase::Preparing && preparedFrames >= 20u)
		StartFlash(physicalSky);
	float pulse = 0.f;
	if (phase == Phase::Flashing) {
		if (time > previousTime)
			lastPulse = LightningPulse::Average(previousTime - flashStart, time - flashStart, pulseParameters);
		pulse = lastPulse * intensity;
		if (time - flashStart >= LightningPulse::Duration(pulseParameters) && lastPulse <= 0.f) {
			phase = Phase::Idle;
			replay = false;
		}
	}
	for (auto& sound : thunder) {
		if (!sound.played && time >= sound.time) {
			if (auto* descriptor = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(sound.descriptor))
				if (auto* audio = RE::BSAudioManager::GetSingleton(); audio && audio->GetSoundHandle(sound.sound, descriptor)) {
					sound.sound.SetPosition(sound.position);
					sound.sound.SetVolume(sound.volume);
					sound.sound.Play();
				}
			sound.played = true;
		}
	}
	std::erase_if(thunder, [&](const auto& sound) { return sound.played && time - sound.time > 1.0 && !sound.sound.IsPlaying(); });
	data.lightningControl = { static_cast<float>(generation), preparedFrames > 0u ? 1.f : 0.f, HasSource() ? static_cast<float>(channel.size()) : 0.f, 1.f };
	const bool preparing = pendingPhase == Phase::Preparing;
	data.lightningPreparationPositionRadius = preparing ? pendingPositionRadius : float4{};
	data.lightningPreparationControl = { pendingCloudBase ? 1.f : 0.f, pendingFrames > 0u ? 1.f : 0.f, static_cast<float>(activeLane), preparing ? 1.f : 0.f };
	data.lightningPhotometry = { settings.lightning.cloudLuminance, settings.lightning.channelLuminance, channelRadius, cloudBase ? 1.f : 0.f };
	data.lightningPlacement = { pendingDistanceRange.x, pendingDistanceRange.y, static_cast<float>(generation), pendingCloudBase ? 1.f : 0.f };
	if (HasSource()) {
		data.lightningPositionRadius = { source.x, source.y, source.z, radius };
		data.lightningEndPosition = { ground.x, ground.y, ground.z, 0.f };
		data.lightningChannelEmitter = channelEmitter;
	}
	data.lightningColorIntensity = { color.x, color.y, color.z, pulse };
}
