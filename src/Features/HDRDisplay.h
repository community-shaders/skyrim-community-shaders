#pragma once

#include "Buffer.h"
#include "Feature.h"

#include <DirectXMath.h>
#include <dxgi.h>
#include <mutex>

struct HDRDisplay : public Feature
{
	virtual inline std::string GetName() override { return "HDR Display"; }
	virtual inline std::string GetShortName() override { return "HDRDisplay"; }
	virtual inline std::string_view GetCategory() const override { return "Display"; }
	virtual inline bool SupportsVR() override { return false; }
	virtual inline bool IsCore() const override { return false; }

	virtual inline std::string_view GetShaderDefineName() override { return "HDR_OUTPUT"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return shaderType == RE::BSShader::Type::ImageSpace;
	}

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Real High Dynamic Range output for HDR displays.",
			{
				"HDR10 output support (10-bit) with upgraded HDR buffers (16-Bit), and fully unclamped rendering pipeline for true HDR values.",
				"HDR-aware tonemapping based on Skyrim's ISHDR path (Reinhard/Hejl-Burgess-Dawson), preserving the vanilla look while improving highlight handling on HDR displays.",
				"Configurable paper white and peak brightness.",
			}
		};
	}

	struct Settings
	{
		bool enableHDR = false;           // false = vanilla SDR, true = HDR output
		uint hdrPaperWhite = 203;         // Reference white brightness in nits for HDR
		uint hdrPeakNits = 800;           // Maximum display brightness in nits for HDR
		float hdrUIBrightness = 1.0f;     // UI brightness multiplier for HDR mode
		bool dontShowHDRWarning = false;  // User preference to suppress HDR warning popup
		bool hdrAutoDetected = false;     // Has auto-detection run at least once?
	};

	// SharedData::HDRData fourth component: menu/scene path for ISHDR + sun scale (HLSL must match).
	static constexpr float kHdrMenuSceneGameplay = 0.f;
	static constexpr float kHdrMenuScenePauseOrMap = 0.58f;
	static constexpr float kHdrMenuSceneMainOrLoading = 1.f;

	Settings settings;
	std::mutex settingsMutex;

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	virtual void DataLoaded() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	virtual void PostPostLoad() override;

	float4 GetSharedDataHDR() const;
	void UpdateHDRData() const;
	void UpdateSwapChainColorSpace() const;

	// UI rendering - redirects UI to separate target for proper compositing
	void BeginUIRendering();
	void EndUIRendering();
	bool IsRenderingUI() const { return renderingUI; }

	// Redirect kFRAMEBUFFER to hdrTexture (float16) so ISHDR writes HDR values >1.0
	void RedirectFramebuffer();
	void RestoreFramebuffer();

	// Frame Gen style UI buffer - redirects kFRAMEBUFFER.RTV for vanilla UI capture
	void SetUIBuffer();
	void ClearUIBuffer();

	// Scale UI brightness in uiBufferWrapped for SDR Frame Gen
	void ScaleUIBrightnessForFG();

	void ApplyHDR();

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
	};

	static_assert((sizeof(HDRDataCB) % 16) == 0, "CB size not padded correctly");

	ConstantBuffer* hdrDataCB = nullptr;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;
	Texture2D* uiTexture = nullptr;  // Separate UI render target for proper compositing

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	ID3D11ComputeShader* GetHDROutputCS();

	ID3D11ComputeShader* uiBrightnessCS = nullptr;
	ID3D11ComputeShader* GetUIBrightnessCS();

	static bool DetectHDR();             // Returns true if Windows HDR is currently active (enabled in OS settings)
	static bool isHDRMonitor;            // Windows HDR is active (enabled in OS settings)
	static bool isHDRCapableMonitor;     // Monitor supports HDR but Windows HDR may be off
	static bool wasExclusiveFullscreen;  // EFS detected at swapchain creation; incompatible with HDR
	bool pendingAutoDetect = false;

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

	// Upgraded LDR render targets (post-tonemapping targets need float16 for HDR values)
	void UpgradeLDRRenderTargets();
	void RestoreLDRRenderTargets();

	struct SavedRenderTarget
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11RenderTargetView* RTV = nullptr;
		ID3D11ShaderResourceView* SRV = nullptr;
	};

	std::vector<std::pair<RE::RENDER_TARGETS::RENDER_TARGET, SavedRenderTarget>> savedLDRTargets;

private:
	bool showHDRWarningPopup = false;
	bool pendingHDREnable = false;

	// True when FFX frame generation is actively compositing UI this frame.
	bool IsFGCompositingThisFrame() const;
};
