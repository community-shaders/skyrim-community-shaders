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

class FidelityFX
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";

	HMODULE module = nullptr;

	ffx::Context swapChainContext{};
	ffx::Context frameGenContext;
	FfxFsr3Context fsrContext;

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

	void LoadFFX();
	void SetupFrameGeneration();
	void Present(bool a_useFrameGeneration, bool a_isHDR = false);

	void CreateFSRResources();

	void DestroyFSRResources();

	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness);

private:
	// FSR scratch buffer - needs to be freed in DestroyFSRResources
	void* fsrScratchBuffer = nullptr;

	// Flag to prevent spamming the log with FSR3 dispatch crash messages
	bool fsrDispatchCrashLogged = false;
};
