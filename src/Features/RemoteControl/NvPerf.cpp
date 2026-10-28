#include "Features/RemoteControl/NvPerf.h"
#include "Features/RemoteControl/NvPerfHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef NVPERF_ENABLED
#	include <nvperf_d3d11_host.h>
#	include <nvperf_d3d11_target.h>
#	include <nvperf_host.h>
#	include <nvperf_target.h>
#endif

namespace NvPerf
{
	namespace
	{
		// NVPW's enums (nvperf_host.h), so the metric helpers below build without the SDK.
		constexpr std::uint8_t kCounter = 0, kRatio = 1, kThroughput = 2;
		constexpr std::uint8_t kRollupAvg = 0, kRollupSum = 3;
		constexpr std::uint16_t kSubmetricNone = 0, kSubmetricPerCycleActive = 10, kSubmetricPctOfPeakActive = 15, kSubmetricPctOfPeakElapsed = 16,
								kSubmetricRatio = 21;

		constexpr std::array<std::string_view, 4> kRollupNames{ "avg", "max", "min", "sum" };
		constexpr std::array<std::string_view, 22> kSubmetricNames{ "", "peak_sustained", "peak_sustained_active", "peak_sustained_active.per_second",
			"peak_sustained_elapsed", "peak_sustained_elapsed.per_second", "peak_sustained_frame", "peak_sustained_frame.per_second", "peak_sustained_region",
			"peak_sustained_region.per_second", "per_cycle_active", "per_cycle_elapsed", "per_cycle_in_frame", "per_cycle_in_region", "per_second",
			"pct_of_peak_sustained_active", "pct_of_peak_sustained_elapsed", "pct_of_peak_sustained_frame", "pct_of_peak_sustained_region", "max_rate", "pct",
			"ratio" };

		struct MetricKind
		{
			std::string_view name;
			std::uint8_t metricType;
			std::uint8_t rollupOp;
			std::uint16_t submetric;
		};

		// A counter is summed; "active"/"elapsed" average it as a percentage of its sustained peak over the unit's active
		// cycles (occupancy: sm__warps_active) or the elapsed ones (how much of the time a unit worked: sm__cycles_active),
		// and "percycle" as the raw rate per active cycle.
		constexpr std::array<MetricKind, 6> kMetricKinds{ {
			{ "counter", kCounter, kRollupSum, kSubmetricNone },
			{ "ratio", kRatio, kRollupAvg, kSubmetricRatio },
			{ "throughput", kThroughput, kRollupAvg, kSubmetricPctOfPeakElapsed },
			{ "active", kCounter, kRollupAvg, kSubmetricPctOfPeakActive },
			{ "elapsed", kCounter, kRollupAvg, kSubmetricPctOfPeakElapsed },
			{ "percycle", kCounter, kRollupAvg, kSubmetricPerCycleActive },
		} };
	}

	std::vector<MetricRequest> DefaultMetrics()
	{
		const auto metric = [](const char* a_name, std::string_view a_kind) {
			MetricRequest request;
			const std::string text = std::format("{}:{}", a_name, a_kind);
			std::string error;
			ParseMetric(text, request, error);
			return request;
		};
		return {
			metric("gpu__time_duration", "counter"),
			metric("sm__throughput", "throughput"),
			metric("l1tex__throughput", "throughput"),
			metric("lts__throughput", "throughput"),
			metric("dram__throughput", "throughput"),
			metric("smsp__inst_executed", "counter"),
			metric("sm__warps_active", "active"),
			metric("smsp__warp_issue_stalled_long_scoreboard_per_warp_active", "ratio"),
			metric("smsp__warp_issue_stalled_short_scoreboard_per_warp_active", "ratio"),
			metric("smsp__warp_issue_stalled_tex_throttle_per_warp_active", "ratio"),
			metric("smsp__warp_issue_stalled_math_pipe_throttle_per_warp_active", "ratio"),
			metric("smsp__warp_issue_stalled_mio_throttle_per_warp_active", "ratio"),
			metric("smsp__warp_issue_stalled_lg_throttle_per_warp_active", "ratio"),
			metric("smsp__warp_issue_stalled_wait_per_warp_active", "ratio"),
			metric("smsp__warp_issue_stalled_no_instruction_per_warp_active", "ratio"),
		};
	}

	bool ParseMetric(std::string_view a_text, MetricRequest& a_metric, std::string& a_error)
	{
		const auto trim = [](std::string_view text) {
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
				text.remove_prefix(1);
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
				text.remove_suffix(1);
			return text;
		};
		const auto colon = a_text.find(':');
		const std::string_view name = trim(a_text.substr(0, colon));
		const std::string_view kind = colon == std::string_view::npos ? std::string_view("counter") : trim(a_text.substr(colon + 1));
		if (name.empty()) {
			a_error = "metric name cannot be empty";
			return false;
		}
		const auto found = std::ranges::find_if(kMetricKinds, [&](const MetricKind& entry) { return entry.name == kind; });
		if (found == kMetricKinds.end()) {
			a_error = std::format("unknown metric kind '{}' (counter, ratio, throughput, active, elapsed, percycle)", kind);
			return false;
		}
		a_metric.name = std::string(name);
		a_metric.metricType = found->metricType;
		a_metric.rollupOp = found->rollupOp;
		a_metric.submetric = found->submetric;
		return true;
	}

	std::string FormatMetric(const MetricRequest& a_metric)
	{
		const auto found = std::ranges::find_if(kMetricKinds, [&](const MetricKind& entry) {
			return entry.metricType == a_metric.metricType && entry.rollupOp == a_metric.rollupOp && entry.submetric == a_metric.submetric;
		});
		return found == kMetricKinds.end() ? MetricLabel(a_metric) : std::format("{}:{}", a_metric.name, found->name);
	}

	std::string MetricLabel(const MetricRequest& a_metric)
	{
		std::string label = a_metric.name;
		if (a_metric.metricType != kRatio && a_metric.rollupOp < kRollupNames.size())
			label += std::format(".{}", kRollupNames[a_metric.rollupOp]);
		if (a_metric.submetric != kSubmetricNone && a_metric.submetric < kSubmetricNames.size())
			label += std::format(".{}", kSubmetricNames[a_metric.submetric]);
		return label;
	}

	std::filesystem::path CsvPath()
	{
		return Host::LogDirectory() / "CommunityShaders-NvPerf.csv";
	}

#ifdef NVPERF_ENABLED

	namespace
	{
		std::string ToUtf8(const std::filesystem::path& a_path)
		{
			const auto text = a_path.u8string();
			return { reinterpret_cast<const char*>(text.data()), text.size() };
		}

		bool IsSummed(const MetricRequest& a_metric)
		{
			return a_metric.metricType == kCounter && a_metric.rollupOp == kRollupSum;
		}

		bool NameMatches(std::string_view a_filter, std::string_view a_name)
		{
			if (!a_filter.empty() && a_filter.back() == '*')
				return a_name.starts_with(a_filter.substr(0, a_filter.size() - 1));
			return a_filter == a_name;
		}

		const std::string* MatchingFilter(const std::vector<std::string>& a_filters, std::string_view a_name)
		{
			const auto found = std::ranges::find_if(a_filters, [&](const std::string& filter) { return NameMatches(filter, a_name); });
			return found == a_filters.end() ? nullptr : &*found;
		}

		std::string CsvEscape(std::string_view a_text)
		{
			if (a_text.find_first_of("\",\r\n") == std::string_view::npos)
				return std::string(a_text);
			std::string escaped = "\"";
			for (char ch : a_text) {
				if (ch == '"')
					escaped.push_back('"');
				escaped.push_back(ch);
			}
			escaped.push_back('"');
			return escaped;
		}

		// Per filter, per frame. Every sample measures the same frame shape, so occurrences and summed counters are
		// divided by the sample count.
		std::vector<RangeSummary> Summarize(const CaptureResult& a_result)
		{
			const auto& metrics = a_result.metrics;
			std::size_t timeMetric = metrics.size();
			for (std::size_t m = 0; m < metrics.size(); ++m)
				if (metrics[m].name == "gpu__time_duration")
					timeMetric = m;
			struct Totals
			{
				std::uint32_t occurrences = 0;
				std::uint32_t incomplete = 0;
				std::vector<double> values, weights;
			};
			std::map<std::string, Totals> byName;
			std::vector<std::string> order;
			const auto keyOf = [&](const std::string& a_name) -> const std::string& {
				const std::string* filter = MatchingFilter(a_result.ranges, a_name);
				return filter ? *filter : a_name;
			};
			for (const auto& range : a_result.rangeResults) {
				const std::string& key = keyOf(range.name);
				// With nesting levels, a selected range inside one with the same key is part of that range's counters.
				if (std::ranges::any_of(range.enclosing, [&](const std::string& a_parent) { return keyOf(a_parent) == key; }))
					continue;
				auto [entry, inserted] = byName.try_emplace(key);
				if (inserted)
					order.push_back(key);
				auto& totals = entry->second;
				totals.values.resize(metrics.size());
				totals.weights.resize(metrics.size());
				const double time = timeMetric < range.values.size() ? range.values[timeMetric] : 1.0;
				bool incomplete = false;
				for (std::size_t m = 0; m < metrics.size() && m < range.values.size(); ++m) {
					const double value = range.values[m];
					if (std::isnan(value)) {
						incomplete = true;
						continue;
					}
					const double weight = IsSummed(metrics[m]) || std::isnan(time) ? 1.0 : time;
					totals.values[m] += value * weight;
					totals.weights[m] += weight;
				}
				totals.incomplete += incomplete;
				++totals.occurrences;
			}
			const double samples = std::max<double>(a_result.samples, 1.0);
			std::vector<RangeSummary> summary;
			summary.reserve(order.size());
			for (const auto& name : order) {
				const auto& totals = byName[name];
				RangeSummary entry;
				entry.name = name;
				entry.occurrencesPerFrame = totals.occurrences / samples;
				entry.incomplete = totals.incomplete;
				for (std::size_t m = 0; m < metrics.size(); ++m) {
					if (!totals.weights[m])
						entry.values.push_back(std::nan(""));
					else if (IsSummed(metrics[m]))
						entry.values.push_back(totals.values[m] / samples);
					else
						entry.values.push_back(totals.values[m] / totals.weights[m]);
				}
				summary.push_back(std::move(entry));
			}
			return summary;
		}

		void LogResult(const CaptureResult& a_result)
		{
			if (!a_result.success && a_result.rangeResults.empty()) {
				logger::warn("[NvPerf] capture {} failed: {}", a_result.id, a_result.error);
				return;
			}
			logger::info("[NvPerf] capture {} (frames {}-{}, {} samples x {} passes, chip {}): {} ranges{}", a_result.id, a_result.startFrame,
				a_result.endFrame, a_result.samples, a_result.passesPerSample, a_result.chipName, a_result.rangeResults.size(),
				a_result.success ? "" : std::format(" ({})", a_result.error));
			for (const auto& entry : a_result.summary) {
				std::string text;
				for (std::size_t m = 0; m < a_result.metrics.size() && m < entry.values.size(); ++m)
					text += std::format("{}{}={:.4g}", text.empty() ? "" : ", ", MetricLabel(a_result.metrics[m]), entry.values[m]);
				logger::info("[NvPerf]   {} (x{:.3g}/frame{}): {}", entry.name, entry.occurrencesPerFrame,
					entry.incomplete ? std::format(", {} incomplete", entry.incomplete) : "", text);
			}
		}

		void WriteCsv(const CaptureResult& a_result)
		{
			if (a_result.csvPath.empty() || a_result.rangeResults.empty())
				return;
			std::error_code ec;
			std::filesystem::create_directories(a_result.csvPath.parent_path(), ec);
			const bool header = !std::filesystem::exists(a_result.csvPath, ec) || std::filesystem::file_size(a_result.csvPath, ec) == 0;
			std::ofstream csv(a_result.csvPath, std::ios::app);
			if (!csv) {
				logger::warn("[NvPerf] could not write {}", ToUtf8(a_result.csvPath));
				return;
			}
			// Metric columns change between captures, so each capture repeats its header when it differs from a new file's.
			if (!header)
				csv << '\n';
			csv << "capture,sample,start_frame,range,path,occurrence";
			for (const auto& metric : a_result.metrics)
				csv << ',' << CsvEscape(MetricLabel(metric));
			csv << '\n';
			for (const auto& range : a_result.rangeResults) {
				csv << a_result.id << ',' << range.sample << ',' << a_result.startFrame << ',' << CsvEscape(range.name) << ',' << CsvEscape(range.path) << ','
					<< range.occurrence;
				for (double value : range.values)
					csv << ',' << value;
				csv << '\n';
			}
		}

		constexpr std::size_t kMaxRangesPerPass = 2048;
		constexpr std::size_t kAvgRangeNameLength = 96;
		constexpr std::uint32_t kMaxRangeNameLength = 256;
		constexpr std::uint32_t kFramesWithoutRangeLimit = 8;
		constexpr std::uint32_t kDecodeFrameLimit = 240;

		const char* StatusName(NVPA_Status a_status)
		{
			const char* statusName = nullptr;
			const char* comment = nullptr;
			NVPW_NVPAStatusToString(a_status, &statusName, &comment);
			return statusName ? statusName : "NVPA_STATUS_UNKNOWN";
		}

		bool Failed(NVPA_Status a_status, const char* a_operation)
		{
			if (a_status == NVPA_STATUS_SUCCESS)
				return false;
			logger::warn("[NvPerf] {} failed with {}", a_operation, StatusName(a_status));
			return true;
		}

		struct SelectedMetric
		{
			MetricRequest spec;
			NVPW_MetricEvalRequest request{};
		};

		// Render-thread state of the capture in flight.
		struct Capture
		{
			std::uint64_t id = 0;
			CaptureConfiguration configuration;
			std::vector<SelectedMetric> metrics;
			std::vector<std::uint8_t> configImage;
			std::vector<std::uint8_t> counterDataPrefix;
			std::vector<std::uint8_t> counterDataImage;
			std::vector<std::uint8_t> counterDataScratch;
			std::size_t passesPerSample = 0;
			std::size_t traceBufferSize = 0;
			std::size_t traceBufferCount = 0;
			std::uint32_t delayFrames = 0;
			std::uint32_t sample = 0;
			std::size_t passesSubmitted = 0;
			std::uint32_t decodeFrames = 0;
			std::uint32_t framesWithoutRange = 0;
			std::uint64_t rangesThisPass = 0;
			bool sessionActive = false;
			bool passActive = false;
			bool previousFrameAnnotations = false;
			CaptureResult result;
		};

		struct Profiler
		{
			std::mutex mutex;  // everything below up to `capture`
			DeviceInfo device;
			std::size_t deviceIndex = 0;
			std::uint64_t nextCaptureId = 1;
			std::optional<std::pair<std::uint64_t, CaptureConfiguration>> pending;
			bool cancelRequested = false;
			Status status;
			std::shared_ptr<const CaptureResult> result;
			std::function<void(const CaptureResult&)> onComplete;

			// Render thread only.
			std::optional<Capture> capture;
			std::vector<bool> rangeStack;  // per open perf event: whether it was forwarded to the profiler
			std::uint64_t unbalancedRanges = 0;
		};

		Profiler& Get()
		{
			static Profiler profiler;
			return profiler;
		}

		std::atomic<bool> g_rangesActive{ false };

		ID3D11DeviceContext* Context()
		{
			return Host::Context();
		}

		// Loads and initializes the library and identifies the device. Caller holds the mutex.
		bool ProbeLocked(Profiler& a_profiler)
		{
			auto& device = a_profiler.device;
			if (device.probed)
				return device.gpuSupported;
			if (!Host::Device()) {
				device.error = "the D3D11 device is not created yet";
				return false;
			}
			device.probed = true;
			device.libraryPath = Host::DataDirectory() / "NvPerf" / "nvperf_grfx_host.dll";
			// The import is delay-loaded: it must be loaded by path before the first NVPW call, which would otherwise
			// raise rather than fail.
			if (!::GetModuleHandleW(L"nvperf_grfx_host.dll") && !::LoadLibraryW(device.libraryPath.c_str())) {
				device.error = std::format("could not load {}", ToUtf8(device.libraryPath));
				logger::info("[NvPerf] {}", device.error);
				return false;
			}
			NVPW_InitializeHost_Params host{ NVPW_InitializeHost_Params_STRUCT_SIZE };
			NVPW_InitializeTarget_Params target{ NVPW_InitializeTarget_Params_STRUCT_SIZE };
			NVPW_D3D11_LoadDriver_Params load{ NVPW_D3D11_LoadDriver_Params_STRUCT_SIZE };
			if (Failed(NVPW_InitializeHost(&host), "NVPW_InitializeHost") || Failed(NVPW_InitializeTarget(&target), "NVPW_InitializeTarget") ||
				Failed(NVPW_D3D11_LoadDriver(&load), "NVPW_D3D11_LoadDriver")) {
				device.error = "the Nsight Perf library could not be initialized (non-NVIDIA GPU or old driver?)";
				return false;
			}
			device.available = true;
			NVPW_D3D11_Device_GetDeviceIndex_Params index{ NVPW_D3D11_Device_GetDeviceIndex_Params_STRUCT_SIZE };
			index.pDevice = Host::Device();
			if (Failed(NVPW_D3D11_Device_GetDeviceIndex(&index), "NVPW_D3D11_Device_GetDeviceIndex")) {
				device.error = "the D3D11 device is not an NVIDIA GPU the profiler knows";
				return false;
			}
			a_profiler.deviceIndex = index.deviceIndex;
			NVPW_Device_GetNames_Params names{ NVPW_Device_GetNames_Params_STRUCT_SIZE };
			names.deviceIndex = index.deviceIndex;
			if (!Failed(NVPW_Device_GetNames(&names), "NVPW_Device_GetNames")) {
				device.deviceName = names.pDeviceName ? names.pDeviceName : "";
				device.chipName = names.pChipName ? names.pChipName : "";
			}
			NVPW_D3D11_Profiler_IsGpuSupported_Params supported{ NVPW_D3D11_Profiler_IsGpuSupported_Params_STRUCT_SIZE };
			supported.deviceIndex = index.deviceIndex;
			device.gpuSupported = !Failed(NVPW_D3D11_Profiler_IsGpuSupported(&supported), "NVPW_D3D11_Profiler_IsGpuSupported") && supported.isSupported &&
			                      !device.chipName.empty();
			if (!device.gpuSupported)
				device.error = "the range profiler does not support this GPU";
			logger::info("[NvPerf] device {} '{}' chip '{}', range profiler supported: {}", index.deviceIndex, device.deviceName, device.chipName,
				device.gpuSupported);
			return device.gpuSupported;
		}

		NVPW_MetricsEvaluator* CreateEvaluator(std::vector<std::uint8_t>& a_scratch, const char* a_chipName, const std::vector<std::uint8_t>* a_image)
		{
			NVPW_D3D11_MetricsEvaluator_CalculateScratchBufferSize_Params size{ NVPW_D3D11_MetricsEvaluator_CalculateScratchBufferSize_Params_STRUCT_SIZE };
			size.pChipName = a_chipName;
			if (Failed(NVPW_D3D11_MetricsEvaluator_CalculateScratchBufferSize(&size), "NVPW_D3D11_MetricsEvaluator_CalculateScratchBufferSize"))
				return nullptr;
			a_scratch.assign(size.scratchBufferSize, 0);
			NVPW_D3D11_MetricsEvaluator_Initialize_Params init{ NVPW_D3D11_MetricsEvaluator_Initialize_Params_STRUCT_SIZE };
			init.pScratchBuffer = a_scratch.data();
			init.scratchBufferSize = a_scratch.size();
			init.pChipName = a_image ? nullptr : a_chipName;
			init.pCounterDataImage = a_image ? a_image->data() : nullptr;
			init.counterDataImageSize = a_image ? a_image->size() : 0;
			if (Failed(NVPW_D3D11_MetricsEvaluator_Initialize(&init), "NVPW_D3D11_MetricsEvaluator_Initialize"))
				return nullptr;
			return init.pMetricsEvaluator;
		}

		void DestroyEvaluator(NVPW_MetricsEvaluator* a_evaluator)
		{
			NVPW_MetricsEvaluator_Destroy_Params destroy{ NVPW_MetricsEvaluator_Destroy_Params_STRUCT_SIZE };
			destroy.pMetricsEvaluator = a_evaluator;
			(void)NVPW_MetricsEvaluator_Destroy(&destroy);
		}

		void DestroyRawConfig(NVPW_RawCounterConfig* a_config)
		{
			NVPW_RawCounterConfig_Destroy_Params destroy{ NVPW_RawCounterConfig_Destroy_Params_STRUCT_SIZE };
			destroy.pRawCounterConfig = a_config;
			(void)NVPW_RawCounterConfig_Destroy(&destroy);
		}

		std::optional<std::size_t> FindMetricIndex(NVPW_MetricsEvaluator* a_evaluator, const MetricRequest& a_spec)
		{
			NVPW_MetricsEvaluator_GetMetricNames_Params names{ NVPW_MetricsEvaluator_GetMetricNames_Params_STRUCT_SIZE };
			names.pMetricsEvaluator = a_evaluator;
			names.metricType = a_spec.metricType;
			if (Failed(NVPW_MetricsEvaluator_GetMetricNames(&names), "NVPW_MetricsEvaluator_GetMetricNames"))
				return std::nullopt;
			for (std::size_t i = 0; i < names.numMetrics; ++i) {
				const char* metricName = names.pMetricNames + names.pMetricNameBeginIndices[i];
				if (metricName && a_spec.name == metricName)
					return i;
			}
			return std::nullopt;
		}

		std::vector<std::uint8_t> CounterAvailability()
		{
			std::vector<std::uint8_t> image;
			NVPW_D3D11_Profiler_DeviceContext_GetCounterAvailability_Params params{ NVPW_D3D11_Profiler_DeviceContext_GetCounterAvailability_Params_STRUCT_SIZE };
			params.pDeviceContext = Context();
			if (Failed(NVPW_D3D11_Profiler_DeviceContext_GetCounterAvailability(&params), "NVPW_D3D11_Profiler_DeviceContext_GetCounterAvailability(size)"))
				return image;
			image.resize(params.counterAvailabilityImageSize);
			params.pCounterAvailabilityImage = image.data();
			if (Failed(NVPW_D3D11_Profiler_DeviceContext_GetCounterAvailability(&params), "NVPW_D3D11_Profiler_DeviceContext_GetCounterAvailability"))
				image.clear();
			return image;
		}

		// The metrics the chip has, the raw counters behind them, and the configuration and counter data prefix that
		// collect them.
		bool BuildConfig(Capture& a_capture, const std::string& a_chipName, std::string& a_error)
		{
			const char* chip = a_chipName.c_str();
			std::vector<std::uint8_t> scratch;
			NVPW_MetricsEvaluator* evaluator = CreateEvaluator(scratch, chip, nullptr);
			if (!evaluator) {
				a_error = "could not create the metrics evaluator";
				return false;
			}
			std::unordered_set<std::string> rawCounterNames;
			const auto requested = a_capture.configuration.metrics.empty() ? DefaultMetrics() : a_capture.configuration.metrics;
			for (const MetricRequest& spec : requested) {
				const auto index = FindMetricIndex(evaluator, spec);
				if (!index) {
					logger::warn("[NvPerf] metric '{}' is unavailable on chip '{}'", FormatMetric(spec), a_chipName);
					a_capture.result.unsupportedMetrics.push_back(spec);
					continue;
				}
				SelectedMetric metric{};
				metric.spec = spec;
				metric.request.metricIndex = *index;
				metric.request.metricType = spec.metricType;
				metric.request.rollupOp = spec.rollupOp;
				metric.request.submetric = spec.submetric;
				NVPW_MetricsEvaluator_GetMetricRawDependencies_Params deps{ NVPW_MetricsEvaluator_GetMetricRawDependencies_Params_STRUCT_SIZE };
				deps.pMetricsEvaluator = evaluator;
				deps.pMetricEvalRequests = &metric.request;
				deps.numMetricEvalRequests = 1;
				deps.metricEvalRequestStructSize = NVPW_MetricEvalRequest_STRUCT_SIZE;
				deps.metricEvalRequestStrideSize = sizeof(NVPW_MetricEvalRequest);
				if (Failed(NVPW_MetricsEvaluator_GetMetricRawDependencies(&deps), "NVPW_MetricsEvaluator_GetMetricRawDependencies(count)"))
					continue;
				std::vector<const char*> dependencies(deps.numRawDependencies);
				deps.ppRawDependencies = dependencies.data();
				deps.numRawDependencies = dependencies.size();
				if (Failed(NVPW_MetricsEvaluator_GetMetricRawDependencies(&deps), "NVPW_MetricsEvaluator_GetMetricRawDependencies"))
					continue;
				for (const char* dependency : dependencies)
					if (dependency)
						rawCounterNames.insert(dependency);
				a_capture.metrics.push_back(metric);
			}
			DestroyEvaluator(evaluator);

			std::vector<NVPW_RawCounterRequest> rawCounters;
			rawCounters.reserve(rawCounterNames.size());
			for (const std::string& name : rawCounterNames) {
				NVPW_RawCounterRequest request{};
				request.pRawCounterName = name.c_str();
				request.domain = NVPW_RAW_COUNTER_DOMAIN_INVALID;
				request.keepInstances = false;
				rawCounters.push_back(request);
			}
			if (a_capture.metrics.empty() || rawCounters.empty()) {
				a_error = "none of the requested metrics is available on this GPU";
				return false;
			}

			NVPW_D3D11_RawCounterConfig_Create_Params create{ NVPW_D3D11_RawCounterConfig_Create_Params_STRUCT_SIZE };
			create.pChipName = chip;
			create.activityKind = NVPA_ACTIVITY_KIND_PROFILER;
			if (Failed(NVPW_D3D11_RawCounterConfig_Create(&create), "NVPW_D3D11_RawCounterConfig_Create")) {
				a_error = "could not create the counter configuration";
				return false;
			}
			NVPW_RawCounterConfig* config = create.pRawCounterConfig;
			const auto availability = CounterAvailability();
			if (!availability.empty()) {
				NVPW_RawCounterConfig_SetCounterAvailability_Params set{ NVPW_RawCounterConfig_SetCounterAvailability_Params_STRUCT_SIZE };
				set.pRawCounterConfig = config;
				set.pCounterAvailabilityImage = availability.data();
				(void)NVPW_RawCounterConfig_SetCounterAvailability(&set);
			}
			NVPW_RawCounterConfig_GetAllAvailableRawCounterDomains_Params domainCount{ NVPW_RawCounterConfig_GetAllAvailableRawCounterDomains_Params_STRUCT_SIZE };
			domainCount.pRawCounterConfig = config;
			std::vector<NVPW_RawCounterDomain> domains;
			bool built = !Failed(NVPW_RawCounterConfig_GetAllAvailableRawCounterDomains(&domainCount), "NVPW_RawCounterConfig_GetAllAvailableRawCounterDomains(count)");
			if (built) {
				domains.resize(domainCount.numAvailableDomains);
				NVPW_RawCounterConfig_GetAllAvailableRawCounterDomains_Params domainValues{ NVPW_RawCounterConfig_GetAllAvailableRawCounterDomains_Params_STRUCT_SIZE };
				domainValues.pRawCounterConfig = config;
				domainValues.numAvailableDomains = domains.size();
				domainValues.pAvailableDomains = domains.data();
				built = !Failed(NVPW_RawCounterConfig_GetAllAvailableRawCounterDomains(&domainValues), "NVPW_RawCounterConfig_GetAllAvailableRawCounterDomains");
				domains.resize(domainValues.numAvailableDomains);
			}
			if (built) {
				NVPW_RawCounterConfig_BeginPassGroup_Params begin{ NVPW_RawCounterConfig_BeginPassGroup_Params_STRUCT_SIZE };
				begin.pRawCounterConfig = config;
				begin.numDomains = domains.size();
				begin.pDomains = domains.data();
				NVPW_RawCounterConfig_AddRawCounters_Params add{ NVPW_RawCounterConfig_AddRawCounters_Params_STRUCT_SIZE };
				add.pRawCounterConfig = config;
				add.rawCounterRequestStructSize = NVPW_RAW_COUNTER_REQUEST_STRUCT_SIZE;
				add.numRawCounterRequests = rawCounters.size();
				add.pRawCounterRequests = rawCounters.data();
				NVPW_RawCounterConfig_EndPassGroup_Params end{ NVPW_RawCounterConfig_EndPassGroup_Params_STRUCT_SIZE };
				end.pRawCounterConfig = config;
				end.numDomains = domains.size();
				end.pDomains = domains.data();
				NVPW_RawCounterConfig_GenerateConfigImage_Params generate{ NVPW_RawCounterConfig_GenerateConfigImage_Params_STRUCT_SIZE };
				generate.pRawCounterConfig = config;
				built = !Failed(NVPW_RawCounterConfig_BeginPassGroup(&begin), "NVPW_RawCounterConfig_BeginPassGroup") &&
				        !Failed(NVPW_RawCounterConfig_AddRawCounters(&add), "NVPW_RawCounterConfig_AddRawCounters") &&
				        !Failed(NVPW_RawCounterConfig_EndPassGroup(&end), "NVPW_RawCounterConfig_EndPassGroup") &&
				        !Failed(NVPW_RawCounterConfig_GenerateConfigImage(&generate), "NVPW_RawCounterConfig_GenerateConfigImage");
			}
			if (built) {
				NVPW_RawCounterConfig_GetConfigImage_Params image{ NVPW_RawCounterConfig_GetConfigImage_Params_STRUCT_SIZE };
				image.pRawCounterConfig = config;
				built = !Failed(NVPW_RawCounterConfig_GetConfigImage(&image), "NVPW_RawCounterConfig_GetConfigImage(size)");
				if (built) {
					a_capture.configImage.resize(image.bytesCopied);
					image.bytesAllocated = a_capture.configImage.size();
					image.pBuffer = a_capture.configImage.data();
					built = !Failed(NVPW_RawCounterConfig_GetConfigImage(&image), "NVPW_RawCounterConfig_GetConfigImage");
				}
			}
			std::size_t passes = 0;
			if (built) {
				NVPW_RawCounterConfig_GetNumPasses_Params numPasses{ NVPW_RawCounterConfig_GetNumPasses_Params_STRUCT_SIZE };
				numPasses.pRawCounterConfig = config;
				built = !Failed(NVPW_RawCounterConfig_GetNumPasses(&numPasses), "NVPW_RawCounterConfig_GetNumPasses");
				passes = numPasses.numPasses;
			}
			DestroyRawConfig(config);
			if (!built || !passes) {
				a_error = "could not build the counter configuration";
				return false;
			}
			a_capture.passesPerSample = passes * a_capture.configuration.nestingLevels;

			NVPW_CounterDataBuilder_Create_Params builderCreate{ NVPW_CounterDataBuilder_Create_Params_STRUCT_SIZE };
			builderCreate.pChipName = chip;
			if (Failed(NVPW_CounterDataBuilder_Create(&builderCreate), "NVPW_CounterDataBuilder_Create")) {
				a_error = "could not create the counter data builder";
				return false;
			}
			NVPA_CounterDataBuilder* builder = builderCreate.pCounterDataBuilder;
			NVPW_CounterDataBuilder_AddRawCounters_Params builderAdd{ NVPW_CounterDataBuilder_AddRawCounters_Params_STRUCT_SIZE };
			builderAdd.pCounterDataBuilder = builder;
			builderAdd.rawCounterRequestStructSize = NVPW_RAW_COUNTER_REQUEST_STRUCT_SIZE;
			builderAdd.numRawCounterRequests = rawCounters.size();
			builderAdd.pRawCounterRequests = rawCounters.data();
			NVPW_CounterDataBuilder_GetCounterDataPrefix_Params prefix{ NVPW_CounterDataBuilder_GetCounterDataPrefix_Params_STRUCT_SIZE };
			prefix.pCounterDataBuilder = builder;
			bool prefixOk = !Failed(NVPW_CounterDataBuilder_AddRawCounters(&builderAdd), "NVPW_CounterDataBuilder_AddRawCounters") &&
			                !Failed(NVPW_CounterDataBuilder_GetCounterDataPrefix(&prefix), "NVPW_CounterDataBuilder_GetCounterDataPrefix(size)");
			if (prefixOk) {
				a_capture.counterDataPrefix.resize(prefix.bytesCopied);
				prefix.bytesAllocated = a_capture.counterDataPrefix.size();
				prefix.pBuffer = a_capture.counterDataPrefix.data();
				prefixOk = !Failed(NVPW_CounterDataBuilder_GetCounterDataPrefix(&prefix), "NVPW_CounterDataBuilder_GetCounterDataPrefix");
			}
			NVPW_CounterDataBuilder_Destroy_Params destroy{ NVPW_CounterDataBuilder_Destroy_Params_STRUCT_SIZE };
			destroy.pCounterDataBuilder = builder;
			(void)NVPW_CounterDataBuilder_Destroy(&destroy);
			if (!prefixOk) {
				a_error = "could not build the counter data prefix";
				return false;
			}

			NVPW_D3D11_Profiler_CalcTraceBufferSize_Params traceSize{ NVPW_D3D11_Profiler_CalcTraceBufferSize_Params_STRUCT_SIZE };
			traceSize.maxRangesPerPass = kMaxRangesPerPass;
			traceSize.avgRangeNameLength = kAvgRangeNameLength;
			if (Failed(NVPW_D3D11_Profiler_CalcTraceBufferSize(&traceSize), "NVPW_D3D11_Profiler_CalcTraceBufferSize")) {
				a_error = "could not size the trace buffer";
				return false;
			}
			a_capture.traceBufferSize = traceSize.traceBufferSize;
			// One per pass in flight: every pass is submitted before the first is decoded.
			a_capture.traceBufferCount = std::max<std::size_t>(a_capture.passesPerSample + 2, 5);
			logger::info("[NvPerf] capture {} configured: chip '{}', {} metrics, {} raw counters, {} passes per sample", a_capture.id, a_chipName,
				a_capture.metrics.size(), rawCounters.size(), a_capture.passesPerSample);
			return true;
		}

		bool InitializeCounterData(Capture& a_capture)
		{
			NVPW_D3D11_Profiler_CounterDataImageOptions options{ NVPW_D3D11_Profiler_CounterDataImageOptions_STRUCT_SIZE };
			options.pCounterDataPrefix = a_capture.counterDataPrefix.data();
			options.counterDataPrefixSize = a_capture.counterDataPrefix.size();
			options.maxNumRanges = static_cast<std::uint32_t>(kMaxRangesPerPass);
			options.maxNumRangeTreeNodes = static_cast<std::uint32_t>(kMaxRangesPerPass);
			options.maxRangeNameLength = kMaxRangeNameLength;
			NVPW_D3D11_Profiler_CounterDataImage_CalculateSize_Params size{ NVPW_D3D11_Profiler_CounterDataImage_CalculateSize_Params_STRUCT_SIZE };
			size.counterDataImageOptionsSize = NVPW_D3D11_Profiler_CounterDataImageOptions_STRUCT_SIZE;
			size.pOptions = &options;
			if (Failed(NVPW_D3D11_Profiler_CounterDataImage_CalculateSize(&size), "NVPW_D3D11_Profiler_CounterDataImage_CalculateSize"))
				return false;
			a_capture.counterDataImage.assign(size.counterDataImageSize, 0);
			NVPW_D3D11_Profiler_CounterDataImage_Initialize_Params init{ NVPW_D3D11_Profiler_CounterDataImage_Initialize_Params_STRUCT_SIZE };
			init.counterDataImageOptionsSize = NVPW_D3D11_Profiler_CounterDataImageOptions_STRUCT_SIZE;
			init.pOptions = &options;
			init.counterDataImageSize = a_capture.counterDataImage.size();
			init.pCounterDataImage = a_capture.counterDataImage.data();
			if (Failed(NVPW_D3D11_Profiler_CounterDataImage_Initialize(&init), "NVPW_D3D11_Profiler_CounterDataImage_Initialize"))
				return false;
			NVPW_D3D11_Profiler_CounterDataImage_CalculateScratchBufferSize_Params scratchSize{
				NVPW_D3D11_Profiler_CounterDataImage_CalculateScratchBufferSize_Params_STRUCT_SIZE
			};
			scratchSize.counterDataImageSize = a_capture.counterDataImage.size();
			scratchSize.pCounterDataImage = a_capture.counterDataImage.data();
			if (Failed(NVPW_D3D11_Profiler_CounterDataImage_CalculateScratchBufferSize(&scratchSize),
					"NVPW_D3D11_Profiler_CounterDataImage_CalculateScratchBufferSize"))
				return false;
			a_capture.counterDataScratch.assign(scratchSize.counterDataScratchBufferSize, 0);
			NVPW_D3D11_Profiler_CounterDataImage_InitializeScratchBuffer_Params scratchInit{
				NVPW_D3D11_Profiler_CounterDataImage_InitializeScratchBuffer_Params_STRUCT_SIZE
			};
			scratchInit.counterDataImageSize = a_capture.counterDataImage.size();
			scratchInit.pCounterDataImage = a_capture.counterDataImage.data();
			scratchInit.counterDataScratchBufferSize = a_capture.counterDataScratch.size();
			scratchInit.pCounterDataScratchBuffer = a_capture.counterDataScratch.data();
			return !Failed(NVPW_D3D11_Profiler_CounterDataImage_InitializeScratchBuffer(&scratchInit),
				"NVPW_D3D11_Profiler_CounterDataImage_InitializeScratchBuffer");
		}

		// A session per sample: its counter data, then the configuration every pass of it uses.
		bool BeginSession(Capture& a_capture)
		{
			if (!InitializeCounterData(a_capture))
				return false;
			NVPW_D3D11_Profiler_DeviceContext_BeginSession_Params begin{ NVPW_D3D11_Profiler_DeviceContext_BeginSession_Params_STRUCT_SIZE };
			begin.pDeviceContext = Context();
			begin.numTraceBuffers = a_capture.traceBufferCount;
			begin.traceBufferSize = a_capture.traceBufferSize;
			begin.maxRangesPerPass = kMaxRangesPerPass;
			begin.maxLaunchesPerPass = kMaxRangesPerPass;
			if (const auto status = NVPW_D3D11_Profiler_DeviceContext_BeginSession(&begin); status != NVPA_STATUS_SUCCESS) {
				logger::warn("[NvPerf] NVPW_D3D11_Profiler_DeviceContext_BeginSession failed with {}{}", StatusName(status),
					status == NVPA_STATUS_INSUFFICIENT_PRIVILEGE ? " (enable GPU performance counters for all users: https://developer.nvidia.com/ERR_NVGPUCTRPERM)" :
					status == NVPA_STATUS_RESOURCE_UNAVAILABLE   ? " (another profiler session is active on this GPU)" :
																   "");
				return false;
			}
			a_capture.sessionActive = true;
			// In-app replay: the profiler steps through the configuration's passes itself, from pass 0.
			NVPW_D3D11_Profiler_DeviceContext_SetConfig_Params config{ NVPW_D3D11_Profiler_DeviceContext_SetConfig_Params_STRUCT_SIZE };
			config.pDeviceContext = Context();
			config.pConfig = a_capture.configImage.data();
			config.configSize = a_capture.configImage.size();
			config.minNestingLevel = 1;
			config.numNestingLevels = a_capture.configuration.nestingLevels;
			config.passIndex = 0;
			config.targetNestingLevel = 1;
			if (Failed(NVPW_D3D11_Profiler_DeviceContext_SetConfig(&config), "NVPW_D3D11_Profiler_DeviceContext_SetConfig"))
				return false;
			a_capture.passesSubmitted = 0;
			a_capture.decodeFrames = 0;
			return true;
		}

		void EndPass(Profiler& a_profiler, Capture& a_capture)
		{
			if (!a_capture.passActive)
				return;
			g_rangesActive.store(false, std::memory_order_relaxed);
			// A perf event left open across Present: close what was forwarded so the pass stays balanced.
			for (auto it = a_profiler.rangeStack.rbegin(); it != a_profiler.rangeStack.rend(); ++it) {
				if (*it) {
					NVPW_D3D11_Profiler_DeviceContext_PopRange_Params pop{ NVPW_D3D11_Profiler_DeviceContext_PopRange_Params_STRUCT_SIZE };
					pop.pDeviceContext = Context();
					(void)NVPW_D3D11_Profiler_DeviceContext_PopRange(&pop);
				}
			}
			a_profiler.unbalancedRanges += a_profiler.rangeStack.size();
			a_profiler.rangeStack.clear();
			NVPW_D3D11_Profiler_DeviceContext_EndPass_Params end{ NVPW_D3D11_Profiler_DeviceContext_EndPass_Params_STRUCT_SIZE };
			end.pDeviceContext = Context();
			(void)Failed(NVPW_D3D11_Profiler_DeviceContext_EndPass(&end), "NVPW_D3D11_Profiler_DeviceContext_EndPass");
			a_capture.passActive = false;
			++a_capture.passesSubmitted;
			a_capture.framesWithoutRange = a_capture.rangesThisPass ? 0 : a_capture.framesWithoutRange + 1;
			a_capture.rangesThisPass = 0;
		}

		void EndSession(Profiler& a_profiler, Capture& a_capture)
		{
			EndPass(a_profiler, a_capture);
			if (!a_capture.sessionActive)
				return;
			NVPW_D3D11_Profiler_DeviceContext_EndSession_Params end{ NVPW_D3D11_Profiler_DeviceContext_EndSession_Params_STRUCT_SIZE };
			end.pDeviceContext = Context();
			(void)Failed(NVPW_D3D11_Profiler_DeviceContext_EndSession(&end), "NVPW_D3D11_Profiler_DeviceContext_EndSession");
			a_capture.sessionActive = false;
		}

		std::vector<std::string> RangePath(const std::vector<std::uint8_t>& a_image, std::size_t a_rangeIndex)
		{
			NVPW_Profiler_CounterData_GetRangeDescriptions_Params desc{ NVPW_Profiler_CounterData_GetRangeDescriptions_Params_STRUCT_SIZE };
			desc.pCounterDataImage = a_image.data();
			desc.rangeIndex = a_rangeIndex;
			if (Failed(NVPW_Profiler_CounterData_GetRangeDescriptions(&desc), "NVPW_Profiler_CounterData_GetRangeDescriptions(count)"))
				return {};
			std::vector<const char*> descriptions(desc.numDescriptions);
			desc.ppDescriptions = descriptions.data();
			desc.numDescriptions = descriptions.size();
			if (Failed(NVPW_Profiler_CounterData_GetRangeDescriptions(&desc), "NVPW_Profiler_CounterData_GetRangeDescriptions"))
				return {};
			std::vector<std::string> path;
			for (const char* description : descriptions)
				path.emplace_back(description ? description : "<unnamed>");
			return path;
		}

		bool EvaluateRanges(Capture& a_capture, const std::string& a_chipName)
		{
			NVPW_CounterData_GetNumRanges_Params numRanges{ NVPW_CounterData_GetNumRanges_Params_STRUCT_SIZE };
			numRanges.pCounterDataImage = a_capture.counterDataImage.data();
			if (Failed(NVPW_CounterData_GetNumRanges(&numRanges), "NVPW_CounterData_GetNumRanges"))
				return false;
			if (!numRanges.numRanges)
				return true;
			std::vector<std::uint8_t> scratch;
			NVPW_MetricsEvaluator* evaluator = CreateEvaluator(scratch, a_chipName.c_str(), &a_capture.counterDataImage);
			if (!evaluator)
				return false;
			std::vector<NVPW_MetricEvalRequest> requests;
			for (const auto& metric : a_capture.metrics)
				requests.push_back(metric.request);
			std::vector<double> values(requests.size());
			std::unordered_map<std::string, std::uint32_t> occurrences;
			for (std::size_t rangeIndex = 0; rangeIndex < numRanges.numRanges; ++rangeIndex) {
				std::ranges::fill(values, 0.0);
				NVPW_MetricsEvaluator_EvaluateToGpuValues_Params eval{ NVPW_MetricsEvaluator_EvaluateToGpuValues_Params_STRUCT_SIZE };
				eval.pMetricsEvaluator = evaluator;
				eval.pMetricEvalRequests = requests.data();
				eval.numMetricEvalRequests = requests.size();
				eval.metricEvalRequestStructSize = NVPW_MetricEvalRequest_STRUCT_SIZE;
				eval.metricEvalRequestStrideSize = sizeof(NVPW_MetricEvalRequest);
				eval.pCounterDataImage = a_capture.counterDataImage.data();
				eval.counterDataImageSize = a_capture.counterDataImage.size();
				eval.rangeIndex = rangeIndex;
				eval.pMetricValues = values.data();
				if (Failed(NVPW_MetricsEvaluator_EvaluateToGpuValues(&eval), "NVPW_MetricsEvaluator_EvaluateToGpuValues"))
					continue;
				const auto path = RangePath(a_capture.counterDataImage, rangeIndex);
				RangeResult range;
				range.sample = a_capture.sample;
				range.name = path.empty() ? "<unnamed>" : path.back();
				for (const auto& part : path)
					range.path += range.path.empty() ? part : " / " + part;
				if (!path.empty())
					range.enclosing.assign(path.begin(), path.end() - 1);
				range.occurrence = occurrences[range.path]++;
				range.values = values;
				a_capture.result.rangeResults.push_back(std::move(range));
			}
			DestroyEvaluator(evaluator);
			return true;
		}

		void SetStatus(Profiler& a_profiler, const Capture& a_capture, CaptureState a_state)
		{
			std::lock_guard lock(a_profiler.mutex);
			a_profiler.status.state = a_state;
			a_profiler.status.captureId = a_capture.id;
			a_profiler.status.sample = a_capture.sample;
			a_profiler.status.samples = a_capture.configuration.samples;
			a_profiler.status.pass = a_capture.passesSubmitted;
			a_profiler.status.passes = a_capture.passesPerSample;
			a_profiler.status.error.clear();
		}

		// Ends the capture: closes its session, publishes its result, restores the annotations it forced on.
		void Finish(Profiler& a_profiler, std::string a_error)
		{
			auto& capture = *a_profiler.capture;
			EndSession(a_profiler, capture);
			Host::SetFrameAnnotations(capture.previousFrameAnnotations);

			auto& result = capture.result;
			result.endFrame = Host::FrameCount();
			result.samples = capture.sample;
			result.passesPerSample = capture.passesPerSample;
			result.metrics.clear();
			for (const auto& metric : capture.metrics)
				result.metrics.push_back(metric.spec);
			if (a_error.empty() && (result.droppedRanges || result.droppedTraceBytes))
				a_error = std::format("the profiler dropped {} ranges and {} trace bytes; select fewer ranges", result.droppedRanges, result.droppedTraceBytes);
			if (a_error.empty() && result.rangeResults.empty())
				a_error = "no selected range was measured";
			result.success = a_error.empty();
			result.error = std::move(a_error);
			result.summary = Summarize(result);
			if (a_profiler.unbalancedRanges)
				logger::warn("[NvPerf] {} perf events were still open at Present", a_profiler.unbalancedRanges);
			LogResult(result);
			WriteCsv(result);

			auto published = std::make_shared<const CaptureResult>(std::move(result));
			std::function<void(const CaptureResult&)> onComplete;
			{
				std::lock_guard lock(a_profiler.mutex);
				a_profiler.status.state = published->success ? CaptureState::Done : CaptureState::Failed;
				a_profiler.status.error = published->error;
				a_profiler.result = published;
				onComplete = a_profiler.onComplete;
			}
			a_profiler.capture.reset();
			if (onComplete)
				onComplete(*published);
		}

		// A pending request becomes the capture in flight: the device, its configuration, the annotations it needs.
		void Start(Profiler& a_profiler, std::uint64_t a_id, CaptureConfiguration a_configuration)
		{
			auto& capture = a_profiler.capture.emplace();
			capture.id = a_id;
			capture.configuration = std::move(a_configuration);
			capture.delayFrames = capture.configuration.delayFrames;
			capture.previousFrameAnnotations = Host::FrameAnnotations();
			// Perf events are the ranges, and most are only emitted with annotations on.
			Host::SetFrameAnnotations(true);
			a_profiler.rangeStack.clear();
			a_profiler.unbalancedRanges = 0;

			auto& result = capture.result;
			result.id = a_id;
			result.ranges = capture.configuration.ranges;
			result.startFrame = Host::FrameCount();
			result.csvPath = CsvPath();

			std::string chipName;
			std::string error;
			{
				std::lock_guard lock(a_profiler.mutex);
				if (!ProbeLocked(a_profiler))
					error = a_profiler.device.error;
				chipName = a_profiler.device.chipName;
			}
			result.chipName = chipName;
			if (error.empty() && !BuildConfig(capture, chipName, error) && error.empty())
				error = "could not configure the capture";
			if (!error.empty()) {
				Finish(a_profiler, std::move(error));
				return;
			}
			SetStatus(a_profiler, capture, CaptureState::Waiting);
		}

		// Once per frame after Present: the sample's counters as soon as the GPU finished them, then the next pass.
		void Advance(Profiler& a_profiler)
		{
			auto& capture = *a_profiler.capture;
			if (capture.sessionActive && capture.passesSubmitted) {
				NVPW_D3D11_Profiler_DeviceContext_DecodeCounters_Params decode{ NVPW_D3D11_Profiler_DeviceContext_DecodeCounters_Params_STRUCT_SIZE };
				decode.pDeviceContext = Context();
				decode.counterDataImageSize = capture.counterDataImage.size();
				decode.pCounterDataImage = capture.counterDataImage.data();
				decode.counterDataScratchBufferSize = capture.counterDataScratch.size();
				decode.pCounterDataScratchBuffer = capture.counterDataScratch.data();
				if (Failed(NVPW_D3D11_Profiler_DeviceContext_DecodeCounters(&decode), "NVPW_D3D11_Profiler_DeviceContext_DecodeCounters")) {
					Finish(a_profiler, "could not decode the counters");
					return;
				}
				capture.result.droppedRanges += decode.numRangesDropped;
				capture.result.droppedTraceBytes += decode.numTraceBytesDropped;
				if (decode.allPassesCollected) {
					EndSession(a_profiler, capture);
					std::string chipName;
					{
						std::lock_guard lock(a_profiler.mutex);
						chipName = a_profiler.device.chipName;
					}
					if (!EvaluateRanges(capture, chipName)) {
						Finish(a_profiler, "could not evaluate the counters");
						return;
					}
					if (++capture.sample >= capture.configuration.samples) {
						Finish(a_profiler, {});
						return;
					}
				} else if (capture.passesSubmitted >= capture.passesPerSample && ++capture.decodeFrames > kDecodeFrameLimit) {
					Finish(a_profiler, "the GPU did not finish the profiler passes");
					return;
				}
			}
			if (capture.framesWithoutRange >= kFramesWithoutRangeLimit) {
				Finish(a_profiler, std::format("no selected range was recorded within {} frames", kFramesWithoutRangeLimit));
				return;
			}
			if (capture.delayFrames) {
				--capture.delayFrames;
				return;
			}
			if (!capture.sessionActive && !BeginSession(capture)) {
				Finish(a_profiler, "could not begin a profiler session");
				return;
			}
			if (capture.passesSubmitted < capture.passesPerSample) {
				NVPW_D3D11_Profiler_DeviceContext_BeginPass_Params begin{ NVPW_D3D11_Profiler_DeviceContext_BeginPass_Params_STRUCT_SIZE };
				begin.pDeviceContext = Context();
				if (Failed(NVPW_D3D11_Profiler_DeviceContext_BeginPass(&begin), "NVPW_D3D11_Profiler_DeviceContext_BeginPass")) {
					Finish(a_profiler, "could not begin a profiler pass");
					return;
				}
				capture.passActive = true;
				g_rangesActive.store(true, std::memory_order_relaxed);
				SetStatus(a_profiler, capture, CaptureState::Capturing);
			} else {
				SetStatus(a_profiler, capture, CaptureState::Decoding);
			}
		}
	}

	bool Probe()
	{
		auto& profiler = Get();
		std::lock_guard lock(profiler.mutex);
		return ProbeLocked(profiler);
	}

	DeviceInfo GetDeviceInfo()
	{
		auto& profiler = Get();
		std::lock_guard lock(profiler.mutex);
		DeviceInfo info = profiler.device;
		info.built = true;
		return info;
	}

	bool RequestCapture(const CaptureConfiguration& a_configuration, std::uint64_t& a_captureId, std::string& a_error)
	{
		if (a_configuration.samples < 1 || a_configuration.samples > 64) {
			a_error = "samples must be between 1 and 64";
			return false;
		}
		if (a_configuration.nestingLevels < 1 || a_configuration.nestingLevels > 8) {
			a_error = "nestingLevels must be between 1 and 8";
			return false;
		}
		if (a_configuration.delayFrames > 10000) {
			a_error = "delayFrames must be at most 10000";
			return false;
		}
		for (const auto& metric : a_configuration.metrics) {
			if (metric.name.empty()) {
				a_error = "metric name cannot be empty";
				return false;
			}
		}
		auto& profiler = Get();
		std::lock_guard lock(profiler.mutex);
		const bool busy = profiler.pending || profiler.status.state == CaptureState::Waiting || profiler.status.state == CaptureState::Capturing ||
		                  profiler.status.state == CaptureState::Decoding;
		if (busy) {
			a_error = std::format("capture {} is still in progress", profiler.pending ? profiler.pending->first : profiler.status.captureId);
			return false;
		}
		a_captureId = profiler.nextCaptureId++;
		profiler.pending.emplace(a_captureId, a_configuration);
		profiler.cancelRequested = false;
		profiler.status = Status{};
		profiler.status.state = CaptureState::Waiting;
		profiler.status.captureId = a_captureId;
		profiler.status.samples = a_configuration.samples;
		return true;
	}

	void CancelCapture()
	{
		auto& profiler = Get();
		std::lock_guard lock(profiler.mutex);
		profiler.cancelRequested = true;
	}

	Status GetStatus()
	{
		auto& profiler = Get();
		std::lock_guard lock(profiler.mutex);
		return profiler.status;
	}

	std::shared_ptr<const CaptureResult> LatestResult()
	{
		auto& profiler = Get();
		std::lock_guard lock(profiler.mutex);
		return profiler.result;
	}

	void SetCompletionCallback(std::function<void(const CaptureResult&)> a_callback)
	{
		auto& profiler = Get();
		std::lock_guard lock(profiler.mutex);
		profiler.onComplete = std::move(a_callback);
	}

	void OnPresentBegin()
	{
		auto& profiler = Get();
		if (profiler.capture)
			EndPass(profiler, *profiler.capture);
	}

	void OnPresentEnd()
	{
		auto& profiler = Get();
		std::optional<std::pair<std::uint64_t, CaptureConfiguration>> pending;
		bool cancel = false;
		{
			std::lock_guard lock(profiler.mutex);
			pending.swap(profiler.pending);
			cancel = std::exchange(profiler.cancelRequested, false);
		}
		if (pending && !profiler.capture)
			Start(profiler, pending->first, std::move(pending->second));
		if (!profiler.capture)
			return;
		if (cancel) {
			Finish(profiler, "cancelled");
			return;
		}
		Advance(profiler);
	}

	bool RangesActive() noexcept
	{
		return g_rangesActive.load(std::memory_order_relaxed);
	}

	void PushRange(std::string_view a_name)
	{
		auto& profiler = Get();
		if (!RangesActive() || !profiler.capture)
			return;
		auto& capture = *profiler.capture;
		const auto& filters = capture.configuration.ranges;
		// State::Draw's per-shader-change events only wrap a marker: hundreds per frame of ranges without GPU work, so
		// they are measured only when a filter names them.
		constexpr std::string_view kDrawMarkerPrefix = "Draw: CS ";
		const bool selected = filters.empty() ? !a_name.starts_with(kDrawMarkerPrefix) : MatchingFilter(filters, a_name) != nullptr;
		bool pushed = false;
		if (selected) {
			NVPW_D3D11_Profiler_DeviceContext_PushRange_Params push{ NVPW_D3D11_Profiler_DeviceContext_PushRange_Params_STRUCT_SIZE };
			push.pDeviceContext = Context();
			push.pRangeName = a_name.data();
			push.rangeNameLength = a_name.size();
			pushed = NVPW_D3D11_Profiler_DeviceContext_PushRange(&push) == NVPA_STATUS_SUCCESS;
			capture.rangesThisPass += pushed;
		}
		profiler.rangeStack.push_back(pushed);
	}

	void PopRange()
	{
		auto& profiler = Get();
		if (!RangesActive() || profiler.rangeStack.empty())
			return;
		const bool pushed = profiler.rangeStack.back();
		profiler.rangeStack.pop_back();
		if (pushed) {
			NVPW_D3D11_Profiler_DeviceContext_PopRange_Params pop{ NVPW_D3D11_Profiler_DeviceContext_PopRange_Params_STRUCT_SIZE };
			pop.pDeviceContext = Context();
			(void)NVPW_D3D11_Profiler_DeviceContext_PopRange(&pop);
		}
	}

#else

	namespace
	{
		std::string NotBuilt()
		{
			return std::format("built without Nsight Perf ({})", Host::BuildHint());
		}
	}

	bool Probe() { return false; }

	DeviceInfo GetDeviceInfo()
	{
		DeviceInfo info;
		info.error = NotBuilt();
		return info;
	}

	bool RequestCapture(const CaptureConfiguration&, std::uint64_t&, std::string& a_error)
	{
		a_error = NotBuilt();
		return false;
	}

	void CancelCapture() {}
	Status GetStatus() { return {}; }
	std::shared_ptr<const CaptureResult> LatestResult() { return nullptr; }
	void SetCompletionCallback(std::function<void(const CaptureResult&)>) {}
	void OnPresentBegin() {}
	void OnPresentEnd() {}
	bool RangesActive() noexcept { return false; }
	void PushRange(std::string_view) {}
	void PopRange() {}

#endif
}
