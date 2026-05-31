#pragma once

#include <d3d11.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

class Profiler
{
public:
	static constexpr uint32_t kMaxTimers = 128;
	static constexpr uint32_t kFrameLatency = 3;
	static constexpr uint32_t kHistorySize = 300;

	using PerfEventCallback = std::function<void(std::string_view)>;

	struct RollingHistory
	{
		float history[kHistorySize]{};
		uint32_t head = 0;
		uint32_t count = 0;
		float lastMs = 0.0f;

		void PushSample(float ms)
		{
			history[head] = ms;
			head = (head + 1) % kHistorySize;
			if (count < kHistorySize)
				count++;
			lastMs = ms;
		}

		float GetAverage() const;
		float GetPercentile(float p) const;
	};

	struct TimerResult
	{
		std::string name;
		float gpuTimeMs = 0.0f;
		float avgMs = 0.0f;
		float p95Ms = 0.0f;
		float p99Ms = 0.0f;
		float cpuTimeMs = 0.0f;
		float cpuAvgMs = 0.0f;
		float cpuP95Ms = 0.0f;
		float cpuP99Ms = 0.0f;
		bool valid = false;

		const float* historyBuffer = nullptr;
		uint32_t historyHead = 0;
		uint32_t historyCount = 0;

		float GetHistorySample(uint32_t index) const
		{
			if (!historyBuffer || index >= historyCount)
				return 0.0f;
			return historyBuffer[(historyHead - historyCount + index + kHistorySize) % kHistorySize];
		}
	};

	void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
	void Release();

	void SetPerfEventCallbacks(PerfEventCallback beginCb, PerfEventCallback endCb)
	{
		beginPerfEvent = std::move(beginCb);
		endPerfEvent = std::move(endCb);
	}

	void BeginFrame();
	void BeginPass(const std::string& name);
	void EndPass();
	void EndFrame();

	const std::vector<TimerResult>& GetResults() const { return results; }
	float GetTotalTimeMs() const { return totalTimeMs; }
	float GetCpuTotalTimeMs() const { return cpuTotalTimeMs; }

	void ClearTimers()
	{
		results.clear();
		knownTimers.clear();
		knownTimerIndex.clear();
		totalTimeMs = 0.0f;
		cpuTotalTimeMs = 0.0f;
	}

	void ClearTimersForFeature(const std::string& featureName)
	{
		std::string prefix = featureName + "::";
		std::erase_if(knownTimers, [&prefix](const KnownTimer& kt) {
			return kt.name.starts_with(prefix);
		});
		knownTimerIndex.clear();
		for (size_t i = 0; i < knownTimers.size(); i++)
			knownTimerIndex[knownTimers[i].name] = i;
	}

private:
	struct FrameQueries
	{
		winrt::com_ptr<ID3D11Query> disjoint;
		struct TimerPair
		{
			winrt::com_ptr<ID3D11Query> begin;
			winrt::com_ptr<ID3D11Query> end;
			std::string name;
			LARGE_INTEGER cpuBegin{};
			float cpuMs = 0.0f;
		};
		std::vector<TimerPair> timers;
		uint32_t activeCount = 0;
		bool inFlight = false;
	};

	ID3D11DeviceContext* context = nullptr;

	FrameQueries frames[kFrameLatency];
	uint32_t writeFrame = 0;
	uint32_t readFrame = 0;
	uint32_t framesSinceInit = 0;
	bool initialized = false;
	bool frameActive = false;
	double cpuTicksToMs = 0.0;

	PerfEventCallback beginPerfEvent;
	PerfEventCallback endPerfEvent;

	std::vector<TimerResult> results;

	struct KnownTimer
	{
		std::string name;
		RollingHistory gpu;
		RollingHistory cpu;
	};
	std::vector<KnownTimer> knownTimers;
	std::unordered_map<std::string, size_t> knownTimerIndex;
	float totalTimeMs = 0.0f;
	float cpuTotalTimeMs = 0.0f;

	void CollectResults();
};
