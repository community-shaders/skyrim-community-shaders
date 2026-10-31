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

	if (sortedDirty) {
		std::copy_n(history, count, sorted.begin());
		std::sort(sorted.begin(), sorted.begin() + count);
		sortedDirty = false;
	}

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

	for (auto& frame : frames) {
		D3D11_QUERY_DESC disjointDesc{};
		disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		if (FAILED(device->CreateQuery(&disjointDesc, frame.disjoint.put()))) {
			Release();
			logger::warn("Profiler disabled: failed to create timestamp disjoint query");
			return;
		}

		frame.timers.resize(kMaxTimers);
		for (auto& timer : frame.timers) {
			D3D11_QUERY_DESC tsDesc{};
			tsDesc.Query = D3D11_QUERY_TIMESTAMP;
			if (FAILED(device->CreateQuery(&tsDesc, timer.begin.put())) ||
				FAILED(device->CreateQuery(&tsDesc, timer.end.put()))) {
				Release();
				logger::warn("Profiler disabled: failed to create timestamp query");
				return;
			}
		}
		frame.activeCount = 0;
	}

	writeFrame = 0;
	readFrame = 0;
	pendingFrames = 0;
	frameSkipped = false;
	initialized = true;
}

void Profiler::Release()
{
	for (auto& frame : frames) {
		frame.disjoint = nullptr;
		frame.timers.clear();
		frame.activeCount = 0;
	}
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	collectedFrames = 0;
	totalTimeMs = 0.0f;
	cpuTotalTimeMs = 0.0f;
	initialized = false;
	frameActive = false;
	context = nullptr;
}

void Profiler::BeginFrame()
{
	if (!initialized || !context || frameActive || frameSkipped || !IsActive())
		return;

	CollectResults();

	// Reusing a pending slot would discard its results when the GPU lags.
	if (pendingFrames >= kFrameLatency) {
		frameSkipped = true;
		return;
	}

	auto& frame = frames[writeFrame];
	frame.activeCount = 0;
	frameActive = true;
	passOpen = false;
	context->Begin(frame.disjoint.get());
}

void Profiler::BeginPass(std::string_view name)
{
	if (!initialized || !context)
		return;

	if (!frameActive)
		BeginFrame();

	if (frameActive) {
		auto& frame = frames[writeFrame];
		if (frame.activeCount < kMaxTimers) {
			auto& timer = frame.timers[frame.activeCount];
			timer.name.assign(name);
			context->End(timer.begin.get());
			QueryPerformanceCounter(&timer.cpuBegin);
			passOpen = true;
		}
	}

	if (beginPerfEvent)
		beginPerfEvent(name);
}

void Profiler::EndPass()
{
	if (!initialized || !context)
		return;

	if (frameActive) {
		auto& frame = frames[writeFrame];
		if (frame.activeCount < kMaxTimers && std::exchange(passOpen, false)) {
			auto& timer = frame.timers[frame.activeCount];

			LARGE_INTEGER cpuEnd;
			QueryPerformanceCounter(&cpuEnd);
			timer.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);

			context->End(timer.end.get());
			frame.activeCount++;
		}
	}

	if (endPerfEvent)
		endPerfEvent({});
}

void Profiler::EndFrame()
{
	// Before the early returns so it ticks while GPU queries are off.
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (lastFrameTicks != 0)
		frameHistory.PushSample(static_cast<float>(static_cast<double>(now.QuadPart - lastFrameTicks) * cpuTicksToMs));
	lastFrameTicks = now.QuadPart;

	if (!initialized || !context)
		return;

	frameCounter.fetch_add(1, std::memory_order_relaxed);

	if (frameSkipped) {
		frameSkipped = false;
		return;
	}
	if (!frameActive)
		return;

	frameActive = false;
	passOpen = false;
	context->End(frames[writeFrame].disjoint.get());
	writeFrame = (writeFrame + 1) % kFrameLatency;
	pendingFrames++;
}

void Profiler::CollectResults()
{
	struct ActiveTimerData
	{
		float gpuMs = 0.0f;
		float cpuMs = 0.0f;
	};
	std::unordered_map<std::string, ActiveTimerData> activeTimers;
	float activeTotalMs = 0.0f;
	float activeCpuTotalMs = 0.0f;
	bool collected = false;

	// Oldest first; only the newest resolved frame is published.
	while (pendingFrames > 0) {
		auto& frame = frames[readFrame];

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
		HRESULT hr = context->GetData(frame.disjoint.get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr != S_OK)
			break;

		readFrame = (readFrame + 1) % kFrameLatency;
		pendingFrames--;
		collected = true;
		collectedFrames++;

		activeTimers.clear();
		activeTotalMs = 0.0f;
		activeCpuTotalMs = 0.0f;

		if (disjointData.Disjoint)
			continue;

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

			if (knownTimerIndex.try_emplace(timer.name, knownTimers.size()).second) {
				KnownTimer kt;
				kt.name = timer.name;
				knownTimers.push_back(std::move(kt));
			}
		}

		// A name timed several times in one frame is one sample: its frame total.
		for (const auto& [name, data] : activeTimers) {
			auto& known = knownTimers[knownTimerIndex.at(name)];
			known.gpu.PushSample(data.gpuMs);
			known.cpu.PushSample(data.cpuMs);
			known.lastSampleFrame = collectedFrames;
		}
	}

	if (!collected)
		return;

	RetireStaleTimers();

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
		result.valid = true;
		result.historyBuffer = known.gpu.history;
		result.historyHead = known.gpu.head;
		result.historyCount = known.gpu.count;
		results.push_back(std::move(result));
	}
}

double Profiler::QpcTicksToMs()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	return 1000.0 / static_cast<double>(freq.QuadPart);
}

Profiler::FrameStats Profiler::GetFrameStats() const
{
	return {
		.lastMs = frameHistory.lastMs,
		.avgMs = frameHistory.GetAverage(),
		.p95Ms = frameHistory.GetPercentile(95.0f),
		.p99Ms = frameHistory.GetPercentile(99.0f),
		.count = frameHistory.count
	};
}

std::vector<Profiler::TimerResult> Profiler::GetStatistics(std::string_view timerPrefix) const
{
	std::vector<TimerResult> snapshot;
	snapshot.reserve(results.size());
	for (const auto& live : results) {
		if (!live.name.starts_with(timerPrefix))
			continue;
		auto it = knownTimerIndex.find(live.name);
		if (it == knownTimerIndex.end())
			continue;
		const auto& known = knownTimers[it->second];
		auto result = live;
		result.avgMs = known.gpu.GetAverage();
		result.p95Ms = known.gpu.GetPercentile(95.0f);
		result.p99Ms = known.gpu.GetPercentile(99.0f);
		result.cpuAvgMs = known.cpu.GetAverage();
		result.cpuP95Ms = known.cpu.GetPercentile(95.0f);
		result.cpuP99Ms = known.cpu.GetPercentile(99.0f);
		snapshot.push_back(std::move(result));
	}
	return snapshot;
}

void Profiler::RetireStaleTimers()
{
	const size_t before = knownTimers.size();
	std::erase_if(knownTimers, [this](const KnownTimer& known) {
		return collectedFrames - known.lastSampleFrame >= kTimerRetireFrames;
	});
	if (knownTimers.size() != before)
		RebuildTimerIndex();
}

void Profiler::RebuildTimerIndex()
{
	knownTimerIndex.clear();
	for (size_t i = 0; i < knownTimers.size(); i++)
		knownTimerIndex[knownTimers[i].name] = i;
}
