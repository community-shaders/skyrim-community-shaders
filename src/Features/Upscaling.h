#pragma once

#include "Feature.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/XeSS.h"
#include <d3d11_4.h>
#include <d3d12.h>
#include <winrt/base.h>

/**
 * @brief Provides upscaling functionality including DLSS, FSR, XeSS and TAA.
 *
 * This feature handles various upscaling methods and frame generation technologies
 * to improve performance while maintaining visual quality.
 */
struct Upscaling : Feature
{
public:
	// Feature interface
	virtual inline std::string GetName() override { return "Upscaling"; }
	virtual inline std::string GetShortName() override { return "Upscaling"; }
	virtual inline bool SupportsVR() override { return true; }
	virtual inline bool IsCore() const override { return false; }
	virtual inline std::string_view GetCategory() const override { return "Display"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Advanced upscaling and frame generation technologies for improved performance",
			{ "DLSS (Deep Learning Super Sampling) support",
				"FSR (FidelityFX Super Resolution) support",
				"XeSS (Intel Xe Super Sampling) support",
				"TAA (Temporal Anti-Aliasing) support",
				"Frame generation for supported systems" }
		};
	}

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kXESS,
		kDLSS
	};

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kDLSS;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
		uint qualityMode = 1;  // Default to Quality (1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA)
		uint frameLimitMode = 1;
		uint frameGenerationMode = 1;
		uint frameGenerationForceEnable = 0;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
	};

	Settings settings;

	// Runtime state
	bool isWindowed = false;
	bool lowRefreshRate = false;
	bool fidelityFXMissing = false;
	bool d3d12Interop = false;
	bool wasUpscaled = false;

	// Timing and scaling
	double refreshRate = 0.0f;
	float resolutionScale = 1.0f;
	LARGE_INTEGER qpf;

	// FG FPS Measurement for Overlay
	bool IsFrameGenerationActive() const;
	float GetFrameGenerationFrameTime() const;

	// Feature interface overrides
	virtual void DrawSettings() override;
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/**
	 * @brief Installs Direct3D-related hooks for device and factory creation.
	 *
	 * Loads FidelityFX support and patches the import address table (IAT) to redirect D3D11 device and DXGI factory creation functions to custom hook implementations.
	**/
	virtual void Load() override;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;

	UpscaleMethod GetUpscaleMethod();

	void CheckResources(UpscaleMethod a_upscalemethod);

	ID3D11ComputeShader* encodeTexturesCS = nullptr;
	ID3D11ComputeShader* GetEncodeTexturesCS();

	ID3D11ComputeShader* encodeTexturesTransparencyCS = nullptr;
	ID3D11ComputeShader* GetEncodeTexturesTransparencyCS();

	ID3D11PixelShader* depthRefractionUpscalePS = nullptr;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();
	
	ID3D11PixelShader* underwaterMaskUpscalePS = nullptr;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	ID3D11VertexShader* upscaleVS = nullptr;
	ID3D11VertexShader* GetUpscaleVS();

	ID3D11DepthStencilState* upscaleDepthStencilState = nullptr;
	ID3D11BlendState* upscaleBlendState = nullptr;
	ID3D11RasterizerState* upscaleRasterizerState = nullptr;

	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

	// D3D11 textures
	Texture2D* reactiveMaskTexture = nullptr;
	Texture2D* transparencyCompositionMaskTexture = nullptr;

	// Resource management
	void DestroyUpscalingResources();

	// Shared D3D12 device and interop resources
	winrt::com_ptr<ID3D12Device> sharedD3D12Device;
	winrt::com_ptr<ID3D12CommandQueue> sharedD3D12CommandQueue;
	winrt::com_ptr<ID3D12CommandAllocator> sharedD3D12CommandAllocator;
	winrt::com_ptr<ID3D12GraphicsCommandList> sharedD3D12CommandList;
	winrt::com_ptr<ID3D12Fence> sharedD3D12Fence;
	HANDLE sharedFenceEvent = nullptr;
	UINT64 sharedFenceValue = 0;

	// D3D11/D3D12 shared fence for interop synchronization
	winrt::com_ptr<ID3D11Fence> sharedD3D11Fence;
	UINT64 sharedInteropFenceValue = 0;

	// Shared D3D12 resources for upscaling systems
	WrappedResource* HUDLessBufferShared12 = nullptr;
	WrappedResource* depthBufferShared12 = nullptr;
	WrappedResource* motionVectorBufferShared12 = nullptr;
	WrappedResource* reactiveMaskShared12 = nullptr;
	WrappedResource* inputColorBufferShared12 = nullptr;
	WrappedResource* outputColorBufferShared12 = nullptr;

	// Frame tracking to ensure shared resources are only copied once per frame
	Util::FrameChecker sharedResourcesFrameChecker;
	Util::FrameChecker HUDLessBufferFrameChecker;

	// Static instances instead of singletons
	static inline Streamline streamline;
	static inline XeSS xess;
	static inline FidelityFX fidelityFX;
	static inline class DX12SwapChain dx12SwapChain;

	ID3D11ComputeShader* copyDepthToSharedBufferCS = nullptr;

	void CreateFrameGenerationResources();
	void CopyHUDLessBuffer();
	void CreateSharedD3D12Device(IDXGIAdapter* a_dxgiAdapter);
	void CreateSharedD3D12Resources();
	void CopyFrameGenerationResources();
	void CopySharedD3D12Resources(bool a_upscale);
	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

	// Unified interface methods - external code should use these instead of direct access
	void LoadUpscalingSDKs();  // Loads all SDKs at once
	void CheckFrameConstants();
	bool IsFrameGenActive() const;
	void SetUIBuffer();
	HANDLE GetFrameLatencyWaitableObject() const;
	float GetFrameTime() const;

	// Backend interface methods
	bool IsBackendInitialized() const;
	void CheckBackendFeatures(IDXGIAdapter* adapter);
	void UpgradeBackendInterface(void** ppInterface);
	void SetBackendD3DDevice(ID3D11Device* device);
	void PostBackendDevice();

	// Module availability methods
	bool HasFrameGenModule() const;

	// Proxy interface methods
	void SetProxyD3D11Device(ID3D11Device* device);
	void SetProxyD3D11DeviceContext(ID3D11DeviceContext* context);
	void CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc);
	void CreateProxyInterop();
	IDXGISwapChain* GetProxySwapChain();

private:
	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct MenuManagerDrawInterfaceStartHook
	{
		static void thunk(int64_t a1);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager* a1, uint32_t a3, uint32_t er8_);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct SetScissorRect
	{
		static void thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPrecipitation
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSFaceGenManager_UpdatePendingCustomizationTextures
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
