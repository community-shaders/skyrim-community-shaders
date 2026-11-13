#include "Features/RemoteControl/FrameRecorder.h"

#include "Features/Upscaling.h"
#include "Globals.h"

#include <atomic>
#include <cassert>
#include <intrin.h>
#include <mutex>

namespace FrameRecorder
{
	namespace
	{
		constexpr double kMsPerSecond = 1000.0;

		/** @brief Recorder lifecycle; Idle is the only state the render thread checks without the mutex. */
		enum class State : std::uint8_t
		{
			Idle,
			StartPending,
			Recording,
			StopPending,
		};

		/** @brief Counters read at one Present. */
		struct Sample
		{
			std::int64_t qpc = 0;
			std::uint64_t tsc = 0;
			std::uint64_t threadCycles = 0;
			std::uint64_t processCycles = 0;
		};

		/** @brief One frame as counter deltas; Read converts them to ms. */
		struct RawFrame
		{
			std::int64_t qpcTicks = 0;
			std::uint64_t threadCycles = 0;
			std::uint64_t processCycles = 0;
			bool frameGeneration = false;
		};

		/** @brief Recording state shared by the API and the render thread; the mutex guards everything but the idle check. */
		struct Recorder
		{
			std::atomic<State> state{ State::Idle };
			std::mutex mutex;
			std::uint64_t id = 0;
			std::uint32_t presentThreadId = 0;
			bool full = false;
			std::vector<RawFrame> frames;
			Sample first;
			Sample last;
		};

		/** @brief The process-wide recorder. */
		Recorder& Get()
		{
			static Recorder recorder;
			return recorder;
		}

		/** @brief QueryPerformanceCounter ticks per second. */
		double QpcFrequency()
		{
			static const double frequency = [] {
				LARGE_INTEGER value;
				QueryPerformanceFrequency(&value);
				return static_cast<double>(value.QuadPart);
			}();
			return frequency;
		}

		/** @brief Reads the QPC, TSC and CPU cycle counters now. */
		Sample TakeSample()
		{
			Sample sample;
			LARGE_INTEGER qpc;
			QueryPerformanceCounter(&qpc);
			sample.qpc = qpc.QuadPart;
			sample.tsc = __rdtsc();
			QueryThreadCycleTime(GetCurrentThread(), &sample.threadCycles);
			QueryProcessCycleTime(GetCurrentProcess(), &sample.processCycles);
			return sample;
		}

		/** @brief Takes the baseline sample and starts recording. */
		void BeginRecording(Recorder& a_recorder)
		{
			a_recorder.frames.reserve(kMaxFrames);
			a_recorder.first = a_recorder.last = TakeSample();
			a_recorder.presentThreadId = GetCurrentThreadId();
			a_recorder.state.store(State::Recording, std::memory_order_release);
		}

		/** @brief Appends the interval since the previous Present as one frame. */
		void CloseFrame(Recorder& a_recorder)
		{
			assert(a_recorder.frames.size() < kMaxFrames);
			const Sample sample = TakeSample();
			a_recorder.frames.push_back({ .qpcTicks = sample.qpc - a_recorder.last.qpc,
				.threadCycles = sample.threadCycles - a_recorder.last.threadCycles,
				.processCycles = sample.processCycles - a_recorder.last.processCycles,
				.frameGeneration = globals::features::upscaling.IsFrameGenerationActive() });
			a_recorder.last = sample;
			a_recorder.full = a_recorder.frames.size() >= kMaxFrames;
		}

		/** @brief Returns to Idle; the frames stay readable. */
		void EndRecording(Recorder& a_recorder)
		{
			a_recorder.state.store(State::Idle, std::memory_order_release);
		}
	}

	bool Start(std::uint64_t& a_id, std::string& a_error)
	{
		auto& recorder = Get();
		std::scoped_lock lock(recorder.mutex);
		if (recorder.state.load(std::memory_order_relaxed) != State::Idle) {
			a_error = "a recording is already pending or running";
			return false;
		}
		recorder.frames.clear();
		recorder.full = false;
		a_id = ++recorder.id;
		recorder.state.store(State::StartPending, std::memory_order_release);
		return true;
	}

	void Stop()
	{
		auto& recorder = Get();
		std::scoped_lock lock(recorder.mutex);
		const State state = recorder.state.load(std::memory_order_relaxed);
		if (state == State::StartPending)
			recorder.state.store(State::Idle, std::memory_order_release);
		else if (state == State::Recording)
			recorder.state.store(State::StopPending, std::memory_order_release);
	}

	Status GetStatus()
	{
		auto& recorder = Get();
		std::scoped_lock lock(recorder.mutex);
		return { .id = recorder.id,
			.recording = recorder.state.load(std::memory_order_relaxed) != State::Idle,
			.full = recorder.full,
			.frames = recorder.frames.size() };
	}

	std::shared_ptr<const Recording> Read()
	{
		auto& recorder = Get();
		auto out = std::make_shared<Recording>();
		std::scoped_lock lock(recorder.mutex);
		out->id = recorder.id;
		out->recording = recorder.state.load(std::memory_order_relaxed) != State::Idle;
		out->full = recorder.full;
		out->presentThreadId = recorder.presentThreadId;
		if (recorder.frames.empty())
			return out;
		// The baseline and the latest recorded Present bound exactly the recorded frames.
		const double seconds = static_cast<double>(recorder.last.qpc - recorder.first.qpc) / QpcFrequency();
		out->tscHz = seconds > 0.0 ? static_cast<double>(recorder.last.tsc - recorder.first.tsc) / seconds : 0.0;
		const double msPerQpcTick = kMsPerSecond / QpcFrequency();
		const double msPerCycle = out->tscHz > 0.0 ? kMsPerSecond / out->tscHz : 0.0;
		out->frames.reserve(recorder.frames.size());
		for (const auto& frame : recorder.frames)
			out->frames.push_back({ .frameMs = static_cast<double>(frame.qpcTicks) * msPerQpcTick,
				.presentThreadCpuMs = static_cast<double>(frame.threadCycles) * msPerCycle,
				.processCpuMs = static_cast<double>(frame.processCycles) * msPerCycle,
				.frameGeneration = frame.frameGeneration });
		return out;
	}

	void OnPresentEnd()
	{
		auto& recorder = Get();
		if (recorder.state.load(std::memory_order_acquire) == State::Idle)
			return;
		std::scoped_lock lock(recorder.mutex);
		switch (recorder.state.load(std::memory_order_relaxed)) {
		case State::StartPending:
			BeginRecording(recorder);
			break;
		case State::Recording:
			CloseFrame(recorder);
			if (recorder.full)
				EndRecording(recorder);
			break;
		case State::StopPending:
			CloseFrame(recorder);
			EndRecording(recorder);
			break;
		case State::Idle:
			break;
		}
	}
}
