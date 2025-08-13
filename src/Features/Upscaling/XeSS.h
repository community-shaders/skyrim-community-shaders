#pragma once

#include "../../Buffer.h"
#include "DX12SwapChain.h"
#include "../../State.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <winrt/base.h>

// Include XeSS headers
#include <xess/xess.h>
#include <xess/xess_d3d12.h>

// Include XeSS Frame Generation headers
#include <xess_fg/xefg_swapchain.h>
#include <xess_fg/xefg_swapchain_d3d12.h>

// XeSS function pointers - matching exact signatures from xess.h and xess_d3d12.h
typedef xess_result_t (*xessGetVersionPtr)(xess_version_t* pVersion);
typedef xess_result_t (*xessD3D12CreateContextPtr)(ID3D12Device* pDevice, xess_context_handle_t* phContext);
typedef xess_result_t (*xessD3D12InitPtr)(xess_context_handle_t hContext, const xess_d3d12_init_params_t* pInitParams);
typedef xess_result_t (*xessD3D12ExecutePtr)(xess_context_handle_t hContext, ID3D12GraphicsCommandList* pCommandList, const xess_d3d12_execute_params_t* pExecParams);
typedef xess_result_t (*xessDestroyContextPtr)(xess_context_handle_t hContext);
typedef xess_result_t (*xessSetJitterScalePtr)(xess_context_handle_t hContext, float x, float y);
typedef xess_result_t (*xessSetVelocityScalePtr)(xess_context_handle_t hContext, float x, float y);
typedef xess_result_t (*xessGetInputResolutionPtr)(xess_context_handle_t hContext, const xess_2d_t* pOutputResolution, xess_quality_settings_t qualitySettings, xess_2d_t* pInputResolution);

typedef xefg_swapchain_result_t (*xefgSwapChainD3D12CreateContextPtr)(ID3D12Device* pDevice, xefg_swapchain_handle_t* phSwapChain);
typedef xefg_swapchain_result_t (*xefgSwapChainD3D12InitFromSwapChainPtr)(xefg_swapchain_handle_t hSwapChain, ID3D12CommandQueue* pCmdQueue, const xefg_swapchain_d3d12_init_params_t* pInitParams);
typedef xefg_swapchain_result_t (*xefgSwapChainD3D12InitFromSwapChainDescPtr)(xefg_swapchain_handle_t hSwapChain, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pSwapChainDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, ID3D12CommandQueue* pCmdQueue, IDXGIFactory* pFactory, const xefg_swapchain_d3d12_init_params_t* pInitParams);
typedef xefg_swapchain_result_t (*xefgSwapChainSetLatencyReductionPtr)(xefg_swapchain_handle_t hSwapChain, void* hXeLLContext);

// XeLL (XeSS Low Latency) function pointers
typedef void* xell_context_handle_t;
typedef enum { XELL_RESULT_SUCCESS = 0 } xell_result_t;
typedef enum _xell_latency_marker_type_t
{
	XELL_SIMULATION_START = 0,    // required
	XELL_SIMULATION_END = 1,      // required
	XELL_RENDERSUBMIT_START = 2,  // required
	XELL_RENDERSUBMIT_END = 3,    // required
	XELL_PRESENT_START = 4,       // required
	XELL_PRESENT_END = 5,         // required
	XELL_INPUT_SAMPLE = 6,

	XELL_MARKER_COUNT = 7
} xell_latency_marker_type_t;

typedef xell_result_t (*xellD3D12CreateContextPtr)(ID3D12Device* pDevice, xell_context_handle_t* phContext);
typedef xell_result_t (*xellDestroyContextPtr)(xell_context_handle_t hContext);
typedef xell_result_t (*xellAddMarkerDataPtr)(xell_context_handle_t hContext, uint32_t frameId, _xell_latency_marker_type_t markerType);

// XeSS FG logging callback
typedef enum { XEFG_SWAPCHAIN_LOG_LEVEL_INFO = 0, XEFG_SWAPCHAIN_LOG_LEVEL_WARNING, XEFG_SWAPCHAIN_LOG_LEVEL_ERROR } xefg_swapchain_log_level_t;
typedef void (*xefg_swapchain_logging_callback_t)(xefg_swapchain_log_level_t level, const char* message, void* pUserData);
typedef xefg_swapchain_result_t (*xefgSwapChainSetLoggingCallbackPtr)(xefg_swapchain_handle_t hSwapChain, xefg_swapchain_logging_level_t logLevel, xefg_swapchain_logging_callback_t callback, void* pUserData);

typedef xefg_swapchain_result_t (*xefgSwapChainD3D12TagFrameResourcePtr)(xefg_swapchain_handle_t hSwapChain, ID3D12CommandList* pCmdList, uint32_t presentId, const xefg_swapchain_d3d12_resource_data_t* pResData);
typedef xefg_swapchain_result_t (*xefgSwapChainTagFrameConstantsPtr)(xefg_swapchain_handle_t hSwapChain, uint32_t presentId, const xefg_swapchain_frame_constant_data_t* pConstants);
typedef xefg_swapchain_result_t (*xefgSwapChainSetEnabledPtr)(xefg_swapchain_handle_t hSwapChain, uint32_t enable);
typedef xefg_swapchain_result_t (*xefgSwapChainD3D12GetSwapChainPtrPtr)(xefg_swapchain_handle_t hSwapChain, REFIID riid, void** ppSwapChain);
typedef xefg_swapchain_result_t (*xefgSwapChainDestroyContextPtr)(xefg_swapchain_handle_t hSwapChain);
typedef xefg_swapchain_result_t (*xefgSwapChainSetPresentIdPtr)(xefg_swapchain_handle_t hSwapChain, uint32_t presentId);

class XeSS
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\SKSE\\Plugins\\XeSS";

	XeSS() = default;

	HMODULE module = nullptr;
	HMODULE moduleFG = nullptr;
	HMODULE moduleLL = nullptr;

	// XeSS function pointers
	xessGetVersionPtr xessGetVersion = nullptr;
	xessD3D12CreateContextPtr xessD3D12CreateContext = nullptr;
	xessD3D12InitPtr xessD3D12Init = nullptr;
	xessD3D12ExecutePtr xessD3D12Execute = nullptr;
	xessDestroyContextPtr xessDestroyContext = nullptr;
	xessSetJitterScalePtr xessSetJitterScale = nullptr;
	xessSetVelocityScalePtr xessSetVelocityScale = nullptr;
	xessGetInputResolutionPtr xessGetInputResolution = nullptr;

	xess_context_handle_t xessContext = nullptr;

	bool featureXeSS = false;  // whether enabled

	// XeSS Frame Generation function pointers - corrected
	xefgSwapChainD3D12CreateContextPtr xefgSwapChainD3D12CreateContext = nullptr;
	xefgSwapChainD3D12InitFromSwapChainPtr xefgSwapChainD3D12InitFromSwapChain = nullptr;
	xefgSwapChainD3D12InitFromSwapChainDescPtr xefgSwapChainD3D12InitFromSwapChainDesc = nullptr;
	xefgSwapChainSetLatencyReductionPtr xefgSwapChainSetLatencyReduction = nullptr;
	xellD3D12CreateContextPtr xellD3D12CreateContext = nullptr;
	xellDestroyContextPtr xellDestroyContext = nullptr;
	xellAddMarkerDataPtr xellAddMarkerData = nullptr;
	xefgSwapChainSetLoggingCallbackPtr xefgSwapChainSetLoggingCallback = nullptr;
	xefgSwapChainD3D12TagFrameResourcePtr xefgSwapChainD3D12TagFrameResource = nullptr;
	xefgSwapChainTagFrameConstantsPtr xefgSwapChainTagFrameConstants = nullptr;
	xefgSwapChainSetEnabledPtr xefgSwapChainSetEnabled = nullptr;
	xefgSwapChainD3D12GetSwapChainPtrPtr xefgSwapChainD3D12GetSwapChainPtr = nullptr;
	xefgSwapChainDestroyContextPtr xefgSwapChainDestroyContext = nullptr;
	xefgSwapChainSetPresentIdPtr xefgSwapChainSetPresentId = nullptr;

	xefg_swapchain_handle_t xefgContext = nullptr;
	xell_context_handle_t xellContext = nullptr;
	uint32_t currentPresentId = 0;
	bool featureXeSSFG = false;  // whether XeSS Frame Generation is enabled
	bool isFrameGenActive = false;  // current frame generation state

	// Cached DLL version info for XeSS plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadXeSS();
	void CreateXeSSResources();
	void DestroyXeSSResources();
	float GetInputResolutionScale(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityPreset);
	void Upscale(
		ID3D12Resource* a_inputColorTexture,
		ID3D12Resource* a_motionVectorTexture,
		ID3D12Resource* a_depthTexture,
		ID3D12Resource* a_reactiveMask,
		ID3D12Resource* a_outputTexture,
		ID3D12GraphicsCommandList* a_commandList,
		uint32_t a_renderWidth,
		uint32_t a_renderHeight,
		float2 a_jitter);

	// XeSS Frame Generation methods
	IDXGISwapChain* SetupFrameGeneration(HWND a_hwnd, DXGI_SWAP_CHAIN_DESC1 a_swapChainDesc, ID3D12CommandQueue* a_commandQueue, IDXGIFactory* a_factory);
	void Present(bool a_useFrameGeneration, ID3D12CommandList* a_commandList);
	void DestroyFrameGenerationResources();
	void TagFrameResources(ID3D12CommandList* a_commandList, uint32_t presentId);
	void SetFrameConstants(uint32_t presentId, const xefg_swapchain_frame_constant_data_t* pConstants);

	// XeLL low latency marker functions
	void AddMarker(_xell_latency_marker_type_t markerType);
	void AddSimulationStartMarker();
	void AddSimulationEndMarker();
	void AddPresentStartMarker();
	void AddPresentEndMarker();
};