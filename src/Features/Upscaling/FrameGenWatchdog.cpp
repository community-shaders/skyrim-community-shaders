#include "FrameGenWatchdog.h"

#include <Windows.h>
#include <algorithm>
#include <chrono>

namespace
{
	uint64_t ClockNs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	bool IsForegroundProcess()
	{
		const HWND window = GetForegroundWindow();
		if (!window)
			return false;
		DWORD processId = 0;
		GetWindowThreadProcessId(window, &processId);
		return processId == GetCurrentProcessId();
	}

}

bool FrameGenStallDetector::Poll(bool a_enabled, uint64_t a_renderHeartbeat,
	uint64_t a_presentHeartbeat, uint64_t a_now, bool a_foreground)
{
	if (!a_enabled) {
		triggered = false;
		return false;
	}
	if (!a_renderHeartbeat || !a_presentHeartbeat || !a_foreground ||
		a_now - a_renderHeartbeat < kTimeoutNs || a_now - a_presentHeartbeat < kTimeoutNs)
		return false;
	if (triggered)
		return false;
	triggered = true;
	return true;
}

void FrameGenWatchdog::Start(const std::atomic<bool>& a_enabled,
	const std::atomic<uint64_t>& a_renderHeartbeat, const std::atomic<uint64_t>& a_presentHeartbeat,
	RecoveryCallback a_recover)
{
	if (worker.joinable())
		return;
	worker = std::jthread([&a_enabled, &a_renderHeartbeat, &a_presentHeartbeat,
			recover = std::move(a_recover)](std::stop_token a_stop) {
		SetThreadDescription(GetCurrentThread(), L"CS DLSS-G hang watchdog");
		FrameGenStallDetector detector;
		while (!a_stop.stop_requested()) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			const uint64_t now = ClockNs();
			const uint64_t render = a_renderHeartbeat.load(std::memory_order_acquire);
			const uint64_t present = a_presentHeartbeat.load(std::memory_order_acquire);
			if (detector.Poll(a_enabled.load(std::memory_order_acquire), render, present, now, IsForegroundProcess())) {
				logger::critical("[DLSSG-Watchdog] render and present stalled for {} ms; forcing WDDM recovery",
					(now - std::max(render, present)) / 1'000'000ull);
				if (recover)
					recover();
			}
		}
	});
}
