#include "Bridge.h"

#include "../DlssEnhancerFeature.h"
#include "../DLSSperf.h"
#include "../../Globals.h"

// ── Route decision ───────────────────────────────────────────────

bool DlssEnhancer::Bridge::IsRouteActive()
{
	return globals::features::dlssEnhancer.IsActive();
	// IsActive() already checks:  globals::game::isVR
	//                            && globals::features::upscaling.streamline.featureDLSS
	//                            && enabledAtBoot
}

// ── Settings forwarding ──────────────────────────────────────────

uint32_t DlssEnhancer::Bridge::GetQualityMode()
{
	return globals::features::dlssEnhancer.settings.qualityMode;
}

uint32_t DlssEnhancer::Bridge::GetPresetDLSS()
{
	return globals::features::dlssEnhancer.settings.presetDLSS;
}

float DlssEnhancer::Bridge::GetSharpnessDLSS()
{
	return globals::features::dlssEnhancer.settings.sharpnessDLSS;
}

// ── Boot-time latches ────────────────────────────────────────────

void DlssEnhancer::Bridge::BootSequence()
{
	auto& enhancer = globals::features::dlssEnhancer;
	enhancer.LatchEnabled();
	enhancer.LatchQualityMode();
	if (!globals::features::dlssPerf.IsHookActive() && enhancer.settings.enablePerf) {
		enhancer.LatchPerfEnabled();
		globals::features::dlssPerf.InstallRenderTargetSizeHook();
	}
}

// ── Derived helpers ──────────────────────────────────────────────

void DlssEnhancer::Bridge::ComputeMvecScale(float& outX, float& outY)
{
	// Default: identity
	outX = 1.0f;
	outY = 1.0f;

	if (!IsRouteActive())
		return;

	auto& enhancer = globals::features::dlssEnhancer;
	auto mode = enhancer.GetDlssMode();
	auto leftUV = enhancer.subrectController.GetLeftEyeUV();
	bool isFullEye = (leftUV.w >= 0.999f && leftUV.h >= 0.999f);

	if (isFullEye)
		return;  // {1, 1}

	if (mode == DlssEnhancerFeature::DlssMode::kExtreme) {
		outX = (leftUV.w > 0.0f) ? (1.0f / (2.0f * leftUV.w)) : 1.0f;
		outY = (leftUV.h > 0.0f) ? (1.0f / leftUV.h) : 1.0f;
	} else {
		outX = (leftUV.w > 0.0f) ? (1.0f / leftUV.w) : 1.0f;
		outY = (leftUV.h > 0.0f) ? (1.0f / leftUV.h) : 1.0f;
	}
}

float DlssEnhancer::Bridge::GetRenderScaleForQuality(uint32_t qualityMode)
{
	return DlssEnhancerFeature::GetRenderScaleForQuality(qualityMode);
}

uint32_t DlssEnhancer::Bridge::GetQualityModeAtBoot()
{
	return globals::features::dlssEnhancer.GetQualityModeAtBoot();
}
