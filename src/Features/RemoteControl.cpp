// Remote Control: status panel for the devbench bridge.
//
// The plugin's Model Context Protocol tools register into the external devbench host (the
// devbench SKSE plugin, https://github.com/alandtse/devbench) via DevBenchBridge
// (src/Features/RemoteControl/DevBenchBridge.cpp), exposed over both MCP and REST. This feature is a read-only
// panel: it reports whether devbench is present, what was registered, and the port
// devbench bound, so users can confirm the integration without leaving the game. It also
// hosts the Nsight Perf capture controls (Features/RemoteControl/NvPerf), which the devbench
// tool communityshaders.nvperf drives too.

#include "Features/RemoteControl.h"

#include "Features/RemoteControl/DevBenchBridge.h"
#include "Features/RemoteControl/NvPerf.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Utils/UI.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <DevBenchAPI.h>
#endif

namespace
{
	// devbench writes the host port it bound to here on startup. We only read it for
	// display; the bridge itself talks to devbench in-process via the C-ABI, not the port.
	constexpr const char* kRuntimeJsonPath = "Data/SKSE/Plugins/devbench/runtime.json";

	/**
	 * @brief Retrieves the bound port from devbench's runtime configuration.
	 * @return The port number from runtime.json, or 0 if the file is missing, unreadable, or malformed.
	 */
	int ReadDevBenchPort()
	{
		std::error_code ec;
		if (!std::filesystem::exists(kRuntimeJsonPath, ec))
			return 0;
		try {
			std::ifstream in(kRuntimeJsonPath);
			if (!in)
				return 0;
			json runtime = json::parse(in, nullptr, /*allow_exceptions=*/false);
			if (runtime.is_discarded() || !runtime.is_object())
				return 0;
			return runtime.value("port", 0);
		} catch (...) {
			return 0;  // malformed runtime.json is non-fatal — just hide the port
		}
	}

	/**
	 * @brief Splits a comma-separated list, trimming spaces and dropping empty entries.
	 */
	std::vector<std::string> SplitList(const std::string& a_text)
	{
		std::vector<std::string> parts;
		std::stringstream stream(a_text);
		for (std::string part; std::getline(stream, part, ',');) {
			const auto first = part.find_first_not_of(" \t");
			if (first == std::string::npos)
				continue;
			parts.push_back(part.substr(first, part.find_last_not_of(" \t") - first + 1));
		}
		return parts;
	}

	bool IsTimeMetric(const NvPerf::MetricRequest& a_metric)
	{
		return a_metric.name == "gpu__time_duration";
	}
}

/**
 * @brief Obtains the singleton instance.
 * @return RemoteControl* The global singleton instance.
 */
RemoteControl* RemoteControl::GetSingleton()
{
	return &globals::features::remoteControl;
}

/**
 * @brief Registers the plugin's tools into the devbench host.
 *
 * Idempotent and inert when the host is absent or the devbench bridge is disabled.
 */
void RemoteControl::DataLoaded()
{
	// Register the plugin's tools into the devbench host. This feature owns the install; it runs at
	// DataLoaded rather than Load because devbench publishes its cross-plugin interface at
	// kPostLoad — by DataLoaded it's ready. Inert (logged) when no host is present or the
	// bridge was built disabled; idempotent on the devbench side (re-registering replaces).
	DevBenchBridge::Install();
}

/**
 * @brief Renders the devbench integration status panel and tool list.
 *
 * Displays connection status, host build number, bound port, and the set of devbench-exposed tools
 * (feature management, engine inspection, shader cache control, capture, and settings). Shows a
 * warning if the devbench host is not detected or if the build was compiled without the devbench
 * bridge.
 */
void RemoteControl::DrawSettings()
{
	const auto& theme = Menu::GetSingleton()->GetTheme().StatusPalette;

	ImGui::TextWrapped(
		"Registers graphics-feature, inspect, capture, shader-cache, and settings tools "
		"into the external devbench host so AI assistants (Claude Code, Cursor, etc.) can "
		"toggle features, inspect engine state, trigger captures, and save/load settings "
		"over MCP and REST. There is no in-game server — install the devbench SKSE plugin "
		"to enable the integration.");
	ImGui::Spacing();

#ifdef DEVBENCH_BRIDGE_ENABLED
	auto* dvb = DevBenchAPI::GetDevBenchInterface001();
	if (dvb) {
		ImGui::TextColored(theme.SuccessColor, "devbench host present (build %u)", dvb->GetBuildNumber());

		// Cache the port — runtime.json I/O + JSON parse every frame would hitch the UI while
		// the panel is open. Refresh on a coarse interval (devbench may bind after the panel
		// first opens, so re-read periodically rather than only once). QPC, not std::chrono.
		static int cachedPort = -1;       // -1 = not yet read
		static LONGLONG lastReadQpc = 0;  // ticks at last read
		LARGE_INTEGER freq, nowQpc;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&nowQpc);
		if (cachedPort < 0 || nowQpc.QuadPart - lastReadQpc > 2 * freq.QuadPart) {  // > 2s
			cachedPort = ReadDevBenchPort();
			lastReadQpc = nowQpc.QuadPart;
		}
		if (cachedPort > 0) {
			ImGui::Text("Host bound on port %d (from %s)", cachedPort, kRuntimeJsonPath);
		} else {
			ImGui::TextDisabled(
				"Port unknown — devbench writes it to %s once it binds.",
				kRuntimeJsonPath);
		}
	} else {
		ImGui::TextColored(theme.Warning,
			"devbench host not detected. Install the devbench SKSE plugin; "
			"the tools register automatically once it is present.");
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Tools exposed through devbench:");
	ImGui::BulletText("communityshaders.feature — list / get / set / reset / toggle features");
	ImGui::BulletText("communityshaders.inspect — engine state and shader-cache status");
	ImGui::BulletText("communityshaders.shadercache — clear / delete the compiled cache");
	ImGui::BulletText("communityshaders.capture — RenderDoc / screenshot capture");
	ImGui::BulletText("%s", T("feature.remote_control.nvperf.tool_bullet", "communityshaders.nvperf — Nsight Perf GPU counters per render pass"));
	ImGui::BulletText("communityshaders.settings — save / load / reset the global config");
	ImGui::TextDisabled(
		"Note: the console tool is provided by devbench itself, not this plugin.");
#else
	ImGui::TextColored(theme.Warning,
		"This build was compiled without the devbench bridge "
		"(DEVBENCH_BRIDGE=OFF). No tools are registered.");
#endif

	ImGui::Spacing();
	DrawNvPerfSection();
}

#define I18N_KEY_PREFIX "feature.remote_control.nvperf."

/**
 * @brief Renders the Nsight Perf capture controls and the latest capture's per-range summary.
 *
 * The profiler library is probed the first time the section is opened. A capture runs over the next frames on the
 * render thread; this panel only requests it and reads its status, so it works with or without the devbench host.
 */
void RemoteControl::DrawNvPerfSection()
{
	if (!ImGui::CollapsingHeader(T(TKEY("header"), "Nsight Perf (DX11)")))
		return;

	const auto& theme = Menu::GetSingleton()->GetTheme().StatusPalette;
	auto& panel = nvperfPanel;

	ImGui::TextWrapped("%s", T(TKEY("description"), "Measures NVIDIA GPU hardware counters (time, unit throughput, warp stalls) over the frame's perf events with the Nsight Perf SDK. Each frame is one profiler pass; a sample takes as many frames as its metrics need passes. Also drivable through the devbench tool communityshaders.nvperf."));
	ImGui::Spacing();

	NvPerf::Probe();
	const auto device = NvPerf::GetDeviceInfo();
	if (!device.built) {
		ImGui::TextColored(theme.Warning, "%s", T(TKEY("not_built"), "This build was compiled without Nsight Perf. Configure with -DNVPERF_SUPPORT=ON and NVPERF_SDK_ROOT."));
		return;
	}
	if (!device.gpuSupported) {
		ImGui::TextColored(theme.Warning, T(TKEY("unavailable"), "Nsight Perf is unavailable: %s"), device.error.c_str());
		ImGui::TextDisabled(T(TKEY("library_path"), "Library: %s"), device.libraryPath.string().c_str());
		return;
	}
	ImGui::TextColored(theme.SuccessColor, T(TKEY("device"), "%s (chip %s)"), device.deviceName.c_str(), device.chipName.c_str());

	const auto defaults = NvPerf::DefaultMetrics();
	if (panel.defaultMetricEnabled.size() != defaults.size())
		panel.defaultMetricEnabled.assign(defaults.size(), 1);

	ImGui::InputTextWithHint(T(TKEY("ranges"), "Ranges"), T(TKEY("ranges_hint"), "every outermost range"), &panel.ranges);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("ranges_tooltip"), "Comma-separated perf event names; a trailing * matches every name with that prefix."));
		ImGui::Text("%s", T(TKEY("ranges_tooltip2"), "Ranges are measured to the nesting depth below; empty measures every outermost range."));
		ImGui::Text("%s", T(TKEY("ranges_tooltip3"), "Engine passes (shadow maps, BSShaderAccumulator batches) only exist when Frame Annotations was enabled at startup."));
	}
	if (ImGui::TreeNode(T(TKEY("metrics"), "Metrics"))) {
		for (std::size_t i = 0; i < defaults.size(); ++i) {
			bool enabled = panel.defaultMetricEnabled[i] != 0;
			if (ImGui::Checkbox(NvPerf::FormatMetric(defaults[i]).c_str(), &enabled))
				panel.defaultMetricEnabled[i] = enabled ? 1 : 0;
		}
		ImGui::InputTextWithHint(T(TKEY("extra_metrics"), "Extra metrics"), "sm__cycles_active:elapsed, ...", &panel.extraMetrics);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("extra_metrics_tooltip"), "Comma-separated name[:counter|ratio|throughput|active|elapsed|percycle]."));
		ImGui::TreePop();
	}
	ImGui::SliderInt(T(TKEY("samples"), "Samples"), &panel.samples, 1, 16);
	ImGui::SliderInt(T(TKEY("nesting"), "Nesting levels"), &panel.nestingLevels, 1, 4);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("nesting_tooltip"), "Selected ranges nested this deep are measured too. Each level multiplies the passes."));
	ImGui::SliderInt(T(TKEY("delay"), "Delay (frames)"), &panel.delayFrames, 0, 600);

	const auto status = NvPerf::GetStatus();
	const bool busy = status.state == NvPerf::CaptureState::Waiting || status.state == NvPerf::CaptureState::Capturing ||
	                  status.state == NvPerf::CaptureState::Decoding;
	if (busy) {
		if (ImGui::Button(T(TKEY("cancel"), "Cancel capture")))
			NvPerf::CancelCapture();
	} else if (ImGui::Button(T(TKEY("capture"), "Capture"))) {
		NvPerf::CaptureConfiguration configuration;
		for (std::size_t i = 0; i < defaults.size(); ++i)
			if (panel.defaultMetricEnabled[i])
				configuration.metrics.push_back(defaults[i]);
		panel.requestError.clear();
		for (const auto& text : SplitList(panel.extraMetrics)) {
			NvPerf::MetricRequest metric;
			if (!NvPerf::ParseMetric(text, metric, panel.requestError))
				break;
			configuration.metrics.push_back(std::move(metric));
		}
		if (panel.requestError.empty() && configuration.metrics.empty())
			panel.requestError = T(TKEY("no_metrics"), "Select at least one metric.");
		configuration.ranges = SplitList(panel.ranges);
		configuration.samples = static_cast<std::uint32_t>(panel.samples);
		configuration.delayFrames = static_cast<std::uint32_t>(panel.delayFrames);
		configuration.nestingLevels = static_cast<std::uint16_t>(panel.nestingLevels);
		std::uint64_t id = 0;
		if (panel.requestError.empty())
			NvPerf::RequestCapture(configuration, id, panel.requestError);
	}
	if (!panel.requestError.empty())
		ImGui::TextColored(theme.Error, "%s", panel.requestError.c_str());

	switch (status.state) {
	case NvPerf::CaptureState::Waiting:
		ImGui::TextColored(theme.InfoColor, T(TKEY("status_waiting"), "Capture %llu: waiting to start"), status.captureId);
		break;
	case NvPerf::CaptureState::Capturing:
		ImGui::TextColored(theme.InfoColor, T(TKEY("status_capturing"), "Capture %llu: sample %u/%u, pass %zu/%zu"), status.captureId, status.sample + 1,
			status.samples, status.pass + 1, status.passes);
		break;
	case NvPerf::CaptureState::Decoding:
		ImGui::TextColored(theme.InfoColor, T(TKEY("status_decoding"), "Capture %llu: sample %u/%u, decoding"), status.captureId, status.sample + 1,
			status.samples);
		break;
	default:
		break;
	}

	const auto result = NvPerf::LatestResult();
	if (!result)
		return;
	ImGui::Separator();
	if (result->success)
		ImGui::TextColored(theme.SuccessColor, T(TKEY("result_ok"), "Capture %llu: %zu ranges, frames %llu-%llu, %u samples x %zu passes"), result->id,
			result->rangeResults.size(), result->startFrame, result->endFrame, result->samples, result->passesPerSample);
	else
		ImGui::TextColored(theme.Error, T(TKEY("result_failed"), "Capture %llu failed: %s"), result->id, result->error.c_str());
	for (const auto& metric : result->unsupportedMetrics)
		ImGui::TextColored(theme.Warning, T(TKEY("unsupported_metric"), "Unavailable on this GPU: %s"), NvPerf::FormatMetric(metric).c_str());

	if (!result->summary.empty()) {
		const int columns = static_cast<int>(result->metrics.size()) + 2;
		const float height = std::min(ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(result->summary.size()) + 2.0f), 400.0f);
		if (ImGui::BeginTable("##NvPerfSummary", columns,
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
				ImVec2(0.0f, height))) {
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableSetupColumn(T(TKEY("column_range"), "Range"), ImGuiTableColumnFlags_WidthFixed, 260.0f);
			ImGui::TableSetupColumn(T(TKEY("column_count"), "Per frame"), ImGuiTableColumnFlags_WidthFixed);
			for (const auto& metric : result->metrics) {
				const std::string label = IsTimeMetric(metric) ? std::string(T(TKEY("column_time"), "GPU time (ms)")) : metric.name;
				ImGui::TableSetupColumn(label.c_str(), ImGuiTableColumnFlags_WidthFixed);
			}
			ImGui::TableHeadersRow();
			for (const auto& entry : result->summary) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(entry.name.c_str());
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(entry.name.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%.3g", entry.occurrencesPerFrame);
				for (std::size_t m = 0; m < result->metrics.size(); ++m) {
					ImGui::TableNextColumn();
					const double value = m < entry.values.size() ? entry.values[m] : std::nan("");
					if (std::isnan(value))
						ImGui::TextDisabled("-");
					else
						ImGui::Text("%.4g", IsTimeMetric(result->metrics[m]) ? value / 1.0e6 : value);
				}
			}
			ImGui::EndTable();
		}
	}
	ImGui::TextDisabled(T(TKEY("csv_path"), "Every range of every sample: %s"), result->csvPath.string().c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton(T(TKEY("copy_path"), "Copy path")))
		ImGui::SetClipboardText(result->csvPath.string().c_str());
}

#undef I18N_KEY_PREFIX
