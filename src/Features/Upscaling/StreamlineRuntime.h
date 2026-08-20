#pragma once

#include "DlssgPresenterState.h"
#include "FrameGenWatchdog.h"
#include "ReflexController.h"
#include "StreamlineSdk.h"
#include "UpscalerEvaluatorState.h"

#include <atomic>
#include <filesystem>

/** Owns the Streamline module, resolved SDK exports, and process-lifetime SDK state. */
class StreamlineRuntime
{
public:
	bool Load(const std::filesystem::path& a_path);
	void Unload();

	template <typename T>
	bool Resolve(T*& a_fn, const char* a_name)
	{
		a_fn = reinterpret_cast<T*>(GetProcAddress(interposer, a_name));
		return a_fn != nullptr;
	}

	HMODULE interposer = nullptr;
	PFun_slInit* slInit = nullptr;
	PFun_slIsFeatureSupported* slIsFeatureSupported = nullptr;
	PFun_slGetNewFrameToken* slGetNewFrameToken = nullptr;
	PFun_slSetTagForFrame* slSetTagForFrame = nullptr;
	PFun_slSetConstants* slSetConstants = nullptr;
	PFun_slEvaluateFeature* slEvaluateFeature = nullptr;
	PFun_slGetFeatureFunction* slGetFeatureFunction = nullptr;
	PFun_slSetFeatureLoaded* slSetFeatureLoaded = nullptr;
	PFun_slIsFeatureLoaded* slIsFeatureLoaded = nullptr;
	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings = nullptr;
	PFun_slDLSSSetOptions* slDLSSSetOptions = nullptr;
	PFun_slReflexGetState* slReflexGetState = nullptr;
	PFun_slReflexSetOptions* slReflexSetOptions = nullptr;
	PFun_slReflexSleep* slReflexSleep = nullptr;
	sl::Result (*slReflexSetExternalPacing)(bool) = nullptr;
	PFun_slPCLSetMarker* slPCLSetMarker = nullptr;
	PFun_slDLSSGSetOptions* slDLSSGSetOptions = nullptr;
	PFun_slDLSSGGetState* slDLSSGGetState = nullptr;
	PFun_slFSRSetOptions* slFSRSetOptions = nullptr;
	PFun_slFSRFrameGenerationSetOptions* slFSRFrameGenerationSetOptions = nullptr;
	PFun_slFSRGetFrameGenState* slFSRGetFrameGenState = nullptr;
	PFun_slFSRFrameGenerationDiscardPreparedFrame* slFSRFrameGenerationDiscardPreparedFrame = nullptr;
	PFun_slFSRFrameGenerationOwnsSwapchain* slFSRFrameGenerationOwnsSwapchain = nullptr;
	PFun_slFSRFrameGenerationCompleteSwapchainTeardown* slFSRFrameGenerationCompleteSwapchainTeardown = nullptr;
	PFun_slXeSSSetOptions* slXeSSSetOptions = nullptr;

	UpscalerEvaluatorState evaluator;
	DlssgPresenterState dlssg;
	ReflexController reflex;
	FrameGenWatchdog watchdog;
	std::atomic<bool> dlssgDesiredLoaded{ false };
	std::atomic<bool> dlssgCurrentlyLoaded{ false };
	std::atomic<bool> fsrfgDesiredLoaded{ false };
	std::atomic<bool> fsrfgCurrentlyLoaded{ false };
	std::atomic<bool> fsrfgOwnsPresent{ false };
};
