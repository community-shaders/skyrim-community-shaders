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

		// Guide §12: DLSS-G MUST be turned OFF while the window/surface is in transition — occluded /
		// minimized / unfocused / resized (IsWindowUnusable) OR mid render-res/preset change
		// (IsUpscalerReconfiguring). Leaving it eOn and merely withholding presents does NOT prevent the
		// freeze: when DLSS-G's present resumes at the transitioning surface it hangs in the driver
		// (nvoglv64!DrvPresentBuffers) and EvaluateDLSS's forced CS-thread sync then deadlocks behind it —
		// reproduced on BOTH alt-tab and a simple quality-slider (qualityMode) change. So actually pause
		// it: PauseDLSSGForWindowGap is idempotent (one eOff via SetDLSSGMode, then no-ops through
		// SetDLSSGMode's cache) and clears dlssgModeOn so the per-frame EngageDLSSG path re-engages once
		// the surface has settled (window usable AND no reconfiguration in flight), at the new render size.
		if (Upscaling::IsWindowUnusable() || Upscaling::IsUpscalerReconfiguring()) {
			Streamline::GetSingleton()->PauseDLSSGForWindowGap();
			return;
		}

		const Method target = DesiredMethod();

		StepPhaseCompletion();
		StepDLSSGModeTeardown(target);
		StepDLSSGLoadState(target);
		StepFSRDelivery(target);
	}

	// Completes an in-flight DLSS-G load/unload once its swapchain recreate has
	// landed (DxvkSwapchainTornDownCallback applied the desired state in the
	// no-swapchain window).
	void Controller::StepPhaseCompletion()
	{
		if (phase == Phase::kIdle)
			return;

		auto* sl = Streamline::GetSingleton();
		if (!sl->IsDLSSGLoadSettled())
			return;

		if (phase == Phase::kLoadingDLSSG && sl->IsDLSSGLoaded()) {
			// Register the DLSS-G viewport against the NEW swapchain with the
			// interpolation mode off; the gameplay path turns it on from here.
			const auto dims = CurrentDims(false);
			sl->SetDLSSGMode(false, dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight);
			owner = Method::kDLSSG;
			logger::info("[FrameGen] DLSS-G load landed - present owner: {}", Name(owner));
		} else if (phase == Phase::kUnloadingDLSSG && !sl->IsDLSSGLoaded()) {
			owner = Method::kNone;
			logger::info("[FrameGen] DLSS-G unload landed - present owner: {}", Name(owner));
		} else {
			// Settled, but not to the state this phase was waiting for (e.g. the
			// target flipped mid-transition). Fall back to idle; the load-state
			// step below issues the next transition.
			logger::info("[FrameGen] load reconcile settled to loaded={} - re-evaluating", sl->IsDLSSGLoaded());
		}

		phase = Phase::kIdle;
	}

	// Per the Streamline DLSS-G guide, interpolation must be OFF and the device
	// drained BEFORE any swapchain manipulation. Runs on the edge where DLSS-G
	// stops being the desired method, ahead of the unload recreate below.
	void Controller::StepDLSSGModeTeardown(Method a_target)
	{
		if (!dlssgModeOn || a_target == Method::kDLSSG)
			return;

		const auto dims = CurrentDims(false);
		Streamline::GetSingleton()->SetDLSSGMode(false,
			dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight);
		if (auto* dxvk = DxvkInterop::GetSingleton())
			dxvk->WaitDeviceIdle();

		dlssgModeOn = false;
		logger::info("[FrameGen] DLSS-G interpolation off + device drained (leaving DLSS-G)");
	}

	// Keeps sl.dlss_g loaded exactly while DLSS-G is the desired method. A
	// dormant loaded plugin costs ~3% FPS (its pass-through present proxy) so
	// this is a real win, not hygiene. The (un)load itself happens inside
	// DXVK's swapchain recreate (guide section 18); exactly one recreate is
	// requested per state change.
	void Controller::StepDLSSGLoadState(Method a_target)
	{
		if (phase != Phase::kIdle)
			return;

		auto* sl = Streamline::GetSingleton();
		const bool wantLoaded = a_target == Method::kDLSSG;
		if (sl->IsDLSSGLoaded() == wantLoaded) {
			if (wantLoaded && owner != Method::kDLSSG) {
				// Already loaded (boot default) - adopt without a recreate, but
				// still register the viewport with interpolation OFF exactly like
				// the load-landed path: engaging straight to mode-on without the
				// registered-off -> on edge leaves the present proxy passive
				// (loaded, stable, but never doubling).
				const auto dims = CurrentDims(false);
				sl->SetDLSSGMode(false, dims.renderWidth, dims.renderHeight,
					dims.displayWidth, dims.displayHeight);
				owner = Method::kDLSSG;
				logger::info("[FrameGen] DLSS-G already loaded - registered + adopted as present owner");
			}
			return;
		}

		sl->SetDLSSGDesiredLoaded(wantLoaded);
		Streamline::RequestDxvkSwapchainRecreate(wantLoaded ? "DLSS-G load" : "DLSS-G unload");
		phase = wantLoaded ? Phase::kLoadingDLSSG : Phase::kUnloadingDLSSG;
		if (!wantLoaded && owner == Method::kDLSSG)
			owner = Method::kNone;
		logger::info("[FrameGen] DLSS-G {} requested (swapchain recreate, guide section 18)",
			wantLoaded ? "load" : "unload");
	}

	// Pushes the desired FSR-FG state to the sl.fsr plugin until it sticks (its
	// entry points come up a few frames after boot), and requests the swapchain
	// recreates the FFX wrap depends on (see the comments below). Enabling is
	// gated on sl.dlss_g being fully unloaded: DLSS-G's present proxy is
	// sticky, and the unload recreate is what evicts it.
	void Controller::StepFSRDelivery(Method a_target)
	{
		auto& upscaling = globals::features::upscaling;
		const bool wantFSR = a_target == Method::kFSR;

		if (wantFSR && (phase != Phase::kIdle || Streamline::GetSingleton()->IsDLSSGLoaded()))
			return;

		// While FSR-FG owns present, a vsync change needs a recreate of its own:
		// under the FFX wrap DXVK's dynamic present-mode switch (the
		// VK_EXT_swapchain_maintenance1 path a vsync toggle normally uses)
		// cannot reach the real swapchain, so the mode only changes when it is
		// baked in at swapchain creation. Deferred by one frame: the presenter
		// bakes the mode from the last-PRESENTED sync interval, and requesting
		// the recreate on the same frame the setting flips races that present
		// (a lost race bakes the old mode - observed in testing).
		if (wantFSR && fsrDelivered == 1 && upscaling.settings.vsync != fsrWrapVsync) {
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
		if ((wantFSR ? 1 : 0) == fsrDelivered && (!wantFSR || debugSig == fsrDebugSigDelivered))
			return;

		// A debug-flag re-push while already enabled applies per-present and must
		// not recreate; only an off->on edge needs the wrap recreate below.
		const bool enableEdge = wantFSR && fsrDelivered != 1;

		const bool hdr = globals::features::hdrDisplay.loaded &&
		                 globals::features::hdrDisplay.settings.enableHDR;
		const auto dims = CurrentDims(false);
		const auto& s = upscaling.settings;
		if (Streamline::GetSingleton()->SetFSRFrameGen(wantFSR,
				dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight, hdr,
				s.fgDebugView, s.fgDebugTearLines, s.fgDebugPacingLines, s.fgShowOnlyGenerated)) {
			fsrDelivered = wantFSR ? 1 : 0;
			fsrDebugSigDelivered = debugSig;
			if (wantFSR) {
				owner = Method::kFSR;
				fsrWrapVsync = s.vsync;
			} else if (owner == Method::kFSR) {
				owner = Method::kNone;
			}
			logger::info("[FrameGen] FSR-FG {} delivered - present owner: {}",
				wantFSR ? "enable" : "disable", Name(owner));

			// The FFX FrameInterpolationSwapChain installs only inside the sl.fsr
			// plugin's vkCreateSwapchainKHR hook, so a mid-session enable needs one
			// swapchain recreate to take effect. (The plugin's cooperative
			// present-hook bootstrap can no longer drive this: DXVK switches
			// present modes via VK_EXT_swapchain_maintenance1 without recreating,
			// so nothing recreates the swapchain organically anymore.) Disable
			// needs no recreate - the plugin unwraps from its own present hook.
			if (enableEdge)
				Streamline::RequestDxvkSwapchainRecreate("FSR-FG wrap");
		}
	}

	// Turns DLSS-G interpolation on for the current dynamic-resolution render
	// size. Gated on the load reconcile having settled so a toggle engages
	// exactly once, on the final swapchain (never on one about to be torn
	// down). SetDLSSGMode caches, so steady-state calls are no-ops.
	void Controller::EngageDLSSG()
	{
		auto* sl = Streamline::GetSingleton();
		if (phase != Phase::kIdle || !sl->IsDLSSGLoaded())
			return;

		auto& upscaling = globals::features::upscaling;
		const auto& s = upscaling.settings;

		// renderSize MUST be the actual DRS render size (the sub-rect where
		// depth/MV are valid), not the lock-inflated full size.
		const auto dims = CurrentDims(true);

		// "Dynamic" maps to eDynamic on hardware with Dynamic MFG (RTX 50+),
		// else eAuto. A fixed multiplier N means N-1 generated frames, clamped
		// to the hardware max inside SetDLSSGMode. targetFps 0 = auto-detect.
		const bool dynamic = s.dlssgDynamic;
		const bool useDynamic = dynamic && sl->IsDLSSGDynamicSupported();
		const bool useAuto = dynamic && !useDynamic;
		const uint32_t numFramesToGenerate = s.frameGenMultiplier > 1 ? s.frameGenMultiplier - 1 : 1;
		const float dynTargetFps = dynamic ? static_cast<float>(upscaling.GetTargetFrameRate()) : 0.0f;

		sl->SetDLSSGMode(true,
			dims.renderWidth, dims.renderHeight, dims.displayWidth, dims.displayHeight,
			numFramesToGenerate, useAuto, useDynamic, dynTargetFps);
		dlssgModeOn = true;
	}

}
