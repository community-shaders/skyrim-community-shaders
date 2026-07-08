#pragma once

#include <cstdint>
#include <d3d11.h>

// NVIDIA Streamline (DLSS / Reflex / DLSS-G) + community plugins (FSR / XeSS) on
// DXVK's Vulkan device via full interposition (sl.interposer.dll IS DXVK's vulkan-1.dll).
//
// SL types are intentionally kept out of this header (sl.h pulls in a large surface);
// all Streamline state lives in Streamline.cpp behind an opaque impl.

class Streamline
{
public:
	static Streamline* GetSingleton();

	/** @brief Map sl.interposer.dll before DXVK creates its VkInstance, so DXVK's loader
	 *  aliases it and routes its entire Vulkan surface through Streamline. Call from
	 *  Upscaling::Load (plugin load), before any DXGI call. Cheap + idempotent. */
	void PreloadInterposer();

	/** @brief Runs slInit on the Vulkan backend. Idempotent.
	 *  @return true if Streamline initialized. */
	bool Initialize();

	/** @brief Probes per-adapter feature support and resolves feature-specific entry points.
	 *  Must be called after the D3D11/DXVK device exists and DxvkInterop is up. */
	void SetVulkanDevice();

	/** @brief slShutdown + frees the interposer. Safe to call when not initialized. */
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return initialized; }
	// The per-adapter feature probe has run (SetVulkanDevice). Until then the
	// Is*Supported() flags read false, so method fallbacks (e.g. DLSS-G -> FSR
	// when unsupported) give transient wrong answers during early boot.
	[[nodiscard]] bool IsFeatureSupportResolved() const { return vulkanDeviceSet; }
	[[nodiscard]] bool IsDLSSSupported() const { return featureDLSS; }
	[[nodiscard]] bool IsReflexSupported() const { return featureReflex; }
	[[nodiscard]] bool IsDLSSGSupported() const { return featureDLSSG; }
	[[nodiscard]] bool IsXeSSSupported() const { return featureXeSS; }
	[[nodiscard]] bool IsFSRSupported() const { return featureFSR; }

	void EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness,
		float a_jitterX, float a_jitterY);

	void EvaluateXeSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness,
		float a_jitterX, float a_jitterY);

	void EvaluateFSR(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness,
		float a_jitterX, float a_jitterY);

	// Standalone FSR frame-generation prepare: tags ONLY depth + motion vectors (no color) and drives the
	// sl.fsr plugin's FG-prepare via slEvaluateFeature(kFeatureFSR). Decoupled from the upscaler, so FSR FG
	// works under any upscale method. Call every gameplay frame while FSR FG is the active method.
	void EvaluateFSRFrameGen(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_hudlessColor,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		float a_jitterX, float a_jitterY);

	[[nodiscard]] bool SetFSRFrameGen(bool a_enable, uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_displayWidth, uint32_t a_displayHeight, bool a_hdr,
		bool a_debugView = false, bool a_debugTearLines = false, bool a_debugPacingLines = false,
		bool a_onlyPresentGenerated = false);

	// Throttled log of the sl.fsr plugin's frame-generation state (framesPresented 2 = doubling
	// active, 1 = not wrapped/pass-through). Call per gameplay frame while FSR-FG is selected.
	void LogFSRFrameGenStats();

	// a_frameLimitUs: Reflex frame-limiter interval in microseconds (0 = no limit). Only takes effect while
	// Reflex is on; the caller uses DXVK's limiter instead when Reflex is off.
	void UpdateReflex(bool a_enable, bool a_boost, uint32_t a_frameLimitUs = 0);

	enum class PclMarker : uint32_t
	{
		SimulationStart = 0,
		SimulationEnd = 1,
		RenderSubmitStart = 2,
		RenderSubmitEnd = 3,
		PresentStart = 4,
		PresentEnd = 5,
		TriggerFlash = 7,    // sl::PCLMarker::eTriggerFlash — sample fires once/frame
		PCLatencyPing = 8,   // sl::PCLMarker::ePCLatencyPing — sample fires once/frame
	};

	void SetPCLMarker(PclMarker a_marker);

	// a_numFramesToGenerate: 1 = 2x (single frame), 2 = 3x …; clamped to the hardware max.
	// a_autoMode: DLSS-G eAuto (fixed multiplier, but the driver auto-disables FG when it would lower FPS).
	// a_dynamic: DLSS-G eDynamic (Dynamic Multi Frame Generation) — overrides a_autoMode; a_dynamicTargetFps
	//   is the desired output fps (0 => auto-detect the monitor refresh). Use only when IsDLSSGDynamicSupported().
	// Takes display dims only — DLSS-G options never carry render dims (Streamline_Sample fixed-res
	// behavior; the per-frame tag extents describe the render sub-rect), so upscaler quality changes
	// are invisible to DLSS-G.
	void SetDLSSGMode(bool a_enable, uint32_t a_displayWidth, uint32_t a_displayHeight,
		uint32_t a_numFramesToGenerate = 1, bool a_autoMode = false, bool a_dynamic = false,
		float a_dynamicTargetFps = 0.0f);

	// Render thread, once per frame at frame start (Main_UpdateJitter hook): establishes the
	// explicit SL frame ID all render-thread SL calls this frame fetch their token with — the
	// Streamline_Sample's engine-frame-counter pattern (no shared token, no cross-thread latch).
	void BeginRenderFrame();

	// Whether the DXVK present-marker bridge is active (PresentStart/End fire on DXVK's submit
	// thread around the real vkQueuePresentKHR). When true, the present hook must call
	// NotifyPresentQueued() instead of firing PresentStart/PresentEnd itself.
	[[nodiscard]] bool PresentMarkersBridged() const;
	// Render thread, at the D3D11 Present call: queue this frame's id for the bridged markers.
	void NotifyPresentQueued();

	// Query DLSS-G capabilities (numFramesToGenerateMax, Dynamic MFG support) and cache them. MUST run on the
	// present thread (slDLSSGGetState requirement); CS calls this from its present hook. Idempotent once cached.
	void QueryDLSSGCapabilities();

	// eBlockNoClientQueues input synchronization (DLSS-G guide §15.1). Under eBlockNoClientQueues the DLSS-G
	// plugin consumes the eValidUntilPresent inputs (motion vectors, HUDless) on a non-presenting queue AFTER
	// present, so the client must not overwrite those live engine targets until the plugin signals it is done.
	//   * CaptureDLSSGInputFence() — present thread, after the present: reads the plugin-internal completion
	//     fence (a Vulkan timeline semaphore) + its last-present value via slDLSSGGetState and stores them.
	//   * WaitDLSSGInputFence()   — render thread, frame start (before the frame overwrites those inputs):
	//     host-waits (vkWaitSemaphores) for the stored value. Bounded timeout; never hangs the render thread.
	// Depth is eOnlyValidNow (SL snapshots it at tag time) and needs no wait.
	void CaptureDLSSGInputFence();
	void WaitDLSSGInputFence();
	// Max numFramesToGenerate the hardware supports (0 = not yet queried). Max multiplier = this + 1.
	[[nodiscard]] uint32_t GetDLSSGMaxFramesToGenerate() const;
	// Whether DLSS-G Dynamic Multi Frame Generation (eDynamic) is supported (50-series + driver + D3D12).
	[[nodiscard]] bool IsDLSSGDynamicSupported() const;

	// DLSS-G runtime (un)load (Streamline DLSS-G guide §18): set the desired loaded state, then call
	// RequestDxvkSwapchainRecreate() — DXVK's torn-down callback applies slSetFeatureLoaded in the
	// no-swapchain window so the next create installs/omits DLSS-G's proxy. Unloaded => no overhead when off.
	void SetDLSSGDesiredLoaded(bool a_loaded);
	[[nodiscard]] bool IsDLSSGLoaded() const;
	// Desired load state has been applied (no load/unload recreate outstanding).
	[[nodiscard]] bool IsDLSSGLoadSettled() const;

	void LogReflexStatus();

	void TagDLSSGResources(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_displayWidth, uint32_t a_displayHeight);

	void ClearDLSSGTags();
	void EnsureDLSSGPresentTag();

	// Register the DXVK frame-gen ownership predicate so DXVK treats all swapchains as
	// externally paced under interposition (skips present-fence, present-wait worker).
	static void RegisterDxvkOwnershipPredicate();

	// Force DXVK to recreate its Vulkan swapchain on the next acquire. The teardown window is where
	// sl.dlss_g gets (un)loaded and how its sticky present proxy is evicted; a_reason is logged.
	static void RequestDxvkSwapchainRecreate(const char* a_reason = "FG method switch");

private:
	Streamline() = default;

	bool triedInit = false;
	bool initialized = false;
	bool vulkanDeviceSet = false;

	bool featureDLSS = false;
	bool featureReflex = false;
	bool featureDLSSG = false;
	bool featureXeSS = false;
	bool featureFSR = false;

	// Pre-slInit hardware capability (VK_NV_optical_flow on the system loader): decides
	// whether sl.dlss_g is loaded at all — and with it, the session's frame-generation
	// method and present path. See ProbeDLSSGHardware in Streamline.cpp.
	bool dlssgHardware = false;

	bool isNvidiaGPU = false;
	bool isRTXBelow40Series = false;
};
