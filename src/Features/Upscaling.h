#pragma once

#include "Feature.h"
#include <array>
#include <atomic>
#include <d3d11_4.h>
#include <winrt/base.h>

/** @brief Provides Vulkan-backed temporal upscaling and frame generation. */
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
		return { T("feature.upscaling.description", "Advanced upscaling technologies for improved performance"),
			{ T("feature.upscaling.key_feature_1", "DLSS (Deep Learning Super Sampling) support"),
				T("feature.upscaling.key_feature_2", "FSR (FidelityFX Super Resolution) support"),
				T("feature.upscaling.key_feature_5", "XeSS (Xe Super Sampling) support"),
				T("feature.upscaling.key_feature_3", "TAA (Temporal Anti-Aliasing) support"),
				T("feature.upscaling.key_feature_4", "FSR 3 and DLSS frame generation support") } };
	};

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS,
		kXeSS,
	};

	enum class FrameGenMethod
	{
		kFSR,
		kDLSSG,
	};

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kFSR;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
		uint qualityMode = 1;
		float sharpnessFSR = 0.0f;
		bool reflexEnabled = false;
		bool reflexBoost = false;
		// Legacy fields kept for JSON backward compatibility.
		bool reflexLowLatencyMode = false;
		bool reflexLowLatencyBoost = false;
		bool frameGeneration = false;
		uint frameGenMethod = (uint)FrameGenMethod::kDLSSG;
		uint frameGenMultiplier = 2;
		bool dlssgDynamic = false;
		bool fgShowOnlyGenerated = false;
		bool fgDebugView = false;
		bool fgDebugTearLines = false;
		bool fgDebugPacingLines = false;
		bool hardwareDefaultsApplied = false;

		bool vsync = false;
		// Zero disables the cap; positive values divide the monitor refresh rate.
		int frameRateLimitDivisor = 1;
	};

	Settings settings;

	struct JitterCB
	{
		float2 jitter;
		float useWideKernel;
		float pad0;
	};

	ConstantBuffer* jitterCB = nullptr;

	// Runtime state
	bool isWindowed = false;

	/** @brief Returns whether the game window is minimized. */
	static bool IsWindowMinimized();

	static void NotifyWindowFocus(bool a_focused);        // WM_ACTIVATEAPP / WM_ACTIVATE
	static void NotifyWindowModifying(bool a_modifying);  // WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE

	/** @brief Returns whether presenting must be suspended. */
	static bool IsWindowUnusable();

	static inline std::atomic<bool> s_windowUnfocused{ false };
	static inline std::atomic<bool> s_windowModifying{ false };

	// Timing and scaling
	double refreshRate = 0.0;
	float2 resolutionScale = { 1.0f, 1.0f };

	bool IsUpscalingActive() const;

	// Feature interface overrides
	virtual void DrawSettings() override;
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DataLoaded() override;

	virtual void Load() override;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;

	UpscaleMethod GetUpscaleMethod() const;
	FrameGenMethod GetFrameGenMethod() const;

	void ApplyHardwareDefaults();

	void CheckResources(UpscaleMethod a_upscalemethod);

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	winrt::com_ptr<ID3D11PixelShader> copyHudlessPS;
	ID3D11PixelShader* GetCopyHudlessPS();

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

	bool IsFrameGenerationActive() const;

	/** @brief Returns the Reflex state required by the active frame generator. */
	[[nodiscard]] bool GetEffectiveReflex() const;

	/** @brief Returns the monitor refresh rate in hertz. */
	[[nodiscard]] int GetMonitorRefreshRate() const;
	/** @brief Highest refresh rate the display offers at its current resolution. */
	[[nodiscard]] int GetHighestRefreshRate() const;
	/** @brief DXVK tearing preference implied by the frame-rate setting: 1 = tearing, 0 = tear-free. */
	[[nodiscard]] uint32_t GetPresentModePreference() const;
	/** @brief Re-applies the present mode when the frame-rate setting changes; recreates the swapchain. */
	void UpdatePresentModePreference();
	/** @brief Returns the configured frame-rate cap, or zero when uncapped. */
	[[nodiscard]] double GetTargetFrameRate() const;
	/** @brief Returns the rendered-frame cap after accounting for fixed frame generation. */
	[[nodiscard]] double GetRenderedFrameRateLimit() const;
	/** @brief Returns the fixed DLSS-G multiplier clamped to the reported hardware limit. */
	[[nodiscard]] uint32_t GetFixedDLSSGMultiplier() const;
	/** @brief Applies the non-Reflex frame-rate limit through DXVK. */
	void ApplyDxvkFrameRateLimit(double a_fps);

	HRESULT PresentWithFrameGeneration(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags,
		const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& a_present);

	// D3D11 textures
	Texture2D* upscaledTexture = nullptr;
	Texture2D* hudlessTexture = nullptr;

	virtual void ClearShaderCache() override;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousUpscalingWasActive = false;

	bool depthUpscaleUseWideKernel = false;

	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();

	static double GetRefreshRate(HWND a_window);

private:
	static constexpr size_t kUpscaleMethodCount = static_cast<size_t>(UpscaleMethod::kXeSS) + 1;
	std::array<bool, kUpscaleMethodCount> failedUpscaleMethods{};

	[[nodiscard]] bool IsUpscaleMethodFailed(UpscaleMethod a_method) const;
	void MarkUpscaleMethodFailed(UpscaleMethod a_method);

	void BeginRenderFrame();
	void CreateUpscaledTexture();
	void DestroyUpscaledTexture();
	void CreateHudlessTexture();
	bool DestroyHudlessTexture(bool a_commandRingDrained = false);
	ID3D11Resource* CaptureHudlessColor();
	bool CopyHudlessColor(ID3D11ShaderResourceView* a_source);
	void PrepareFrameGeneration(ID3D11Resource* a_hudlessColor);

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
