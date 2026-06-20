#pragma once

#include "Buffer.h"
#include "Feature.h"

#include <DirectXMath.h>
#include <dxgi.h>
#include <functional>
#include <mutex>
#include <unordered_map>

struct HDRDisplay : public Feature
{
private:
	static constexpr std::string_view MOD_ID = "179371";

public:
	virtual inline std::string GetName() override { return "HDR Display"; }
	virtual std::string GetDisplayName() override { return T("feature.hdr_display.name", "HDR Display"); }
	virtual inline std::string GetShortName() override { return "HDRDisplay"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetCategory() const override { return "Display"; }
	virtual inline bool IsCore() const override { return false; }

	virtual inline std::string_view GetShaderDefineName() override { return "HDR_OUTPUT"; }
	/** @brief Returns true for ImageSpace and Sky shader types. */
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return shaderType == RE::BSShader::Type::ImageSpace || shaderType == RE::BSShader::Type::Sky;
	}

	/** @brief Returns a localized description and key feature bullet points for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.hdr_display.description", "Real High Dynamic Range output for HDR displays."),
			{ T("feature.hdr_display.key_feature_1", "HDR10 output support (10-bit) with upgraded HDR buffers (16-Bit), and fully unclamped rendering pipeline for true HDR values."),
				T("feature.hdr_display.key_feature_2", "HDR-aware tonemapping based on Skyrim's ISHDR path (Reinhard/Hejl-Burgess-Dawson), preserving the vanilla look while improving highlight handling on HDR displays."),
				T("feature.hdr_display.key_feature_3", "Configurable paper white and peak brightness.") } };
	};

	struct Settings
	{
		bool enableHDR = false;           // false = vanilla SDR, true = HDR output
		uint hdrPaperWhite = 203;         // Reference white brightness in nits for HDR
		uint hdrPeakNits = 800;           // Maximum display brightness in nits for HDR
		float hdrUIBrightness = 1.0f;     // UI brightness multiplier for HDR mode
		bool dontShowHDRWarning = false;  // User preference to suppress HDR warning popup
		bool hdrAutoDetected = false;     // Has auto-detection run at least once?
	};

	// SharedData::HDRData.w: menu/scene path for ISHDR; HDRSun uses w>0 to scale sun toward kMenuSunNits (see HDRSun.hlsli).
	static constexpr float kHdrMenuSceneGameplay = 0.f;
	static constexpr float kHdrMenuScenePauseOrMap = 0.58f;
	static constexpr float kHdrMenuSceneMainOrLoading = 1.f;

	Settings settings;
	std::mutex settingsMutex;

	/** @brief Resets HDR settings to defaults, auto-detecting HDR monitor status. */
	virtual void RestoreDefaultSettings() override;
	/** @brief Loads HDR settings from JSON and schedules auto-detection if needed. */
	virtual void LoadSettings(json& o_json) override;
	/** @brief Saves HDR settings to JSON (thread-safe). */
	virtual void SaveSettings(json& o_json) override;
	/** @brief Draws the ImGui settings UI for HDR enable, paper white, peak brightness, and UI brightness. */
	virtual void DrawSettings() override;

	/** @brief Enables the bUse64bitsHDRRenderTarget INI setting for float16 render targets. */
	virtual void DataLoaded() override;
	/** @brief Creates HDR, output, and UI textures, constant buffer, and upgrades LDR render targets. */
	virtual void SetupResources() override;
	/** @brief Releases cached HDR output and UI brightness compute shaders. */
	virtual void ClearShaderCache() override;
	/** @brief Installs HDR pipeline hooks when the Upscaling feature is not loaded. */
	virtual void PostPostLoad() override;

	/** @brief Returns the HDR shared data (enable flag, paper white, peak nits, menu scene encoding). */
	float4 GetSharedDataHDR() const;
	/** @brief Updates the HDR constant buffer with current settings and menu state. */
	void UpdateHDRData() const;
	/** @brief Sets the swap chain color space to HDR10 (PQ/BT.2020) or SDR (sRGB) based on settings. */
	void UpdateSwapChainColorSpace() const;

	/** @brief Redirects UI rendering to the separate UI texture for HDR compositing. */
	void BeginUIRendering();
	/** @brief Restores the original render target after UI rendering. */
	void EndUIRendering();
	/** @brief Returns true while UI is being rendered to the separate UI texture. */
	bool IsRenderingUI() const { return renderingUI; }

	/** @brief Redirects kFRAMEBUFFER to the float16 HDR texture so ISHDR can write values above 1.0. */
	void RedirectFramebuffer();
	/** @brief Restores the original kFRAMEBUFFER texture, SRV, and RTV after HDR rendering. */
	void RestoreFramebuffer();

	/** @brief Redirects kFRAMEBUFFER.RTV to the UI texture for vanilla UI capture. */
	void SetUIBuffer();
	/** @brief Clears the UI texture and restores the original kFRAMEBUFFER.RTV. */
	void ClearUIBuffer();
	/** @brief Returns true when non-FG HDR deferred compositing is active (composite after Present-hook mods). */
	bool UsesDeferredPresentComposite() const;
	/** @brief Aligns kFRAMEBUFFER.RTV with uiTexture for engine paths when ImGui has already bound the OM. */
	void SyncFramebufferUIRedirect();

	/** @brief Scales UI brightness in the Frame Gen UI buffer using the UI brightness compute shader. */
	void ScaleUIBrightnessForFG();
	/** @brief Returns true when the D3D12 UI buffer path should be used for frame generation. */
	bool ShouldUseD3D12UIBuffer();

	/** @brief Runs the HDR output compute shader to composite scene and UI, then copies to the back buffer. */
	void ApplyHDR();

	/** @brief Snapshots hdrTexture before the menu blur dirties it in place. */
	void SnapshotCleanScene();

	/** @brief Returns true when the snapshot was refreshed this frame; stale means hdrTexture wasn't blurred. */
	bool IsCleanSceneCaptureFresh() const;

	/** @brief Runs the HDR output transform on a clean scene (no UI buffer) and writes into outputTexture.
	 *  @param sceneSRV The clean HDR scene SRV.
	 *  @param sdrPreview If true, applies SDR preview transform instead of HDR output.
	 *  @return The composed output texture.
	 */
	ID3D11Texture2D* ComposeCleanCapture(ID3D11ShaderResourceView* sceneSRV, bool sdrPreview);

	/**
	 * @brief Returns a patched blend state with corrected alpha blending for HDR UI compositing.
	 * @param original The original blend state to potentially patch.
	 * @return The patched blend state, or the original if no patch is needed.
	 */
	ID3D11BlendState* GetPatchedAlphaBlendState(ID3D11BlendState* original);

	/**
	 * @brief Installs swap chain Present vtable hooks and OMSetBlendState detour for HDR pipeline.
	 * @param swapChain The DXGI swap chain to hook.
	 */
	static void InstallSwapChainPresentHooks(IDXGISwapChain* swapChain);
	/**
	 * @brief Handles the swap chain Present call, drawing ImGui overlay and running HDR compositing.
	 * @param swapChain The DXGI swap chain.
	 * @param syncInterval VSync interval.
	 * @param flags Present flags.
	 * @param presentChain The original Present call chain to invoke.
	 * @return The HRESULT from the final Present call.
	 */
	HRESULT HandleSwapChainPresent(
		IDXGISwapChain* swapChain,
		UINT syncInterval,
		UINT flags,
		const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& presentChain);

	/** @brief Returns true while the Present bottom hook is suppressed during deferred compositing. */
	bool IsPresentSuppressed() const { return presentSuppressed; }
	/**
	 * @brief Sets whether the Present bottom hook should be suppressed.
	 * @param value True to suppress, false to allow.
	 */
	void SetPresentSuppressed(bool value) { presentSuppressed = value; }

	/** @brief Releases all HDR textures, constant buffers, and restores LDR render targets. */
	void DestroyResources();

	XM_ALIGNED_STRUCT(16)
	HDRDataCB
	{
		float enableHDR;                 ///< 1.0 = HDR output with PQ, 0.0 = SDR output with gamma
		float paperWhite;                ///< Reference white brightness in nits for HDR
		float peakNits;                  ///< Maximum display brightness in nits for HDR
		float skipUIComposite;           ///< 1.0 = FG handles UI, skip our compositing
		float uiBrightness;              ///< UI brightness multiplier (Frame Gen compositing)
		float isSceneLinear;             ///< 1.0 = Linear Lighting active, scene already linear
		float pad0;                      ///< 1.0 = main menu/loading screen active
		float fgTweenMenuMidAlphaBoost;  ///< 1.0 = TweenMenu (pause) open — FG UIBrightnessCS mid-alpha boost only
		float previewSDR;                ///< 1.0 = emit sRGB SDR (crop preview) instead of PQ HDR10
		float pad1;
		float pad2;
		float pad3;
	};

	static_assert((sizeof(HDRDataCB) % 16) == 0, "CB size not padded correctly");

	// HDR data CB contents from current settings/game state (previewSDR=0).
	HDRDataCB BuildHDRData() const;

	ConstantBuffer* hdrDataCB = nullptr;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;
	Texture2D* uiTexture = nullptr;          // Separate UI render target for proper compositing
	Texture2D* cleanSceneCapture = nullptr;  // Pre-blur copy of hdrTexture for clean captures
	uint cleanSceneCaptureFrame = UINT32_MAX;  // frameCount when cleanSceneCapture was last refreshed

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	/** @brief Returns the HDR output compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetHDROutputCS();

	ID3D11ComputeShader* uiBrightnessCS = nullptr;
	/** @brief Returns the UI brightness scaling compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetUIBrightnessCS();

	/** @brief Detects whether Windows HDR is currently active on the swap chain's monitor. */
	static bool DetectHDR();
	static bool isHDRMonitor;            // Windows HDR is active (enabled in OS settings)
	static bool isHDRCapableMonitor;     // Monitor supports HDR but Windows HDR may be off
	static bool wasExclusiveFullscreen;  // EFS detected at swapchain creation; incompatible with HDR
	bool pendingAutoDetect = false;

	/** @brief Queries the DXGI output for the display's maximum luminance in nits. */
	float GetDisplayMaxLuminance() const;
	mutable float cachedDisplayMaxLuminance = 1000.0f;

	// Saved state for UI rendering redirection
	bool renderingUI = false;
	ID3D11RenderTargetView* savedRTV = nullptr;
	ID3D11DepthStencilView* savedDSV = nullptr;
	ID3D11RenderTargetView* savedFramebufferRTV = nullptr;  // Original kFRAMEBUFFER.RTV for restoration

	// Saved kFRAMEBUFFER state for HDR redirect (ISHDR writes to hdrTexture instead)
	ID3D11Texture2D* savedFramebufferTexture = nullptr;
	ID3D11ShaderResourceView* savedFramebufferSRV = nullptr;
	bool framebufferRedirected = false;

	/** @brief Upgrades post-tonemapping LDR render targets to R16G16B16A16_FLOAT for HDR values. */
	void UpgradeLDRRenderTargets();
	/** @brief Restores all upgraded render targets to their original format and releases upgraded resources. */
	void RestoreLDRRenderTargets();

	struct SavedRenderTarget
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11RenderTargetView* RTV = nullptr;
		ID3D11ShaderResourceView* SRV = nullptr;
		ID3D11UnorderedAccessView* UAV = nullptr;
	};

	std::vector<std::pair<RE::RENDER_TARGETS::RENDER_TARGET, SavedRenderTarget>> savedLDRTargets;

private:
	bool showHDRWarningPopup = false;
	bool pendingHDREnable = false;
	bool presentSuppressed = false;
	std::unordered_map<ID3D11BlendState*, winrt::com_ptr<ID3D11BlendState>> patchedBlendStateCache;

	HRESULT PresentToSwapChain(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
	void DrawImGuiForPresent(bool frameGenActive, bool hdrReady);
	void RunHDRBeforePresentChain(bool hdrReady);
	HRESULT RunPresentChainWithHDR(
		IDXGISwapChain* swapChain,
		UINT syncInterval,
		UINT flags,
		bool hdrReady,
		bool frameGenActive,
		const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& presentChain);

	struct D3D12UIBufferMode
	{
		bool useUIBuffer = false;
		bool useFallbackCopy = false;
	};

	D3D12UIBufferMode GetD3D12UIBufferMode();

	// Bind scene (t0), UI (t1, may be null), UAV (u0), CB (b0); dispatch the output CS; unbind.
	void DispatchHDROutput(ID3D11ShaderResourceView* sceneSRV, ID3D11ShaderResourceView* uiSRV, ID3D11UnorderedAccessView* uav);

	// True when FFX frame generation is actively compositing UI this frame.
	bool IsFGCompositingThisFrame() const;
};
