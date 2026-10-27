#pragma once

#include <string>
#include <vector>

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

	/** @brief Renders the Nsight Perf tab: GPU support, the capture's metrics and ranges, and the latest result. */
	void DrawNvPerfPanel();

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
	} nvperfPanel;
};
