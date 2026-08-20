#pragma once

#include "StreamlineSdk.h"

#include <array>
#include <atomic>

/** Owns Reflex configuration, diagnostics, and DXVK pacing ownership state. */
class ReflexController
{
public:
	struct SleepSample
	{
		uint32_t frame = 0;
		uint64_t sleepUs = 0;
	};

	ReflexController();

	[[nodiscard]] bool TracePacing() const { return tracePacing; }
	[[nodiscard]] bool ForceOff() const { return forceOff; }
	[[nodiscard]] bool ForceUnlimited() const { return forceUnlimited; }
	[[nodiscard]] bool SkipSleep() const { return skipSleep; }
	sl::ReflexState& TraceState() { return traceState; }
	void LogPacingBatch(const std::array<SleepSample, 8>& a_samples, sl::Result a_stateResult) const;

	bool cacheValid = false;
	sl::ReflexMode cachedMode = sl::ReflexMode::eOff;
	uint32_t cachedFrameLimitUs = 0;
	std::atomic_bool dxvkOwnsPacing{ false };

private:
	bool tracePacing = false;
	bool forceOff = false;
	bool forceUnlimited = false;
	bool skipSleep = false;
	sl::ReflexState traceState{};
};
