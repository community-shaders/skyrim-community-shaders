#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include <dx12/ffx_api_dx12.h>
#include <ffx_api.hpp>
#include <ffx_api_loader.h>
#include <ffx_api_types.h>
#include <ffx_framegeneration.hpp>

#include "Buffer.h"
#include "State.h"

/**
 * @brief AMD FidelityFX integration class for advanced upscaling and frame generation.
 * 
 * This singleton class provides integration with AMD's FidelityFX technologies, including
 * FSR3 upscaling and frame generation capabilities. It manages the lifecycle of FidelityFX
 * contexts and provides high-level interfaces for upscaling operations and frame generation.
 * 
 * The class handles:
 * - Dynamic loading of FidelityFX DLL
 * - Setup and management of FSR3 upscaling context
 * - Frame generation context management for DX12
 * - Presentation pipeline with optional frame generation
 * - Resource management for upscaling operations
 */
class FidelityFX
{
public:
	/**
	 * @brief Gets the singleton instance of the FidelityFX class.
	 * 
	 * Uses the Meyer's singleton pattern to ensure thread-safe initialization
	 * and guaranteed destruction on program termination.
	 * 
	 * @return FidelityFX* Pointer to the singleton instance
	 */
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	/**
	 * @brief Handle to the dynamically loaded FidelityFX DLL module.
	 * 
	 * This handle is used to access FidelityFX functions from the 
	 * amd_fidelityfx_dx12.dll library. Set to nullptr if the module
	 * failed to load or hasn't been loaded yet.
	 */
	HMODULE module = nullptr;

	/**
	 * @brief FidelityFX context for swap chain operations and frame pacing.
	 * 
	 * This context manages swap chain-related FidelityFX operations including
	 * frame pacing tuning and presentation flow coordination.
	 */
	ffx::Context swapChainContext{};

	/**
	 * @brief FidelityFX context for frame generation operations.
	 * 
	 * This context handles frame generation workloads using FidelityFX
	 * frame generation technology to improve frame rates through interpolation.
	 */
	ffx::Context frameGenContext;

	/**
	 * @brief FSR3 upscaling context for spatial upscaling operations.
	 * 
	 * This context manages FidelityFX Super Resolution 3.0 upscaling operations,
	 * including temporal accumulation, motion vector processing, and sharpening.
	 */
	FfxFsr3Context fsrContext;

	/**
	 * @brief Dynamically loads the FidelityFX DLL and initializes function pointers.
	 * 
	 * Attempts to load the amd_fidelityfx_dx12.dll from the SKSE plugins directory
	 * and initializes the global ffxModule function table. This must be called
	 * before any other FidelityFX operations.
	 */
	void LoadFFX();

	/**
	 * @brief Sets up the frame generation context for DX12 operations.
	 * 
	 * Initializes the frame generation context with appropriate display and render
	 * sizes based on the current swap chain configuration. Enables async workload
	 * support and configures the backend for DX12 operations.
	 * 
	 * @note Requires a valid DX12 swap chain to be available in globals::dx12SwapChain
	 */
	void SetupFrameGeneration();

	/**
	 * @brief Presents the current frame with optional frame generation.
	 * 
	 * Configures and dispatches the presentation pipeline, optionally enabling
	 * FidelityFX frame generation for improved frame rates. Sets up frame pacing
	 * tuning, prepares required resources (HUD-less color, depth, motion vectors),
	 * and manages the frame generation callback system.
	 * 
	 * @param a_useFrameGeneration If true, enables frame generation and dispatches
	 *                             the necessary workloads; otherwise presents without
	 *                             frame generation
	 */
	void Present(bool a_useFrameGeneration);

	/**
	 * @brief Creates and initializes FSR3 upscaling resources and context.
	 * 
	 * Sets up the FSR3 upscaling context with appropriate render, upscale, and display
	 * sizes based on current screen resolution. Initializes the DX11 backend interface
	 * and allocates scratch memory for FSR operations. Enables upscaling-only mode
	 * with auto-exposure.
	 * 
	 * @note Must be called before any upscaling operations can be performed
	 */
	void CreateFSRResources();

	/**
	 * @brief Destroys FSR3 resources and cleans up the upscaling context.
	 * 
	 * Properly shuts down the FSR3 context and frees associated resources.
	 * Should be called during cleanup or when FSR is no longer needed.
	 */
	void DestroyFSRResources();

	/**
	 * @brief Performs FSR3 upscaling on the provided input textures.
	 * 
	 * Dispatches FSR3 upscaling using the provided color and alpha mask textures.
	 * Configures motion vectors, depth, jitter offset, and camera parameters for
	 * optimal upscaling quality. Applies temporal accumulation and optional sharpening.
	 * 
	 * @param a_color Input color texture to be upscaled
	 * @param a_alphaMask Alpha mask texture used as reactive mask for better quality
	 * @param a_jitter Camera jitter offset applied for temporal anti-aliasing
	 * @param a_sharpness Sharpening intensity to apply during upscaling (0.0-1.0)
	 * 
	 * @note Requires CreateFSRResources() to have been called first
	 * @note The upscaling is performed in-place, modifying the input color texture
	 */
	void Upscale(Texture2D* a_color, Texture2D* a_alphaMask, float2 a_jitter, float a_sharpness);
};
