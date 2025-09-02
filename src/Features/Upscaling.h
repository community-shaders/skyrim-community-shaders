#pragma once

#include "Feature.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/DX12SwapChain.h"
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
				"TAA (Temporal Anti-Aliasing) support",
				"Frame generation for supported systems" }
		};
	}

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kDLSS
	};

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kDLSS;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kTAA;
		uint qualityMode = 1;  // Default to Quality (1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA)
		uint frameLimitMode = 1;
		uint frameGenerationMode = 1;
		uint frameGenerationForceEnable = 0;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
	};

	Settings settings;

	struct JitterCB
	{
		float2 jitter;
		float2 pad0;
	};

	ConstantBuffer* jitterCB = nullptr;

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

	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[3];  // One for each UpscaleMethod
	ID3D11ComputeShader* GetEncodeTexturesCS();

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();
	void UpscaleDepth();

	// D3D11 textures
	Texture2D* reactiveMaskTexture = nullptr;
	Texture2D* transparencyCompositionMaskTexture = nullptr;

	virtual void ClearShaderCache() override;


	// Frame Generation resources (dev branch style - D3D11 textures + D3D12 shared handles)
	Texture2D* HUDLessBufferShared = nullptr;
	Texture2D* depthBufferShared = nullptr;
	Texture2D* motionVectorBufferShared = nullptr;
	
	// D3D12 shared resource handles
	winrt::com_ptr<ID3D12Resource> HUDLessBufferShared12;
	winrt::com_ptr<ID3D12Resource> depthBufferShared12;
	winrt::com_ptr<ID3D12Resource> motionVectorBufferShared12;

	// Frame tracking to ensure shared resources are only copied once per frame
	Util::FrameChecker sharedResourcesFrameChecker;
	Util::FrameChecker HUDLessBufferFrameChecker;

	// Static instances instead of singletons
	static inline Streamline streamline;
	static inline FidelityFX fidelityFX;
	static inline class DX12SwapChain dx12SwapChain;

	winrt::com_ptr<ID3D11ComputeShader> copyDepthToSharedBufferCS;

	void CreateFrameGenerationResources();
	void CopyHUDLessBuffer();
	void CopyBuffersToSharedResources();
	void PostDisplay();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

	// Unified interface methods - external code should use these instead of direct access
	// Frame generation interface methods
	bool IsFrameGenActive() const;
	HANDLE GetFrameLatencyWaitableObject() const;
	float GetFrameTime() const;

	// Module availability methods
	bool HasFrameGenModule() const;

	// SDK loading methods
	void LoadUpscalingSDKs();
	void CheckFrameConstants();
	bool IsBackendInitialized() const;
	void CheckBackendFeatures(IDXGIAdapter* adapter);
	void UpgradeBackendInterface(void** ppInterface);
	void CreateSharedD3D12Device(IDXGIAdapter* a_dxgiAdapter);
	void SetBackendD3DDevice(ID3D11Device* device);
	void PostBackendDevice();
	void CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void CheckResources(UpscaleMethod a_upscalemethod);
	void CreateSharedD3D12Resources(UpscaleMethod a_upscalemethod);
	void DestroySharedD3D12Resources(UpscaleMethod a_upscalemethod);
	void PerformUpscaling();

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
