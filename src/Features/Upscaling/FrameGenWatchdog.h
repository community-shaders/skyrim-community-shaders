#pragma once

#include <atomic>
#include <functional>
#include <thread>

class FrameGenStallDetector
{
public:
	static constexpr uint64_t kTimeoutNs = 8'000'000'000ull;

	bool Poll(bool a_enabled, uint64_t a_renderHeartbeat, uint64_t a_presentHeartbeat,
		uint64_t a_now, bool a_foreground);

private:
	bool triggered = false;
};

class FrameGenWatchdog
{
public:
	using RecoveryCallback = std::function<void()>;

	void Start(const std::atomic<bool>& a_enabled, const std::atomic<uint64_t>& a_renderHeartbeat,
		const std::atomic<uint64_t>& a_presentHeartbeat, RecoveryCallback a_recover);

private:
	std::jthread worker;
};
