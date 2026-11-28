#include "LoadWatchdog.h"

#include <condition_variable>

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr auto kStallWarnInterval = std::chrono::seconds(10);
	constexpr auto kPollInterval = std::chrono::seconds(1);

	struct Step
	{
		const char* name = nullptr;
		const char* hint = nullptr;
		Clock::time_point start{};
	};

	std::mutex stepMutex;
	Step currentStep;

	Step GetStep()
	{
		std::scoped_lock lock(stepMutex);
		return currentStep;
	}

	void StoreStep(const Step& step)
	{
		std::scoped_lock lock(stepMutex);
		currentStep = step;
	}

	/** @brief Warns every kStallWarnInterval that the same step stays active, until stop is requested. */
	void Watch(const std::stop_token& stop)
	{
		std::mutex waitMutex;
		std::condition_variable_any wake;
		std::unique_lock lock(waitMutex);

		Clock::time_point watchedStart{};
		auto nextWarn = kStallWarnInterval;

		while (!stop.stop_requested()) {
			wake.wait_for(lock, stop, kPollInterval, [] { return false; });

			const auto step = GetStep();
			if (stop.stop_requested() || !step.name)
				continue;

			if (step.start != watchedStart) {
				watchedStart = step.start;
				nextWarn = kStallWarnInterval;
			}

			const auto elapsed = Clock::now() - step.start;
			if (elapsed < nextWarn)
				continue;

			nextWarn += kStallWarnInterval;
			logger::warn("[Unified Water] Still {} after {} s; the game is blocked until it finishes. Likely cause: {}",
				step.name, std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), step.hint);
		}
	}
}

LoadWatchdog::LoadWatchdog()
{
	// Steps set while no watchdog was running (menu-triggered rebuilds) would read as stalled
	StoreStep({});
	thread = std::jthread(Watch);
}

LoadWatchdog::~LoadWatchdog()
{
	StoreStep({});
}

void LoadWatchdog::SetStep(const char* step, const char* hint)
{
	assert(step && hint);
	logger::info("[Unified Water] Step: {}", step);
	StoreStep({ step, hint, Clock::now() });
}
