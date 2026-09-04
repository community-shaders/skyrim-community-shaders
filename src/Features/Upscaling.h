#pragma once

#include "Feature.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/IntelXeSS.h"
#include "Upscaling/IntelXeSSD3D12.h"
#include "Upscaling/IntelXeSSFrameGeneration.h"
#include "Upscaling/RCAS/RCAS.h"
#include "Upscaling/Streamline.h"
#include <d3d11_4.h>
#include <d3d12.h>
#include <winrt/base.h>

/**
 * @brief Provides upscaling functionality including DLSS, FSR and TAA.
 *
 * This feature handles various upscaling methods and frame generation technologies
 * to improve performance while maintaining visual quality.
 */
struct Upscaling : Feature
{
private:
	static constexpr std::string_view MOD_ID = "156952";

public:
	// Feature interface
	virtual inline std::string GetName() override { return "Upscaling"; }
	virtual std::string GetDisplayName() override { return T("feature.upscaling.name", "Upscaling"); }
	virtual inline std::string GetShortName() override { return "Upscaling"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline bool IsCore() const override { return false; }
	virtual inline std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.upscaling.description", "Advanced upscaling and frame generation technologies for improved performance"),
			{ T("feature.upscaling.key_feature_1", "DLSS (Deep Learning Super Sampling) support"),
				T("feature.upscaling.key_feature_2", "FSR (FidelityFX Super Resolution) support"),
				T("feature.upscaling.key_feature_3", "XeSS (Xe Super Sampling) support"),
				T("feature.upscaling.key_feature_4", "TAA (Temporal Anti-Aliasing) support"),
				T("feature.upscaling.key_feature_5", "Frame generation for supported systems") } };
	};

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS,
		kXESS,
		kTOTAL
	};

	enum class FrameGenerationMethod
	{
		kNONE,
		kFSR,
		kXESS
	};

	/** Presented frames per rendered frame. 2 = one interpolated frame (the only rate FSR-FG supports). */
	static constexpr uint kMinFrameGenerationMultiplier = 2;
	/** XeSS-FG 1.3 interpolates at most three frames; the runtime maximum is queried per adapter. */
	static constexpr uint kMaxFrameGenerationMultiplier = 4;

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kDLSS;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
		uint qualityMode = 1;      // Default to Quality (1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA)
		uint qualityModeXeSS = 3;  // 0=AA, 1=Ultra Quality Plus, 2=Ultra Quality, 3=Quality, 4=Balanced, 5=Performance, 6=Ultra Performance
		uint frameLimitMode = 1;
		uint frameGenerationMode = 1;
		uint frameGenerationForceEnable = 0;
		bool frameGenerationAllowInMenus = false;
		/** Presented frames per rendered frame (2 = one interpolated frame). XeSS-FG only. */
		uint frameGenerationMultiplier = 2;
		/**
		 * xefg_swapchain_ui_mode_t value used for XeSS-FG UI handling. 1 = NONE interpolates the
		 * UI with the back buffer; the other modes composite it from the HUD-less/UI textures.
		 * Applied when the proxy swap chain is created, so it needs a restart.
		 */
		uint frameGenerationXeSSUIMode = 3;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
		float sharpnessFSR = 0.0f;
		bool sharpnessEnabledDLSS = false;
		float sharpnessDLSS = 0.0f;
		uint presetDLSS = 0;  // 0=Default, 1=J, 2=K, 3=L, 4=M
		bool reflexLowLatencyMode = false;
		bool reflexLowLatencyBoost = false;
		bool reflexUseMarkersToOptimize = false;
		bool reflexUseFPSLimit = false;
		float reflexFPSLimit = 60.0f;
	};

	Settings settings;

	struct JitterCB
	{
		float2 jitter;
		float useWideKernel;
		float pad0;
	};

	struct UpscalingDataCB
	{
		float2 trueSamplingDim;
		float2 pad0;
	};

	ConstantBuffer* jitterCB = nullptr;
	ConstantBuffer* upscalingDataCB = nullptr;

	// Runtime state
	bool isWindowed = false;
	bool lowRefreshRate = false;
	bool fidelityFXMissing = false;
	bool intelXeSSFrameGenerationMissing = false;
	bool d3d12SwapChainActive = false;
	bool xessD3D12PathActive = false;
	bool isIntelAdapter = false;
	uint activeFrameGenerationMode = static_cast<uint>(FrameGenerationMethod::kNONE);

	// Timing and scaling
	double refreshRate = 0.0f;
	float2 resolutionScale = { 1.0f, 1.0f };
	LARGE_INTEGER qpf;

	// FG FPS Measurement for Overlay
	bool IsFrameGenerationDx12PathActive() const;
	bool IsFrameGenerationActive() const;
	bool ShouldUseFrameGenerationThisFrame() const;
	/** Presented frames per rendered frame for the active backend (1 when frame generation is off). */
	uint32_t GetFrameGenerationMultiplier() const;
	float GetFrameGenerationFrameTime() const;
	bool IsUpscalingActive() const;

	// Feature interface overrides
	virtual void DrawSettings() override;
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DataLoaded() override;

	/**
	 * @brief Installs Direct3D-related hooks for device and factory creation.
	 *
	 * Loads FidelityFX support and patches the import address table (IAT) to redirect D3D11 device and DXGI factory creation functions to custom hook implementations.
	**/
	virtual void Load() override;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;

	UpscaleMethod GetUpscaleMethod() const;

	void CheckResources(UpscaleMethod a_upscalemethod);
	void CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);

	/** One permutation per UpscaleMethod (kNONE, kTAA, kFSR, kDLSS, kXESS). */
	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[(size_t)UpscaleMethod::kTOTAL];
	ID3D11ComputeShader* GetEncodeTexturesCS();

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	winrt::com_ptr<ID3D11ComputeShader> uiCompositeCS;
	ID3D11ComputeShader* GetUICompositeCS();

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	// Helper: Create a Texture2D matching source format at a given size
	static eastl::unique_ptr<Texture2D> CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
		bool copyBindFlags = false, bool createSRV = false, bool createUAV = false, const char* name = nullptr);

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

	// D3D11 textures
	Texture2D* reactiveMaskTexture = nullptr;
	Texture2D* transparencyCompositionMaskTexture = nullptr;
	Texture2D* motionVectorCopyTexture = nullptr;
	Texture2D* sharpenerTexture = nullptr;
	/** Private XeSS-SR depth input; unused when the frame-generation depth buffer is shared instead. */
	Texture2D* xessDepthTexture = nullptr;
	Texture2D* xessOutputTexture = nullptr;

	/**
	 * @brief Whether native XeSS-SR reads its depth/motion vectors from the buffers XeSS-FG tags.
	 *
	 * Native D3D11 XeSS-SR reads plain D3D11 textures, so the D3D11 side of the shared
	 * depth/motion-vector interop buffers can feed it directly and one EncodeTexturesCS write
	 * serves both SDKs. The cross-vendor D3D12 path copies into the shared depth buffer itself and
	 * must therefore keep its private input.
	 */
	bool XeSSSharesFrameGenerationInputs() const;
	ID3D11Resource* GetXeSSDepthResource() const;
	ID3D11UnorderedAccessView* GetXeSSDepthUAV() const;

	virtual void ClearShaderCache() override;

	// Static instances instead of singletons
	static inline Streamline streamline;
	static inline FidelityFX fidelityFX;  ///< Only for frame generation
	static inline IntelXeSS intelXeSS;
	static inline IntelXeSSD3D12 intelXeSSD3D12;
	static inline IntelXeSSFrameGeneration intelXeSSFrameGeneration;
	static inline DX12SwapChain dx12SwapChain;
	static inline RCAS rcas;  ///< Standalone RCAS sharpening for DLSS

	winrt::com_ptr<ID3D11PixelShader> copyDepthToSharedBufferPS;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousUpscalingWasActive = false;
	bool depthUpscaleUseWideKernel = false;

	/**
	 * Set when a XeSS lifecycle change had to be skipped because the GPU drain failed.
	 * CheckResources retries the teardown on later frames; without it the tracking update
	 * would hide the pending kXESS state and leak the SDK context for the session.
	 */
	bool pendingXeSSTeardown = false;
	/** Retry budget for pendingXeSSTeardown; each attempt can block for the full drain timeout. */
	uint32_t pendingXeSSTeardownAttempts = 0;
	static constexpr uint32_t kMaxXeSSTeardownAttempts = 3;

	/**
	 * Set when XeSS resources change or LoadingMenu closes. The next XeSS-SR
	 * dispatch drops temporal history to avoid carrying data across discontinuities.
	 */
	std::atomic<bool> pendingXeSSReset{ false };
	std::atomic<bool> pendingXeSSFrameGenerationReset{ true };

	void CopySharedD3D12Resources();

	/**
	 * @brief Blends the FG UI texture onto the HUD-less back buffer into the present buffer.
	 *
	 * XeSS-FG composites UI onto interpolated frames only and presents the application's back
	 * buffer verbatim, so that buffer has to arrive with the UI already blended in or the UI
	 * appears on generated frames only, which reads as flicker. Uses the same premultiplied
	 * blend XeSS-FG applies internally so real and generated frames match. FidelityFX
	 * composites both frame types itself and presents the HUD-less buffer directly.
	 *
	 * @return true when the present buffer holds the composited image and must be the Present source.
	 */
	bool CompositeFrameGenerationUI();

	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();

	/**
	 * @brief Applies RCAS sharpening to the main render target after DLSS upscaling.
	 *
	 * Runs in HDR space before tonemapping. Only called when DLSS is active and sharpness > 0.
	 */
	void ApplySharpening();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

	// Unified interface methods - external code should use these instead of direct access
	void LoadUpscalingSDKs();  // Loads all SDKs at once
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
	bool CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc, bool enableFrameGenerationProvider);
	void CreateProxyInterop();
	IDXGISwapChain* GetProxySwapChain();

	using BlurResources = DX12SwapChain::BlurResources;

	// Get all D3D11 resources needed for background blur when D3D12 swap chain is active
	BlurResources GetBlurResources() const;

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
		static void thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5);
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

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
		static bool Register();
	};
};
