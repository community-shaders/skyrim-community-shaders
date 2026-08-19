#include "Streamline.h"

#include "../Upscaling.h"
#include "DXVKInterop.h"
#include "DlssgPresenterState.h"
#include "EvaluationCleanupPolicy.h"
#include "FrameGenController.h"
#include "FrameGenWatchdog.h"
#include "FrameGenerationBridge.h"
#include "ReflexController.h"
#include "StreamlineSdk.h"
#include "StreamlineRuntime.h"
#include "UpscalerEvaluatorState.h"
#include "UpscalerEvaluator.h"
#include "UpscalingRuntime.h"
#include "VulkanResourceBridge.h"
#include "WindowsGpuRecovery.h"

#include "../../DxvkLoader.h"
#include "../../Globals.h"
#include "../../State.h"
#include "../../Utils/Game.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

#define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4471 5103)
#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_device_wrappers.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_fsr.h>
#include <sl_fsr_g.h>
#include <sl_xess.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

namespace
{
	StreamlineRuntime* g_activeState = nullptr;

	StreamlineRuntime& ActiveState()
	{
		assert(g_activeState);
		return *g_activeState;
	}

	using DxvkPresentCallbackInfo = CsDxvkPresentCallbackInfo;

	uint64_t PresentClockNs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	void StartDlssgWatchdog()
	{
		ActiveState().watchdog.Start(ActiveState().dlssg.modeOn, ActiveState().dlssg.renderHeartbeatNs, ActiveState().dlssg.presentHeartbeatNs,
			WindowsGpuRecovery::Request);
	}

	// Keep this free of C++ unwinding because it executes inside __try.
	bool ReconcileFgFeatureLoad(sl::Feature a_feature, std::atomic<bool>& a_desired, std::atomic<bool>& a_current)
	{
		const bool want = a_desired.load(std::memory_order_acquire);
		if (want == a_current.load(std::memory_order_acquire))
			return true;
		if (!ActiveState().slSetFeatureLoaded || ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
			return false;

		// The present thread calls into DLSS-G through SetOptions/GetState. Serialize
		// feature load changes with those calls so Streamline cannot tear down its
		// Vulkan dispatch state while one of them is still submitting work. Publish
		// the unloaded state before calling Streamline so newly arriving callbacks
		// bail out instead of waiting to use stale entry points.
		if (a_feature == sl::kFeatureDLSS_G) {
			ActiveState().dlssg.apiMutex.lock();
			const bool lockedWant = a_desired.load(std::memory_order_acquire);
			const bool wasLoaded = a_current.load(std::memory_order_acquire);
			if (lockedWant == wasLoaded) {
				ActiveState().dlssg.apiMutex.unlock();
				return true;
			}

			if (!lockedWant)
				a_current.store(false, std::memory_order_release);

			bool succeeded = false;
			__try {
				succeeded = ActiveState().slSetFeatureLoaded(a_feature, lockedWant) == sl::Result::eOk;
				if (succeeded && lockedWant) {
					ActiveState().slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void*&>(ActiveState().slDLSSGSetOptions));
					ActiveState().slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(ActiveState().slDLSSGGetState));
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				ActiveState().evaluator.dispatchFaulted = true;
			}

			if (succeeded) {
				a_current.store(lockedWant, std::memory_order_release);
			} else {
				a_current.store(wasLoaded, std::memory_order_release);
			}
			ActiveState().dlssg.apiMutex.unlock();
			return succeeded;
		}

		__try {
			if (ActiveState().slSetFeatureLoaded(a_feature, want) != sl::Result::eOk)
				return false;
			a_current.store(want, std::memory_order_release);
			if (a_feature == sl::kFeatureFSR_G && !want)
				ActiveState().fsrfgOwnsPresent.store(false, std::memory_order_release);
			if (want) {
				// DLSS-G returned through the serialized path above. Only FSR-G
				// reaches this generic feature-load path.
				if (a_feature == sl::kFeatureFSR_G) {
					ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationSetOptions", reinterpret_cast<void*&>(ActiveState().slFSRFrameGenerationSetOptions));
					ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRGetFrameGenState", reinterpret_cast<void*&>(ActiveState().slFSRGetFrameGenState));
					ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationDiscardPreparedFrame", reinterpret_cast<void*&>(ActiveState().slFSRFrameGenerationDiscardPreparedFrame));
					ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationOwnsSwapchain", reinterpret_cast<void*&>(ActiveState().slFSRFrameGenerationOwnsSwapchain));
					ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationCompleteSwapchainTeardown", reinterpret_cast<void*&>(ActiveState().slFSRFrameGenerationCompleteSwapchainTeardown));
				}
			}
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			ActiveState().evaluator.dispatchFaulted = true;
			return false;
		}
	}

	// Runs between DXVK swapchain destruction and creation.
	bool DxvkSwapchainTornDownCallback()
	{
		if (ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
			return false;
		const bool wantDLSSG = ActiveState().dlssgDesiredLoaded.load(std::memory_order_acquire);
		const bool wantFSRFG = ActiveState().fsrfgDesiredLoaded.load(std::memory_order_acquire);
		if (wantDLSSG && wantFSRFG) {
			logger::error("[Streamline] refusing to load both frame-generation present plugins");
			return false;
		}

		// Per-swapchain options and semaphores are invalid after teardown.
		ActiveState().dlssg.modeCached = false;
		ActiveState().dlssg.modeOn = false;
		ActiveState().dlssg.optionsPending = false;
		ActiveState().dlssg.cloneTagsPrimed.store(false, std::memory_order_release);

		if (ActiveState().fsrfgCurrentlyLoaded.load(std::memory_order_acquire)) {
			if (!ActiveState().slFSRFrameGenerationCompleteSwapchainTeardown)
				return false;
			bool teardownComplete = false;
			const bool releaseFeatureContext = !ActiveState().fsrfgDesiredLoaded.load(std::memory_order_acquire);
			__try {
				teardownComplete = ActiveState().slFSRFrameGenerationCompleteSwapchainTeardown(releaseFeatureContext);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				ActiveState().evaluator.dispatchFaulted = true;
			}
			if (!teardownComplete)
				return false;
			Upscaling::Runtime().CompleteFSRSwapchainTeardown();
		}

		// Remove the outgoing WSI hook set before mapping the incoming one. This
		// keeps the interposer's hook table single-owner throughout a method switch.
		if (!wantDLSSG && !ReconcileFgFeatureLoad(
				sl::kFeatureDLSS_G, ActiveState().dlssgDesiredLoaded, ActiveState().dlssgCurrentlyLoaded))
			return false;
		if (!wantFSRFG && !ReconcileFgFeatureLoad(
				sl::kFeatureFSR_G, ActiveState().fsrfgDesiredLoaded, ActiveState().fsrfgCurrentlyLoaded))
			return false;
		if (wantDLSSG && !ReconcileFgFeatureLoad(
				sl::kFeatureDLSS_G, ActiveState().dlssgDesiredLoaded, ActiveState().dlssgCurrentlyLoaded))
			return false;
		if (wantFSRFG && !ReconcileFgFeatureLoad(
				sl::kFeatureFSR_G, ActiveState().fsrfgDesiredLoaded, ActiveState().fsrfgCurrentlyLoaded))
			return false;
		return true;
	}

	// DXVK uses the owner kind to preserve DLSS-G's present ID while still
	// stripping unsupported pNext structures from FSR's replacement swapchain.
	uint32_t DxvkFrameGenerationOwnsSwapchain(VkSwapchainKHR a_swapchain)
	{
		if (ActiveState().dlssgCurrentlyLoaded.load(std::memory_order_acquire))
			return CS_DXVK_FRAME_GEN_DLSS_G;
		if (ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
			return ActiveState().fsrfgOwnsPresent.load(std::memory_order_acquire) ? CS_DXVK_FRAME_GEN_FSR : CS_DXVK_FRAME_GEN_NONE;
		if (!ActiveState().fsrfgOwnsPresent.load(std::memory_order_acquire) || !ActiveState().slFSRFrameGenerationOwnsSwapchain)
			return CS_DXVK_FRAME_GEN_NONE;

		bool ownsSwapchain = false;
		__try {
			ownsSwapchain = ActiveState().slFSRFrameGenerationOwnsSwapchain(a_swapchain);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			ActiveState().evaluator.dispatchFaulted = true;
		}
		return ownsSwapchain ? CS_DXVK_FRAME_GEN_FSR : CS_DXVK_FRAME_GEN_NONE;
	}

	uint32_t EmitBridgedPresentMarker(bool a_begin)
	{
		if (!ActiveState().slPCLSetMarker || ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
			return 0;

		uint32_t frameId = 0;
		if (a_begin) {
			const uint32_t head = ActiveState().dlssg.presentMarkerHead.load(std::memory_order_relaxed);
			if (head == ActiveState().dlssg.presentMarkerTail.load(std::memory_order_acquire))
				return 0;
			frameId = ActiveState().dlssg.presentMarkerFrames[head % ActiveState().dlssg.presentMarkerFrames.size()].load(std::memory_order_relaxed);
			ActiveState().dlssg.presentMarkerHead.store(head + 1u, std::memory_order_release);
			ActiveState().dlssg.activePresentMarkerFrame.store(frameId, std::memory_order_release);
		} else {
			frameId = ActiveState().dlssg.activePresentMarkerFrame.exchange(0u, std::memory_order_acq_rel);
			if (!frameId)
				return 0;
		}

		__try {
			sl::FrameToken* token = nullptr;
			if (ActiveState().slGetNewFrameToken(token, &frameId) == sl::Result::eOk && token)
				ActiveState().slPCLSetMarker(a_begin ? sl::PCLMarker::ePresentStart : sl::PCLMarker::ePresentEnd, *token);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			ActiveState().evaluator.dispatchFaulted = true;
		}
		return frameId;
	}

	// Apply pending options immediately before the real, Streamline-interposed
	// vkQueuePresentKHR. This makes the mode change and the Present that consumes
	// it unambiguous and avoids racing DXVK's asynchronous presenter.
	void DxvkPresentBeginCallback(const DxvkPresentCallbackInfo* a_info)
	{
		if (!a_info || a_info->size < sizeof(DxvkPresentCallbackInfo) ||
			a_info->version != 1u || a_info->frameGenOwner != 2u) {
			logger::critical("[DLSSG-Ledger] rejected invalid DXVK present callback payload");
			ActiveState().evaluator.dispatchFaulted = true;
			return;
		}
		if (!ActiveState().dlssgCurrentlyLoaded.load(std::memory_order_acquire) ||
			!ActiveState().slDLSSGSetOptions || ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
			return;

		ActiveState().dlssg.apiMutex.lock();
		if (!ActiveState().dlssgCurrentlyLoaded.load(std::memory_order_acquire) ||
			!ActiveState().slDLSSGSetOptions || ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire)) {
			ActiveState().dlssg.apiMutex.unlock();
			return;
		}
		if (!ActiveState().dlssg.optionsPending.load(std::memory_order_acquire)) {
			ActiveState().dlssg.apiMutex.unlock();
			const uint32_t markerFrame = EmitBridgedPresentMarker(true);
			ActiveState().dlssg.activeDxvkFrameId.store(a_info->frameId, std::memory_order_release);
			ActiveState().dlssg.activeSwapchainSerial.store(a_info->swapchainSerial, std::memory_order_release);
			ActiveState().dlssg.activePresentStartNs.store(PresentClockNs(), std::memory_order_release);
			if (ActiveState().dlssg.ledgerBudget.load(std::memory_order_acquire)) {
				logger::info("[DLSSG-Ledger] begin epoch={} dxvkFrame={} markerFrame={} swapchain={:#x}/{} presenter={:#x} image={} queue={:#x} waitGen={} pendingWaits={} markerDepth={} tid={}",
					ActiveState().dlssg.optionsEpoch.load(std::memory_order_acquire), a_info->frameId, markerFrame,
					a_info->swapchain, a_info->swapchainSerial, a_info->presenter, a_info->imageIndex,
					a_info->queue, a_info->presentWaitGeneration, a_info->pendingPresentWaitCount,
					ActiveState().dlssg.presentMarkerTail.load(std::memory_order_acquire) - ActiveState().dlssg.presentMarkerHead.load(std::memory_order_acquire),
					GetCurrentThreadId());
			}
			return;
		}

		bool succeeded = false;
		__try {
			const sl::Result result = ActiveState().slDLSSGSetOptions(ActiveState().evaluator.viewport, ActiveState().dlssg.pendingOptions);
			if (result == sl::Result::eOk) {
				succeeded = true;
				ActiveState().dlssg.modeOn.store(ActiveState().dlssg.pendingEnable, std::memory_order_release);
				ActiveState().dlssg.transitionPresentAcks.store(0u, std::memory_order_release);
				ActiveState().dlssg.transitionCompletionValue.store(0u, std::memory_order_release);
				logger::info("[Streamline] applied present-ordered DLSS-G mode={} numFrames={} render={}x{} display={}x{}",
					ActiveState().dlssg.pendingEnable, ActiveState().dlssg.pendingOptions.numFramesToGenerate,
					ActiveState().dlssg.pendingRenderW, ActiveState().dlssg.pendingRenderH,
					ActiveState().dlssg.pendingDisplayW, ActiveState().dlssg.pendingDisplayH);
			} else {
				logger::warn("[Streamline] present-thread slDLSSGSetOptions failed (result {})", static_cast<int>(result));
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			ActiveState().evaluator.dispatchFaulted = true;
			logger::error("[Streamline] present-thread DLSS-G SetOptions faulted - Streamline disabled for this session");
		}
		ActiveState().dlssg.optionsPending.store(false, std::memory_order_release);
		if (!succeeded)
			ActiveState().dlssg.modeCached = false;
		ActiveState().dlssg.apiMutex.unlock();

		// The marker pair and the present must observe the same DLSS-G mode.
		// In particular, enabling after PresentStart can make the pacer wait for
		// work that was never scheduled for that frame.
		const uint32_t markerFrame = EmitBridgedPresentMarker(true);
		ActiveState().dlssg.activeDxvkFrameId.store(a_info->frameId, std::memory_order_release);
		ActiveState().dlssg.activeSwapchainSerial.store(a_info->swapchainSerial, std::memory_order_release);
		ActiveState().dlssg.activePresentStartNs.store(PresentClockNs(), std::memory_order_release);
		if (ActiveState().dlssg.ledgerBudget.load(std::memory_order_acquire)) {
			logger::info("[DLSSG-Ledger] begin epoch={} dxvkFrame={} markerFrame={} swapchain={:#x}/{} presenter={:#x} image={} queue={:#x} waitGen={} pendingWaits={} markerDepth={} tid={}",
				ActiveState().dlssg.optionsEpoch.load(std::memory_order_acquire), a_info->frameId, markerFrame,
				a_info->swapchain, a_info->swapchainSerial, a_info->presenter, a_info->imageIndex,
				a_info->queue, a_info->presentWaitGeneration, a_info->pendingPresentWaitCount,
				ActiveState().dlssg.presentMarkerTail.load(std::memory_order_acquire) - ActiveState().dlssg.presentMarkerHead.load(std::memory_order_acquire),
				GetCurrentThreadId());
		}
	}

	// DXVK invokes this after the real, Streamline-interposed vkQueuePresentKHR
	// returns. NVIDIA requires slDLSSGGetState to run on that present thread.
	// In queue-parallel mode, associate the input-completion timeline with the
	// command-ring slot for this present. Slot selection later polls completion
	// without blocking; a Vulkan queue wait here could form a GPU queue cycle.
	void DxvkPresentCompletedCallback(const DxvkPresentCallbackInfo* a_info)
	{
		if (!a_info || a_info->size < sizeof(DxvkPresentCallbackInfo) ||
			a_info->version != 1u || a_info->frameGenOwner != 2u) {
			logger::critical("[DLSSG-Ledger] rejected invalid DXVK present completion payload");
			ActiveState().evaluator.dispatchFaulted = true;
			return;
		}
		const uint64_t beginNs = ActiveState().dlssg.activePresentStartNs.exchange(0u, std::memory_order_acq_rel);
		const uint64_t durationUs = beginNs ? (PresentClockNs() - beginNs) / 1000u : 0u;
		const uint64_t activeFrame = ActiveState().dlssg.activeDxvkFrameId.exchange(0u, std::memory_order_acq_rel);
		const uint64_t activeSwapchainSerial = ActiveState().dlssg.activeSwapchainSerial.exchange(0u, std::memory_order_acq_rel);
		if (!activeFrame)
			return;

		// Close the PCL present interval immediately after vkQueuePresentKHR.
		// Querying state inside that interval can make the pacer observe an
		// incomplete marker pair while it flushes the just-presented frame.
		const uint32_t markerFrame = EmitBridgedPresentMarker(false);
		const uint32_t budget = ActiveState().dlssg.ledgerBudget.load(std::memory_order_acquire);
		if (budget) {
			logger::info("[DLSSG-Ledger] end epoch={} dxvkFrame={} activeFrame={} markerFrame={} swapchain={:#x}/{} activeSerial={} image={} waitGen={} result={} presentUs={} tid={}",
				ActiveState().dlssg.optionsEpoch.load(std::memory_order_acquire), a_info->frameId, activeFrame,
				markerFrame, a_info->swapchain, a_info->swapchainSerial, activeSwapchainSerial,
				a_info->imageIndex, a_info->presentWaitGeneration, a_info->presentResult, durationUs,
				GetCurrentThreadId());
			ActiveState().dlssg.ledgerBudget.fetch_sub(1u, std::memory_order_acq_rel);
		}
		if (activeFrame != a_info->frameId || activeSwapchainSerial != a_info->swapchainSerial) {
			logger::critical("[DLSSG-Ledger] present identity mismatch beginFrame={} endFrame={} beginSerial={} endSerial={}",
				activeFrame, a_info->frameId, activeSwapchainSerial, a_info->swapchainSerial);
			ActiveState().evaluator.dispatchFaulted = true;
		}

		if (!ActiveState().dlssgCurrentlyLoaded.load(std::memory_order_acquire) ||
			!ActiveState().slDLSSGGetState || ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
			return;

		ActiveState().dlssg.apiMutex.lock();
		if (!ActiveState().dlssgCurrentlyLoaded.load(std::memory_order_acquire) ||
			!ActiveState().slDLSSGGetState || ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire)) {
			ActiveState().dlssg.apiMutex.unlock();
			return;
		}
		__try {
			sl::DLSSGState state{};
			if (ActiveState().slDLSSGGetState(ActiveState().evaluator.viewport, state, nullptr) == sl::Result::eOk) {
				if (ActiveState().dlssg.ledgerBudget.load(std::memory_order_acquire)) {
					logger::info("[DLSSG-Ledger] state completionFence={:#x} completionValue={} status={} presented={}",
						reinterpret_cast<uintptr_t>(state.inputsProcessingCompletionFence),
						state.lastPresentInputsProcessingCompletionFenceValue,
						static_cast<uint32_t>(state.status), state.numFramesActuallyPresented);
				}
				if (ActiveState().dlssg.modeOn.load(std::memory_order_acquire) &&
					state.inputsProcessingCompletionFence &&
					state.lastPresentInputsProcessingCompletionFenceValue) {
					const uint64_t previousTransitionValue =
						ActiveState().dlssg.transitionCompletionValue.exchange(
							state.lastPresentInputsProcessingCompletionFenceValue,
							std::memory_order_acq_rel);
					if (state.numFramesActuallyPresented > 1u &&
						state.lastPresentInputsProcessingCompletionFenceValue > previousTransitionValue)
						ActiveState().dlssg.transitionPresentAcks.fetch_add(1u, std::memory_order_acq_rel);
					const bool tracked = Upscaling::Runtime().TrackPresentInputCompletion(
						a_info->presentWaitGeneration,
						reinterpret_cast<VkSemaphore>(state.inputsProcessingCompletionFence),
						state.lastPresentInputsProcessingCompletionFenceValue);
					if (ActiveState().dlssg.ledgerBudget.load(std::memory_order_acquire))
						logger::info("[DLSSG-Ledger] completion tracked waitGen={} value={} tracked={}",
							a_info->presentWaitGeneration,
							state.lastPresentInputsProcessingCompletionFenceValue, tracked);
					if (!tracked) {
						ActiveState().evaluator.dispatchFaulted = true;
						logger::error("[Streamline] DLSS-G input completion could not be associated with its tagged slot; disabling Streamline for this session");
					}
				}
				// A successful state query after Present proves that the proxy swapchain
				// and its cloned buffers are ready, even while interpolation is off.
				ActiveState().dlssg.cloneTagsPrimed.store(true, std::memory_order_release);
				if (state.numFramesToGenerateMax > 0u) {
					const uint32_t previous = ActiveState().dlssg.maxFramesToGenerate.exchange(
						state.numFramesToGenerateMax, std::memory_order_acq_rel);
					ActiveState().dlssg.dynamicSupported.store(
						state.bIsDynamicMFGSupported == sl::Boolean::eTrue, std::memory_order_release);
					if (!previous) {
						logger::info("[Streamline] DLSS-G numFramesToGenerateMax = {} (max {}x multiplier), DynamicMFG supported = {}",
							state.numFramesToGenerateMax, state.numFramesToGenerateMax + 1u,
							state.bIsDynamicMFGSupported == sl::Boolean::eTrue);
					}
				}

				if (ActiveState().dlssg.modeOn.load(std::memory_order_acquire)) {
					ActiveState().dlssg.frameGenerationMultiplier.store(
						std::max(state.numFramesActuallyPresented, 1u), std::memory_order_release);
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			ActiveState().evaluator.dispatchFaulted = true;
		}
		ActiveState().dlssg.apiMutex.unlock();
		ActiveState().dlssg.presentHeartbeatNs.store(PresentClockNs(), std::memory_order_release);
	}

	// Suppress exact known-benign diagnostics; pass all other messages through.
	bool IsBenignSLWarning(const char* a_msg)
	{
		if (!a_msg)
			return false;
		static constexpr const char* kBenign[] = {
			"setAsyncFrameMarker is not implemented",
			"is NOT supported, plugin will not function properly",
			"RSync will not run because it was not initialized",
			"Invalid backbuffer resource extent",
			"some DX/VK APIs were invoked before slInit",
			"reseting frame timer",
		};
		for (const char* needle : kBenign) {
			if (std::strstr(a_msg, needle))
				return true;
		}
		return false;
	}

	void LogCallback(sl::LogType a_type, const char* a_msg)
	{
		static const bool s_verbose = [] {
			char v[2] = {};
			return GetEnvironmentVariableA("CS_SL_VERBOSE", v, sizeof(v)) && v[0] == 0x31;
		}();
		if (s_verbose) {
			logger::info("[Streamline/SL] {}", a_msg);
			return;
		}
		if (a_type == sl::LogType::eWarn && IsBenignSLWarning(a_msg))
			return;
		switch (a_type) {
		case sl::LogType::eError:
			logger::warn("[Streamline/SL] {}", a_msg);
			break;
		case sl::LogType::eWarn:
			logger::debug("[Streamline/SL] {}", a_msg);
			break;
		default:
			logger::trace("[Streamline/SL] {}", a_msg);
			break;
		}
	}

	std::filesystem::path GetStreamlineDir()
	{
		return DxvkLoader::GetRuntimeDir();
	}

	template <typename T>
	bool Resolve(T*& a_fn, const char* a_name)
	{
		if (!ActiveState().Resolve(a_fn, a_name)) {
			logger::warn("[Streamline] missing interposer export '{}'", a_name);
			return false;
		}
		return true;
	}
}

StreamlineSession::StreamlineSession(VulkanDeviceContext& a_vulkan) : vulkan(a_vulkan), state(std::make_unique<StreamlineRuntime>())
{
	assert(!g_activeState);
	g_activeState = state.get();
}

StreamlineSession::~StreamlineSession()
{
	if (g_activeState == state.get())
		g_activeState = nullptr;
}

void StreamlineSession::PreloadInterposer()
{
	// Preload before DXVK creates VkInstance so its Vulkan loader aliases the interposer.
	if (disabledByConfig || ActiveState().interposer)
		return;
	const auto slDir = GetStreamlineDir();
	if (slDir.empty())
		return;
	const auto interposerPath = (slDir / L"sl.interposer.dll").wstring();
	ActiveState().Load(interposerPath);
	logger::info("[Streamline] interposer preload for DXVK Vulkan interposition: {}",
		ActiveState().interposer ? "mapped" : "FAILED (DXVK uses real driver)");
	if (!ActiveState().interposer)
		return;
	// slInit must precede DXVK's VkInstance creation.
	Initialize();
}

// Probe with the system loader before slInit decides which FG plugin to load.
static bool ProbeDLSSGHardware()
{
	if (char v[2] = {}; GetEnvironmentVariableA("CS_FORCE_FSR_FG", v, sizeof(v)) && v[0] == '1') {
		logger::info("[Streamline] CS_FORCE_FSR_FG=1: hardware probe reports no DLSS-G (FSR-FG path forced)");
		return false;
	}

	wchar_t sysDir[MAX_PATH]{};
	if (!GetSystemDirectoryW(sysDir, MAX_PATH))
		return false;
	const auto vkPath = std::wstring(sysDir) + L"\\vulkan-1.dll";
	HMODULE vk = LoadLibraryExW(vkPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!vk) {
		logger::warn("[Streamline] system vulkan-1.dll unavailable - assuming no DLSS-G hardware");
		return false;
	}

	bool found = false;
	auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr"));
	auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(GetProcAddress(vk, "vkCreateInstance"));
	if (gipa && createInstance) {
		VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		ici.pApplicationInfo = &app;
		VkInstance instance = VK_NULL_HANDLE;
		if (createInstance(&ici, nullptr, &instance) == VK_SUCCESS && instance) {
			auto enumDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
			auto enumExts = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gipa(instance, "vkEnumerateDeviceExtensionProperties"));
			auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
			if (enumDevices && enumExts) {
				uint32_t count = 0;
				enumDevices(instance, &count, nullptr);
				std::vector<VkPhysicalDevice> devices(count);
				enumDevices(instance, &count, devices.data());
				for (auto dev : devices) {
					uint32_t extCount = 0;
					enumExts(dev, nullptr, &extCount, nullptr);
					std::vector<VkExtensionProperties> exts(extCount);
					enumExts(dev, nullptr, &extCount, exts.data());
					for (const auto& e : exts) {
						if (std::strcmp(e.extensionName, "VK_NV_optical_flow") == 0) {
							found = true;
							break;
						}
					}
					if (found)
						break;
				}
			}
			if (destroyInstance)
				destroyInstance(instance, nullptr);
		}
	}
	FreeLibrary(vk);
	logger::info("[Streamline] hardware probe: DLSS-G-class GPU (VK_NV_optical_flow) {}", found ? "present" : "absent");
	return found;
}

bool StreamlineSession::Initialize()
{
	if (disabledByConfig)
		return false;
	if (triedInit)
		return initialized;
	triedInit = true;

	const auto slDir = GetStreamlineDir();
	if (slDir.empty()) {
		logger::warn("[Streamline] could not resolve plugin directory");
		return false;
	}

	const auto interposerPath = (slDir / L"sl.interposer.dll").wstring();
	if (!ActiveState().interposer)
		ActiveState().Load(interposerPath);
	if (!ActiveState().interposer) {
		logger::info("[Streamline] sl.interposer.dll not present in '{}' — Streamline features disabled", slDir.string());
		return false;
	}

	const bool resolved =
		Resolve(ActiveState().slInit, "slInit") &&
		Resolve(ActiveState().slIsFeatureSupported, "slIsFeatureSupported") &&
		Resolve(ActiveState().slGetNewFrameToken, "slGetNewFrameToken") &&
		Resolve(ActiveState().slSetTagForFrame, "slSetTagForFrame") &&
		Resolve(ActiveState().slSetConstants, "slSetConstants") &&
		Resolve(ActiveState().slEvaluateFeature, "slEvaluateFeature") &&
		Resolve(ActiveState().slGetFeatureFunction, "slGetFeatureFunction") &&
		Resolve(ActiveState().slIsFeatureLoaded, "slIsFeatureLoaded");

	Resolve(ActiveState().slSetFeatureLoaded, "slSetFeatureLoaded");
	if (!resolved) {
		ActiveState().Unload();
		return false;
	}

	const auto slDirWide = slDir.wstring();
	const wchar_t* pluginPaths[] = { slDirWide.c_str() };
	// Request both supported frame-generation plugins so support probing, menu
	// population, and later runtime switching have access to both interfaces.
	// They must not both retain Vulkan WSI hooks once a swapchain exists; the
	// inactive plugin is disabled after probing and before the first swapchain.
	dlssgHardware = ProbeDLSSGHardware();

	std::vector<sl::Feature> featuresToLoad = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL,
		sl::kFeatureFSR, sl::kFeatureFSR_G, sl::kFeatureXeSS };
	if (dlssgHardware) {
		featuresToLoad.push_back(sl::kFeatureDLSS_G);
		ActiveState().dlssgCurrentlyLoaded.store(true, std::memory_order_release);
	}
	ActiveState().fsrfgCurrentlyLoaded.store(true, std::memory_order_release);

	sl::Preferences pref{};
	pref.renderAPI = sl::RenderAPI::eVulkan;
	pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	pref.featuresToLoad = featuresToLoad.data();
	pref.numFeaturesToLoad = static_cast<uint32_t>(featuresToLoad.size());
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;
	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0";
	pref.projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";
	if (char v[2] = {}; GetEnvironmentVariableA("CS_SL_VERBOSE", v, sizeof(v)) && v[0] == 0x31)
		pref.logLevel = sl::LogLevel::eVerbose;
	else
		pref.logLevel = sl::LogLevel::eDefault;
	pref.logMessageCallback = &LogCallback;

	const sl::Result res = ActiveState().slInit(pref, sl::kSDKVersion);
	if (res != sl::Result::eOk) {
		logger::warn("[Streamline] slInit failed (result {}) — Streamline features disabled", static_cast<int>(res));
		ActiveState().Unload();
		return false;
	}

	initialized = true;
	StartDlssgWatchdog();
	logger::info("[Streamline] initialized on Vulkan (SDK {}.{}.{})",
		SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH);
	return true;
}

void StreamlineSession::SetVulkanDevice()
{
	if (!initialized || vulkanDeviceSet)
		return;

	auto* dxvk = &vulkan;
	if (!dxvk || !dxvk->IsAvailable()) {
		logger::warn("[Streamline] DXVK interop unavailable — cannot hand Vulkan device to SL");
		return;
	}

	vulkanDeviceSet = true;

	// Probe support against DXVK's physical device.
	sl::AdapterInfo adapter{};
	adapter.vkPhysicalDevice = dxvk->GetPhysicalDevice();
	const auto supported = [&](sl::Feature f) {
		const sl::Result r = ActiveState().slIsFeatureSupported(f, adapter);
		if (r != sl::Result::eOk)
			logger::info("[Streamline] feature {} unsupported (result {})", f, static_cast<int>(r));
		return r == sl::Result::eOk;
	};

	featureDLSS = supported(sl::kFeatureDLSS);
	featureReflex = supported(sl::kFeatureReflex);
	featureDLSSG = supported(sl::kFeatureDLSS_G);
	featureXeSS = supported(sl::kFeatureXeSS);
	featureFSR = supported(sl::kFeatureFSR);
	featureFSRFG = supported(sl::kFeatureFSR_G);

	// A requested plugin can be rejected during device startup even though it
	// was mapped at slInit. Seed reconciliation from Streamline's actual state;
	// otherwise unloading an already-rejected plugin fails and can mask the
	// other frame-generation method from the menu.
	const auto refreshLoadedState = [&](sl::Feature a_feature, bool a_supported,
									  std::atomic<bool>& a_current) {
		bool loaded = false;
		if (ActiveState().slIsFeatureLoaded(a_feature, loaded) == sl::Result::eOk)
			a_current.store(loaded, std::memory_order_release);
		else if (!a_supported)
			a_current.store(false, std::memory_order_release);
	};
	refreshLoadedState(sl::kFeatureDLSS_G, featureDLSSG, ActiveState().dlssgCurrentlyLoaded);
	refreshLoadedState(sl::kFeatureFSR_G, featureFSRFG, ActiveState().fsrfgCurrentlyLoaded);

	if (featureDLSS) {
		ActiveState().slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", reinterpret_cast<void*&>(ActiveState().slDLSSGetOptimalSettings));
		ActiveState().slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", reinterpret_cast<void*&>(ActiveState().slDLSSSetOptions));
		featureDLSS = ActiveState().slDLSSSetOptions != nullptr;
	}
	if (featureReflex) {
		ActiveState().slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", reinterpret_cast<void*&>(ActiveState().slReflexSetOptions));
		ActiveState().slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", reinterpret_cast<void*&>(ActiveState().slReflexSleep));
		ActiveState().slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetExternalPacing", reinterpret_cast<void*&>(ActiveState().slReflexSetExternalPacing));
		ActiveState().slGetFeatureFunction(sl::kFeatureReflex, "slReflexGetState", reinterpret_cast<void*&>(ActiveState().slReflexGetState));
		featureReflex = ActiveState().slReflexSetOptions != nullptr && ActiveState().slReflexSleep != nullptr &&
		                ActiveState().slReflexGetState != nullptr;
	}
	ActiveState().slGetFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", reinterpret_cast<void*&>(ActiveState().slPCLSetMarker));
	logger::info("[Streamline] PCL latency markers {}", ActiveState().slPCLSetMarker ? "available" : "unavailable");
	if (featureDLSSG) {
		ActiveState().slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void*&>(ActiveState().slDLSSGSetOptions));
		ActiveState().slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(ActiveState().slDLSSGGetState));
		featureDLSSG = ActiveState().slDLSSGSetOptions != nullptr && ActiveState().slDLSSGGetState != nullptr;
	}
	if (featureFSR) {
		ActiveState().slGetFeatureFunction(sl::kFeatureFSR, "slFSRSetOptions", reinterpret_cast<void*&>(ActiveState().slFSRSetOptions));
		featureFSR = ActiveState().slFSRSetOptions != nullptr;
	}
	if (featureFSRFG) {
		ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationSetOptions", reinterpret_cast<void*&>(ActiveState().slFSRFrameGenerationSetOptions));
		ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRGetFrameGenState", reinterpret_cast<void*&>(ActiveState().slFSRGetFrameGenState));
		ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationDiscardPreparedFrame", reinterpret_cast<void*&>(ActiveState().slFSRFrameGenerationDiscardPreparedFrame));
		ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationOwnsSwapchain", reinterpret_cast<void*&>(ActiveState().slFSRFrameGenerationOwnsSwapchain));
		ActiveState().slGetFeatureFunction(sl::kFeatureFSR_G, "slFSRFrameGenerationCompleteSwapchainTeardown", reinterpret_cast<void*&>(ActiveState().slFSRFrameGenerationCompleteSwapchainTeardown));
		featureFSRFG = ActiveState().slFSRFrameGenerationSetOptions != nullptr &&
			ActiveState().slFSRFrameGenerationDiscardPreparedFrame != nullptr &&
			ActiveState().slFSRFrameGenerationOwnsSwapchain != nullptr &&
			ActiveState().slFSRFrameGenerationCompleteSwapchainTeardown != nullptr;
	}
	if (featureXeSS) {
		ActiveState().slGetFeatureFunction(sl::kFeatureXeSS, "slXeSSSetOptions", reinterpret_cast<void*&>(ActiveState().slXeSSSetOptions));
		featureXeSS = ActiveState().slXeSSSetOptions != nullptr;
	}

	const bool frameGenerationInteropReady = DxvkLoader::HasFrameGenerationControl();
	const bool dlssgInteropReady = frameGenerationInteropReady && DxvkLoader::HasPresentCallbacks();
	featureDLSSG = featureDLSSG && dlssgHardware && dlssgInteropReady &&
	               dxvk->FrameGenerationQueueInteropReady();
	featureFSRFG = featureFSRFG && frameGenerationInteropReady &&
	               dxvk->FrameGenerationQueueInteropReady();

	// Cache support while both requested plugins are still loaded, then leave
	// exactly one present owner enabled before swapchain creation. This preserves
	// both menu choices without stacking the FFX and DLSS-G WSI proxies. Runtime
	// switches repeat this operation in DXVK's no-swapchain callback.
	ActiveState().dlssgDesiredLoaded.store(featureDLSSG, std::memory_order_release);
	ActiveState().fsrfgDesiredLoaded.store(!featureDLSSG && featureFSRFG, std::memory_order_release);
	if (!ReconcileFgFeatureLoad(sl::kFeatureDLSS_G, ActiveState().dlssgDesiredLoaded, ActiveState().dlssgCurrentlyLoaded) ||
		!ReconcileFgFeatureLoad(sl::kFeatureFSR_G, ActiveState().fsrfgDesiredLoaded, ActiveState().fsrfgCurrentlyLoaded)) {
		featureDLSSG = false;
		featureFSRFG = false;
		logger::error("[Streamline] failed to isolate frame-generation plugins before swapchain creation");
	}

	logger::info("[Streamline] feature support: DLSS={} Reflex={} DLSS-G={} FSR={} FSR-G={} XeSS={} (FSR-FG fns {})",
		featureDLSS, featureReflex, featureDLSSG, featureFSR, featureFSRFG, featureXeSS,
		ActiveState().slFSRFrameGenerationSetOptions && ActiveState().slFSRFrameGenerationDiscardPreparedFrame &&
			ActiveState().slFSRFrameGenerationOwnsSwapchain &&
			ActiveState().slFSRFrameGenerationCompleteSwapchainTeardown ? "ok" : "missing");

	// Use Vulkan IDs because the D3D create hook may not see the adapter.
	if (auto getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
			dxvk->GetInstanceProcAddr()(dxvk->GetInstance(), "vkGetPhysicalDeviceProperties"))) {
		VkPhysicalDeviceProperties props{};
		getProps(dxvk->GetPhysicalDevice(), &props);
		isNvidiaGPU = props.vendorID == 0x10DE;
		isRTXBelow40Series = isNvidiaGPU &&
		                     ((props.deviceID >= 0x2200 && props.deviceID <= 0x2600) ||   // RTX 30 (Ampere)
								(props.deviceID >= 0x1E00 && props.deviceID <= 0x1FFF));   // RTX 20 (Turing w/ RT)
		logger::info("[Streamline] GPU vendor=0x{:04X} device=0x{:04X} -> DLSS preset group: {}",
			props.vendorID, props.deviceID,
			isNvidiaGPU ? (isRTXBelow40Series ? "RTX 20/30 (J)" : "RTX 40+ (M)") : "non-NVIDIA (default)");
	}
}

static sl::FrameToken* TokenForFrame(uint32_t a_frameId)
{
	sl::FrameToken* token = nullptr;
	if (ActiveState().slGetNewFrameToken(token, &a_frameId) != sl::Result::eOk)
		return nullptr;
	return token;
}

static sl::FrameToken* RenderFrameToken()
{
	return TokenForFrame(ActiveState().evaluator.renderFrameId);
}

static sl::Result cs_SetTagForFrame(sl::FrameToken& a_token, const sl::ViewportHandle& a_viewport,
	const sl::ResourceTag* a_tags, uint32_t a_tagCount, VkCommandBuffer a_commandBuffer)
{
	sl::Result result = sl::Result::eErrorExceptionHandler;
	__try {
		result = ActiveState().slSetTagForFrame(a_token, a_viewport, a_tags, a_tagCount, a_commandBuffer);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
	}
	return result;
}

static sl::Result cs_EvaluateFeature(sl::Feature a_feature, sl::FrameToken& a_token,
	const sl::ViewportHandle& a_viewport, VkCommandBuffer a_commandBuffer)
{
	sl::Result result = sl::Result::eErrorExceptionHandler;
	__try {
		const sl::BaseStructure* inputs[] = { &a_viewport };
		result = ActiveState().slEvaluateFeature(
			a_feature, a_token, inputs, static_cast<uint32_t>(std::size(inputs)), a_commandBuffer);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
	}
	return result;
}

static sl::Result cs_DiscardFSRFrameGenerationPreparedFrame(const sl::ViewportHandle& a_viewport)
{
	sl::Result result = sl::Result::eErrorNotInitialized;
	if (!ActiveState().slFSRFrameGenerationDiscardPreparedFrame)
		return result;

	__try {
		result = ActiveState().slFSRFrameGenerationDiscardPreparedFrame(a_viewport);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		result = sl::Result::eErrorExceptionHandler;
	}
	return result;
}

static uint32_t SimFrameId()
{
	return globals::state->frameCountAtomic.load(std::memory_order_relaxed) + 1;
}

void StreamlineSession::BeginRenderFrame()
{
	ActiveState().dlssg.renderHeartbeatNs.store(PresentClockNs(), std::memory_order_release);
	if (ActiveState().fsrfgCurrentlyLoaded.load(std::memory_order_acquire) &&
		!ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
		(void)DiscardFSRFrameGenerationPreparedFrame();
	// Present increments State::frameCount at the end of a frame.  The render
	// hook therefore prepares frame N+1, matching the token already used by
	// Reflex Sleep and SimulationStart at this same hook.  Using frameCount
	// directly split one logical frame's Reflex markers across N and N+1.
	ActiveState().evaluator.renderFrameId = SimFrameId();
	ActiveState().dlssg.taggedThisFrame = false;
}

bool StreamlineSession::DiscardFSRFrameGenerationPreparedFrame()
{
	if (!ActiveState().fsrfgCurrentlyLoaded.load(std::memory_order_acquire))
		return true;
	if (!ActiveState().slFSRFrameGenerationDiscardPreparedFrame ||
		ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
		return false;

	const sl::ViewportHandle fgViewport{ 1 };
	const sl::Result result = cs_DiscardFSRFrameGenerationPreparedFrame(fgViewport);
	if (result == sl::Result::eErrorInvalidState) {
		vulkan.QuarantineUnconsumedFSRPresentViews();
		return true;
	}
	if (result != sl::Result::eOk) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] FSR prepared-frame discard failed (result {})",
			static_cast<int>(result));
		return false;
	}

	vulkan.NotifyFSRFrameConsumed();
	return true;
}

void StreamlineSession::UpdateReflex(bool a_enable, bool a_boost, uint32_t a_frameLimitUs)
{
	if (!initialized || !featureReflex || ActiveState().evaluator.dispatchFaulted)
		return;

	const bool requested = a_enable && !ActiveState().reflex.ForceOff();
	auto* dxvk = &vulkan;
	const bool useDxvkReflex = requested && dxvk->ReflexAvailable() && ActiveState().slReflexSetExternalPacing;
	const bool previousDxvkOwner = ActiveState().reflex.dxvkOwnsPacing.exchange(useDxvkReflex, std::memory_order_acq_rel);
	if (previousDxvkOwner != useDxvkReflex) {
		logger::info("[Streamline] Reflex pacing owner={}", useDxvkReflex ? "DXVK-presented swapchain" : "Streamline Vulkan");
		ActiveState().slReflexSetExternalPacing(useDxvkReflex);
	}
	if (dxvk->ReflexAvailable() &&
		!dxvk->SetReflexMode(useDxvkReflex, useDxvkReflex && a_boost, useDxvkReflex ? a_frameLimitUs : 0u))
		logger::warn("[Streamline] Failed to configure DXVK-presented swapchain Reflex controller");

	// DLSS-G validates Streamline's Reflex state at present time, so leave the
	// feature logically enabled. External pacing suppresses only Streamline's
	// driver-facing sleep, mode, and marker calls; its token/PCL bookkeeping stays
	// active while DXVK drives VK_NV_low_latency2 for the swapchain it presents.
	const sl::ReflexMode mode = !requested ? sl::ReflexMode::eOff :
	                            a_boost   ? sl::ReflexMode::eLowLatencyWithBoost :
	                                        sl::ReflexMode::eLowLatency;
	if (ActiveState().reflex.ForceOff() || ActiveState().reflex.ForceUnlimited())
		a_frameLimitUs = 0;

	__try {
		static uint32_t s_sleepSamples = 0;
		static uint64_t s_sleepTotalUs = 0;
		static uint64_t s_sleepMaxUs = 0;
		static sl::Result s_sleepResult = sl::Result::eOk;
		if (!ActiveState().reflex.cacheValid || ActiveState().reflex.cachedMode != mode || ActiveState().reflex.cachedFrameLimitUs != a_frameLimitUs) {
			sl::ReflexOptions options{};
			options.mode = mode;
			options.frameLimitUs = a_frameLimitUs;
			const sl::Result result = ActiveState().slReflexSetOptions(options);
			if (result == sl::Result::eOk) {
				ActiveState().reflex.cachedMode = mode;
				ActiveState().reflex.cachedFrameLimitUs = a_frameLimitUs;
				ActiveState().reflex.cacheValid = true;
			}
			logger::info("[Streamline] Reflex options mode={} frameLimitUs={} result={}",
				static_cast<uint32_t>(mode), a_frameLimitUs, static_cast<int32_t>(result));
			s_sleepSamples = 0;
			s_sleepTotalUs = 0;
			s_sleepMaxUs = 0;
			s_sleepResult = sl::Result::eOk;
		}
		{
			// PollInputDevices can run more than once per rendered frame.
			static uint32_t s_lastSleepFrame = UINT32_MAX;
			const uint32_t simFrame = SimFrameId();
			if (s_lastSleepFrame != simFrame) {
				s_lastSleepFrame = simFrame;
				if (sl::FrameToken* token = TokenForFrame(simFrame); token && !ActiveState().reflex.SkipSleep()) {
					const auto sleepStart = std::chrono::steady_clock::now();
					if (useDxvkReflex) {
						const bool dxvkSlept = dxvk->ReflexSleep();
						const sl::Result slResult = ActiveState().slReflexSleep(*token);
						s_sleepResult = dxvkSlept ? slResult : sl::Result::eErrorIO;
					} else
						s_sleepResult = ActiveState().slReflexSleep(*token);
					const uint64_t sleepUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - sleepStart).count());
					s_sleepTotalUs += sleepUs;
					s_sleepMaxUs = std::max(s_sleepMaxUs, sleepUs);

					// The Reflex report is the only timing source that sees the driver's
					// hardware metering path.  Keep this opt-in and batch the output so
					// diagnostics do not turn per-frame logging into a pacing input.
					static std::array<ReflexController::SleepSample, 8> s_traceBatch{};
					static uint32_t s_traceCount = 0;
					if (ActiveState().reflex.TracePacing()) {
						s_traceBatch[s_traceCount++] = { simFrame, sleepUs };
						if (s_traceCount == s_traceBatch.size()) {
							const sl::Result stateResult = ActiveState().slReflexGetState(ActiveState().reflex.TraceState());
							ActiveState().reflex.LogPacingBatch(s_traceBatch, stateResult);
							s_traceCount = 0;
						}
					}
					if (++s_sleepSamples == 600u) {
						logger::info("[Streamline] Reflex sleep samples=600 averageUs={} maxUs={} lastResult={}",
							s_sleepTotalUs / s_sleepSamples, s_sleepMaxUs, static_cast<int32_t>(s_sleepResult));
						s_sleepSamples = 0;
						s_sleepTotalUs = 0;
						s_sleepMaxUs = 0;
					}
				}
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] Reflex dispatch faulted — Streamline disabled for this session");
	}
}

void StreamlineSession::SetPCLMarker(PclMarker a_marker)
{
	if (!initialized || !ActiveState().slPCLSetMarker || ActiveState().evaluator.dispatchFaulted)
		return;

	// Emit SimulationStart once per simulated frame, not once per input poll.
	uint32_t simFrame = 0;
	if (a_marker == PclMarker::SimulationStart) {
		static uint32_t s_lastSimFrame = UINT32_MAX;
		simFrame = SimFrameId();
		if (s_lastSimFrame == simFrame)
			return;
		s_lastSimFrame = simFrame;
	}

	__try {
		const bool renderThreadMarker =
			a_marker == PclMarker::RenderSubmitStart || a_marker == PclMarker::RenderSubmitEnd ||
			a_marker == PclMarker::PresentStart || a_marker == PclMarker::PresentEnd ||
			a_marker == PclMarker::TriggerFlash ||
			a_marker == PclMarker::SimulationEnd;
		sl::FrameToken* token = renderThreadMarker ?
		                            RenderFrameToken() :
		                            TokenForFrame(simFrame ? simFrame : SimFrameId());
		if (token)
			ActiveState().slPCLSetMarker(static_cast<sl::PCLMarker>(a_marker), *token);
		if (token && ActiveState().reflex.dxvkOwnsPacing.load(std::memory_order_acquire))
			vulkan.SetReflexMarker(
				static_cast<uint64_t>(*token), static_cast<uint32_t>(a_marker));
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] PCL marker faulted — Streamline disabled for this session");
	}
}

bool StreamlineSession::QueueDLSSGPresentMarkers()
{
	if (!initialized || !ActiveState().slPCLSetMarker || ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire))
		return false;
	const uint32_t tail = ActiveState().dlssg.presentMarkerTail.load(std::memory_order_relaxed);
	const uint32_t head = ActiveState().dlssg.presentMarkerHead.load(std::memory_order_acquire);
	if (tail - head >= ActiveState().dlssg.presentMarkerFrames.size()) {
		logger::warn("[Streamline] Vulkan present-marker queue overflow; using app-thread markers");
		return false;
	}
	ActiveState().dlssg.presentMarkerFrames[tail % ActiveState().dlssg.presentMarkerFrames.size()].store(
		ActiveState().evaluator.renderFrameId, std::memory_order_relaxed);
	ActiveState().dlssg.presentMarkerTail.store(tail + 1u, std::memory_order_release);
	// Streamline consumes this frame on DXVK's later Vulkan-present thread.
	// DXVK instead needs PresentStart now, before D3D11 Present enters its frame
	// mapper; CompleteDXVKPresentMarker closes that app-side interval on return.
	if (ActiveState().reflex.dxvkOwnsPacing.load(std::memory_order_acquire)) {
		const uint32_t previous = ActiveState().dlssg.activeDxvkAppPresentFrame.exchange(
			ActiveState().evaluator.renderFrameId, std::memory_order_acq_rel);
		if (previous)
			logger::warn("[Streamline] DXVK app-present marker {} was not closed before frame {}", previous, ActiveState().evaluator.renderFrameId);
		vulkan.SetReflexMarker(
			ActiveState().evaluator.renderFrameId, static_cast<uint32_t>(PclMarker::PresentStart));
	}
	return true;
}

void StreamlineSession::CompleteDXVKPresentMarker()
{
	const uint32_t frameId = ActiveState().dlssg.activeDxvkAppPresentFrame.exchange(0u, std::memory_order_acq_rel);
	if (frameId && ActiveState().reflex.dxvkOwnsPacing.load(std::memory_order_acquire))
		vulkan.SetReflexMarker(
			frameId, static_cast<uint32_t>(PclMarker::PresentEnd));
}

// Returns false until the engine camera matrices are finite and invertible.
static bool cs_IsFiniteMatrix(const Matrix& a_matrix)
{
	return std::isfinite(a_matrix._11) && std::isfinite(a_matrix._12) &&
	       std::isfinite(a_matrix._13) && std::isfinite(a_matrix._14) &&
	       std::isfinite(a_matrix._21) && std::isfinite(a_matrix._22) &&
	       std::isfinite(a_matrix._23) && std::isfinite(a_matrix._24) &&
	       std::isfinite(a_matrix._31) && std::isfinite(a_matrix._32) &&
	       std::isfinite(a_matrix._33) && std::isfinite(a_matrix._34) &&
	       std::isfinite(a_matrix._41) && std::isfinite(a_matrix._42) &&
	       std::isfinite(a_matrix._43) && std::isfinite(a_matrix._44);
}

static bool cs_BuildConstants(sl::Constants& a_consts, uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY)
{
	a_consts = {};
	a_consts.cameraAspectRatio = static_cast<float>(a_outputWidth) / static_cast<float>(a_outputHeight);
	a_consts.cameraFOV = Util::GetVerticalFOVRad();
	a_consts.cameraNear = *globals::game::cameraNear;
	a_consts.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

	a_consts.cameraMotionIncluded = sl::Boolean::eTrue;
	a_consts.cameraPinholeOffset = { 0.f, 0.f };
	a_consts.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	a_consts.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	a_consts.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	const auto cameraPosAdjust = globals::game::frameBufferCached.GetCameraPosAdjust();
	a_consts.cameraPos = *reinterpret_cast<const sl::float3*>(&cameraPosAdjust);
	a_consts.cameraViewToClip = *reinterpret_cast<const sl::float4x4*>(&cameraViewToClip);
	a_consts.depthInverted = sl::Boolean::eFalse;

	sl::recalculateCameraMatrices(a_consts);

	// Translate between the current and previous camera-relative origins before reprojection.
	// Streamline applies jitter separately, so both matrices remain unjittered.
	Matrix curVP = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Transpose();
	Matrix prevVP = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered().Transpose();
	const auto& posAdj = globals::game::frameBufferCached.GetCameraPosAdjust();
	const auto& prevPosAdj = globals::game::frameBufferCached.GetCameraPreviousPosAdjust();
	Matrix camDelta = Matrix::CreateTranslation(posAdj.x - prevPosAdj.x, posAdj.y - prevPosAdj.y, posAdj.z - prevPosAdj.z);
	Matrix clipToPrevClip = curVP.Invert() * camDelta * prevVP;
	Matrix prevClipToClip = clipToPrevClip.Invert();
	a_consts.clipToPrevClip = *reinterpret_cast<const sl::float4x4*>(&clipToPrevClip);
	a_consts.prevClipToClip = *reinterpret_cast<const sl::float4x4*>(&prevClipToClip);

	a_consts.jitterOffset = { -a_jitterX, -a_jitterY };
	// Reset temporal history after leaving a loading screen.
	{
		static bool s_wasLoading = false;
		static uint32_t s_observedFrame = UINT32_MAX;
		static uint32_t s_resetFrame = UINT32_MAX;
		const bool loading = globals::state->isLoadingMenuOpen;
		if (s_observedFrame != ActiveState().evaluator.renderFrameId) {
			s_observedFrame = ActiveState().evaluator.renderFrameId;
			if (!loading && s_wasLoading)
				s_resetFrame = ActiveState().evaluator.renderFrameId;
			s_wasLoading = loading;
		}
		a_consts.reset = s_resetFrame == ActiveState().evaluator.renderFrameId ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	}
	a_consts.mvecScale = { 1.0f, 1.0f };
	a_consts.motionVectors3D = sl::Boolean::eFalse;
	a_consts.motionVectorsInvalidValue = FLT_MIN;
	a_consts.orthographicProjection = sl::Boolean::eFalse;
	a_consts.motionVectorsDilated = sl::Boolean::eFalse;
	a_consts.motionVectorsJittered = sl::Boolean::eFalse;

	// Reject singular engine matrices before passing constants to Streamline.
	const bool matricesFinite = cs_IsFiniteMatrix(cameraViewToClip) &&
	                            cs_IsFiniteMatrix(clipToPrevClip) &&
	                            cs_IsFiniteMatrix(prevClipToClip);
	const bool basisFinite = std::isfinite(a_consts.cameraRight.x) &&
	                         std::isfinite(a_consts.cameraRight.y) &&
	                         std::isfinite(a_consts.cameraRight.z) &&
	                         std::isfinite(a_consts.cameraUp.x) &&
	                         std::isfinite(a_consts.cameraUp.y) &&
	                         std::isfinite(a_consts.cameraUp.z) &&
	                         std::isfinite(a_consts.cameraFwd.x) &&
	                         std::isfinite(a_consts.cameraFwd.y) &&
	                         std::isfinite(a_consts.cameraFwd.z);
	const bool scalarsFinite = std::isfinite(a_consts.cameraAspectRatio) &&
	                           std::isfinite(a_consts.cameraFOV) &&
	                           std::isfinite(a_consts.cameraNear) &&
	                           std::isfinite(a_consts.cameraFar);
	static bool s_cameraDataInvalid = false;
	if (!matricesFinite || !basisFinite || !scalarsFinite) {
		if (!s_cameraDataInvalid) {
			s_cameraDataInvalid = true;
			logger::warn("[Streamline] skipping evaluate: invalid camera constants "
				"(matrices={} basis={} scalars={} proj=[{},{},{},{},{},{}])",
				matricesFinite, basisFinite, scalarsFinite,
				cameraViewToClip._11, cameraViewToClip._22, cameraViewToClip._33,
				cameraViewToClip._34, cameraViewToClip._43, cameraViewToClip._44);
		}
		return false;
	}
	if (s_cameraDataInvalid) {
		s_cameraDataInvalid = false;
		logger::info("[Streamline] camera constants became valid; evaluations resumed");
	}

	return true;
}

static VkImageAspectFlags cs_ImageAspect(VkFormat a_format)
{
	switch (a_format) {
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

struct cs_VulkanResultAttempt
{
	VkResult result = VK_ERROR_DEVICE_LOST;
	DWORD exceptionCode = 0;
};

struct cs_VulkanVoidAttempt
{
	DWORD exceptionCode = 0;
	bool completed = false;
};

struct cs_VulkanProcAttempt
{
	PFN_vkVoidFunction function = nullptr;
	DWORD exceptionCode = 0;
};

struct cs_GetVkImageAttempt
{
	bool succeeded = false;
	DWORD exceptionCode = 0;
};

static cs_GetVkImageAttempt cs_GetVkImageSEH(VulkanDeviceContext* a_dxvk, ID3D11Resource* a_resource,
	VkImage* a_image, VkImageLayout* a_layout, VkImageCreateInfo* a_info) noexcept
{
	cs_GetVkImageAttempt attempt{};
	__try {
		attempt.succeeded = a_dxvk &&
			a_dxvk->GetVkImage(a_resource, a_image, a_layout, a_info);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static cs_VulkanProcAttempt cs_GetDeviceProcAddrSEH(
	PFN_vkGetDeviceProcAddr a_getProcAddr, VkDevice a_device, const char* a_name) noexcept
{
	cs_VulkanProcAttempt attempt{};
	__try {
		if (a_getProcAddr)
			attempt.function = a_getProcAddr(a_device, a_name);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static cs_VulkanResultAttempt cs_CreateImageViewSEH(PFN_vkCreateImageView a_createImageView,
	VkDevice a_device, const VkImageViewCreateInfo* a_createInfo, VkImageView* a_view) noexcept
{
	cs_VulkanResultAttempt attempt{};
	__try {
		if (a_createImageView)
			attempt.result = a_createImageView(a_device, a_createInfo, nullptr, a_view);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static cs_VulkanVoidAttempt cs_DestroyImageViewSEH(PFN_vkDestroyImageView a_destroyImageView,
	VkDevice a_device, VkImageView a_view) noexcept
{
	cs_VulkanVoidAttempt attempt{};
	__try {
		if (a_destroyImageView && a_view != VK_NULL_HANDLE)
			a_destroyImageView(a_device, a_view, nullptr);
		attempt.completed = true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static cs_VulkanVoidAttempt cs_PipelineBarrierSEH(VkCommandBuffer a_commandBuffer,
	const VkImageMemoryBarrier* a_barrier) noexcept
{
	cs_VulkanVoidAttempt attempt{};
	__try {
		vkCmdPipelineBarrier(a_commandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
			0, nullptr, 0, nullptr, 1, a_barrier);
		attempt.completed = true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		attempt.exceptionCode = GetExceptionCode();
	}
	return attempt;
}

static bool cs_DestroyViews(VulkanDeviceContext* a_dxvk, VkDevice a_device,
	PFN_vkDestroyImageView a_destroyImageView, VkImageView* a_views, uint32_t a_count,
	ID3D11Resource* const* a_resources = nullptr, uint32_t a_resourceCount = 0)
{
	if (!a_dxvk)
		return false;
	VulkanResourceBridge bridge(*a_dxvk, ActiveState().evaluator.dispatchFaulted);
	return bridge.DestroyViews(a_device, a_destroyImageView, a_views, a_count, a_resources, a_resourceCount);
}

static bool cs_BarrierUpscalerOutput(VulkanDeviceContext* a_dxvk,
	VkCommandBuffer a_commandBuffer, const sl::Resource& a_output)
{
	if (!a_dxvk)
		return false;
	VulkanResourceBridge bridge(*a_dxvk, ActiveState().evaluator.dispatchFaulted);
	return bridge.BarrierUpscalerOutput(a_commandBuffer, a_output);
}

static bool cs_SubmitPresentTags(VulkanDeviceContext* a_dxvk, sl::FrameToken& a_token,
	const sl::ViewportHandle& a_viewport, const sl::ResourceTag* a_tags, uint32_t a_tagCount,
	const VkImageView* a_views, uint32_t a_viewCount,
	ID3D11Resource* const* a_resources, uint32_t a_resourceCount, sl::Result& a_tagResult,
	bool& a_lifetimesRetained)
{
	a_lifetimesRetained = false;
	auto transaction = a_dxvk->BeginFrameCommandBuffer();
	if (!transaction)
		return false;

	a_tagResult = cs_SetTagForFrame(
		a_token, a_viewport, a_tags, a_tagCount, transaction.GetCommandBuffer());
	if (a_tagResult != sl::Result::eOk)
		return false;
	// Signal the slot semaphore so DXVK can add it to the matching present's
	// wait list. The presenter may use a different Vulkan queue and can otherwise
	// overtake this tag/copy submission even though it was queued first on the
	// graphics queue.
	if (!a_dxvk->SubmitFrameCommandBuffer(transaction, true)) {
		if (transaction.SubmissionMayBeInFlight()) {
			a_dxvk->QueueViewsForDeferredDelete(transaction, a_views, a_viewCount);
			a_dxvk->QueueResourcesForDeferredRelease(transaction, a_resources, a_resourceCount);
			a_lifetimesRetained = true;
		}
		return false;
	}

	a_dxvk->QueueViewsForDeferredDelete(transaction, a_views, a_viewCount);
	a_dxvk->QueueResourcesForDeferredRelease(transaction, a_resources, a_resourceCount);
	return true;
}

static bool cs_CanReleaseFailedFSRFrame(VulkanDeviceContext* a_dxvk,
	VulkanDeviceContext::CommandTransaction& a_transaction, const sl::ViewportHandle& a_viewport,
	const VkImageView* a_views, uint32_t a_viewCount,
	ID3D11Resource* const* a_resources, uint32_t a_resourceCount)
{
	if (!a_transaction.SubmissionMayBeInFlight() &&
		!ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire)) {
		const sl::Result discardResult = cs_DiscardFSRFrameGenerationPreparedFrame(a_viewport);
		if (discardResult == sl::Result::eOk || discardResult == sl::Result::eErrorInvalidState)
			return true;
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] failed to discard a partially accepted FSR-FG frame (result {})",
			static_cast<int>(discardResult));
	}

	a_dxvk->QueueResourcesForPresent(a_transaction, a_resources, a_resourceCount);
	a_dxvk->QuarantineViewsUntilFSRSwapchainTeardown(a_transaction, a_views, a_viewCount);
	return false;
}

static sl::Result cs_EvaluateFeatureCore(VulkanDeviceContext* a_dxvk,
	sl::Feature a_feature, const sl::ViewportHandle& a_viewport,
	ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY, ID3D11Resource* a_hudlessColor = nullptr,
	bool* a_outputReady = nullptr, bool* a_skipped = nullptr)
{
	if (a_outputReady)
		*a_outputReady = false;
	if (a_skipped)
		*a_skipped = false;

	auto* dxvk = a_dxvk;
	if (!dxvk)
		return sl::Result::eErrorNotInitialized;

	sl::FrameToken* token = RenderFrameToken();
	if (!token) {
		if (a_skipped)
			*a_skipped = true;
		return sl::Result::eOk;
	}

	// Pair constants and evaluation once per frame and viewport.
	static uint32_t s_evalFrameByVp[2] = { UINT32_MAX, UINT32_MAX };
	static uint32_t s_constFrameByVp[2] = { UINT32_MAX, UINT32_MAX };
	const uint32_t vpId = a_viewport;
	if (vpId < 2) {
		if (s_evalFrameByVp[vpId] == ActiveState().evaluator.renderFrameId) {
			if (a_skipped)
				*a_skipped = true;
			return sl::Result::eOk;
		}
		if (s_constFrameByVp[vpId] != ActiveState().evaluator.renderFrameId) {
			sl::Constants consts;
			if (!cs_BuildConstants(consts, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY)) {
				if (a_skipped)
					*a_skipped = true;
				return sl::Result::eOk;
			}
			const sl::Result constantsRes = ActiveState().slSetConstants(consts, *token, a_viewport);
			if (constantsRes != sl::Result::eOk) {
				logger::error("[Streamline] slSetConstants failed for viewport {} (result {})",
					vpId, static_cast<int>(constantsRes));
				return constantsRes;
			}
			s_constFrameByVp[vpId] = ActiveState().evaluator.renderFrameId;
			if (vpId == 0)
				ActiveState().evaluator.viewport0ConstantsFrame = ActiveState().evaluator.renderFrameId;
		}
	} else {
		sl::Constants consts;
		if (!cs_BuildConstants(consts, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY)) {
			if (a_skipped)
				*a_skipped = true;
			return sl::Result::eOk;
		}
		const sl::Result constantsRes = ActiveState().slSetConstants(consts, *token, a_viewport);
		if (constantsRes != sl::Result::eOk)
			return constantsRes;
	}

	VulkanResourceBridge resourceBridge(*dxvk, ActiveState().evaluator.dispatchFaulted);
	auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(
		resourceBridge.GetDeviceProcAddress("vkCreateImageView"));
	auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
		resourceBridge.GetDeviceProcAddress("vkDestroyImageView"));
	if (!vkCreateImageView || !vkDestroyImageView)
		return sl::Result::eErrorNotInitialized;
	std::array<ID3D11Resource*, EvaluationResourceTransaction::kMaxResources> resources = {
		a_colorIn, a_colorOut, a_depth, a_motionVectors, a_hudlessColor
	};
	EvaluationResourceTransaction evaluationResources(
		*dxvk, resourceBridge, vkCreateImageView, vkDestroyImageView, resources);
	const auto wrap = [&](ID3D11Resource* a_res, sl::Resource& a_out) -> bool {
		return evaluationResources.Wrap(a_res, a_out);
	};

	const bool haveColor = (a_colorIn && a_colorOut);
	const bool haveHudless = (a_hudlessColor != nullptr);
	sl::Resource colorInRes{}, colorOutRes{}, depthRes{}, mvecRes{}, hudlessRes{};
	bool ok = wrap(a_depth, depthRes) &&
	          wrap(a_motionVectors, mvecRes);
	if (ok && haveColor)
		ok = wrap(a_colorIn, colorInRes) &&
		     wrap(a_colorOut, colorOutRes);
	if (ok && haveHudless)
		ok = wrap(a_hudlessColor, hudlessRes);
	if (!ok) {
		const auto cleanup = SelectEvaluationCleanup(
			evaluationResources.HasTerminalFault(), false, true, false);
		if (cleanup == EvaluationCleanupAction::kQuarantine)
			evaluationResources.Quarantine();
		else
			(void)evaluationResources.Destroy();
		return sl::Result::eErrorMissingInputParameter;
	}

	sl::Extent renderExtent{ 0, 0, a_renderWidth, a_renderHeight };
	sl::Extent outputExtent{ 0, 0, a_outputWidth, a_outputHeight };
	sl::ResourceTag tags[5];
	uint32_t nt = 0;
	if (haveColor) {
		tags[nt++] = sl::ResourceTag{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent };
		tags[nt++] = sl::ResourceTag{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &outputExtent };
	}
	const auto inputLifecycle = haveColor ?
		sl::ResourceLifecycle::eValidUntilEvaluate : sl::ResourceLifecycle::eOnlyValidNow;
	tags[nt++] = sl::ResourceTag{ &depthRes, sl::kBufferTypeDepth, inputLifecycle, &renderExtent };
	tags[nt++] = sl::ResourceTag{ &mvecRes, sl::kBufferTypeMotionVectors, inputLifecycle, &renderExtent };
	if (haveHudless)
		tags[nt++] = sl::ResourceTag{ &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, &outputExtent };

	sl::Result evalRes = sl::Result::eErrorNotInitialized;
	auto transaction = dxvk->BeginFrameCommandBuffer();
	if (transaction) {
		const VkCommandBuffer cmd = transaction.GetCommandBuffer();
		const sl::Result tagRes = cs_SetTagForFrame(*token, a_viewport, tags, nt, cmd);
		if (tagRes != sl::Result::eOk) {
			logger::error("[Streamline] slSetTagForFrame failed for feature {} viewport {} (result {})",
				static_cast<uint32_t>(a_feature), vpId, static_cast<int>(tagRes));
			const bool canRelease = a_feature != sl::kFeatureFSR_G ||
				cs_CanReleaseFailedFSRFrame(dxvk, transaction, a_viewport,
					evaluationResources.Views(), evaluationResources.ViewCount(), evaluationResources.Resources(),
					EvaluationResourceTransaction::kMaxResources);
			if (canRelease)
				(void)evaluationResources.Destroy();
			return tagRes;
		}

		evalRes = cs_EvaluateFeature(a_feature, *token, a_viewport, cmd);
		if (evalRes != sl::Result::eOk) {
			const bool canRelease = a_feature != sl::kFeatureFSR_G ||
				cs_CanReleaseFailedFSRFrame(dxvk, transaction, a_viewport,
					evaluationResources.Views(), evaluationResources.ViewCount(), evaluationResources.Resources(),
					EvaluationResourceTransaction::kMaxResources);
			if (canRelease)
				(void)evaluationResources.Destroy();
			return evalRes;
		}

		if (haveColor && !cs_BarrierUpscalerOutput(dxvk, cmd, colorOutRes)) {
			evaluationResources.Quarantine();
			return sl::Result::eErrorExceptionHandler;
		}

		if (dxvk->SubmitFrameCommandBuffer(transaction)) {
			if (a_feature == sl::kFeatureFSR_G) {
				evaluationResources.RetainForFSRPresent(transaction);
			} else {
				evaluationResources.Defer(transaction);
			}
			if (a_outputReady)
				*a_outputReady = true;
			if (vpId < 2)
				s_evalFrameByVp[vpId] = ActiveState().evaluator.renderFrameId;
		} else {
			if (a_feature == sl::kFeatureFSR_G) {
				const bool canRelease = cs_CanReleaseFailedFSRFrame(dxvk, transaction, a_viewport,
					evaluationResources.Views(), evaluationResources.ViewCount(), evaluationResources.Resources(),
					EvaluationResourceTransaction::kMaxResources);
				if (canRelease)
					(void)evaluationResources.Destroy();
			} else {
				const auto cleanup = SelectEvaluationCleanup(
					false, false, true, transaction.SubmissionMayBeInFlight());
				if (cleanup == EvaluationCleanupAction::kDefer)
					evaluationResources.Defer(transaction);
				else
					(void)evaluationResources.Destroy();
			}
			evalRes = sl::Result::eErrorExceptionHandler;
		}
	} else {
		(void)evaluationResources.Destroy();
	}
	return evalRes;
}

static StreamlineSession::EvaluationResult cs_ClassifyEvaluation(
	sl::Result a_result, bool a_outputReady, bool a_skipped)
{
	return UpscalerEvaluator::Classify(static_cast<int32_t>(a_result), a_outputReady, a_skipped);
}

StreamlineSession::EvaluationResult StreamlineSession::EvaluateUpscaler(const UpscaleRequest& a_request)
{
	return UpscalerEvaluator(*this).Evaluate(a_request);
}

StreamlineSession::EvaluationResult StreamlineSession::EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode,
	float a_jitterX, float a_jitterY)
{
	bool outputReady = false;
	bool evaluationSkipped = false;
	EvaluationResult result = EvaluationResult::kFailed;
	if (!initialized || !featureDLSS || ActiveState().evaluator.dispatchFaulted)
		return result;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return result;

	auto* dxvk = &vulkan;
	if (!dxvk->CommandResourcesReady())
		return result;

	__try {
		sl::DLSSMode dlssMode = sl::DLSSMode::eMaxQuality;
		switch (a_qualityMode) {
		case 0:
			dlssMode = sl::DLSSMode::eDLAA;
			break;
		case 1:
			dlssMode = sl::DLSSMode::eMaxQuality;
			break;
		case 2:
			dlssMode = sl::DLSSMode::eBalanced;
			break;
		case 3:
			dlssMode = sl::DLSSMode::eMaxPerformance;
			break;
		case 4:
			dlssMode = sl::DLSSMode::eUltraPerformance;
			break;
		default:
			dlssMode = sl::DLSSMode::eMaxQuality;
			break;
		}

		sl::DLSSOptions options{};
		options.mode = dlssMode;
		options.outputWidth = a_outputWidth;
		options.outputHeight = a_outputHeight;
		// The Vulkan scene chain is always FP16 HDR.
		options.colorBuffersHDR = sl::Boolean::eTrue;
		options.useAutoExposure = sl::Boolean::eTrue;

		// Use the recommended preset for the detected NVIDIA architecture.
		if (isRTXBelow40Series) {
			options.dlaaPreset = sl::DLSSPreset::ePresetJ;
			options.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
			options.qualityPreset = sl::DLSSPreset::ePresetJ;
			options.balancedPreset = sl::DLSSPreset::ePresetJ;
			options.performancePreset = sl::DLSSPreset::ePresetJ;
			options.ultraPerformancePreset = sl::DLSSPreset::ePresetM;
		} else if (isNvidiaGPU) {
			options.dlaaPreset = sl::DLSSPreset::ePresetJ;
			options.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
			options.qualityPreset = sl::DLSSPreset::ePresetM;
			options.balancedPreset = sl::DLSSPreset::ePresetM;
			options.performancePreset = sl::DLSSPreset::ePresetM;
			options.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
		}

		static bool s_loggedPreset = false;
		if (!s_loggedPreset) {
			s_loggedPreset = true;
			logger::info("[Streamline] DLSS presets set (mode {}): quality={} (RTX40+={} below40={})",
				static_cast<int>(dlssMode), static_cast<int>(options.qualityPreset),
				isNvidiaGPU && !isRTXBelow40Series, isRTXBelow40Series);
		}

		const sl::Result optionsResult = ActiveState().slDLSSSetOptions(ActiveState().evaluator.viewport, options);
		if (optionsResult != sl::Result::eOk) {
			logger::error("[Streamline] DLSS options failed (result {})", static_cast<int>(optionsResult));
			return result;
		}

		const sl::Result evalRes = cs_EvaluateFeatureCore(&vulkan, sl::kFeatureDLSS, ActiveState().evaluator.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY,
			nullptr, &outputReady, &evaluationSkipped);
		result = cs_ClassifyEvaluation(evalRes, outputReady, evaluationSkipped);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] DLSS evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] DLSS dispatch faulted — Streamline disabled for this session");
	}
	return result;
}

StreamlineSession::EvaluationResult StreamlineSession::EvaluateXeSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	bool outputReady = false;
	bool evaluationSkipped = false;
	EvaluationResult result = EvaluationResult::kFailed;
	if (!initialized || !featureXeSS || ActiveState().evaluator.dispatchFaulted)
		return result;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return result;

	auto* dxvk = &vulkan;
	if (!dxvk->CommandResourcesReady())
		return result;

	__try {
		sl::XeSSMode xessMode = sl::XeSSMode::eQuality;
		switch (a_qualityMode) {
		case 0:
			xessMode = sl::XeSSMode::eNativeAA;
			break;
		case 1:
			xessMode = sl::XeSSMode::eQuality;
			break;
		case 2:
			xessMode = sl::XeSSMode::eBalanced;
			break;
		case 3:
			xessMode = sl::XeSSMode::ePerformance;
			break;
		case 4:
			xessMode = sl::XeSSMode::eUltraPerformance;
			break;
		}

		sl::XeSSOptions xessOpts{};
		xessOpts.mode = xessMode;
		xessOpts.outputWidth = a_outputWidth;
		xessOpts.outputHeight = a_outputHeight;
		xessOpts.sharpness = a_sharpness;
		xessOpts.colorBuffersHDR = sl::Boolean::eTrue;
		const sl::Result optionsResult = ActiveState().slXeSSSetOptions(ActiveState().evaluator.viewport, xessOpts);
		if (optionsResult != sl::Result::eOk) {
			logger::error("[Streamline] XeSS options failed (result {})", static_cast<int>(optionsResult));
			return result;
		}

		const sl::Result evalRes = cs_EvaluateFeatureCore(&vulkan, sl::kFeatureXeSS, ActiveState().evaluator.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY,
			nullptr, &outputReady, &evaluationSkipped);
		result = cs_ClassifyEvaluation(evalRes, outputReady, evaluationSkipped);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] XeSS evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] XeSS dispatch faulted — Streamline disabled for this session");
	}
	return result;
}

StreamlineSession::EvaluationResult StreamlineSession::EvaluateFSR(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
	ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	uint32_t a_qualityMode, float a_sharpness,
	float a_jitterX, float a_jitterY)
{
	bool outputReady = false;
	bool evaluationSkipped = false;
	EvaluationResult result = EvaluationResult::kFailed;
	if (!initialized || !featureFSR || ActiveState().evaluator.dispatchFaulted)
		return result;
	if (!a_colorIn || !a_colorOut || !a_depth || !a_motionVectors)
		return result;

	auto* dxvk = &vulkan;
	if (!dxvk->CommandResourcesReady())
		return result;

	__try {
		sl::FSRMode fsrMode = sl::FSRMode::eMaxQuality;
		switch (a_qualityMode) {
		case 0:
			fsrMode = sl::FSRMode::eNativeAA;
			break;
		case 1:
			fsrMode = sl::FSRMode::eMaxQuality;
			break;
		case 2:
			fsrMode = sl::FSRMode::eBalanced;
			break;
		case 3:
			fsrMode = sl::FSRMode::eMaxPerformance;
			break;
		case 4:
			fsrMode = sl::FSRMode::eUltraPerformance;
			break;
		}

		sl::FSROptions fsrOpts{};
		fsrOpts.mode = fsrMode;
		fsrOpts.outputWidth = a_outputWidth;
		fsrOpts.outputHeight = a_outputHeight;
		fsrOpts.sharpness = a_sharpness;
		fsrOpts.colorBuffersHDR = sl::Boolean::eTrue;
		const sl::Result optionsResult = ActiveState().slFSRSetOptions(ActiveState().evaluator.viewport, fsrOpts);
		if (optionsResult != sl::Result::eOk) {
			logger::error("[Streamline] FSR options failed (result {})", static_cast<int>(optionsResult));
			return result;
		}

		const sl::Result evalRes = cs_EvaluateFeatureCore(&vulkan, sl::kFeatureFSR, ActiveState().evaluator.viewport,
			a_colorIn, a_colorOut, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY,
			nullptr, &outputReady, &evaluationSkipped);
		result = cs_ClassifyEvaluation(evalRes, outputReady, evaluationSkipped);

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		static uint32_t s_loggedDims = 0;
		const uint32_t dims = (a_renderWidth << 16) | (a_outputWidth & 0xFFFF);
		if (evalRes != s_loggedRes || dims != s_loggedDims) {
			s_loggedRes = evalRes;
			s_loggedDims = dims;
			logger::info("[Streamline] FSR evaluate result={} render={}x{} output={}x{}",
				static_cast<int>(evalRes), a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] FSR dispatch faulted — Streamline disabled for this session");
	}
	return result;
}

bool StreamlineSession::EvaluateFSRFrameGen(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_hudlessColor,
	uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight,
	float a_jitterX, float a_jitterY)
{
	// Isolate FSR frame-generation preparation from viewport 0 upscaling tags and constants.
	if (!initialized || !featureFSRFG || ActiveState().evaluator.dispatchFaulted)
		return false;
	if (!a_depth || !a_motionVectors || !a_hudlessColor)
		return false;

	auto* dxvk = &vulkan;
	if (!dxvk->CommandResourcesReady())
		return false;

	bool evaluationSubmitted = false;
	bool accepted = false;
	__try {
		const sl::ViewportHandle fgViewport{ 1 };
		const sl::Result evalRes = cs_EvaluateFeatureCore(&vulkan, sl::kFeatureFSR_G, fgViewport,
			nullptr, nullptr, a_depth, a_motionVectors,
			a_renderWidth, a_renderHeight, a_outputWidth, a_outputHeight, a_jitterX, a_jitterY,
			a_hudlessColor, &evaluationSubmitted);
		accepted = evalRes == sl::Result::eOk && evaluationSubmitted;

		static sl::Result s_loggedRes = sl::Result::eErrorNotInitialized;
		if (evalRes != s_loggedRes) {
			s_loggedRes = evalRes;
			logger::info("[Streamline] FSR FG-prepare result={} render={}x{}", static_cast<int>(evalRes), a_renderWidth, a_renderHeight);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] FSR FG-prepare faulted — Streamline disabled for this session");
	}
	return accepted;
}

bool StreamlineSession::SetDLSSGMode(bool a_enable, uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_displayWidth, uint32_t a_displayHeight,
	uint32_t a_numFramesToGenerate, bool a_autoMode, bool a_dynamic, float a_dynamicTargetFps)
{
	if (!initialized || !featureDLSSG || ActiveState().evaluator.dispatchFaulted)
		return false;

	// Do not call the options entry point while DLSS-G is runtime-unloaded.
	if (!ActiveState().dlssgCurrentlyLoaded.load(std::memory_order_acquire))
		return false;
	// Starting interpolation without matching current-frame constants and real
	// input tags can wedge Streamline's Vulkan pacer on its first flush.
	if (a_enable && (!ActiveState().dlssg.cloneTagsPrimed.load(std::memory_order_acquire) ||
		!ActiveState().dlssg.taggedThisFrame || ActiveState().evaluator.viewport0ConstantsFrame != ActiveState().evaluator.renderFrameId))
		return false;

	// Clamp the requested multiplier to the reported hardware limit.
	const uint32_t maxFrames = ActiveState().dlssg.maxFramesToGenerate.load(std::memory_order_acquire);
	uint32_t numFrames = a_numFramesToGenerate < 1u ? 1u : a_numFramesToGenerate;
	if (maxFrames > 0u && numFrames > maxFrames)
		numFrames = maxFrames;

	const bool dynamicResolution = a_renderWidth < a_displayWidth || a_renderHeight < a_displayHeight;
	{
		std::lock_guard lock(ActiveState().dlssg.apiMutex);
		// The cache represents the latest published request; the present callback
		// updates dlssgModeOn only after Streamline accepts it. Keep the cache and
		// pending-option check under the same lock as the present callback.
		const bool changed = !(ActiveState().dlssg.modeCached &&
			ActiveState().dlssg.cachedNumFrames == numFrames && ActiveState().dlssg.cachedAuto == a_autoMode &&
			ActiveState().dlssg.cachedDynamic == a_dynamic && ActiveState().dlssg.cachedDynamicFps == a_dynamicTargetFps &&
			ActiveState().dlssg.cachedRenderW == a_renderWidth && ActiveState().dlssg.cachedRenderH == a_renderHeight &&
			ActiveState().dlssg.cachedDisplayW == a_displayWidth && ActiveState().dlssg.cachedDisplayH == a_displayHeight &&
			ActiveState().dlssg.pendingEnable == a_enable);
		if (!changed)
			return !ActiveState().dlssg.optionsPending.load(std::memory_order_acquire) &&
			       ActiveState().dlssg.modeOn.load(std::memory_order_acquire) == a_enable;

		// Drain DXVK's asynchronous presenter only for this option transition.
		// The present-thread callback is the acknowledgment boundary; the
		// controller restores asynchronous DLSS-G presentation afterward.
		Upscaling::Runtime().Dxvk().SetPresentQueuePolicy(DxvkControl::PresentQueuePolicy::kSynchronous);

		sl::DLSSGOptions options{};
		options.mode = !a_enable ? sl::DLSSGMode::eOff :
		               a_dynamic ? sl::DLSSGMode::eDynamic :
		               a_autoMode ? sl::DLSSGMode::eAuto :
		                            sl::DLSSGMode::eOn;
		options.numFramesToGenerate = numFrames;
		if (a_dynamic)
			options.dynamicTargetFrameRate = a_dynamicTargetFps;
		// Retain resources across temporary loading-screen and menu disables.
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
		if (dynamicResolution) {
			options.flags |= sl::DLSSGFlags::eDynamicResolutionEnabled;
			options.dynamicResWidth = a_renderWidth;
			options.dynamicResHeight = a_renderHeight;
		}
		options.mvecDepthWidth = a_displayWidth;
		options.mvecDepthHeight = a_displayHeight;
		options.colorWidth = a_displayWidth;
		options.colorHeight = a_displayHeight;
		// DXVK presents on a dedicated Vulkan queue. Let Streamline block that
		// queue until its input reads complete instead of manually inserting the
		// eBlockNoClientQueues completion fence into the graphics queue. An
		// immediate graphics-queue wait can be ordered before SL's internal signal
		// submission and permanently wedge both queues.
		// Vulkan queue-parallel mode keeps Streamline from parking a client queue
		// inside the driver. Completion is observed on the present thread below;
		// any required reuse wait must be applied at the resource reuse point, not
		// immediately submitted ahead of Streamline's signal-producing work.
		options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockNoClientQueues;
		ActiveState().dlssg.pendingOptions = options;
		ActiveState().dlssg.optionsPending.store(true, std::memory_order_release);
		ActiveState().dlssg.pendingEnable = a_enable;
		ActiveState().dlssg.pendingRenderW = a_renderWidth;
		ActiveState().dlssg.pendingRenderH = a_renderHeight;
		ActiveState().dlssg.pendingDisplayW = a_displayWidth;
		ActiveState().dlssg.pendingDisplayH = a_displayHeight;
		ActiveState().dlssg.optionsEpoch.fetch_add(1u, std::memory_order_acq_rel);
		ActiveState().dlssg.ledgerBudget.store(32u, std::memory_order_release);
		ActiveState().dlssg.modeCached = true;
		ActiveState().dlssg.cachedNumFrames = numFrames;
		ActiveState().dlssg.cachedAuto = a_autoMode;
		ActiveState().dlssg.cachedDynamic = a_dynamic;
		ActiveState().dlssg.cachedDynamicFps = a_dynamicTargetFps;
		ActiveState().dlssg.cachedRenderW = a_renderWidth;
		ActiveState().dlssg.cachedRenderH = a_renderHeight;
		ActiveState().dlssg.cachedDisplayW = a_displayWidth;
		ActiveState().dlssg.cachedDisplayH = a_displayHeight;
	}
	logger::info("[Streamline] queued present-ordered DLSS-G mode={} ({}) numFrames={} targetFps={} (max {}) render={}x{} display={}x{} drs={}", a_enable,
		!a_enable ? "off" : a_dynamic ? "dynamic" : a_autoMode ? "auto" : "on", numFrames, a_dynamicTargetFps, maxFrames,
		a_renderWidth, a_renderHeight, a_displayWidth, a_displayHeight, dynamicResolution);
	// Delivery completes in DxvkPresentBeginCallback. Returning false keeps the
	// controller from draining/unloading the device before eOff has taken effect.
	return false;
}

bool StreamlineSession::SetFSRFrameGen(bool a_enable, bool a_hdr,
	bool a_debugView, bool a_debugTearLines, bool a_debugPacingLines, bool a_onlyPresentGenerated)
{
	// The caller retries until the runtime-loaded plugin accepts the option.
	if (!initialized || !featureFSRFG || !ActiveState().slFSRFrameGenerationSetOptions || ActiveState().evaluator.dispatchFaulted)
		return false;
	if (!ActiveState().fsrfgCurrentlyLoaded.load(std::memory_order_acquire))
		return false;

	bool ok = false;
	__try {
		sl::FSRFrameGenOptions options{};
		options.enabled = a_enable ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.colorBuffersHDR = a_hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugView = a_debugView ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugTearLines = a_debugTearLines ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.debugPacingLines = a_debugPacingLines ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.onlyPresentGenerated = a_onlyPresentGenerated ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		const sl::Result res = ActiveState().slFSRFrameGenerationSetOptions(ActiveState().evaluator.viewport, options);
		if (res != sl::Result::eOk) {
			ActiveState().evaluator.dispatchFaulted = true;
			logger::error("[Streamline] slFSRFrameGenerationSetOptions failed (result {})", static_cast<int>(res));
		} else {
			ok = true;
			ActiveState().fsrfgOwnsPresent.store(a_enable, std::memory_order_release);
			if (!a_enable)
				ActiveState().dlssg.frameGenerationMultiplier.store(1, std::memory_order_release);
			logger::info("[Streamline] FSR frame generation {} (HDR={})",
				a_enable ? "enabled" : "disabled", a_hdr);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] FSR SetFrameGen faulted — Streamline disabled for this session");
	}
	return ok;
}

void StreamlineSession::CaptureFSRFrameGenState()
{
	if (!initialized || !featureFSRFG || !ActiveState().slFSRGetFrameGenState || ActiveState().evaluator.dispatchFaulted)
		return;
	__try {
		sl::FSRFrameGenState frameGenState{};
		if (ActiveState().slFSRGetFrameGenState(ActiveState().evaluator.viewport, frameGenState) == sl::Result::eOk) {
			ActiveState().dlssg.frameGenerationMultiplier.store(
				std::max(frameGenState.numFramesActuallyPresented, 1u), std::memory_order_release);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
	}
}

uint32_t StreamlineSession::GetDLSSGMaxFramesToGenerate() const
{
	return ActiveState().dlssg.maxFramesToGenerate.load(std::memory_order_acquire);
}

uint32_t StreamlineSession::GetFrameGenerationMultiplier() const
{
	return ActiveState().dlssg.frameGenerationMultiplier.load(std::memory_order_acquire);
}

bool StreamlineSession::IsDLSSGDynamicSupported() const
{
	return ActiveState().dlssg.dynamicSupported.load(std::memory_order_acquire);
}

bool StreamlineSession::IsDLSSGFrameReady() const
{
	return ActiveState().dlssg.cloneTagsPrimed.load(std::memory_order_acquire) &&
	       ActiveState().dlssg.taggedThisFrame &&
	       ActiveState().evaluator.viewport0ConstantsFrame == ActiveState().evaluator.renderFrameId;
}

bool StreamlineSession::IsDLSSGOptionsPending() const
{
	return ActiveState().dlssg.optionsPending.load(std::memory_order_acquire);
}

bool StreamlineSession::IsDLSSGTransitionSettled() const
{
	// SetOptions acceptance only acknowledges option delivery. The Vulkan pacer
	// is created asynchronously inside the first intercepted presents, so retain
	// the transition barrier until it has produced and retired a short run of
	// monotonically advancing generated frames.
	constexpr uint32_t requiredPresentAcks = 8u;
	return !ActiveState().dlssg.optionsPending.load(std::memory_order_acquire) &&
		ActiveState().dlssg.modeOn.load(std::memory_order_acquire) &&
		ActiveState().dlssg.transitionPresentAcks.load(std::memory_order_acquire) >= requiredPresentAcks;
}

void StreamlineSession::TagDLSSGResources(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight,
	uint32_t a_displayWidth, uint32_t a_displayHeight)
{
	if (!initialized || !featureDLSSG || ActiveState().evaluator.dispatchFaulted)
		return;
	if (!a_depth || !a_motionVectors)
		return;

	auto* dxvk = &vulkan;
	if (!dxvk->CommandResourcesReady())
		return;
	if (!ActiveState().dlssg.cloneTagsPrimed.load(std::memory_order_acquire)) {
		ClearDLSSGTags();
		return;
	}

	__try {
		sl::FrameToken* token = RenderFrameToken();
		if (!token)
			return;

		VkDevice vkDevice = dxvk->GetDevice();
		const cs_VulkanProcAttempt createProcAttempt = cs_GetDeviceProcAddrSEH(
			dxvk->GetDeviceProcAddr(), vkDevice, "vkCreateImageView");
		const cs_VulkanProcAttempt destroyProcAttempt = cs_GetDeviceProcAddrSEH(
			dxvk->GetDeviceProcAddr(), vkDevice, "vkDestroyImageView");
		if (createProcAttempt.exceptionCode || destroyProcAttempt.exceptionCode) {
			ActiveState().evaluator.dispatchFaulted = true;
			return;
		}
		auto vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(createProcAttempt.function);
		auto vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(destroyProcAttempt.function);
		if (!vkCreateImageView || !vkDestroyImageView)
			return;
		ID3D11Resource* resources[] = { a_depth, a_motionVectors, a_hudlessColor };
		VkImageView views[3]{};
		uint32_t viewCount = 0;
		bool viewCreationTerminalFault = false;
		const auto destroyViews = [&]() {
			return cs_DestroyViews(dxvk, vkDevice, vkDestroyImageView, views, viewCount,
				resources, static_cast<uint32_t>(std::size(resources)));
		};
		const auto abandonViewsAfterCreationFailure = [&]() {
			if (!viewCreationTerminalFault) {
				destroyViews();
				return;
			}
			dxvk->QuarantineResourcesAfterVulkanDestructionFault(
				resources, static_cast<uint32_t>(std::size(resources)));
			std::fill(std::begin(views), std::end(views), VK_NULL_HANDLE);
		};

		const auto makeResource = [&](ID3D11Resource* a_res, sl::Resource& a_out,
			                          sl::SubresourceRange& a_subresource) {
			VkImage image = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			if (!vkCreateImageView || viewCount >= std::size(views))
				return false;
			const cs_GetVkImageAttempt imageAttempt =
				cs_GetVkImageSEH(dxvk, a_res, &image, &layout, &info);
			if (imageAttempt.exceptionCode) {
				viewCreationTerminalFault = true;
				ID3D11Resource* resource = a_res;
				dxvk->QuarantineResourcesAfterVulkanDestructionFault(&resource, 1);
				ActiveState().evaluator.dispatchFaulted = true;
				logger::error("[Streamline] DLSS-G DXVK image interop faulted (SEH {:#x})",
					imageAttempt.exceptionCode);
				return false;
			}
			if (!imageAttempt.succeeded || image == VK_NULL_HANDLE)
				return false;
			VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			ci.image = image;
			ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ci.format = info.format;
			ci.subresourceRange.aspectMask = cs_ImageAspect(info.format);
			ci.subresourceRange.levelCount = 1;
			ci.subresourceRange.layerCount = 1;
			VkImageView& view = views[viewCount];
			const cs_VulkanResultAttempt createAttempt =
				cs_CreateImageViewSEH(vkCreateImageView, vkDevice, &ci, &view);
			if (createAttempt.exceptionCode || createAttempt.result != VK_SUCCESS || view == VK_NULL_HANDLE) {
				if (createAttempt.exceptionCode || view != VK_NULL_HANDLE) {
					viewCreationTerminalFault = true;
					ID3D11Resource* resource = a_res;
					dxvk->QuarantineResourcesAfterVulkanDestructionFault(&resource, 1);
				}
				view = VK_NULL_HANDLE;
				ActiveState().evaluator.dispatchFaulted = true;
				logger::error("[Streamline] DLSS-G image-view creation failed (result {}, SEH {:#x})",
					static_cast<int>(createAttempt.result), createAttempt.exceptionCode);
				return false;
			}
			++viewCount;
			a_out = sl::Resource{ sl::ResourceType::eTex2d, image, nullptr, view, static_cast<uint32_t>(layout) };
			// Resource dimensions describe the image; tag extents describe the valid subrect.
			a_out.width = info.extent.width;
			a_out.height = info.extent.height;
			a_out.nativeFormat = static_cast<uint32_t>(info.format);
			a_out.mipLevels = info.mipLevels;
			a_out.arrayLayers = info.arrayLayers;
			a_out.usage = static_cast<uint32_t>(info.usage);
			a_out.flags = static_cast<uint32_t>(info.flags);
			a_subresource.aspectMask = ci.subresourceRange.aspectMask;
			a_subresource.baseMipLevel = 0;
			a_subresource.levelCount = 1;
			a_subresource.baseArrayLayer = 0;
			a_subresource.layerCount = 1;
			a_out.next = &a_subresource;
			return true;
		};

		sl::Resource depthRes{}, mvecRes{};
		sl::SubresourceRange depthRange{}, mvecRange{}, hudlessRange{};
		if (!makeResource(a_depth, depthRes, depthRange) ||
			!makeResource(a_motionVectors, mvecRes, mvecRange)) {
			abandonViewsAfterCreationFailure();
			return;
		}

		sl::Extent extent{};
		extent.width = a_renderWidth;
		extent.height = a_renderHeight;

		sl::ResourceTag tags[3];
		uint32_t tagCount = 0;
		tags[tagCount++] = { &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &extent };
		tags[tagCount++] = { &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &extent };

		// HUD-less color uses display dimensions rather than the render subrect.
		sl::Extent displayExtent{};
		displayExtent.width = a_displayWidth;
		displayExtent.height = a_displayHeight;
		sl::Resource hudlessRes{};
		const uint32_t viewsBeforeHudless = viewCount;
		if (a_hudlessColor) {
			if (makeResource(a_hudlessColor, hudlessRes, hudlessRange)) {
				tags[tagCount++] = { &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, &displayExtent };
			} else if (ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire) ||
				viewCount != viewsBeforeHudless) {
				abandonViewsAfterCreationFailure();
				return;
			} else {
				tags[tagCount++] = { nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, nullptr };
			}
		} else {
			// Clear stale HUD-less input when capture is unavailable.
			tags[tagCount++] = { nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, nullptr };
		}

		sl::Result tagResult = sl::Result::eErrorNotInitialized;
		bool lifetimesRetained = false;
		if (cs_SubmitPresentTags(dxvk, *token, ActiveState().evaluator.viewport, tags, tagCount,
				views, viewCount, resources, static_cast<uint32_t>(std::size(resources)), tagResult,
				lifetimesRetained)) {
			ActiveState().dlssg.taggedThisFrame = true;
		} else {
			if (!lifetimesRetained)
				destroyViews();
			logger::error("[Streamline] DLSS-G resource tag submission failed (result {})",
				static_cast<int>(tagResult));
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G tag faulted — Streamline disabled for this session");
	}
}

void StreamlineSession::ClearDLSSGTags()
{
	if (!initialized || !featureDLSSG || ActiveState().evaluator.dispatchFaulted)
		return;

	__try {
		sl::FrameToken* token = RenderFrameToken();
		if (!token)
			return;

		// Null tags force passthrough when interpolation inputs are unavailable.
		sl::ResourceTag tags[] = {
			sl::ResourceTag{ nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
			sl::ResourceTag{ nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
			sl::ResourceTag{ nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, nullptr },
		};
		auto* dxvk = &vulkan;
		if (!dxvk->CommandResourcesReady())
			return;
		sl::Result tagResult = sl::Result::eErrorNotInitialized;
		bool lifetimesRetained = false;
		if (cs_SubmitPresentTags(dxvk, *token, ActiveState().evaluator.viewport, tags,
				static_cast<uint32_t>(std::size(tags)), nullptr, 0, nullptr, 0, tagResult,
				lifetimesRetained)) {
			ActiveState().dlssg.taggedThisFrame = true;
		} else {
			logger::error("[Streamline] DLSS-G passthrough tag submission failed (result {})",
				static_cast<int>(tagResult));
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ActiveState().evaluator.dispatchFaulted = true;
		logger::error("[Streamline] DLSS-G clear-tags faulted — Streamline disabled for this session");
	}
}

bool StreamlineSession::EnsureDLSSGPresentTag()
{
	// Supply passthrough tags when the render pass did not provide interpolation inputs.
	if (!initialized || !featureDLSSG || ActiveState().evaluator.dispatchFaulted)
		return false;
	if (!ActiveState().dlssg.taggedThisFrame)
		ClearDLSSGTags();
	return ActiveState().dlssg.taggedThisFrame;
}

void StreamlineSession::RegisterDxvkOwnershipPredicate()
{
	// Streamline-owned swapchains must bypass DXVK's present-wait worker.
	if (!DxvkLoader::IsLoaded()) {
		logger::warn("[Streamline] DXVK module not loaded — cannot register ownership predicate");
		return;
	}
	if (!DxvkLoader::HasFrameGenerationOwnershipCallback()) {
		logger::warn("[Streamline] dxvkSetFrameGenOwnershipQuery not found in DXVK module");
		return;
	}
	const FrameGenerationBridge frameGenerationBridge;
	if (!frameGenerationBridge.Register({ &DxvkFrameGenerationOwnsSwapchain,
			&DxvkPresentBeginCallback, &DxvkPresentCompletedCallback, &DxvkSwapchainTornDownCallback })) {
		logger::warn("[Streamline] failed to register DXVK frame-generation callbacks");
		return;
	}
	logger::info("[Streamline] registered DXVK frame-generation ownership predicate");

	if (DxvkLoader::HasPresentCallbacks()) {
		logger::info("[Streamline] registered DXVK Vulkan present-thread options/state callbacks");
	} else {
		logger::warn("[Streamline] DXVK DLSS-G present-thread bridge unavailable");
	}

	// Streamline features may only be loaded or unloaded while no swapchain exists.
	if (DxvkLoader::HasSwapchainTeardownCallback()) {
		logger::info("[Streamline] registered DXVK swapchain-torn-down callback");
	} else {
		logger::warn("[Streamline] dxvkSetSwapchainTornDownCallback not found — frame-generation switching disabled");
	}

}

bool StreamlineSession::HasDispatchFaulted() const
{
	return ActiveState().evaluator.dispatchFaulted.load(std::memory_order_acquire);
}

void StreamlineSession::SetDLSSGDesiredLoaded(bool a_loaded)
{
	ActiveState().dlssgDesiredLoaded.store(a_loaded, std::memory_order_release);
}

bool StreamlineSession::IsDLSSGLoaded() const
{
	return ActiveState().dlssgCurrentlyLoaded.load(std::memory_order_acquire);
}

bool StreamlineSession::IsDLSSGLoadSettled() const
{
	return ActiveState().dlssgDesiredLoaded.load(std::memory_order_acquire) ==
	       ActiveState().dlssgCurrentlyLoaded.load(std::memory_order_acquire);
}

void StreamlineSession::SetFSRFGDesiredLoaded(bool a_loaded)
{
	ActiveState().fsrfgDesiredLoaded.store(a_loaded, std::memory_order_release);
}

bool StreamlineSession::IsFSRFGLoaded() const
{
	return ActiveState().fsrfgCurrentlyLoaded.load(std::memory_order_acquire);
}

bool StreamlineSession::IsFSRFGLoadSettled() const
{
	return ActiveState().fsrfgDesiredLoaded.load(std::memory_order_acquire) ==
	       ActiveState().fsrfgCurrentlyLoaded.load(std::memory_order_acquire);
}

bool StreamlineSession::IsFSRFGPresentOwner() const
{
	return ActiveState().fsrfgOwnsPresent.load(std::memory_order_acquire);
}
