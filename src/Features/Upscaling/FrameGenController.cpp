#include "FrameGenController.h"

#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "DxvkInterop.h"
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
			if (!upscaling.settings.frameGeneration)
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
		// Do nothing until the inputs are trustworthy: settings are not loaded
		// for the first frames of boot, and until Streamline's per-adapter
		// feature probe has run, GetFrameGenMethod()'s unsupported-DLSS-G ->
		// FSR fallback reads a transient wrong target. Acting early causes a
		// spurious boot unload->reload bounce of sl.dlss_g.
		if (!globals::features::upscaling.loaded ||
			!Streamline::GetSingleton()->IsFeatureSupportResolved())
			return;

		// Sample-exact window gap (donut DeviceManager::AnimateRenderPresent): while the window is not
		// visible/focused, the sample runs NOTHING — no SL calls, no DLSS-G mode change, no lifecycle
		// work; DLSS-G simply stays in its last mode with no presents flowing. So during the gap the
		// controller only idles (no phase transitions, no recreates initiated against a surface that
		// cannot present). It resumes reconciling on the first usable frame.
		if (Upscaling::IsWindowUnusable())
			return;

		const Method target = DesiredMethod();

		StepPhaseCompletion();
		StepModeTeardown(target);
		StepLoadState(target);
		StepFSRDelivery(target);
	}

	// Completes an in-flight FG method switch once its swapchain recreate has
	// landed (DxvkSwapchainTornDownCallback applied BOTH features' desired load
	// states in the no-swapchain window). Both features route through the SAME
	// single recreate, so the transition is done only when both have settled.
	void Controller::StepPhaseCompletion()
	{
		if (phase != Phase::kTransitioning)
			return;

		auto* sl = Streamline::GetSingleton();
		if (!sl->IsDLSSGLoadSettled() || !sl->IsFSRFGLoadSettled())
			return;

		// After a settle exactly one FG feature is loaded (or none, when FG was
		// turned off). Adopt whichever is now loaded as the present owner.
		if (sl->IsDLSSGLoaded()) {
			// Register the DLSS-G viewport against the NEW swapchain with the
			// interpolation mode off; the gameplay path turns it on from here.
			const auto dims = CurrentDims(false);
			sl->SetDLSSGMode(false, dims.displayWidth, dims.displayHeight);
			owner = Method::kDLSSG;
		} else if (sl->IsFSRFGLoaded()) {
			// FSR-FG's per-present enable is delivered by StepFSRDelivery below.
			owner = Method::kFSR;
		} else {
			owner = Method::kNone;
			// No FG feature loaded anymore: drop to stock async present (faster; sync present's
			// render-thread wait exists only for the FG present proxies). The ON edge is in
			// StepLoadState, so sync stays on through the whole transition and only releases here,
			// after the unload recreate has fully settled.
			Streamline::PushDxvkSyncPresent(false);
		}

		phase = Phase::kIdle;
		logger::info("[FrameGen] FG method switch settled - present owner: {}", Name(owner));
	}

	// Disengages the OUTGOING engaged method BEFORE the load change so the unload
	// is clean. Runs on the edge where a method stops being the desired one,
	// ahead of the unload recreate in StepLoadState.
	void Controller::StepModeTeardown(Method a_target)
	{
		auto* sl = Streamline::GetSingleton();

		// Leaving DLSS-G while its interpolation was engaged: per the guide,
		// interpolation must be OFF and the device drained BEFORE any swapchain
		// manipulation.
		if (dlssgModeOn && a_target != Method::kDLSSG) {
			const auto dims = CurrentDims(false);
			sl->SetDLSSGMode(false, dims.displayWidth, dims.displayHeight);
			if (auto* dxvk = DxvkInterop::GetSingleton())
				dxvk->WaitDeviceIdle();

			dlssgModeOn = false;
			logger::info("[FrameGen] DLSS-G interpolation off + device drained (leaving DLSS-G)");
		}

		// Leaving FSR-FG while it was delivered: unwrap the FFX
		// FrameInterpolationSwapChain from the sl.fsr_g plugin's present hook
		// before the unload recreate, so no live wrap sits over a torn-down
		// swapchain. sl.fsr_g is still loaded at this point (the unload recreate
		// has not run yet), so the disable is delivered.
		if (fsrDelivered == 1 && a_target != Method::kFSR) {
			const bool hdr = globals::features::hdrDisplay.loaded &&
			                 globals::features::hdrDisplay.settings.enableHDR;
			const auto dims = CurrentDims(false);
			const auto& s = globals::features::upscaling.settings;
			// Disable-delivery result is intentionally ignored: the unload recreate that follows tears the
			// plugin down regardless, so a missed disable this frame is harmless.
			(void)sl->SetFSRFrameGen(false, dims.renderWidth, dims.renderHeight,
				dims.displayWidth, dims.displayHeight, hdr,
				s.fgDebugView, s.fgDebugTearLines, s.fgDebugPacingLines, s.fgShowOnlyGenerated);
			fsrDelivered = 0;
			fsrVsyncRebakePending = false;
			if (owner == Method::kFSR)
				owner = Method::kNone;
			logger::info("[FrameGen] FSR-FG unwrapped (leaving FSR-FG)");
		}
	}

	// Keeps exactly the selected FG feature loaded: DLSS-G loaded iff DLSS-G is
	// the target, FSR-FG loaded iff FSR-FG is the target, the other unloaded. A
	// dormant loaded plugin costs FPS (its pass-through present proxy) so this is
	// a real win, not hygiene. Both (un)loads happen inside a SINGLE DXVK
	// swapchain recreate (guide section 18): SetDLSSGDesiredLoaded +
	// SetFSRFGDesiredLoaded are staged, then one recreate's torn-down callback
	// applies both in the no-swapchain window. Only acted on from kIdle so
	// recreates never stack.
	void Controller::StepLoadState(Method a_target)
	{
		if (phase != Phase::kIdle)
			return;

		auto* sl = Streamline::GetSingleton();
		const bool wantDLSSG = a_target == Method::kDLSSG;
		const bool wantFSRFG = a_target == Method::kFSR;

		// Sync present must be ON before any FG present proxy can be live (alt-tab safety), and it is
		// pure render-thread overhead without one. Turn it ON here the moment an FG method is targeted
		// (BEFORE the load lands — conservative through the whole transition); the OFF edge is in
		// StepPhaseCompletion once the unload has settled to no FG. Idempotent per-frame push.
		if (wantDLSSG || wantFSRFG)
			Streamline::PushDxvkSyncPresent(true);

		if (sl->IsDLSSGLoaded() == wantDLSSG && sl->IsFSRFGLoaded() == wantFSRFG) {
			// Load state already matches the target (steady state). Adopt the
			// selected FG feature as owner without a recreate. For DLSS-G, still
			// register the viewport with interpolation OFF exactly like the
			// load-landed path: engaging straight to mode-on without the
			// registered-off -> on edge leaves the present proxy passive (loaded,
			// stable, but never doubling).
			if (wantDLSSG && owner != Method::kDLSSG) {
				const auto dims = CurrentDims(false);
				sl->SetDLSSGMode(false, dims.displayWidth, dims.displayHeight);
				owner = Method::kDLSSG;
				logger::info("[FrameGen] DLSS-G already loaded - registered + adopted as present owner");
			} else if (wantFSRFG && owner != Method::kFSR) {
				owner = Method::kFSR;
				logger::info("[FrameGen] FSR-FG already loaded - adopted as present owner");
			}
			return;
		}

		// A method switch: unload the outgoing feature and load the incoming one.
		// Both desired states are applied in the SAME no-swapchain window, so ONE
		// recreate performs both transitions at once.
		sl->SetDLSSGDesiredLoaded(wantDLSSG);
		sl->SetFSRFGDesiredLoaded(wantFSRFG);
		Streamline::RequestDxvkSwapchainRecreate("FG method switch");
		phase = Phase::kTransitioning;
		// Clear the outgoing owner; StepPhaseCompletion re-adopts on settle.
		if (owner == Method::kDLSSG && !wantDLSSG)
			owner = Method::kNone;
		if (owner == Method::kFSR && !wantFSRFG)
			owner = Method::kNone;
		logger::info("[FrameGen] FG method switch requested: DLSS-G load={} FSR-FG load={} (swapchain recreate, guide section 18)",
			wantDLSSG, wantFSRFG);
	}

	// Pushes the desired FSR-FG enable to the sl.fsr_g plugin until it sticks
	// (its entry points come up a few frames after a load), and requests the
	// swapchain recreates the FFX wrap depends on (see the comments below).
	// Gated on sl.fsr_g being loaded and no transition outstanding; disabling is
	// handled by StepModeTeardown (it unwraps ahead of the unload recreate), so
	// this step only DELIVERS the enable while loaded.
	void Controller::StepFSRDelivery(Method a_target)
	{
		auto& upscaling = globals::features::upscaling;
		auto* sl = Streamline::GetSingleton();
		const bool wantFSR = a_target == Method::kFSR;

		if (!wantFSR || phase != Phase::kIdle || !sl->IsFSRFGLoaded())
			return;

		// While FSR-FG owns present, a vsync change needs a recreate of its own:
		// under the FFX wrap DXVK's dynamic present-mode switch (the
		// VK_EXT_swapchain_maintenance1 path a vsync toggle normally uses)
		// cannot reach the real swapchain, so the mode only changes when it is
		// baked in at swapchain creation. Deferred by one frame: the presenter
		// bakes the mode from the last-PRESENTED sync interval, and requesting
		// the recreate on the same frame the setting flips races that present
		// (a lost race bakes the old mode - observed in testing).
		if (fsrDelivered == 1 && upscaling.settings.vsync != fsrWrapVsync) {
			if (!fsrVsyncRebakePending) {
				fsrVsyncRebakePending = true;
			} else {
				fsrVsyncRebakePending = false;
				fsrWrapVsync = upscaling.settings.vsync;
				Streamline::RequestDxvkSwapchainRecreate("FSR-FG vsync change");
			}
		} else {
			fsrVsyncRebakePending = false;
		}

		const uint32_t debugSig = FSRDebugSignature(upscaling.settings);
		if (fsrDelivered == 1 && debugSig == fsrDebugSigDelivered)
			return;

		// A debug-flag re-push while already enabled applies per-present and must
		// not recreate; only an off->on edge needs the wrap recreate below.
		const bool enableEdge = fsrDelivered != 1;

		const bool hdr = globals::features::hdrDisplay.loaded &&
		                 globals::features::hdrDisplay.settings.enableHDR;
		const auto dims = CurrentDims(false);
		const auto& s = upscaling.settings;
		if (sl->SetFSRFrameGen(true,
				dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight, hdr,
				s.fgDebugView, s.fgDebugTearLines, s.fgDebugPacingLines, s.fgShowOnlyGenerated)) {
			fsrDelivered = 1;
			fsrDebugSigDelivered = debugSig;
			owner = Method::kFSR;
			fsrWrapVsync = s.vsync;
			logger::info("[FrameGen] FSR-FG enable delivered - present owner: {}", Name(owner));

			// The FFX FrameInterpolationSwapChain installs only inside the sl.fsr_g
			// plugin's vkCreateSwapchainKHR hook, so a mid-session enable needs one
			// swapchain recreate to take effect. (The plugin's cooperative
			// present-hook bootstrap can no longer drive this: DXVK switches
			// present modes via VK_EXT_swapchain_maintenance1 without recreating,
			// so nothing recreates the swapchain organically anymore.) Disable is
			// handled by StepModeTeardown, which unwraps from the plugin's own
			// present hook ahead of the unload recreate.
			if (enableEdge)
				Streamline::RequestDxvkSwapchainRecreate("FSR-FG wrap");
		}
	}

	// Turns DLSS-G interpolation on for the current display size. Gated on the
	// load reconcile having settled so a toggle engages exactly once, on the
	// final swapchain (never on one about to be torn down). SetDLSSGMode caches,
	// so steady-state calls are no-ops. Options carry no render dims (sample
	// fixed-res behavior) — the per-frame tag extents describe the render sub-rect.
	void Controller::EngageDLSSG()
	{
		auto* sl = Streamline::GetSingleton();
		if (phase != Phase::kIdle || !sl->IsDLSSGLoaded())
			return;

		auto& upscaling = globals::features::upscaling;
		const auto& s = upscaling.settings;

		const auto dims = CurrentDims(true);

		// "Dynamic" maps to eDynamic on hardware with Dynamic MFG (RTX 50+),
		// else eAuto. A fixed multiplier N means N-1 generated frames, clamped
		// to the hardware max inside SetDLSSGMode. targetFps 0 = auto-detect.
		const bool dynamic = s.dlssgDynamic;
		const bool useDynamic = dynamic && sl->IsDLSSGDynamicSupported();
		const bool useAuto = dynamic && !useDynamic;
		const uint32_t numFramesToGenerate = s.frameGenMultiplier > 1 ? s.frameGenMultiplier - 1 : 1;
		const float dynTargetFps = dynamic ? static_cast<float>(upscaling.GetTargetFrameRate()) : 0.0f;

		sl->SetDLSSGMode(true, dims.displayWidth, dims.displayHeight,
			numFramesToGenerate, useAuto, useDynamic, dynTargetFps);
		dlssgModeOn = true;
	}

}
