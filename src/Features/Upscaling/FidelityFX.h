#pragma once

#include <atomic>
#include <d3d12.h>
#include <winrt/base.h>

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>

#include <FidelityFX/api/include/ffx_api.hpp>
#include <FidelityFX/api/include/ffx_api_loader.h>
#include <FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp>
#include <FidelityFX/framegeneration/include/ffx_framegeneration.hpp>

#include "../../Buffer.h"
#include "../../State.h"

/** @brief Manages AMD FidelityFX Super Resolution 3 upscaling and frame generation. */
class FidelityFX
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";

	HMODULE module = nullptr;

	ffx::Context swapChainContext{};
	ffx::Context frameGenContext;
	FfxFsr3Context fsrContext[2];

	bool featureFSR3FG = false;

	// Track if FidelityFX is currently being used for frame generation
	bool isFrameGenActive = false;

	// Track HDR state for frame generation callback (needs to be accessible from static callback)
	// Using atomic for thread safety since async workloads may read this from different threads
	static inline std::atomic<bool> isHDRActive = false;
	static inline std::atomic<float> hdrPeakNits = 1000.0f;
	static inline std::atomic<bool> needsReset = false;

	// Track previous HDR parameters to detect changes that require FG reset
	bool prevHDRActive = false;
	float prevPeakNits = 1000.0f;

	// Cached DLL version info for FidelityFX plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	/** @brief Loads the FidelityFX loader and frame generation DLLs from the plugin directory. */
	void LoadFFX();
	/** @brief Creates the FidelityFX frame generation context for the current swap chain. */
	void SetupFrameGeneration();
	/**
	 * @brief Presents the current frame, optionally performing FidelityFX frame generation.
	 * @param a_useFrameGeneration Whether to enable frame generation for this present call.
	 * @param a_isHDR Whether HDR output is active, affecting color space configuration.
	 */
	void Present(bool a_useFrameGeneration, bool a_isHDR = false);

	/** @brief Creates FSR3 upscaling resources including scratch buffers and context. */
	void CreateFSRResources();

	/** @brief Destroys FSR3 upscaling resources and frees the scratch buffer. */
	void DestroyFSRResources();

	/**
	 * @brief Dispatches FSR3 upscaling for the current frame.
	 * @param a_upscalingTexture The input color texture to upscale.
	 * @param a_reactiveMask Reactive mask identifying areas needing temporal stability.
	 * @param a_transparencyCompositionMask Mask for transparency and composition handling.
	 * @param a_motionVectors Per-pixel motion vectors for temporal reprojection.
	 * @param a_sharpness RCAS sharpening strength applied after upscaling.
	 */
	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness);

private:
	// FSR scratch buffer - needs to be freed in DestroyFSRResources
	void* fsrScratchBuffer = nullptr;

	// Flag to prevent spamming the log with FSR3 dispatch crash messages
	bool fsrDispatchCrashLogged = false;
};
