#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace NvPerf
{
	struct CaptureResult;
}

/**
 * @brief devbench bridge and Nsight Perf panels. A component of the Developer Tools feature, not a feature itself.
 *
 * Exposes shader tools (feature, inspect, shadercache, capture, nvperf, settings) to AI assistants
 * through the external devbench host (see src/Features/RemoteControl/DevBenchBridge.cpp), accessible
 * via MCP and REST. Also hosts the Nsight Perf capture controls (Features/RemoteControl/NvPerf).
 */
class RemoteControl
{
public:
	static RemoteControl* GetSingleton();

	/**
	 * @brief Registers the plugin's tools into the devbench host. Called from DeveloperTools::DataLoaded.
	 *
	 * Runs at DataLoaded rather than Load: Load runs during SKSEPluginLoad, before devbench's
	 * kPostLoad init, so its cross-plugin interface isn't ready yet.
	 */
	void DataLoaded();

	/** @brief True when the devbench host's cross-plugin interface is present. */
	static bool IsDevBenchHostPresent();

	/** @brief True when this build was compiled with the devbench bridge. */
	static bool IsDevBenchBridgeBuilt();

	/** @brief Renders the devbench tab: host status, bound port and the registered tools. */
	void DrawDevBenchPanel();

	/** @brief Renders the Nsight Perf tab: GPU support, the capture's metrics and ranges, recent captures and the viewed result. */
	void DrawNvPerfPanel();

	/** @brief Port the devbench host bound, read from its runtime.json at most every two seconds; 0 when unknown. */
	static int GetDevBenchPort();

	/** @brief Starts an Nsight Perf capture with the panel's metrics, ranges and counts. @return False with the panel's error set when it could not start. */
	bool RequestNvPerfCapture();

	/** @brief Records the latest Nsight Perf result in the recent captures list when it is new. Cheap; call every frame the panel's card shows. */
	void UpdateNvPerfHistory();

	RemoteControl() = default;
	~RemoteControl() = default;

	RemoteControl(const RemoteControl&) = delete;
	RemoteControl& operator=(const RemoteControl&) = delete;
	RemoteControl(RemoteControl&&) = delete;
	RemoteControl& operator=(RemoteControl&&) = delete;

private:
	// The Nsight Perf section's inputs. UI state only: nothing here is saved.
	struct NvPerfPanel
	{
		std::vector<char> defaultMetricEnabled;  // per NvPerf::DefaultMetrics() entry
		std::string extraMetrics;                // comma-separated name[:kind]
		std::string ranges;                      // comma-separated range filters
		int samples = 1;
		int delayFrames = 0;
		int nestingLevels = 1;
		std::string requestError;

		/** @brief A finished capture and when it was first seen. */
		struct RecentCapture
		{
			std::shared_ptr<const NvPerf::CaptureResult> result;
			std::filesystem::file_time_type time;
		};
		std::vector<RecentCapture> recent;  // newest first
		std::uint64_t viewedCaptureId = 0;  // 0: the newest
	} nvperfPanel;

	static constexpr std::size_t kMaxRecentNvPerfCaptures = 8;

	/** @brief The recent captures table of the Nsight Perf panel. */
	void DrawRecentNvPerfCaptures(NvPerfPanel& a_panel);
};
