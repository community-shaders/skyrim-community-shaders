#pragma once

// ============================================================================
// DlssEnhancerFeature — VR DLSS enhancement feature (settings, GUI, persistence)
// ============================================================================
//
// Currently VR + DLSS only.  Non-VR / FSR users will see this disabled.
// Future contributors: extend IsRuntimeSupported() and the Streamline path
// for flat-screen or FSR support.
//
//  Key advantages over stock DLSS upscaling:
//   - DLSSperf: hooks BSOpenVR render target size so all engine RTs stay at
//     low RenderRes, while DLSS outputs to a private DisplayRes testTexture.
//     Eliminates UpscaleRT, reduces VRAM and bandwidth dramatically.
//     Game menus are no longer occluded by the upscaler.
//   - Subrect DLSS: only the user-selected region gets DLSS; periphery is
//     cheaply stretched.  Halves or more of the DLSS workload.
//     Works with VRS, Screenshot, and the upcoming lossless recording feature
//     via the shared Subrect module — use the same preset for best results.
//
//  DLSSperf integration note:
//   DLSSperf is installed at BSShaderRenderTargets::Create time (before any
//   frame renders).  It replaces GetRenderTargetSize so the engine allocates
//   smaller RTs.  The downscaled result is composited onto kMAIN via a
//   low-quality (bilinear) downsample — fast and visually acceptable.
//   After the post-process chain is rewritten to operate on testTexture
//   directly, this downsample step can be removed.
//
// ============================================================================

#include "Feature.h"
#include "Subrect/Subrect.h"

struct DlssEnhancerFeature : Feature
{
public:
	// DLSS execution mode for VR
	enum class DlssMode : uint
	{
		kDefault = 0,   // Per-eye isolation: 2 extra resource sets, 2 evaluates. Supports F/J/K/L/M.
		kFaster = 1,    // SBS viewport: tell SL to read subrect from SBS directly, no extra resources, 2 evaluates. J/K incompatible, only L/M/F.
		kExtreme = 2,   // Combined strip: both eyes' subrect merged into one long texture, 1 extra resource set, 1 evaluate. Supports F/J/K/L/M. (Not recommended)
	};

	// Stretch algorithm for DRS → full-eye background
	enum class StretchMode : uint
	{
		kBilinear = 0,  // Default bilinear sampling (clean upscale)
		kPoint = 1,     // Nearest-neighbor / point (cheapest, VRS-like broadcast)
		kGaussianBlur = 2,  // 3x3 Gaussian blur (soft periphery)
	};

	// Sharpening algorithm selection (extensible)
	enum class SharpenMode : uint
	{
		kRCAS = 0,   // AMD FidelityFX RCAS (current default)
		kNone = 1,   // No post-DLSS sharpening
	};

	// Subrect blend mode when writing DLSS output back over stretched background
	enum class SubrectBlendMode : uint
	{
		kHardCopy = 0,    // CopySubresourceRegion (no blending — sharp edge)
		kFeather = 1,     // smoothstep alpha ramp over N pixels
		kDither = 2,      // Blue-noise binary threshold in feather band
	};

	// Periphery AA algorithm applied after background stretch
	enum class PeripheryAAMode : uint
	{
		kNone = 0,            // No dedicated periphery AA
		kTemporalSmooth = 1,  // Motion-compensated temporal accumulation (anti-flicker)
	};

	struct Settings
	{
		uint enabled = 1;
		uint qualityMode = 4;            // Ultra Performance (maximum VRAM saving)
		uint streamlineLogLevel = 0;
		float sharpnessDLSS = 1.0f;
		uint presetDLSS = 0;
		uint dlssMode = (uint)DlssMode::kDefault;
		uint stretchMode = (uint)StretchMode::kGaussianBlur;
		float peripheryBlurRadius = 1.0f;
		uint sharpenMode = (uint)SharpenMode::kRCAS;
		uint enableMVDilation = 0;
		uint enableReactiveMask = 0;
		uint enableTransparencyMask = 0;
		uint peripheryAAMode = (uint)PeripheryAAMode::kTemporalSmooth;
		float peripheryTemporalAlpha = 0.16f;
		uint subrectBlendMode = (uint)SubrectBlendMode::kDither;
		float subrectFeatherWidth = 64.0f;
		float subrectDitherStrength = 1.0f;
		uint enablePerf = 1;
	};

	Settings settings;
	Subrect::Controller subrectController;

	std::string GetName() override { return "DLSSenhancer"; }
	std::string GetShortName() override { return "DLSSENHANCER"; }
	bool SupportsVR() override { return true; }
	bool IsCore() const override { return false; }
	std::string_view GetCategory() const override { return "Display"; }

	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"VR DLSS enhancer path with independent control surface.",
			{
				"Own DLSS quality/preset/sharpness settings",
				"3 DLSS modes: Default / Faster / Extreme",
				"Visual subrect cropping via drag editor",
				"Direct settings source for Streamline"
			}
		};
	}

	void DrawSettings() override;
	void SaveSettings(json& o_json) override;
	void LoadSettings(json& o_json) override;
	void RestoreDefaultSettings() override;
	void ClearShaderCache() override;

	bool IsRuntimeSupported() const;
	bool IsActive() const;

	// Main enable: latched at boot, change requires restart
	void LatchEnabled() { enabledAtBoot = (settings.enabled != 0); }

	// Quality mode: latched at boot for DLSSperf resolution calculation
	void LatchQualityMode() { qualityModeAtBoot = std::clamp(settings.qualityMode, 1u, 4u); }
	uint GetQualityModeAtBoot() const { return qualityModeAtBoot; }

	/// Render-to-display scale denominator for a given quality mode.
	/// Quality=1.5, Balanced=1.7, Performance=2.0, UltraPerformance=3.0.
	static float GetRenderScaleForQuality(uint qualityMode);

	// DLSSperf sub-feature: latched at first boot, change requires restart
	bool IsPerfEnabled() const { return perfEnabledAtBoot; }
	void LatchPerfEnabled() { perfEnabledAtBoot = (settings.enablePerf != 0); }

	DlssMode GetDlssMode() const { return (DlssMode)std::min(settings.dlssMode, 2u); }
	StretchMode GetStretchMode() const { return (StretchMode)std::min(settings.stretchMode, 2u); }
	SharpenMode GetSharpenMode() const { return (SharpenMode)std::min(settings.sharpenMode, 1u); }
	PeripheryAAMode GetPeripheryAAMode() const { return (PeripheryAAMode)std::min(settings.peripheryAAMode, 1u); }
	SubrectBlendMode GetSubrectBlendMode() const { return (SubrectBlendMode)std::min(settings.subrectBlendMode, 2u); }

	bool IsEncodeMVDilation() const { return settings.enableMVDilation != 0; }
	bool IsEncodeReactiveMask() const { return settings.enableReactiveMask != 0; }
	bool IsEncodeTransparencyMask() const { return settings.enableTransparencyMask != 0; }
	bool IsAnyEncodeEnabled() const { return IsEncodeMVDilation() || IsEncodeReactiveMask() || IsEncodeTransparencyMask(); }

private:
	bool enabledAtBoot = false;      // latched from settings.enabled at boot
	uint qualityModeAtBoot = 4;      // latched from settings.qualityMode at boot (default: UltraPerf)
	bool perfEnabledAtBoot = false;  // latched from settings.enablePerf during hook installation

	// Returns true if the current preset is compatible with the active DlssMode
	bool IsPresetCompatibleWithMode(uint presetIndex) const;
	// Clamp preset to a compatible value for the active mode
	void ClampPresetToMode();
	// Clamp all settings to valid ranges
	void ClampSettings();
};
