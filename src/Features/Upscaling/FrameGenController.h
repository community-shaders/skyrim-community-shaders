#pragma once

#include <cstdint>

// Frame-generation present-ownership controller.
//
// Exactly one frame-generation method may own presentation at a time: DLSS-G
// wraps the swapchain with Streamline's sticky present proxy, FSR-FG wraps it
// with FFX's FrameInterpolationSwapChain. This controller is the single
// authority for which method that is. Everything else (menu, settings,
// per-frame hooks) only expresses a *desired* method; all Streamline/FFX
// frame-generation mutations and all FG-related swapchain recreates are issued
// from here, serialized as an explicit state machine.
//
// DLSS-G (sl.dlss_g / kFeatureDLSS_G) and FSR-FG (sl.fsr_g / kFeatureFSR_G) are
// TWIN load-toggled Streamline features: each is loaded only while it is the
// selected method, and at most one is loaded at a time. Both start LOADED at
// slInit, so the controller's first reconcile unloads the non-selected one.
// A method switch (DLSS-G <-> FSR-FG, or either <-> off) unloads the outgoing
// feature and loads the incoming one inside a SINGLE DXVK swapchain-recreate
// window: the torn-down callback applies BOTH desired load states while no
// swapchain exists, so one recreate performs both transitions at once.
//
// The transition rules encode constraints that were each established
// empirically (see git history / the Streamline DLSS-G programming guide):
//
//  * Each FG feature is LOADED only while it is the selected method. A dormant
//    loaded plugin costs FPS (its pass-through present proxy taxes every
//    present) plus VRAM, so unloading when off is a real win. Loading/unloading
//    may only happen inside DXVK's swapchain-recreate window (guide section
//    18), so a method switch requests exactly one recreate and waits for BOTH
//    features' load states to settle before declaring the transition done.
//  * The OUTGOING engaged method is disengaged BEFORE the load change so the
//    unload is clean: DLSS-G interpolation is turned OFF and the device drained
//    (guide ordering requirement); FSR-FG's FFX FrameInterpolationSwapChain is
//    unwrapped from the plugin's present hook.
//  * The first slDLSSGSetOptions(on) is deferred until the load recreate has
//    landed, so a toggle engages FG exactly once on the FINAL swapchain
//    instead of engage -> teardown -> re-engage (a visible bounce).
//  * FSR-FG enablement can take several frames to deliver (the sl.fsr_g entry
//    points come up after the first frames), so the desired state is re-pushed
//    until the plugin accepts it. Each delivered off->on edge then requests one
//    swapchain recreate: the FFX FrameInterpolationSwapChain installs only
//    inside the plugin's vkCreateSwapchainKHR hook, and nothing else recreates
//    the swapchain organically anymore (DXVK switches present modes without a
//    recreate via VK_EXT_swapchain_maintenance1). Disable is handled in the
//    method-teardown step (it unwraps from the plugin's own present hook ahead
//    of the unload recreate).
//
// Threading: all methods run on the render thread (the immediate-context
// cadence of Upscaling's hooks), like the code they replaced.
namespace FrameGen
{
	enum class Method : uint8_t
	{
		kNone,
		kFSR,
		kDLSSG,
	};

	class Controller
	{
	public:
		static Controller* GetSingleton()
		{
			static Controller singleton;
			return &singleton;
		}

		// Drives the state machine toward the method selected in Upscaling's
		// settings. Call once per frame from the render thread; cheap when
		// settled. Reads settings/dimensions from the Upscaling singleton.
		void Reconcile();

		// Turns DLSS-G interpolation on for the current frame dimensions.
		// Call from the per-frame gameplay path while DLSS-G is the desired
		// method; no-ops (and stays off) until the load reconcile has settled
		// on the final swapchain, so a toggle engages exactly once.
		void EngageDLSSG();

	private:
		Controller() = default;

		enum class Phase : uint8_t
		{
			kIdle,          // load state matches the target; nothing in flight
			kTransitioning  // a load/unload swapchain recreate is outstanding for
			                // either FG feature; waiting for both to settle
		};

		// Sub-steps of Reconcile, in the order they run.
		void StepPhaseCompletion();
		void StepModeTeardown(Method a_target);
		void StepLoadState(Method a_target);
		void StepFSRDelivery(Method a_target);

		static const char* Name(Method a_method);

		Phase phase = Phase::kIdle;
		Method owner = Method::kNone;

		// DLSS-G interpolation mode was issued ON (needs off + device drain
		// before any swapchain manipulation when leaving the method).
		bool dlssgModeOn = false;


		// Last FSR-FG enable state actually accepted by the sl.fsr_g plugin
		// (-1 = nothing delivered yet), and the debug-flag signature it was
		// delivered with. Re-pushed until they match the desired state.
		int fsrDelivered = -1;
		uint32_t fsrDebugSigDelivered = 0;
		// vsync state baked into the current FFX wrap's swapchain (valid while
		// fsrDelivered == 1). A change requests a recreate: the wrapped
		// swapchain cannot follow DXVK's dynamic present-mode switch.
		bool fsrWrapVsync = false;
		// One-frame defer latch for that recreate (a present must carry the new
		// sync interval before the recreate bakes the mode from it).
		bool fsrVsyncRebakePending = false;
	};
}
