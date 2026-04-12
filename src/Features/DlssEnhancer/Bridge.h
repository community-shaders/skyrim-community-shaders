#pragma once

/// DlssEnhancer::Bridge — single point of contact between the DlssEnhancer
/// subsystem and the rest of Community Shaders (Upscaling, Streamline, Hooks).
///
/// All "is DlssEnhancer active?", "what settings should DLSS use?", and
/// "what happened at boot?" questions are answered here, so consumers never
/// need to #include DlssEnhancerFeature.h or poke globals::features::dlssEnhancer
/// directly.
///
/// IMPORTANT: when the DlssEnhancer route is inactive every query returns a
/// neutral / identity value — callers must still check IsRouteActive() and
/// fall back to their own settings when it returns false.

#include <cstdint>

struct sl_float2 { float x; float y; };  // forward-compatible with sl::float2

namespace DlssEnhancer::Bridge
{
	// ── Route decision ────────────────────────────────────────────
	/// True when VR + DLSS available + DlssEnhancer enabled-at-boot.
	bool IsRouteActive();

	// ── Settings forwarding (live values from DlssEnhancer GUI) ──
	uint32_t GetQualityMode();
	uint32_t GetPresetDLSS();
	float    GetSharpnessDLSS();

	// ── Boot-time latches ─────────────────────────────────────────
	/// Run once during BSShaderRenderTargets::Create.
	/// Latches enable / qualityMode, installs DLSSperf hook if requested.
	void BootSequence();

	// ── Derived helpers ───────────────────────────────────────────
	/// Compute motion-vector scale for Streamline constants.
	/// Returns {1,1} when route is inactive or subrect is full-eye.
	void ComputeMvecScale(float& outX, float& outY);

	/// Render-to-display scale for a quality mode index.
	/// Quality(1)=1.5, Balanced(2)=1.7, Performance(3)=2.0, UltraPerf(4)=3.0.
	float GetRenderScaleForQuality(uint32_t qualityMode);

	/// Quality mode latched at boot (for DLSSperf resolution calculation).
	uint32_t GetQualityModeAtBoot();
}
