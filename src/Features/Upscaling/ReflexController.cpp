#include "ReflexController.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string>

namespace
{
	bool EnvironmentFlag(const char* a_name)
	{
		const char* value = std::getenv(a_name);
		return value && value[0] != '\0' && value[0] != '0';
	}
}

ReflexController::ReflexController() :
	tracePacing(EnvironmentFlag("CS_PACING_TRACE")),
	forceOff(EnvironmentFlag("CS_REFLEX_DIAGNOSTIC_OFF")),
	forceUnlimited(EnvironmentFlag("CS_REFLEX_DIAGNOSTIC_UNLIMITED")),
	skipSleep(EnvironmentFlag("CS_REFLEX_DIAGNOSTIC_NO_SLEEP"))
{}

void ReflexController::LogPacingBatch(
	const std::array<SleepSample, 8>& a_samples, sl::Result a_stateResult) const
{
	std::string sleepPayload;
	sleepPayload.reserve(128);
	for (const auto& sample : a_samples) {
		if (!sleepPayload.empty())
			sleepPayload.push_back(';');
		sleepPayload += std::format("{},{}", sample.frame, sample.sleepUs);
	}

	std::array<const sl::ReflexReport*, sl::kReflexFrameReportCount> reports{};
	uint32_t reportCount = 0;
	if (a_stateResult == sl::Result::eOk && traceState.latencyReportAvailable) {
		for (const auto& candidate : traceState.frameReport) {
			if (candidate.frameID)
				reports[reportCount++] = &candidate;
		}
		std::sort(reports.begin(), reports.begin() + reportCount,
			[](const auto* a_left, const auto* a_right) { return a_left->frameID < a_right->frameID; });
	}
	std::string reportPayload;
	reportPayload.reserve(2048);
	const uint32_t firstReport = reportCount > 8 ? reportCount - 8 : 0;
	for (uint32_t i = firstReport; i < reportCount; i++) {
		const auto* report = reports[i];
		if (!reportPayload.empty())
			reportPayload.push_back(';');
		reportPayload += std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
			report->frameID, report->simStartTime, report->simEndTime,
			report->renderSubmitStartTime, report->renderSubmitEndTime,
			report->presentStartTime, report->presentEndTime,
			report->driverStartTime, report->driverEndTime,
			report->osRenderQueueStartTime, report->osRenderQueueEndTime,
			report->gpuRenderStartTime, report->gpuRenderEndTime,
			report->gpuActiveRenderTimeUs, report->gpuFrameTimeUs);
	}
	logger::info("[PacingTrace] stateResult={} available={} sleep={} reports={}",
		static_cast<int32_t>(a_stateResult), traceState.latencyReportAvailable, sleepPayload, reportPayload);
}
