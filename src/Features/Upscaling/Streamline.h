#pragma once

#include <cstdint>
#include <d3d11.h>

// Streamline and community upscalers run on DXVK's Vulkan device through full interposition.

class Streamline
{
public:
	static Streamline* GetSingleton();

	/** @brief Maps the interposer before DXVK creates its Vulkan instance. */
	void PreloadInterposer();

	/** @brief Initializes Streamline's Vulkan backend. */
	bool Initialize();

	/** @brief Resolves feature support after the DXVK device is available. */
	void SetVulkanDevice();

	/** @brief Returns whether feature support is final for this session. */
	[[nodiscard]] bool IsFeatureSupportResolved() const { return vulkanDeviceSet || unavailable; }
	/** @brief Whether a guarded Streamline call faulted and frame generation must be torn down. */

	/** @brief Disables interposition when no Streamline feature is configured. */
	void MarkUnavailable() { unavailable = true; }
	[[nodiscard]] bool IsUnavailable() const { return unavailable; }
	[[nodiscard]] bool IsDLSSSupported() const { return featureDLSS; }
	[[nodiscard]] bool IsReflexSupported() const { return featureReflex; }
	[[nodiscard]] bool IsDLSSGSupported() const { return featureDLSSG; }
	[[nodiscard]] bool IsXeSSSupported() const { return featureXeSS; }
	/** @brief True when XeSS would use its XMX path (Intel Arc) rather than the softer DP4a fallback. */
	[[nodiscard]] bool IsXeSSHardwareAccelerated() const { return featureXeSS && isIntelGPU; }
	[[nodiscard]] bool IsFSRSupported() const { return featureFSR; }
	[[nodiscard]] bool IsFSRFGSupported() const { return featureFSRFG; }

	/** @brief Outcome of one regular upscaler evaluation. */
	enum class EvaluationResult : uint8_t
	{
		kReady,
		kSkipped,
		kFailed,
	};

	[[nodiscard]] EvaluationResult EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode,
		float a_jitterX, float a_jitterY);

	[[nodiscard]] EvaluationResult EvaluateXeSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness,
		float a_jitterX, float a_jitterY);

	[[nodiscard]] EvaluationResult EvaluateFSR(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness,
		float a_jitterX, float a_jitterY);

	/** @brief Prepares FSR frame generation independently of the active upscaler. */
	[[nodiscard]] bool EvaluateFSRFrameGen(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_hudlessColor,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		float a_jitterX, float a_jitterY);

	[[nodiscard]] bool SetFSRFrameGen(bool a_enable, bool a_hdr,
		bool a_debugView = false, bool a_debugTearLines = false, bool a_debugPacingLines = false,
		bool a_onlyPresentGenerated = false);

	void CaptureFSRFrameGenState();

	/** @brief Updates Reflex and its optional frame-limit interval in microseconds. */
	void UpdateReflex(bool a_enable, bool a_boost, uint32_t a_frameLimitUs = 0);

	enum class PclMarker : uint32_t
	{
		SimulationStart = 0,
		SimulationEnd = 1,
		RenderSubmitStart = 2,
		RenderSubmitEnd = 3,
		PresentStart = 4,
		PresentEnd = 5,
		TriggerFlash = 7,
		PCLatencyPing = 8,
	};

	void SetPCLMarker(PclMarker a_marker);

	/** @brief Updates DLSS-G mode and generated-frame count. */
	bool SetDLSSGMode(bool a_enable, uint32_t a_displayWidth, uint32_t a_displayHeight,
		uint32_t a_numFramesToGenerate = 1, bool a_autoMode = false, bool a_dynamic = false,
		float a_dynamicTargetFps = 0.0f);

	/** @brief Establishes the Streamline frame ID at render-frame start. */
	void BeginRenderFrame();
	/** @brief Discards any prepared FSR frame that did not reach Present. */
	[[nodiscard]] bool DiscardFSRFrameGenerationPreparedFrame();

	/** @brief Queries and caches DLSS-G capabilities on the present thread. */
	void QueryDLSSGCapabilities();

	/** @brief Updates DLSS-G state after the real present completes. */
	void CaptureDLSSGPresentState();
	[[nodiscard]] uint32_t GetDLSSGMaxFramesToGenerate() const;

	/** @brief Returns the latest number of frames presented per rendered frame. */
	[[nodiscard]] uint32_t GetFrameGenerationMultiplier() const;
	/** @brief Running total of frames the FSR-FG swapchain has presented; difference for the true rate. */
	[[nodiscard]] uint64_t GetTotalPresentedFrames() const;
	[[nodiscard]] bool IsDLSSGDynamicSupported() const;

	/** @brief Sets the desired DLSS-G runtime load state. */
	void SetDLSSGDesiredLoaded(bool a_loaded);
	[[nodiscard]] bool IsDLSSGLoaded() const;
	[[nodiscard]] bool IsDLSSGLoadSettled() const;

	/** @brief Sets the desired FSR frame-generation runtime load state. */
	void SetFSRFGDesiredLoaded(bool a_loaded);
	[[nodiscard]] bool IsFSRFGLoaded() const;
	[[nodiscard]] bool IsFSRFGLoadSettled() const;
	[[nodiscard]] bool IsFSRFGPresentOwner() const;

	void TagDLSSGResources(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_displayWidth, uint32_t a_displayHeight);

	void ClearDLSSGTags();
	[[nodiscard]] bool EnsureDLSSGPresentTag();

	/** @brief Registers Streamline ownership of DXVK present pacing. */
	static void RegisterDxvkOwnershipPredicate();

	/** @brief Requests a Vulkan swapchain recreation. */
	static void RequestDxvkSwapchainRecreate(const char* a_reason = "FG method switch");

	/** @brief Enables synchronous present while a frame-generation proxy is active. */
	static void PushDxvkSyncPresent(bool a_sync);
	/** @brief Sets a bounded present overlap for a settled frame-generation proxy. */
	static void PushDxvkPresentQueueDepth(uint32_t a_depth);
	/** @brief 0 = tear-free (MAILBOX), 1 = tearing (IMMEDIATE); set before a swapchain recreate. */
	static void PushDxvkTearingPreference(uint32_t a_preference);

private:
	Streamline() = default;

	bool triedInit = false;
	bool initialized = false;
	bool vulkanDeviceSet = false;
	// Set when Streamline genuinely cannot be brought up (no plugin dir, missing sl.interposer.dll,
	// unresolvable exports, slInit failure). Never a configuration choice: the interposer is always
	// preloaded so every upscaler and frame generation can be switched on in-game without a restart.
	bool unavailable = false;

	bool featureDLSS = false;
	bool featureReflex = false;
	bool featureDLSSG = false;
	bool featureXeSS = false;
	bool featureFSR = false;
	bool featureFSRFG = false;

	// DLSS-G requires VK_NV_optical_flow before Streamline initialization.
	bool dlssgHardware = false;

	bool isNvidiaGPU = false;
	bool isIntelGPU = false;
	bool isRTXBelow40Series = false;
};
