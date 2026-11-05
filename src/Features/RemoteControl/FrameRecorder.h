#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Per-frame timings for the profiling bench: Present-to-Present frame time, Present-thread and process CPU.
 *
 * Idle until Start; then one Frame per Present into a fixed buffer until Stop or full. Start, Stop, GetStatus and Read are
 * thread-safe; OnPresentEnd runs on the render thread. Reads no Profiler, NvPerf or Tracy state.
 */
namespace FrameRecorder
{
	inline constexpr std::size_t kMaxFrames = 16384;

	/** @brief One Present-to-Present interval. */
	struct Frame
	{
		double frameMs = 0.0;
		double presentThreadCpuMs = 0.0; /**< @brief Cycle-time delta scaled by the measured TSC rate: an estimate, since the cycle unit is undocumented. */
		double processCpuMs = 0.0;       /**< @brief As presentThreadCpuMs, summed over the process; GetProcessTimes is too coarse per frame. */
		bool frameGeneration = false;
	};

	/** @brief Snapshot of the latest recording, in ms. */
	struct Recording
	{
		std::uint64_t id = 0;
		bool recording = false;
		bool full = false;
		double tscHz = 0.0;
		std::uint32_t presentThreadId = 0; /**< @brief GetCurrentThreadId() of the thread that calls Present, taken at the baseline. */
		std::vector<Frame> frames;
	};

	/** @brief Recording state without the frames. */
	struct Status
	{
		std::uint64_t id = 0;
		bool recording = false;
		bool full = false;
		std::size_t frames = 0;
	};

	/**
	 * @brief Starts a recording at the next Present; clears the previous one.
	 * @return false with a_error while a recording is pending or running.
	 */
	bool Start(std::uint64_t& a_id, std::string& a_error);
	/** @brief Ends the recording at the next Present. */
	void Stop();
	/** @brief Cheap state query, no frame copy. */
	Status GetStatus();
	/** @brief Copy of the latest recording with counters converted to ms. */
	std::shared_ptr<const Recording> Read();
	/** @brief Render thread, once per Present, after it returns. */
	void OnPresentEnd();
}
