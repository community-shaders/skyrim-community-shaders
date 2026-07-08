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
// The transition rules encode constraints that were each established
// empirically (see git history / the Streamline DLSS-G programming guide):
//
//  * sl.dlss_g is LOADED only while DLSS-G is the selected method. A dormant
//    loaded plugin costs ~3% FPS uncapped (its pass-through present proxy
//    taxes every present) plus 18 MB of VRAM, so unloading when off is a real
//    win. Loading/unloading may only happen inside DXVK's swapchain-recreate
//    window (guide section 18), so each load-state change requests exactly one
//    recreate and waits for it to land.
//  * DLSS-G interpolation must be turned OFF and the device drained BEFORE any
//    swapchain manipulation (guide ordering requirement).
//  * The first slDLSSGSetOptions(on) is deferred until the load recreate has
//    landed, so a toggle engages FG exactly once on the FINAL swapchain
//    instead of engage -> teardown -> re-engage (a visible bounce).
//  * DLSS-G's present proxy is sticky: it bypasses the Vulkan present hooks,
//    so FSR-FG can never cooperatively reclaim presentation from it. FSR-FG
//    enablement is therefore gated on sl.dlss_g being fully unloaded — the
//    unload recreate doubles as the proxy eviction.
//  * FSR-FG enablement can take several frames to deliver (the sl.fsr entry
//    points come up after the first frames), so the desired state is re-pushed
//    until the plugin accepts it. Each delivered off->on edge then requests one
//    swapchain recreate: the FFX FrameInterpolationSwapChain installs only
//    inside the plugin's vkCreateSwapchainKHR hook, and nothing else recreates
//    the swapchain organically anymore (DXVK switches present modes without a
//    recreate via VK_EXT_swapchain_maintenance1). Disable needs no recreate -
//    the plugin unwraps from its own present hook.
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
			kIdle,            // load state matches the target; nothing in flight
			kLoadingDLSSG,    // load recreate requested; waiting for it to land
			kUnloadingDLSSG,  // mode off + drained; unload recreate requested
		};

		// Sub-steps of Reconcile, in the order they run.
		void StepPhaseCompletion();
		void StepDLSSGModeTeardown(Method a_target);
		void StepDLSSGLoadState(Method a_target);
		void StepFSRDelivery(Method a_target);

		static const char* Name(Method a_method);

		Phase phase = Phase::kIdle;
		Method owner = Method::kNone;

		// DLSS-G interpolation mode was issued ON (needs off + device drain
		// before any swapchain manipulation when leaving the method).
		bool dlssgModeOn = false;


		// Last FSR-FG enable state actually accepted by the sl.fsr plugin
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
