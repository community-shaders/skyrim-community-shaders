#pragma once

#include "Buffer.h"
#include "State.h"

#include <d3d11_4.h>
#include <d3d12.h>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

/**
 * @brief Returns the singleton instance of the Streamline class.
 *
 * Ensures only one instance of Streamline exists throughout the application's lifetime.
 *
 * @return Pointer to the singleton Streamline instance.
 */
static Streamline* GetSingleton();

/**
 * @brief Returns a short name identifier for the Streamline class.
 *
 * @return The string "Streamline".
 */
inline std::string GetShortName();

/**
 * @brief Loads the NVIDIA Streamline interposer DLL and initializes function pointers.
 *
 * Dynamically loads the required Streamline SDK library and sets up function pointers for feature management.
 */
void LoadInterposer();

/**
 * @brief Checks which NVIDIA Streamline features are supported on the given DXGI adapter.
 *
 * Updates internal flags for DLSS, DLSSG, and Reflex feature support based on the provided adapter.
 *
 * @param a_adapter Pointer to the DXGI adapter to query for feature support.
 */
void CheckFeatures(IDXGIAdapter* a_adapter);

/**
 * @brief Performs post-device creation setup for Streamline integration.
 *
 * Should be called after the Direct3D device is created to complete Streamline initialization.
 */
void PostDevice();

/**
 * @brief Validates or updates frame-related constants for Streamline operations.
 *
 * Ensures that frame constants required by Streamline features are current and correct.
 */
void CheckFrameConstants();

/**
 * @brief Performs DLSS upscaling on the provided color and alpha mask textures.
 *
 * Uses the specified DLSS preset to upscale the input textures via NVIDIA Streamline.
 *
 * @param a_color Pointer to the color texture to be upscaled.
 * @param a_alphaMask Pointer to the alpha mask texture, if used.
 * @param a_preset DLSS preset specifying the upscaling quality and performance settings.
 */
void Upscale(Texture2D* a_color, Texture2D* a_alphaMask, sl::DLSSPreset a_preset);

/**
 * @brief Handles presentation logic for Streamline, finalizing or submitting the current frame.
 *
 * Should be called at the end of the rendering pipeline to complete Streamline processing for the frame.
 */
void Present();

/**
 * @brief Releases and cleans up resources allocated for DLSS operations.
 *
 * Frees any memory or handles associated with DLSS to prevent resource leaks.
 */
void DestroyDLSSResources();
class Streamline
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\SKSE\\Plugins\\Streamline";

	static Streamline* GetSingleton()
	{
		static Streamline singleton;
		return &singleton;
	}

	inline std::string GetShortName() { return "Streamline"; }

	bool enabledAtBoot = false;
	bool initialized = false;
	bool triedInitialization = false;

	bool featureDLSS = false;
	bool featureDLSSG = false;
	bool featureReflex = false;
	bool featurePCL = false;

	// Track if Streamline is currently being used for frame generation
	bool isFrameGenActive = false;

	sl::ViewportHandle viewport{ 0 };

	HMODULE interposer = NULL;

	// SL Interposer Functions
	PFun_slInit* slInit{};
	PFun_slShutdown* slShutdown{};
	PFun_slIsFeatureSupported* slIsFeatureSupported{};
	PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
	PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
	PFun_slEvaluateFeature* slEvaluateFeature{};
	PFun_slAllocateResources* slAllocateResources{};
	PFun_slFreeResources* slFreeResources{};
	PFun_slSetTag* slSetTag{};
	PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
	PFun_slGetFeatureVersion* slGetFeatureVersion{};
	PFun_slUpgradeInterface* slUpgradeInterface{};
	PFun_slSetConstants* slSetConstants{};
	PFun_slGetNativeInterface* slGetNativeInterface{};
	PFun_slGetFeatureFunction* slGetFeatureFunction{};
	PFun_slGetNewFrameToken* slGetNewFrameToken{};
	PFun_slSetD3DDevice* slSetD3DDevice{};

	// DLSS specific functions
	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
	PFun_slDLSSGetState* slDLSSGetState{};
	PFun_slDLSSSetOptions* slDLSSSetOptions{};

	// DLSSG specific functions
	PFun_slDLSSGGetState* slDLSSGGetState{};
	PFun_slDLSSGSetOptions* slDLSSGSetOptions{};

	// Reflex specific functions
	PFun_slReflexGetState* slReflexGetState{};
	PFun_slReflexSleep* slReflexSleep{};
	PFun_slReflexSetOptions* slReflexSetOptions{};

	// PCL (PC Latency) marker API for Streamline 2.8.0+
	PFun_slPCLSetMarker* slPCLSetMarker{};
	PFun_slPCLSetOptions* slPCLSetOptions{};

	Util::FrameChecker frameChecker;
	sl::FrameToken* frameToken;

	decltype(&CreateDXGIFactory1) slCreateDXGIFactory1{};
	decltype(&D3D11CreateDeviceAndSwapChain) slD3D11CreateDeviceAndSwapChain{};

	// Cached DLL version info for Streamline plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadInterposer();

	void CheckFeatures(IDXGIAdapter* a_adapter);

	void PostDevice();

	void CheckFrameConstants();

	void Upscale(Texture2D* a_color, Texture2D* a_alphaMask, sl::DLSSPreset a_preset);
	void Present();
	void DestroyDLSSResources();
};
