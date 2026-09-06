#include "FrameGenController.h"

#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "DXVKInterop.h"
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
			if (!upscaling.IsFrameGenerationActive())
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

		void BeginPresenterRecreateTransition()
		{
			DXVKInterop::GetSingleton()->BeginPresenterColorSpaceTransition(IsHDRActive(), true);
		}
	}

	const char* Controller::Name(Method a_method)
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

	void Controller::Reconcile()
	{
		// Wait for settings and hardware fallbacks to settle.
		if (!globals::features::upscaling.loaded ||
			!Streamline::GetSingleton()->IsFeatureSupportResolved() ||
			!globals::game::graphicsState)
			return;

		if (Upscaling::IsWindowUnusable())
			return;

		auto* streamline = Streamline::GetSingleton();
		const bool dispatchFaulted = streamline->HasDispatchFaulted();
		if (dispatchFaulted)
			globals::features::upscaling.settings.frameGeneration = false;

		if (dispatchFaulted || faultRecoveryRequested) {
			if (!faultRecoveryRequested) {
				auto* dxvk = DXVKInterop::GetSingleton();
				const bool completionProven = dxvk->IsDeviceLost() ||
				                              (dxvk->HasPendingPresentWaitSemaphore() ?
				                                      dxvk->DiscardPendingPresentWaitSemaphore() :
				                                      dxvk->WaitDeviceIdle());
				if (!completionProven) {
					// Once only: this re-enters every reconcile, and a fault that cannot prove
					// completion would otherwise log on every frame for the rest of the session.
					static bool deferralReported = false;
					if (!std::exchange(deferralReported, true))
						logger::error("[FrameGen] Streamline fault teardown deferred because GPU completion could not be proven");
					return;
				}
				streamline->SetDLSSGDesiredLoaded(false);
				streamline->SetFSRFGDesiredLoaded(false);
				faultRecoveryRequested = true;
				BeginPresenterRecreateTransition();
				Streamline::RequestDxvkSwapchainRecreate("Streamline dispatch fault");
				phase = Phase::kTransitioning;
				logger::error("[FrameGen] Streamline faulted; forcing frame-generation teardown at swapchain recreation");
			}
			if (phase == Phase::kTransitioning) {
				if (!streamline->IsDLSSGLoadSettled() || !streamline->IsFSRFGLoadSettled() ||
					streamline->IsDLSSGLoaded() || streamline->IsFSRFGLoaded())
					return;
				if (!DXVKInterop::GetSingleton()->DrainCommandRing()) {
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

	void Controller::StepPhaseCompletion()
	{
		if (phase != Phase::kTransitioning)
			return;

		auto* sl = Streamline::GetSingleton();
		if (!sl->IsDLSSGLoadSettled() || !sl->IsFSRFGLoadSettled())
			return;

		if (sl->IsDLSSGLoaded()) {
			const auto dims = CurrentDims(false);
			if (!sl->SetDLSSGMode(false, dims.displayWidth, dims.displayHeight)) {
				if (++phaseCompletionRetries <= 120)
					return;
				logger::error("[FrameGen] SetDLSSGMode(false) failed {} times; forcing phase completion",
					phaseCompletionRetries);
			}
			owner = Method::kDLSSG;
		} else if (sl->IsFSRFGLoaded()) {
			owner = Method::kFSR;
		} else {
			owner = Method::kNone;
			Streamline::PushDxvkSyncPresent(false);
		}

		phaseCompletionRetries = 0;
		phase = Phase::kIdle;
		logger::info("[FrameGen] FG method switch settled - present owner: {}", Name(owner));

		// The proxy is installed and steady now, so relax the fully-synchronous present used across
		// the switch itself into a bounded overlap. Depth zero blocks the render thread in
		// waitForSubmission on every present; measured with FSR-FG that left the GPU idle 33% of the
		// frame while the CPU was the limiter.
		if (owner == Method::kFSR)
			Streamline::PushDxvkPresentQueueDepth(2u);
	}

	bool Controller::StepModeTeardown(Method a_target)
	{
		auto* sl = Streamline::GetSingleton();
		if (!sl->IsDLSSGLoaded() && (dlssgModeOn || owner == Method::kDLSSG)) {
			dlssgModeOn = false;
			if (owner == Method::kDLSSG)
				owner = Method::kNone;
			logger::warn("[FrameGen] DLSS-G was unloaded before mode teardown - reconciled local state");
		}
		if (!sl->IsFSRFGLoaded() && (fsrDelivered == FSRDeliveryState::kDelivered || owner == Method::kFSR)) {
			fsrDelivered = FSRDeliveryState::kTornDown;
			fsrVsyncRebakePending = false;
			if (owner == Method::kFSR)
				owner = Method::kNone;
			logger::warn("[FrameGen] FSR-FG was unloaded before mode teardown - reconciled local state");
		}

		// Streamline requires DLSS-G to be disabled and drained before teardown.
		if (dlssgModeOn && a_target != Method::kDLSSG) {
			const auto dims = CurrentDims(false);
			if (!sl->SetDLSSGMode(false, dims.displayWidth, dims.displayHeight))
				return false;
			// A lost device has nothing left in flight to drain, and waiting on it can never
			// succeed, so deferring here spins forever -- 17.6k of these in one session even after
			// the wait itself was latched. Treat the mode as torn down and let the controller move
			// on; the loss is reported once by DXVKInterop.
			if (!DXVKInterop::GetSingleton()->IsDeviceLost() &&
				!DXVKInterop::GetSingleton()->WaitDeviceIdle()) {
				static bool deferralReported = false;
				if (!std::exchange(deferralReported, true))
					logger::error("[FrameGen] DLSS-G teardown deferred because device idle could not be proven");
				return false;
			}

			dlssgModeOn = false;
			logger::info("[FrameGen] DLSS-G interpolation off + device drained (leaving DLSS-G)");
		}

		if (fsrDelivered == FSRDeliveryState::kDelivered && a_target != Method::kFSR) {
			if (!DXVKInterop::GetSingleton()->DrainCommandRing()) {
				logger::error("[FrameGen] FSR-FG teardown deferred because command completion could not be proven");
				return false;
			}
			const auto& s = globals::features::upscaling.settings;
			if (!sl->SetFSRFrameGen(false, fsrHDRDelivered,
					s.fgDebugView, s.fgDebugTearLines, s.fgDebugPacingLines, s.fgShowOnlyGenerated))
				return false;
			fsrDelivered = FSRDeliveryState::kTornDown;
			fsrVsyncRebakePending = false;
			if (owner == Method::kFSR)
				owner = Method::kNone;
			logger::info("[FrameGen] FSR-FG unwrapped (leaving FSR-FG)");
		}

		return true;
	}

	void Controller::StepLoadState(Method a_target)
	{
		if (phase != Phase::kIdle)
			return;

		auto* sl = Streamline::GetSingleton();
		const bool wantDLSSG = a_target == Method::kDLSSG;
		const bool wantFSRFG = a_target == Method::kFSR;

		// Enable synchronous present before installing either present proxy.
		if (wantDLSSG || wantFSRFG)
			Streamline::PushDxvkSyncPresent(true);

		if (sl->IsDLSSGLoaded() == wantDLSSG && sl->IsFSRFGLoaded() == wantFSRFG) {
			if (wantDLSSG && owner != Method::kDLSSG) {
				const auto dims = CurrentDims(false);
				if (!sl->SetDLSSGMode(false, dims.displayWidth, dims.displayHeight))
					return;
				owner = Method::kDLSSG;
				logger::info("[FrameGen] DLSS-G already loaded - registered + adopted as present owner");
			} else if (wantFSRFG && owner != Method::kFSR) {
				owner = Method::kFSR;
				logger::info("[FrameGen] FSR-FG already loaded - adopted as present owner");
			}
			return;
		}

		// The two interpolators want opposite presentation behaviour, so choose per method rather
		// than once for the session. Tear-free replaces a queued image instead of queueing it, which
		// is what DLSS-G's flip metering needs and what destroys FSR-FG once FFX stops spacing its
		// pair: the interpolated frame lands a fraction of a millisecond before the real one and is
		// superseded before scanout. Measured on a 60 Hz display at a 20 fps cap, tear-free left
		// FSR-FG at 10.5 fps against a 20 fps target with 48% of presents never displayed, where a
		// queueing mode held 18.9; DLSS-G wanted the reverse, holding its target exactly under
		// tear-free and losing 44% under the queueing mode.
		//
		// Presenter::pickPresentMode reads this at swapchain creation, so it has to be set before
		// the recreate below -- which a method switch performs anyway, so no extra recreate is
		// introduced by choosing here.
		Streamline::PushDxvkTearingPreference(
			wantFSRFG ? 2u : globals::features::upscaling.GetPresentModePreference());

		sl->SetDLSSGDesiredLoaded(wantDLSSG);
		sl->SetFSRFGDesiredLoaded(wantFSRFG);
		BeginPresenterRecreateTransition();
		Streamline::RequestDxvkSwapchainRecreate("FG method switch");
		phase = Phase::kTransitioning;
		if (owner == Method::kDLSSG && !wantDLSSG)
			owner = Method::kNone;
		if (owner == Method::kFSR && !wantFSRFG)
			owner = Method::kNone;
		logger::info("[FrameGen] FG method switch requested: DLSS-G load={} FSR-FG load={} (swapchain recreate)",
			wantDLSSG, wantFSRFG);
	}

	void Controller::StepFSRDelivery(Method a_target)
	{
		auto& upscaling = globals::features::upscaling;
		auto* sl = Streamline::GetSingleton();
		const bool wantFSR = a_target == Method::kFSR;

		if (!wantFSR || phase != Phase::kIdle || !sl->IsFSRFGLoaded())
			return;

		// Present the new interval once before recreating the FFX-wrapped swapchain.
		if (fsrDelivered == FSRDeliveryState::kDelivered && upscaling.settings.vsync != fsrWrapVsync) {
			if (!fsrVsyncRebakePending) {
				fsrVsyncRebakePending = true;
			} else {
				fsrVsyncRebakePending = false;
				fsrWrapVsync = upscaling.settings.vsync;
				BeginPresenterRecreateTransition();
				Streamline::RequestDxvkSwapchainRecreate("FSR-FG vsync change");
			}
		} else {
			fsrVsyncRebakePending = false;
		}

		const uint32_t debugSig = FSRDebugSignature(upscaling.settings);
		const bool hdr = IsHDRActive();
		if (fsrDelivered == FSRDeliveryState::kDelivered && debugSig == fsrDebugSigDelivered && hdr == fsrHDRDelivered)
			return;

		const bool enableEdge = fsrDelivered != FSRDeliveryState::kDelivered;
		const bool hdrChanged = fsrDelivered == FSRDeliveryState::kDelivered && hdr != fsrHDRDelivered;

		const auto& s = upscaling.settings;
		if (sl->SetFSRFrameGen(true, hdr,
				s.fgDebugView, s.fgDebugTearLines, s.fgDebugPacingLines, s.fgShowOnlyGenerated)) {
			fsrDelivered = FSRDeliveryState::kDelivered;
			fsrDebugSigDelivered = debugSig;
			fsrHDRDelivered = hdr;
			owner = Method::kFSR;
			fsrWrapVsync = s.vsync;
			logger::info("[FrameGen] FSR-FG enable delivered - present owner: {}", Name(owner));

			// FFX installs its interpolation swapchain during vkCreateSwapchainKHR.
			if (enableEdge) {
				BeginPresenterRecreateTransition();
				Streamline::RequestDxvkSwapchainRecreate("FSR-FG wrap");
			} else if (hdrChanged) {
				BeginPresenterRecreateTransition();
				Streamline::RequestDxvkSwapchainRecreate("FSR-FG HDR transfer change");
			}
		}
	}

	bool Controller::IsFSRPresenterReady() const
	{
		if (phase != Phase::kIdle || DesiredMethod() != Method::kFSR || fsrDelivered != FSRDeliveryState::kDelivered ||
			!Streamline::GetSingleton()->IsFSRFGLoaded())
			return false;

		const bool hdr = IsHDRActive();
		return fsrHDRDelivered == hdr &&
		       DXVKInterop::GetSingleton()->IsPresenterStateReadyForFrame(hdr);
	}

	void Controller::EngageDLSSG()
	{
		auto* sl = Streamline::GetSingleton();
		if (phase != Phase::kIdle || !sl->IsDLSSGLoaded())
			return;

		auto& upscaling = globals::features::upscaling;
		const auto& s = upscaling.settings;

		const auto dims = CurrentDims(true);

		const bool dynamic = s.dlssgDynamic;
		const bool useDynamic = dynamic && sl->IsDLSSGDynamicSupported();
		const bool useAuto = dynamic && !useDynamic;
		const uint32_t numFramesToGenerate = upscaling.GetFixedDLSSGMultiplier() - 1u;
		const float dynTargetFps = dynamic ? static_cast<float>(upscaling.GetTargetFrameRate()) : 0.0f;

		if (sl->SetDLSSGMode(true, dims.displayWidth, dims.displayHeight,
				numFramesToGenerate, useAuto, useDynamic, dynTargetFps))
			dlssgModeOn = true;
	}

	void Controller::NotifyFaultTeardownRequested()
	{
		dlssgModeOn = false;
		fsrDelivered = FSRDeliveryState::kTornDown;
		fsrVsyncRebakePending = false;
		faultRecoveryRequested = true;
		phase = Phase::kTransitioning;
	}

}
