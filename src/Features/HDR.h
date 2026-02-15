#pragma once

#include "Buffer.h"

#include <DirectXMath.h>
#include <dxgi.h>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class HDR
{
public:
	static HDR* GetSingleton()
	{
		static HDR singleton;
		return &singleton;
	}

	static std::string GetShortName() { return "High Dynamic Range"; }

	struct Settings
	{
		bool enableHDR = false;           // false = vanilla SDR, true = HDR output
		uint hdrPaperWhite = 203;         // Reference white brightness in nits for HDR
		uint hdrPeakNits = 1000;          // Maximum display brightness in nits for HDR
		float hdrUIBrightness = 2.3f;     // UI brightness multiplier for HDR mode (1.0x = 100 nits)
		bool dontShowHDRWarning = false;  // User preference to suppress HDR warning popup
	};

	Settings settings;
	std::mutex settingsMutex;

	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();

	void SetupResources();
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

	// Scale UI brightness in uiBufferWrapped for SDR Frame Gen (FidelityFX composites gamma UI over gamma scene)
	// For HDR, UI compositing is handled in ApplyHDR to ensure correct gamma-space blending
	void ScaleUIBrightnessForFG();

	void ApplyHDR();

	void DestroyResources();
	void ClearShaderCache();

	XM_ALIGNED_STRUCT(16)
	HDRDataCB
	{
		float enableHDR;        ///< 1.0 = HDR output with PQ, 0.0 = SDR output with gamma
		float paperWhite;       ///< Reference white brightness in nits for HDR
		float peakNits;         ///< Maximum display brightness in nits for HDR
		float skipUIComposite;  ///< 1.0 = FG handles UI, skip our compositing
		float uiBrightness;     ///< UI brightness multiplier
		float isSceneLinear;    ///< 1.0 = Linear Lighting active, scene already linear
		float pad0;             ///< Padding
		float pad1;             ///< Padding
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

	static bool DetectHDRDisplay();
	static bool isHDRMonitor;

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
};
