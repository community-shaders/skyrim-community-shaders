#include "FrameGenController.h"

#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "DXVKInterop.h"
#include "DxvkControl.h"
#include "Streamline.h"

#include "../../Globals.h"
#include "../../Utils/Game.h"

namespace FrameGen
{
	namespace
	{
		Method DesiredMethod()
		{
			auto& upscaling = globals::features::upscaling;
			// Reconcile persistent configuration intent, not transient presenter
			// readiness. A swapchain transition deliberately makes Active false;
			// treating that as intent would tear down the transition being awaited.
			if (!upscaling.IsFrameGenerationRequested())
				return Method::kNone;
			return upscaling.GetFrameGenMethod() == Upscaling::FrameGenMethod::kDLSSG
			           ? Method::kDLSSG
			           : Method::kFSR;
		}

		uint32_t FSRDebugSignature(const Upscaling::Settings& a_settings)
		{
			return (a_settings.fgDebugView ? 1u : 0u) |
			       (a_settings.fgDebugTearLines ? 2u : 0u) |
			       (a_settings.fgDebugPacingLines ? 4u : 0u) |
			       (a_settings.fgShowOnlyGenerated ? 8u : 0u);
		}

		struct Dims
		{
			uint32_t renderWidth;
			uint32_t renderHeight;
			uint32_t displayWidth;
			uint32_t displayHeight;
		};

		Dims CurrentDims(bool a_ignoreDynamicResolutionLock)
		{
			const auto display = float2{ (float)globals::game::graphicsState->screenWidth,
				(float)globals::game::graphicsState->screenHeight };
			const auto render = Util::ConvertToDynamic(display, a_ignoreDynamicResolutionLock);
			return { (uint32_t)render.x, (uint32_t)render.y, (uint32_t)display.x, (uint32_t)display.y };
		}

		bool IsHDRActive()
		{
			const auto& hdr = globals::features::hdrDisplay;
			return hdr.loaded && hdr.IsHDREnabledForFrame();
		}

	}

	void FrameGenerationCoordinator::BeginPresenterRecreateTransition()
	{
		vulkan.BeginPresenterColorSpaceTransition(IsHDRActive(), true);
	}

	const char* FrameGenerationCoordinator::Name(Method a_method)
	{
		switch (a_method) {
		case Method::kFSR:
			return "FSR-FG";
		case Method::kDLSSG:
			return "DLSS-G";
		default:
			return "off";
		}
	}

	Method FrameGenerationCoordinator::GetDesiredMethod() const
	{
		return DesiredMethod();
	}

	void FrameGenerationCoordinator::Reconcile()
	{
		// Wait for settings and hardware fallbacks to settle.
		if (!globals::features::upscaling.loaded ||
			!streamline.IsFeatureSupportResolved() ||
			!globals::game::graphicsState)
			return;

		if (Upscaling::IsWindowUnusable())
			return;

		auto* streamlineBackend = &streamline;
		const bool dispatchFaulted = streamlineBackend->HasDispatchFaulted();
		if (dispatchFaulted)
			globals::features::upscaling.settings.frameGeneration = false;

		if (dispatchFaulted || faultRecoveryRequested) {
			if (!faultRecoveryRequested) {
				const bool completionProven = vulkan.HasPendingPresentWaitSemaphore() ?
					vulkan.DiscardPendingPresentWaitSemaphore() : vulkan.WaitDeviceIdle();
				if (!completionProven) {
					logger::error("[FrameGen] Streamline fault teardown deferred because GPU completion could not be proven");
					return;
				}
				streamlineBackend->SetDLSSGDesiredLoaded(false);
				streamlineBackend->SetFSRFGDesiredLoaded(false);
				faultRecoveryRequested = true;
				BeginPresenterRecreateTransition();
				dxvk.RequestSwapchainRecreate("Streamline dispatch fault");
				phase = Phase::kTransitioning;
				logger::error("[FrameGen] Streamline faulted; forcing frame-generation teardown at swapchain recreation");
			}
			if (phase == Phase::kTransitioning) {
				if (!streamlineBackend->IsDLSSGLoadSettled() || !streamlineBackend->IsFSRFGLoadSettled() ||
					streamlineBackend->IsDLSSGLoaded() || streamlineBackend->IsFSRFGLoaded())
					return;
				if (!vulkan.DrainCommandRing()) {
					logger::error("[FrameGen] Streamline fault cleanup deferred because command completion could not be proven");
					return;
				}
				StepPhaseCompletion();
			}
			if (!dispatchFaulted && phase == Phase::kIdle)
				faultRecoveryRequested = false;
			return;
		}

		const Method target = DesiredMethod();

		StepPhaseCompletion();
		if (!StepModeTeardown(target))
			return;
		StepLoadState(target);
		StepFSRDelivery(target);
	}

	void FrameGenerationCoordinator::StepPhaseCompletion()
	{
		if (phase != Phase::kTransitioning)
			return;

		auto* sl = &streamline;
		if (!sl->IsDLSSGLoadSettled() || !sl->IsFSRFGLoadSettled())
			return;

		if (sl->IsDLSSGLoaded()) {
			const auto dims = CurrentDims(false);
			if (!sl->SetDLSSGMode(false, dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight))
				return;
			owner = Method::kDLSSG;
		} else if (sl->IsFSRFGLoaded()) {
			owner = Method::kFSR;
		} else {
			owner = Method::kNone;
			dxvk.SetPresentQueuePolicy(DxvkControl::PresentQueuePolicy::kUnrestricted);
		}

		phase = Phase::kIdle;
		logger::info("[FrameGen] FG method switch settled - present owner: {}", Name(owner));
	}

	bool FrameGenerationCoordinator::StepModeTeardown(Method a_target)
	{
		auto* sl = &streamline;
		if (!sl->IsDLSSGLoaded() && (dlssgModeOn || owner == Method::kDLSSG)) {
			dlssgModeOn = false;
			if (owner == Method::kDLSSG)
				owner = Method::kNone;
			logger::warn("[FrameGen] DLSS-G was unloaded before mode teardown - reconciled local state");
		}

		// Streamline requires DLSS-G to be disabled and drained before teardown.
		if (dlssgModeOn && a_target != Method::kDLSSG) {
			const auto dims = CurrentDims(false);
			if (!sl->SetDLSSGMode(false, dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight))
				return false;
			if (!vulkan.WaitDeviceIdle()) {
				logger::error("[FrameGen] DLSS-G teardown deferred because device idle could not be proven");
				return false;
			}

			dlssgModeOn = false;
			logger::info("[FrameGen] DLSS-G interpolation off + device drained (leaving DLSS-G)");
		}

		if (fsrDelivery == FSRDelivery::kDelivered && a_target != Method::kFSR) {
			if (!vulkan.DrainCommandRing()) {
				logger::error("[FrameGen] FSR-FG teardown deferred because command completion could not be proven");
				return false;
			}
			const auto& s = globals::features::upscaling.settings;
			if (!sl->SetFSRFrameGen(false, fsrHDRDelivered,
					s.fgDebugView, s.fgDebugTearLines, s.fgDebugPacingLines, s.fgShowOnlyGenerated))
				return false;
			fsrDelivery = FSRDelivery::kPending;
			fsrVsyncRebakePending = false;
			if (owner == Method::kFSR)
				owner = Method::kNone;
			logger::info("[FrameGen] FSR-FG unwrapped (leaving FSR-FG)");
		}

		return true;
	}

	void FrameGenerationCoordinator::StepLoadState(Method a_target)
	{
		if (phase != Phase::kIdle)
			return;

		auto* sl = &streamline;
		const bool wantDLSSG = a_target == Method::kDLSSG;
		const bool wantFSRFG = a_target == Method::kFSR;

		// FSR-G requires synchronous present continuously. DLSS-G transitions are
		// fully drained, while steady state permits one overlapping intercepted
		// present (depth two including the call being queued).
		const bool dlssgTransition = wantDLSSG &&
			(owner != Method::kDLSSG || !sl->IsDLSSGTransitionSettled());
		// The FIFO interop-submit contract makes a tag semaphore presenter-visible
		// only after its signal submission executes, so this bounded overlap cannot
		// recreate the older-present/future-semaphore cycle.
		const auto presentQueuePolicy = wantFSRFG ? DxvkControl::PresentQueuePolicy::kSynchronous :
			dlssgTransition ? DxvkControl::PresentQueuePolicy::kSynchronous :
			wantDLSSG ? DxvkControl::PresentQueuePolicy::kBoundedOverlap :
			             DxvkControl::PresentQueuePolicy::kUnrestricted;
		dxvk.SetPresentQueuePolicy(presentQueuePolicy);

		if (sl->IsDLSSGLoaded() == wantDLSSG && sl->IsFSRFGLoaded() == wantFSRFG) {
			if (wantDLSSG && owner != Method::kDLSSG) {
				const auto dims = CurrentDims(false);
				if (!sl->SetDLSSGMode(false, dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight))
					return;
				owner = Method::kDLSSG;
				logger::info("[FrameGen] DLSS-G already loaded - registered + adopted as present owner");
			} else if (wantFSRFG && owner != Method::kFSR) {
				owner = Method::kFSR;
				logger::info("[FrameGen] FSR-FG already loaded - adopted as present owner");
			}
			return;
		}

		sl->SetDLSSGDesiredLoaded(wantDLSSG);
		sl->SetFSRFGDesiredLoaded(wantFSRFG);
		BeginPresenterRecreateTransition();
		dxvk.RequestSwapchainRecreate("FG method switch");
		phase = Phase::kTransitioning;
		if (owner == Method::kDLSSG && !wantDLSSG)
			owner = Method::kNone;
		if (owner == Method::kFSR && !wantFSRFG)
			owner = Method::kNone;
		logger::info("[FrameGen] FG method switch requested: DLSS-G load={} FSR-FG load={} (swapchain recreate)",
			wantDLSSG, wantFSRFG);
	}

	void FrameGenerationCoordinator::StepFSRDelivery(Method a_target)
	{
		auto& upscaling = globals::features::upscaling;
		auto* sl = &streamline;
		const bool wantFSR = a_target == Method::kFSR;

		if (!wantFSR || phase != Phase::kIdle || !sl->IsFSRFGLoaded())
			return;

		// Present the new interval once before recreating the FFX-wrapped swapchain.
		if (fsrDelivery == FSRDelivery::kDelivered && upscaling.settings.vsync != fsrWrapVsync) {
			if (!fsrVsyncRebakePending) {
				fsrVsyncRebakePending = true;
			} else {
				fsrVsyncRebakePending = false;
				fsrWrapVsync = upscaling.settings.vsync;
				BeginPresenterRecreateTransition();
				dxvk.RequestSwapchainRecreate("FSR-FG vsync change");
			}
		} else {
			fsrVsyncRebakePending = false;
		}

		const uint32_t debugSig = FSRDebugSignature(upscaling.settings);
		const bool hdr = IsHDRActive();
		if (fsrDelivery == FSRDelivery::kDelivered && debugSig == fsrDebugSigDelivered && hdr == fsrHDRDelivered)
			return;

		const bool enableEdge = fsrDelivery != FSRDelivery::kDelivered;
		const bool hdrChanged = fsrDelivery == FSRDelivery::kDelivered && hdr != fsrHDRDelivered;

		const auto& s = upscaling.settings;
		if (sl->SetFSRFrameGen(true, hdr,
				s.fgDebugView, s.fgDebugTearLines, s.fgDebugPacingLines, s.fgShowOnlyGenerated)) {
			fsrDelivery = FSRDelivery::kDelivered;
			fsrDebugSigDelivered = debugSig;
			fsrHDRDelivered = hdr;
			owner = Method::kFSR;
			fsrWrapVsync = s.vsync;
			logger::info("[FrameGen] FSR-FG enable delivered - present owner: {}", Name(owner));

			// FFX installs its interpolation swapchain during vkCreateSwapchainKHR.
			// On the enable edge, its present hook must first present once on the
			// existing plain swapchain and return VK_SUBOPTIMAL_KHR. That gives FFX
			// a safe present/fence hand-off before DXVK recreates and wraps on the
			// following acquire. Keep the presenter transition barrier so frame-gen
			// evaluation waits for the resulting serial, but do not force recreation
			// from acquireNextImage and bypass that required present.
			if (enableEdge) {
				BeginPresenterRecreateTransition();
				logger::info("[FrameGen] awaiting present-ordered FSR-FG swapchain wrap");
			} else if (hdrChanged) {
				BeginPresenterRecreateTransition();
				dxvk.RequestSwapchainRecreate("FSR-FG HDR transfer change");
			}
		}
	}

	bool FrameGenerationCoordinator::IsFSRPresenterReady() const
	{
		if (phase != Phase::kIdle || DesiredMethod() != Method::kFSR || fsrDelivery != FSRDelivery::kDelivered ||
			!streamline.IsFSRFGLoaded())
			return false;

		const bool hdr = IsHDRActive();
		return fsrHDRDelivered == hdr &&
		       vulkan.IsPresenterStateReadyForFrame(hdr);
	}

	void FrameGenerationCoordinator::EngageDLSSG()
	{
		auto* sl = &streamline;
		// StepLoadState first adopts an already-loaded DLSS-G presenter by
		// delivering and acknowledging eOff. Do not overwrite that pending
		// request with eOn in the same frame; doing so leaves owner unset and
		// repeats the off/on sequence on every subsequent frame.
		if (phase != Phase::kIdle || owner != Method::kDLSSG || !sl->IsDLSSGLoaded())
			return;
		if (!sl->IsDLSSGFrameReady())
			return;

		auto& upscaling = globals::features::upscaling;
		const auto& s = upscaling.settings;

		const auto dims = CurrentDims(true);

		const bool dynamic = s.dlssgDynamic;
		const bool useDynamic = dynamic && sl->IsDLSSGDynamicSupported();
		const bool useAuto = dynamic && !useDynamic;
		const uint32_t numFramesToGenerate = upscaling.GetFixedDLSSGMultiplier() - 1u;
		const float dynTargetFps = dynamic ? static_cast<float>(upscaling.GetTargetFrameRate()) : 0.0f;

		if (sl->SetDLSSGMode(true, dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight,
				numFramesToGenerate, useAuto, useDynamic, dynTargetFps))
			dlssgModeOn = true;
	}

	void FrameGenerationCoordinator::NotifyFaultTeardownRequested()
	{
		dlssgModeOn = false;
		faultRecoveryRequested = true;
		phase = Phase::kTransitioning;
	}

}
