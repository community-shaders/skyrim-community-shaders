#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include <cstdint>
#include <limits>
#include <mutex>

#include <xell/xell_d3d12.h>
#include <xess_fg/xefg_swapchain_d3d12.h>

/**
 * @brief Dynamically loaded Intel XeSS Frame Generation and Xe Low Latency backend.
 *
 * The backend owns the XeSS-FG and XeLL contexts and the XeSS proxy swap chain. It deliberately
 * leaves GPU-idle waits and swap-chain buffer ownership to the caller, because those operations
 * must be coordinated with the application's D3D11/D3D12 interop fences.
 */
class IntelXeSSFrameGeneration final
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\XeSS";

	struct CreateInfo
	{
		ID3D12Device* device = nullptr;
		ID3D12CommandQueue* commandQueue = nullptr;
		IDXGIFactory2* factory = nullptr;
		HWND window = nullptr;
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc = nullptr;

		/** HUD-less and UI formats must both match the swap-chain format to enable UI composition. */
		DXGI_FORMAT hudlessFormat = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT uiFormat = DXGI_FORMAT_UNKNOWN;
		bool enableUiCompositionWhenCompatible = true;
		/**
		 * UI composition mode used when composition is enabled. NONE interpolates the UI as part of
		 * the back buffer (the SDK default) and disables composition entirely. Fixed at init.
		 */
		xefg_swapchain_ui_mode_t uiMode = XEFG_SWAPCHAIN_UI_MODE_HUDLESS_UITEXTURE;

		/** XeSS-FG initialization flags excluding external heap/descriptor flags. */
		uint32_t initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;
		uint32_t maxInterpolatedFrames = XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES;
	};

	struct FrameData
	{
		ID3D12CommandList* taggingCommandList = nullptr;
		ID3D12Resource* hudlessColor = nullptr;
		ID3D12Resource* uiTexture = nullptr;
		ID3D12Resource* depth = nullptr;
		ID3D12Resource* motionVectors = nullptr;

		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		uint32_t displayWidth = 0;
		uint32_t displayHeight = 0;

		/** Row-major, unjittered 4x4 matrices containing at least sixteen floats each. */
		const float* viewMatrix = nullptr;
		const float* projectionMatrix = nullptr;

		float jitterOffsetX = 0.0f;
		float jitterOffsetY = 0.0f;
		float motionVectorScaleX = 1.0f;
		float motionVectorScaleY = 1.0f;
		float frameRenderTimeMs = 0.0f;
		bool resetHistory = false;

		/**
		 * Resources tagged ONLY_NOW require taggingCommandList. Resources tagged UNTIL_NEXT_PRESENT
		 * must remain valid and in their declared incoming state through the matching Present call.
		 */
		xefg_swapchain_resource_validity_t resourceValidity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
		D3D12_RESOURCE_STATES hudlessState = D3D12_RESOURCE_STATE_COMMON;
		D3D12_RESOURCE_STATES uiState = D3D12_RESOURCE_STATE_COMMON;
		D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_COMMON;
		D3D12_RESOURCE_STATES motionVectorState = D3D12_RESOURCE_STATE_COMMON;
	};

	IntelXeSSFrameGeneration() = default;
	~IntelXeSSFrameGeneration();

	IntelXeSSFrameGeneration(const IntelXeSSFrameGeneration&) = delete;
	IntelXeSSFrameGeneration& operator=(const IntelXeSSFrameGeneration&) = delete;

	/** Loads both SDK DLLs exclusively from Data\Shaders\Upscaling\XeSS. */
	bool Load();
	/** Returns whether both DLLs and every required export are loaded. */
	bool IsAvailable() const;
	bool IsInitialized() const;

	bool CreateContextAndSwapChain(const CreateInfo& info);
	bool CreateContextAndSwapChain(ID3D12Device* device,
		ID3D12CommandQueue* commandQueue,
		IDXGIFactory2* factory,
		HWND window,
		const DXGI_SWAP_CHAIN_DESC1& swapChainDesc,
		DXGI_FORMAT hudlessFormat = DXGI_FORMAT_UNKNOWN,
		DXGI_FORMAT uiFormat = DXGI_FORMAT_UNKNOWN);

	/** Returns a borrowed pointer. The caller must release all of its references before Shutdown. */
	IDXGISwapChain4* GetSwapChain() const;

	/**
	 * Enables or disables XeSS-FG and its mandatory XeLL mode.
	 * The caller must ensure all relevant GPU work is idle before changing this state.
	 */
	bool SetEnabled(bool enabled, uint32_t minimumIntervalUs = 0);

	/**
	 * @brief Sets how many frames XeSS-FG interpolates between two rendered frames.
	 *
	 * Clamped to the maximum the proxy swap chain was initialized with. Changing this may
	 * trigger shader compilation and internal reallocation, so calls that do not change the
	 * value are dropped. Returns false when the loaded SDK predates multi-frame generation.
	 */
	bool SetNumInterpolatedFrames(uint32_t interpolatedFrames);

	/** Maximum interpolated frames this adapter supports, or 0 before initialization. */
	uint32_t GetMaxInterpolatedFrames() const;

	/** Interpolated frames currently configured. */
	uint32_t GetNumInterpolatedFrames() const;

	/** Whether the loaded SDK exposes runtime multi-frame generation. */
	bool SupportsMultiFrameGeneration() const;

	/** UI mode the proxy swap chain was initialized with, as the raw SDK value; NONE before init. */
	uint32_t GetUiMode() const;

	/**
	 * True when SetEnabled(enabled, minimumIntervalUs) would call xellSetSleepMode. The XeLL guide
	 * requires all GPU activity to be finished before that call, so callers drain the GPU first.
	 */
	bool SleepModeWouldChange(bool enabled, uint32_t minimumIntervalUs) const;

	/** Called before input sampling. Repeated calls during the same globals frame are ignored. */
	bool BeginFrame();
	/**
	 * Called when command recording for the frame starts. Closes the XeLL simulation phase and
	 * opens render submit so XeLL sees real phase timings instead of one long simulation phase.
	 */
	bool BeginRenderSubmit();
	/** Explicit frame-sequence overload, useful for callers that already own a stable frame counter. */
	bool BeginFrame(uint64_t globalsFrame);

	/** Tags XeSS-FG resources/constants. Any ONLY_NOW tagging command list must be submitted before Present. */
	bool TagFrame(const FrameData& frame);
	/** Emits render-submit/present markers and assigns the XeSS-FG present ID. */
	bool BeforePresent();
	/** Emits the present-end marker and captures the last XeSS-FG presentation status. */
	bool AfterPresent(HRESULT presentResult = S_OK);

	xefg_swapchain_present_status_t GetLastPresentStatus() const;
	/** Whether xefgSwapChainSetEnabled(1) is currently in effect (independent of per-frame success). */
	bool IsEnabled() const;
	bool IsActive() const;
	bool UsesUiComposition() const;

	/** Internal heaps require no resize staging; this updates validation state and resets interpolation history. */
	bool OnResize(uint32_t width, uint32_t height, DXGI_FORMAT format);

	/**
	 * Releases the proxy before XeSS-FG, then destroys XeLL and unloads both DLLs.
	 * Returns false when an external proxy-swap-chain reference prevents safe destruction; callers may retry.
	 */
	bool Shutdown();

private:
	// decltype keeps the pinned SDK headers as the single source of truth for these prototypes,
	// so a future SDK bump that changes one fails to compile instead of corrupting the stack at
	// runtime. Matches the pattern already used in IntelXeSSD3D12.h.
	struct XeFGFunctions
	{
		decltype(&xefgSwapChainGetVersion) getVersion = nullptr;
		decltype(&xefgSwapChainGetProperties) getProperties = nullptr;
		decltype(&xefgSwapChainTagFrameConstants) tagFrameConstants = nullptr;
		decltype(&xefgSwapChainSetEnabled) setEnabled = nullptr;
		decltype(&xefgSwapChainSetPresentId) setPresentId = nullptr;
		decltype(&xefgSwapChainGetLastPresentStatus) getLastPresentStatus = nullptr;
		decltype(&xefgSwapChainSetLoggingCallback) setLoggingCallback = nullptr;
		decltype(&xefgSwapChainDestroy) destroy = nullptr;
		/** The pinned SDK deliberately types the second parameter as void*. */
		decltype(&xefgSwapChainSetLatencyReduction) setLatencyReduction = nullptr;
		decltype(&xefgSwapChainSetUiCompositionState) setUiCompositionState = nullptr;
		/** Optional: absent in SDKs older than the multi-frame-generation release. */
		decltype(&xefgSwapChainSetNumInterpolatedFrames) setNumInterpolatedFrames = nullptr;
		decltype(&xefgSwapChainD3D12CreateContext) d3d12CreateContext = nullptr;
		decltype(&xefgSwapChainD3D12GetProperties) d3d12GetProperties = nullptr;
		decltype(&xefgSwapChainD3D12InitFromSwapChainDesc) d3d12InitFromSwapChainDesc = nullptr;
		decltype(&xefgSwapChainD3D12GetSwapChainPtr) d3d12GetSwapChainPtr = nullptr;
		decltype(&xefgSwapChainD3D12TagFrameResource) d3d12TagFrameResource = nullptr;
	};

	struct XeLLFunctions
	{
		decltype(&xellGetVersion) getVersion = nullptr;
		decltype(&xellD3D12CreateContext) d3d12CreateContext = nullptr;
		decltype(&xellDestroyContext) destroy = nullptr;
		decltype(&xellSetSleepMode) setSleepMode = nullptr;
		decltype(&xellSleep) sleep = nullptr;
		decltype(&xellAddMarkerData) addMarkerData = nullptr;
		decltype(&xellSetLoggingCallback) setLoggingCallback = nullptr;
	};

	enum class FrameStage : uint8_t
	{
		Idle,
		Simulation,
		RenderSubmit,
		Present
	};

	static void XeFGLogCallback(const char* message, xefg_swapchain_logging_level_t level, void* userData);
	static void XeLLLogCallback(const char* message, xell_logging_level_t level);

	bool ResolveExportsUnlocked();
	void UnloadModulesUnlocked();
	bool DestroyContextsUnlocked(bool unloadModules);
	/**
	 * @brief Opens the XeLL marker sequence for a new game frame.
	 * @param a_lock Lock held on mutex_; released around the blocking xellSleep and re-acquired
	 *        before the marker sequence, so the presenting thread is not stalled by the frame cap.
	 */
	bool StartFrameUnlocked(std::unique_lock<std::mutex>& a_lock, uint64_t globalsFrame);
	bool CompleteOpenFrameUnlocked();
	bool AddMarkerUnlocked(xell_latency_marker_type_t marker);
	bool UpdateUiCompositionUnlocked(bool enabled);
	bool TagResourceUnlocked(ID3D12CommandList* commandList,
		xefg_swapchain_resource_type_t type,
		xefg_swapchain_resource_validity_t validity,
		ID3D12Resource* resource,
		uint32_t width,
		uint32_t height,
		D3D12_RESOURCE_STATES incomingState);

	mutable std::mutex mutex_;
	HMODULE xefgModule_ = nullptr;
	HMODULE xellModule_ = nullptr;
	XeFGFunctions xefg_{};
	XeLLFunctions xell_{};

	xefg_swapchain_handle_t xefgContext_ = nullptr;
	xell_context_handle_t xellContext_ = nullptr;
	winrt::com_ptr<ID3D12Device> device_;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue_;
	winrt::com_ptr<IDXGISwapChain4> swapChain_;

	DXGI_FORMAT swapChainFormat_ = DXGI_FORMAT_UNKNOWN;
	uint32_t displayWidth_ = 0;
	uint32_t displayHeight_ = 0;
	uint32_t maxInterpolatedFrames_ = 0;
	uint32_t numInterpolatedFrames_ = 1;
	xefg_swapchain_ui_mode_t uiMode_ = XEFG_SWAPCHAIN_UI_MODE_NONE;
	xefg_swapchain_properties_t properties_{};
	xefg_swapchain_present_status_t lastPresentStatus_{};

	bool apiAvailable_ = false;
	bool initialized_ = false;
	bool enabled_ = false;
	bool xellLowLatencyEnabled_ = false;
	bool active_ = false;
	bool uiCompositionSupported_ = false;
	bool uiCompositionEnabled_ = false;
	bool forceHistoryReset_ = true;

	FrameStage frameStage_ = FrameStage::Idle;
	uint64_t lastGlobalsFrame_ = std::numeric_limits<uint64_t>::max();
	uint64_t fallbackGlobalsFrame_ = 0;
	uint32_t nextPresentId_ = 1;
	uint32_t currentPresentId_ = 0;
	bool taggedThisFrame_ = false;
	bool inputSampleSent_ = false;
	bool warnedMissingBeginFrame_ = false;
	bool warnedIncompleteFrame_ = false;
	bool warnedUntaggedPresent_ = false;
	uint32_t minimumIntervalUs_ = 0;
	int32_t lastLoggedFrameGenResult_ = XEFG_SWAPCHAIN_RESULT_SUCCESS;
};
