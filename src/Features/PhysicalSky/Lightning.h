#pragma once

#include "Buffer.h"
#include "LightningPulse.h"

#include <atomic>

struct PhysicalSky;

/// One active discharge and one pending cloud source; lighting and thunder use only the active one.
class SkyLightning
{
public:
	struct Segment
	{
		float4 start;  // absolute game units, channel radius in w
		float4 end;    // absolute game units, relative energy in w
	};
	static_assert(sizeof(Segment) == 32);
	static constexpr uint32_t kCandidates = 32;
	static constexpr uint32_t kMaxSegments = 128;

	/// Installs the SE/AE weather-sound filter; unrelated weather sounds keep their normal path.
	static void InstallHooks();
	/// Creates the candidate readback and immutable-per-discharge channel upload.
	void SetupResources();
	/// Advances the pause-aware event and fills Physical Sky's shared shader data.
	void Update(PhysicalSky& sky, const std::string& worldspace, bool available);
	/// Submits a density-only placement query using the cloud renderer's current resources.
	void QuerySource(ID3D11ComputeShader* shader);
	/// Counts successful cloud updates, rather than display frames, for mask preparation.
	void CloudFrameComplete(bool historyValid);
	/// Replays the last channel seed for visual comparisons, without waiting for weather.
	void Replay();
	/// Searches for a new discharge using the current distance, size and type settings.
	void Trigger();
	/// Reports ownership even between flashes, so vanilla cannot start a second scheduler.
	bool OwnsWeather() const { return ownsWeather.load(std::memory_order_relaxed); }
	/// Clears an existing vanilla flash and stops a thunder sound already playing at takeover.
	void SuppressVanilla(RE::Sky* sky) const;
	ID3D11ShaderResourceView* Segments() const { return segments ? segments->SRV() : nullptr; }
	bool HasSource() const { return phase == Phase::Preparing || phase == Phase::Flashing; }

private:
	enum class Phase
	{
		Idle,
		Query,
		Readback,
		Preparing,
		Flashing
	};
	struct Thunder
	{
		RE::BSSoundHandle sound;
		RE::FormID descriptor = 0;
		RE::NiPoint3 position;
		double time = 0;
		float volume = 1.f;
		bool played = false;
	};
	std::atomic_bool ownsWeather = false;
	Phase phase = Phase::Idle;
	// The pending source owns the other mask lane, but never contributes light or sound.
	Phase pendingPhase = Phase::Idle;
	float4 pendingPositionRadius = {};
	float pendingHeight = 0, pendingChannelRadius = 0.12f;
	float2 pendingDistanceRange = {};
	bool pendingGrounded = false, pendingCloudBase = false;
	uint32_t pendingSeed = 0, pendingFrames = 0, activeLane = 0;
	std::string worldspace;
	uint32_t randomState = 0x82a1f37bu, eventSeed = 0, generation = 0, preparedFrames = 0;
	double time = 0, previousTime = 0, nextEvent = 0, flashStart = 0, queryStart = 0;
	double eventHazard = 0, targetHazard = 1;
	float sourceHeight = 0;
	float3 source = {}, ground = {}, color = { 0.78f, 0.82f, 1.f };
	float radius = 0;
	float channelRadius = 0.12f;
	float4 channelEmitter = {};
	float2 distanceRange = {};
	LightningPulse::Parameters pulseParameters;
	float lastPulse = 0;
	bool replay = false, grounded = false, cloudBase = false;
	std::vector<Segment> channel;
	std::vector<Thunder> thunder;
	eastl::unique_ptr<Buffer> candidates;
	eastl::unique_ptr<StructuredBuffer> segments;
	winrt::com_ptr<ID3D11Buffer> readback;

	float Random();
	void Cancel();
	void ReadSource(PhysicalSky& sky);
	void PromoteSource(PhysicalSky& sky);
	void BuildChannel(float bottomZ);
	void StartFlash(const PhysicalSky& physicalSky);
};
