#pragma once

#include "Feature.h"
#include "Upscaling/RCAS/RCAS.h"
#include <atomic>
#include <d3d11_4.h>
#include <winrt/base.h>

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
			{ T("feature.upscaling.key_feature_2", "FSR (FidelityFX Super Resolution) support"),
				T("feature.upscaling.key_feature_3", "TAA (Temporal Anti-Aliasing) support") } };
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
		uint frameGenMethod = (uint)FrameGenMethod::kFSR;
		// DLSS-G fixed frame multiplier (2 = 2x single-frame … up to 6x). numFramesToGenerate = multiplier - 1.
		// Capped at runtime to the hardware max (numFramesToGenerateMax+1): 40-series caps at 2x, 50-series up to 6x.
		uint frameGenMultiplier = 2;
		// DLSS-G "Dynamic" mode: the driver picks the multiplier itself. Maps to eDynamic when the hardware
		// supports Dynamic MFG (50-series), else eAuto (40-series). The target fps is the Display frame-rate
		// option. false = a fixed multiplier (frameGenMultiplier).
		bool dlssgDynamic = false;
		bool fgShowOnlyGenerated = false;
		bool fgDebugView = false;
		bool fgDebugTearLines = false;
		bool fgDebugPacingLines = false;
		bool hardwareDefaultsApplied = false;

		// Present pacing. VSync is applied in our present hook (forced OFF whenever DLSS-G is the active FG
		// method — VSync is incompatible with DLSS-G on Vulkan). The cap is driven by Reflex when available
		// (lowest latency; DLSS-G handles it this way), else by DXVK's frame limiter.
		bool vsync = false;
		// Frame-rate cap as a DIVISOR of the monitor refresh rate: 1 = refresh (default), 2 = half, 3 = third…;
		// 0 = unlocked (variable, no cap). Stored as the divisor (not an fps) so the cap stays meaningful across
		// monitors/refresh rates. Resolved to fps by GetTargetFrameRate().
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

	// When true, the discrete settings steppers render as PhotoMode-style "< value >" arrow controls rather
	// than native sliders. DisplaySettingsMenu sets this around its DrawSettings() call so the in-game Display
	// Settings window matches PhotoMode, while the CS main menu keeps native sliders.
	static inline bool useArrowSteppers = false;

	// True while the game window is minimized: every Streamline/GPU-interop call (and the
	// present itself, see the present hook) must be skipped for the gap's duration.
	static bool IsWindowGapActive();

	// Frame generation MUST be off whenever the window is not in a normal focused state:
	// DXVK recreates the swapchain on occlusion and an FG-wrapped swapchain freezes the GPU
	// (and DLSS-G's pacer wedges across a present gap, guide §17). These flags are set from
	// the game's window/message thread (WndProc hook) — atomics ONLY, never touch SL/FFX
	// there (wrong thread) — and read on the render/present thread via IsWindowUnusable().
	static void NotifyWindowFocus(bool a_focused);        // WM_ACTIVATEAPP / WM_ACTIVATE
	static void NotifyWindowModifying(bool a_modifying);  // WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE

	// True while the window is minimized, unfocused (alt-tabbed / occluded), or being
	// resized/moved. Frame generation is suspended for the duration (present hook ->
	// FrameGen::Controller::SuspendForWindowGap). Superset of IsWindowGapActive() (minimize).
	static bool IsWindowUnusable();

	// Set from WndProc (window/message thread), read on the render/present thread.
	static inline std::atomic<bool> s_windowUnfocused{ false };
	static inline std::atomic<bool> s_windowModifying{ false };

	// Focus-change settle deadline (GetTickCount64 ms). A focus change (either direction) starts a
	// transition where the swapchain moves between flip-model and DWM-composited; a DLSS-G present
	// issued DURING that transition stalls in the driver even though the window reports focused (the
	// residual freeze after synchronous present). So IsWindowUnusable() stays true for a short settle
	// after ANY focus change: aggressive alt-tab keeps re-arming it (no presents through the churn),
	// and it expires once focus is stable so presents resume on a settled flip surface.
	static inline std::atomic<uint64_t> s_focusSettleUntilTick{ 0 };

	// A CS-initiated upscaler reconfiguration (upscale method or quality/preset change) changes the
	// render resolution. Per DLSS-G guide §12, DLSS-G must be OFF while the host modifies resolution
	// or the DLSS-G present stalls in the driver (and EvaluateDLSS's forced CS-thread sync then
	// deadlocks — the in-game "changed the upscale preset and it froze" wedge). CS owns this change,
	// so it brackets it: NotifyUpscalerReconfig() marks the next few frames, during which CS skips ALL
	// its per-frame Streamline work + the present (exactly like a window gap) so no DLSS-G present runs
	// through the transition; it re-engages once the new size settles. Frame-count based so it is
	// immune to Main_PostProcessing running more than once per frame.
	static void NotifyUpscalerReconfig();
	[[nodiscard]] static bool IsUpscalerReconfiguring();
	// Wall-clock deadline (GetTickCount64 ms), NOT a frame count. While reconfiguring, the present hook
	// returns early (skips the present) BEFORE State::Reset increments frameCount — so frameCount is
	// frozen for the whole bracket. A frame-count deadline would therefore never be reached and the
	// present would be skipped FOREVER (permanent freeze — the reproduced "changed preset and it froze").
	// Wall clock always advances, so the bracket always expires.
	static inline std::atomic<uint64_t> s_reconfigUntilTick{ 0 };

	// Push IsWindowUnusable() to DXVK's present-suspend flag (dxvkSetPresentSuspended @109). While set,
	// DXVK skips acquiring + presenting to the occluded/off-flip surface, so a DLSS-G frame-generation
	// present can never stall in the driver and wedge DXVK's serial submit thread (the alt-tab freeze
	// that CS's D3D11-level present-hook skip can't reach — an already-enqueued present is below it).
	// Called from the WndProc notifiers (immediate on focus/resize, to catch an in-flight present) and
	// once per present (covers minimize + steady state). Resolves the export once from dxvk_d3d11.dll.
	static void PushPresentSuspendToDxvk();


	// Timing and scaling
	double refreshRate = 0.0f;
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

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	static eastl::unique_ptr<Texture2D> CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
		bool copyBindFlags = false, bool createSRV = false, bool createUAV = false, const char* name = nullptr);

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

	bool IsFrameGenerationActive() const;

	// Effective NVIDIA Reflex state under the frame-generation policy: DLSS-G frame-gen forces Reflex ON
	// (DLSS-G stalls without it), FSR frame-gen forces it OFF, and with no frame generation it follows the
	// user's reflexEnabled toggle. Never mutates the saved preference.
	[[nodiscard]] bool GetEffectiveReflex() const;

	// Monitor refresh rate in Hz (from the cached swapchain refreshRate, else the current display mode, else 60).
	[[nodiscard]] int GetMonitorRefreshRate() const;
	// Resolve the frame-rate cap to an fps value: refresh / frameRateLimitDivisor, or 0 ("no limit") when the
	// divisor is 0 (unlocked). Driven by Reflex when active, else DXVK's limiter.
	[[nodiscard]] int GetTargetFrameRate() const;
	// Apply a frame-rate cap through DXVK's own limiter (the non-Reflex fallback). fps<=0 clears the limit.
	void ApplyDxvkFrameRateLimit(double a_fps);

	HRESULT PresentWithFrameGeneration(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags,
		const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& a_present);

	// D3D11 textures
	Texture2D* upscaledTexture = nullptr;
	Texture2D* hudlessTexture = nullptr;

	virtual void ClearShaderCache() override;

	static inline RCAS rcas;

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
	void CreateUpscaledTexture();
	void DestroyUpscaledTexture();
	void CreateHudlessTexture();
	void DestroyHudlessTexture();

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
