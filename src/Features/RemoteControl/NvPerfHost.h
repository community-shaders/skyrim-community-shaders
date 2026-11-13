#pragma once

#include <cstdint>
#include <filesystem>

struct ID3D11Device;
struct ID3D11DeviceContext;

/**
 * @brief What NvPerf needs from the plugin hosting it, so the profiler itself is host-independent.
 *
 * The host implements these in its own translation unit (NvPerfHost.cpp here).
 */
namespace NvPerf::Host
{
	ID3D11Device* Device();
	/// The immediate context the frame's perf events are recorded on.
	ID3D11DeviceContext* Context();
	/// The render thread's frame counter.
	std::uint64_t FrameCount();
	/// Whether perf events are emitted; a capture forces this on for its duration.
	bool FrameAnnotations();
	void SetFrameAnnotations(bool enabled);
	/// The directory of the plugin's log; the CSV is written there.
	std::filesystem::path LogDirectory();
	/// The plugin's data directory; nvperf_grfx_host.dll is loaded from its NvPerf folder.
	std::filesystem::path DataDirectory();
	/// How to build the plugin with Nsight Perf, reported by a build without it.
	const char* BuildHint();
}
