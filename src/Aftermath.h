#pragma once

// NVIDIA Nsight Aftermath.
//
// A GPU fault reaches us as VK_ERROR_DEVICE_LOST and nothing else: no shader, no draw, no
// indication of which of several thousand dispatches per frame was the one that hung. Aftermath
// asks the driver for a post-mortem dump instead, naming the faulting shader and the last markers
// the GPU retired, which is the difference between "FSR-FG lost the device on a 1080 Ti" and a
// reproducible bug.
//
// Compiled out entirely unless the AFTERMATH build option is on (CS_ENABLE_AFTERMATH); every entry
// point below is then an inline no-op, so callers need no #ifdef of their own.
namespace Aftermath
{
	/**
	 * @brief Arms GPU crash dump collection.
	 *
	 * Must run before any D3D or Vulkan device exists -- the SDK installs itself into the driver at
	 * device creation, and enabling it afterwards silently collects nothing. In practice that means
	 * before DxvkLoader::Load().
	 *
	 * @return Whether crash dump collection is armed.
	 */
	bool Enable();

	/** @brief Whether Enable() succeeded and dumps are being collected. */
	[[nodiscard]] bool IsEnabled();

	/**
	 * @brief Whether the Vulkan device should be created with GPU crash analysis support.
	 *
	 * True whenever crash analysis is compiled in, on any vendor. Aftermath itself is Nvidia-only,
	 * but the debug-utils labels this turns on are what give AMD's Radeon GPU Detective the [APP]
	 * half of its execution marker tree, and on Nvidia they name the pass in the Aftermath dump.
	 * Which vendor-specific extensions to enable is DXVK's decision, not ours.
	 */
	[[nodiscard]] bool WantsCrashAnalysis();

	/** @brief Disarms collection. Safe to call when never enabled. */
	void Disable();
}
