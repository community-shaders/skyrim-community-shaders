#pragma once

#include "../../Buffer.h"
#include "../../State.h"

#include <cstdint>
#include <d3d11_4.h>
#include <d3d12.h>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_matrix_helpers.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

/** @brief Manages NVIDIA Streamline integration for DLSS upscaling and Reflex latency reduction. */
class Streamline
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\Streamline";

	Streamline() = default;

	/** @brief Returns the short identifier used for logging. */
	inline std::string GetShortName() { return "Streamline"; }

	bool enabledAtBoot = false;
	bool initialized = false;
	bool triedInitialization = false;

	bool featureDLSS = false;
	bool featureReflex = false;
	bool featurePCL = false;
	bool reflexSupportedOnCurrentAdapter = false;

	sl::ViewportHandle viewport{ 0 };
	static constexpr uint32_t MAX_RESOLUTION = 8192;
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

	// Reflex specific functions
	PFun_slReflexGetState* slReflexGetState{};
	PFun_slReflexSleep* slReflexSleep{};
	PFun_slReflexSetOptions* slReflexSetOptions{};
	PFun_slPCLSetMarker* slPCLSetMarker{};

	Util::FrameChecker frameChecker;
	sl::FrameToken* frameToken = nullptr;

	bool isRTXBelow40series = false;

	struct ReflexOptionsCache
	{
		bool valid = false;
		sl::ReflexMode mode = sl::ReflexMode::eOff;
		uint32_t frameLimitUs = 0;
		bool useMarkersToOptimize = false;
	};
	ReflexOptionsCache reflexOptionsCache{};
	uint32_t lastReflexSleepFrame = UINT32_MAX;

	/**
	 * @brief Executes DLSS evaluation for a single viewport with the given resources.
	 * @param vp The viewport handle identifying the DLSS instance.
	 * @param colorIn Input color texture to upscale.
	 * @param colorOut Output texture receiving the upscaled result.
	 * @param depth Depth buffer for temporal reprojection.
	 * @param mvec Per-pixel motion vectors.
	 * @param reactiveMask Reactive mask for temporal stability hints.
	 * @param transparencyMask Mask for transparency and composition handling.
	 * @param extentIn Input resolution extent.
	 * @param extentOut Output resolution extent.
	 * @param outputWidth Target output width for DLSS options.
	 */
	void EvaluateDLSS(sl::ViewportHandle vp,
		ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
		ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
		const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth);

	// Cached DLL version info for Streamline plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	/** @brief Loads the Streamline interposer DLL and initializes the SDK with feature preferences. */
	void LoadInterposer();

	/**
	 * @brief Queries available Streamline features (DLSS, Reflex, PCL) on the given adapter.
	 * @param a_adapter The DXGI adapter to check feature support against.
	 */
	void CheckFeatures(IDXGIAdapter* a_adapter);

	/** @brief Binds DLSS and Reflex feature functions after the D3D device is created. */
	void PostDevice();

	/** @brief Acquires a new frame token from Streamline for the current frame. */
	bool EnsureFrameToken();
	/**
	 * @brief Sets camera and jitter constants on the Streamline viewport for the current frame.
	 * @param p_viewport The viewport handle to configure.
	 * @return True if constants were set successfully.
	 */
	bool CheckFrameConstants(sl::ViewportHandle p_viewport);

	/**
	 * @brief Detects whether the GPU is an NVIDIA RTX card below the 40-series generation.
	 * @param a_adapter The DXGI adapter to inspect.
	 * @return True if the adapter is RTX 20xx or 30xx series.
	 */
	bool IsRTXAndBelow40Series(IDXGIAdapter* a_adapter);

	/**
	 * @brief Configures DLSS quality mode and resolution options for a viewport.
	 * @param p_viewport The viewport handle to configure.
	 * @param width The target output width.
	 */
	void SetDLSSOptions(sl::ViewportHandle p_viewport, uint32_t width);

	/**
	 * @brief Dispatches DLSS upscaling for the current frame.
	 * @param a_upscalingTexture The input color texture to upscale.
	 * @param a_reactiveMask Reactive mask for temporal stability hints.
	 * @param a_transparencyCompositionMask Mask for transparency handling.
	 * @param a_motionVectors Per-pixel motion vectors for temporal reprojection.
	 */
	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors);
	/** @brief Updates Reflex latency reduction state and performs the Reflex sleep call. */
	void UpdateReflex();

	/** @brief Frees DLSS viewport resources through the Streamline SDK. */
	void DestroyDLSSResources();
};
