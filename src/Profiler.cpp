#include "Profiler.h"

#include <algorithm>
#include <unordered_map>

float Profiler::RollingHistory::GetAverage() const
{
	if (count == 0)
		return lastMs;
	float sum = 0.0f;
	for (uint32_t i = 0; i < count; i++)
		sum += history[i];
	return sum / static_cast<float>(count);
}

float Profiler::RollingHistory::GetPercentile(float p) const
{
	if (count == 0)
		return lastMs;

	thread_local std::vector<float> sorted;
	sorted.resize(count);
	for (uint32_t i = 0; i < count; i++)
		sorted[i] = history[i];
	std::sort(sorted.begin(), sorted.end());

	float idx = (p / 100.0f) * static_cast<float>(count - 1);
	uint32_t lo = static_cast<uint32_t>(idx);
	uint32_t hi = std::min(lo + 1, count - 1);
	float frac = idx - static_cast<float>(lo);
	return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
}

void Profiler::Initialize(ID3D11Device* device, ID3D11DeviceContext* a_context)
{
	Release();

	context = a_context;

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	cpuTicksToMs = 1000.0 / static_cast<double>(freq.QuadPart);

	for (auto& frame : frames) {
		D3D11_QUERY_DESC disjointDesc{};
		disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		device->CreateQuery(&disjointDesc, frame.disjoint.put());

		frame.timers.resize(kMaxTimers);
		for (auto& timer : frame.timers) {
			D3D11_QUERY_DESC tsDesc{};
			tsDesc.Query = D3D11_QUERY_TIMESTAMP;
			device->CreateQuery(&tsDesc, timer.begin.put());
			device->CreateQuery(&tsDesc, timer.end.put());
		}
		frame.activeCount = 0;
		frame.inFlight = false;
	}

	writeFrame = 0;
	readFrame = 0;
	framesSinceInit = 0;
	initialized = true;
}

void Profiler::Release()
{
	for (auto& frame : frames) {
		frame.disjoint = nullptr;
		frame.timers.clear();
		frame.activeCount = 0;
		frame.inFlight = false;
	}
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	totalTimeMs = 0.0f;
	cpuTotalTimeMs = 0.0f;
	initialized = false;
	context = nullptr;
}

void Profiler::BeginFrame()
{
	if (!initialized || !context || frameActive)
		return;

	CollectResults();

	auto& frame = frames[writeFrame];
	frame.activeCount = 0;
	frame.inFlight = true;
	frameActive = true;
	context->Begin(frame.disjoint.get());
}

void Profiler::BeginPass(const std::string& name)
{
	if (!initialized || !context)
		return;

	if (!frameActive)
		BeginFrame();

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return;

	auto& timer = frame.timers[frame.activeCount];
	timer.name = name;
	context->End(timer.begin.get());
	QueryPerformanceCounter(&timer.cpuBegin);

	if (beginPerfEvent)
		beginPerfEvent(name);
}

void Profiler::EndPass()
{
	if (!initialized || !context || !frameActive)
		return;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return;

	auto& timer = frame.timers[frame.activeCount];

	LARGE_INTEGER cpuEnd;
	QueryPerformanceCounter(&cpuEnd);
	timer.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);

	context->End(timer.end.get());
	frame.activeCount++;

	if (endPerfEvent)
		endPerfEvent({});
}

void Profiler::EndFrame()
{
	if (!initialized || !context || !frameActive)
		return;

	frameActive = false;
	context->End(frames[writeFrame].disjoint.get());
	writeFrame = (writeFrame + 1) % kFrameLatency;
	framesSinceInit++;
}

void Profiler::CollectResults()
{
	if (framesSinceInit < kFrameLatency)
		return;

	readFrame = writeFrame;
	auto& frame = frames[readFrame];
	if (!frame.inFlight)
		return;

	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
	HRESULT hr = context->GetData(frame.disjoint.get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	if (hr != S_OK)
		return;

	frame.inFlight = false;

	struct ActiveTimerData
	{
		float gpuMs = 0.0f;
		float cpuMs = 0.0f;
	};
	std::unordered_map<std::string, ActiveTimerData> activeTimers;
	float activeTotalMs = 0.0f;
	float activeCpuTotalMs = 0.0f;

	if (!disjointData.Disjoint) {
		double ticksToMs = 1000.0 / static_cast<double>(disjointData.Frequency);

		for (uint32_t i = 0; i < frame.activeCount; i++) {
			auto& timer = frame.timers[i];
			UINT64 tsBegin = 0, tsEnd = 0;

			if (context->GetData(timer.begin.get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			if (context->GetData(timer.end.get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;

			float ms = static_cast<float>(static_cast<double>(tsEnd - tsBegin) * ticksToMs);
			auto& entry = activeTimers[timer.name];
			entry.gpuMs += ms;
			entry.cpuMs += timer.cpuMs;
			activeTotalMs += ms;
			activeCpuTotalMs += timer.cpuMs;

			auto [it, inserted] = knownTimerIndex.try_emplace(timer.name, knownTimers.size());
			if (inserted) {
				KnownTimer kt;
				kt.name = timer.name;
				knownTimers.push_back(std::move(kt));
			}
			auto& known = knownTimers[it->second];
			known.gpu.PushSample(ms);
			known.cpu.PushSample(timer.cpuMs);
		}
	}

	totalTimeMs = activeTotalMs;
	cpuTotalTimeMs = activeCpuTotalMs;

	results.clear();
	results.reserve(knownTimers.size());
	for (const auto& known : knownTimers) {
		TimerResult result;
		result.name = known.name;
		auto it = activeTimers.find(known.name);
		if (it != activeTimers.end()) {
			result.gpuTimeMs = it->second.gpuMs;
			result.cpuTimeMs = it->second.cpuMs;
		} else {
			result.gpuTimeMs = known.gpu.lastMs;
			result.cpuTimeMs = known.cpu.lastMs;
		}
		result.avgMs = known.gpu.GetAverage();
		result.p95Ms = known.gpu.GetPercentile(95.0f);
		result.p99Ms = known.gpu.GetPercentile(99.0f);
		result.cpuAvgMs = known.cpu.GetAverage();
		result.cpuP95Ms = known.cpu.GetPercentile(95.0f);
		result.cpuP99Ms = known.cpu.GetPercentile(99.0f);
		result.valid = true;
		result.historyBuffer = known.gpu.history;
		result.historyHead = known.gpu.head;
		result.historyCount = known.gpu.count;
		results.push_back(std::move(result));
	}
}
