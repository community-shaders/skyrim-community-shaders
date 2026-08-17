#include "../Upscaling.h"
#include "PerfModeRestartState.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kPerfModeScaleThreshold = 0.99f;

	uint32_t ClampQualityMode(uint32_t a_qualityMode)
	{
		return std::min<uint32_t>(a_qualityMode, Upscaling::kQualityModeMaxIndex);
	}

	bool IsVendorMethod(Upscaling::UpscaleMethod a_method)
	{
		return a_method == Upscaling::UpscaleMethod::kDLSS ||
		       a_method == Upscaling::UpscaleMethod::kFSR;
	}
}

void Upscaling::PerfModeState::ResetBootLatch()
{
	boot = {};
	restartRequired = false;
	displaySizeChanged = false;
}

void Upscaling::PerfModeState::RestoreBootLatch(const BootSnapshot& a_snapshot)
{
	boot = a_snapshot;
	displaySizeChanged =
		boot.valid &&
		boot.active &&
		trueHMDEyeWidth != 0 &&
		trueHMDEyeHeight != 0 &&
		(boot.displayEyeWidth != trueHMDEyeWidth || boot.displayEyeHeight != trueHMDEyeHeight);
	restartRequired = displaySizeChanged;
}

void Upscaling::PerfModeState::RecordTrueHMDSize(uint32_t a_eyeWidth, uint32_t a_eyeHeight)
{
	if (!a_eyeWidth || !a_eyeHeight)
		return;

	trueHMDEyeWidth = a_eyeWidth;
	trueHMDEyeHeight = a_eyeHeight;
	if (boot.valid && boot.active) {
		displaySizeChanged = boot.displayEyeWidth != a_eyeWidth || boot.displayEyeHeight != a_eyeHeight;
		if (displaySizeChanged)
			restartRequired = true;
	}
}

bool Upscaling::PerfModeState::IsRequested(const Settings& a_settings) const
{
	return std::min<uint32_t>(a_settings.perfMode, 1u) != 0;
}

bool Upscaling::PerfModeState::IsEligible(const Settings& a_settings, UpscaleMethod a_method) const
{
	if (!REL::Module::IsVR())
		return false;

	if (!IsRequested(a_settings))
		return false;

	if (std::min<uint32_t>(a_settings.renderScaleMode, 1u) == 0)
		return false;

	if (!IsVendorMethod(a_method))
		return false;

	const uint32_t qualityMode = ClampQualityMode(a_settings.qualityMode);
	return Upscaling::GetQualityModeResolutionScale(qualityMode) < kPerfModeScaleThreshold;
}

// Hot-Envelope (experimental): does the requested quality render into a region
// that fits the targets already allocated at boot?
//
// Only ever RELAXES the restart condition, and only downward. A quality larger
// than the latched one still fails this and still forces the relatch, because
// the targets genuinely are too small for it. Returns false unless the feature
// is switched on, so the shipped path is unchanged.
//
// Compared in the same units the latch was created in - ScaleVRRenderDimension
// of the true HMD size - so this cannot disagree with EnsureBootLatch about what
// a quality's render size is.
bool Upscaling::PerfModeState::HotEnvelopeFits(const Settings& a_settings, uint32_t a_qualityMode) const
{
	if (!a_settings.vrHotEnvelope) {
		return false;
	}
	if (!boot.valid || !boot.active || !boot.renderEyeWidth || !boot.renderEyeHeight) {
		return false;
	}
	if (!trueHMDEyeWidth || !trueHMDEyeHeight) {
		return false;
	}

	const float scale = Upscaling::GetQualityModeResolutionScale(a_qualityMode);
	if (!std::isfinite(scale) || scale <= 0.0f) {
		return false;
	}

	const uint32_t wantWidth = ScaleVRRenderDimension(trueHMDEyeWidth, scale);
	const uint32_t wantHeight = ScaleVRRenderDimension(trueHMDEyeHeight, scale);
	const bool fits = wantWidth <= boot.renderEyeWidth && wantHeight <= boot.renderEyeHeight;

	// Called from five sites per change, so log only when the question or the
	// answer moves. Upscaling is a singleton and these are diagnostic state, not
	// logic - the return value does not depend on them.
	static uint32_t loggedQuality = 0xFFFFFFFFu;
	static uint32_t loggedBoot = 0xFFFFFFFFu;
	static int loggedFits = -1;
	if (a_qualityMode != loggedQuality || boot.qualityMode != loggedBoot || static_cast<int>(fits) != loggedFits) {
		loggedQuality = a_qualityMode;
		loggedBoot = boot.qualityMode;
		loggedFits = static_cast<int>(fits);
		logger::info(
			"[HotEnvelope][fits] quality {} wants {}x{}; envelope is quality {} at {}x{} -> {}",
			a_qualityMode,
			wantWidth,
			wantHeight,
			boot.qualityMode,
			boot.renderEyeWidth,
			boot.renderEyeHeight,
			fits ? "FITS (no relatch)" : "TOO LARGE (relatch required)");
	}

	return fits;
}

void Upscaling::PerfModeState::UpdateRestartRequiredState(const Settings& a_settings, UpscaleMethod a_method)
{
	const uint32_t qualityMode = ClampQualityMode(a_settings.qualityMode);
	const bool requestedNow = IsRequested(a_settings);
	const bool eligibleNow = IsEligible(a_settings, a_method);

	if (boot.valid) {
		// Once a boot latch exists, any setting drift that would change that latched RT sizing
		// means the user now has a restart-required delta.
		VRPerfModeRestartState::Refresh(
			restartRequired,
			VRPerfModeRestartState::ActiveBootContractInputs{
				.bootActive = boot.active,
				.requestedNow = requestedNow,
				.displaySizeChanged = displaySizeChanged,
				.eligibleNow = eligibleNow,
				.methodMatches = boot.method == a_method,
				.qualityModeMatches = boot.qualityMode == qualityMode,
				.renderSizeFitsAllocation = HotEnvelopeFits(a_settings, qualityMode),
			});
		return;
	}

	restartRequired = requestedNow && eligibleNow && (trueHMDEyeWidth != 0) && (trueHMDEyeHeight != 0);
}

bool Upscaling::PerfModeState::EnsureBootLatch(const Settings& a_settings, UpscaleMethod a_method, bool a_allowCreate, uint32_t a_generation)
{
	if (boot.valid) {
		UpdateRestartRequiredState(a_settings, a_method);
		return boot.active;
	}

	if (!a_allowCreate)
		UpdateRestartRequiredState(a_settings, a_method);
	else
		restartRequired = false;

	const uint32_t qualityMode = ClampQualityMode(a_settings.qualityMode);
	const bool eligibleNow = IsEligible(a_settings, a_method);
	if (!eligibleNow)
		return false;

	if (!trueHMDEyeWidth || !trueHMDEyeHeight)
		return false;

	if (!a_allowCreate)
		return false;

	const float renderScale = Upscaling::GetQualityModeResolutionScale(qualityMode);
	if (!std::isfinite(renderScale) || renderScale <= 0.0f || renderScale >= kPerfModeScaleThreshold)
		return false;

	boot.valid = true;
	boot.active = true;
	boot.method = a_method;
	boot.qualityMode = qualityMode;
	boot.dlssPreset = Upscaling::ClampDLSSPresetUInt(a_settings.dlssPreset);
	boot.renderScale = renderScale;
	boot.displayEyeWidth = trueHMDEyeWidth;
	boot.displayEyeHeight = trueHMDEyeHeight;
	boot.renderEyeWidth = ScaleVRRenderDimension(trueHMDEyeWidth, renderScale);
	boot.renderEyeHeight = ScaleVRRenderDimension(trueHMDEyeHeight, renderScale);
	boot.renderScaleEnabled = std::min<uint32_t>(a_settings.renderScaleMode, 1u) != 0;
	boot.perfModeEnabled = IsRequested(a_settings);
	boot.submitStageVendorAllowed = true;
	boot.generation = std::max(a_generation, 1u);
	restartRequired = false;
	displaySizeChanged = false;

	logger::info(
		"[VRRenderScale] Boot-latched {} quality {} at display {}x{} per eye -> render {}x{} per eye (generation {}).",
		magic_enum::enum_name(a_method),
		qualityMode,
		boot.displayEyeWidth,
		boot.displayEyeHeight,
		boot.renderEyeWidth,
		boot.renderEyeHeight,
		boot.generation);

	return true;
}

bool Upscaling::PerfModeState::IsActive(const Settings& a_settings, UpscaleMethod a_method) const
{
	(void)a_settings;
	(void)a_method;
	return boot.valid && boot.active;
}

bool Upscaling::PerfModeState::TryGetOpenVRRenderTargetSize(const Settings& a_settings, UpscaleMethod a_method, uint32_t& a_width, uint32_t& a_height, bool a_allowCreate, uint32_t a_generation)
{
	if (!EnsureBootLatch(a_settings, a_method, a_allowCreate, a_generation))
		return false;

	if (!boot.renderEyeWidth || !boot.renderEyeHeight)
		return false;

	a_width = boot.renderEyeWidth;
	a_height = boot.renderEyeHeight;
	return true;
}

void Upscaling::PerfModeState::SetSubmitStageVendorAllowed(bool a_allowed)
{
	if (boot.valid)
		boot.submitStageVendorAllowed = a_allowed;
}

float2 Upscaling::PerfModeState::GetDisplayScreenSize() const
{
	if (boot.valid && boot.displayEyeWidth && boot.displayEyeHeight)
		return { static_cast<float>(boot.displayEyeWidth * 2u), static_cast<float>(boot.displayEyeHeight) };

	if (trueHMDEyeWidth && trueHMDEyeHeight)
		return { static_cast<float>(trueHMDEyeWidth * 2u), static_cast<float>(trueHMDEyeHeight) };

	return { 0.0f, 0.0f };
}

float2 Upscaling::PerfModeState::GetRenderScreenSize() const
{
	if (!boot.valid || !boot.renderEyeWidth || !boot.renderEyeHeight)
		return { 0.0f, 0.0f };

	return { static_cast<float>(boot.renderEyeWidth * 2u), static_cast<float>(boot.renderEyeHeight) };
}

// Hot-Envelope: the region the ACTIVE quality should render into, which is a
// sub-rect of the latched allocation that GetRenderScreenSize returns.
//
// Returns the allocation itself whenever the feature is off or the quality does
// not fit, so callers can use it unconditionally and get today's behaviour.
float2 Upscaling::PerfModeState::GetActiveRenderScreenSize(const Settings& a_settings) const
{
	const float2 allocation = GetRenderScreenSize();
	const uint32_t qualityMode = ClampQualityMode(a_settings.qualityMode);
	if (!HotEnvelopeFits(a_settings, qualityMode)) {
		return allocation;
	}

	const float scale = Upscaling::GetQualityModeResolutionScale(qualityMode);
	const uint32_t width = ScaleVRRenderDimension(trueHMDEyeWidth, scale);
	const uint32_t height = ScaleVRRenderDimension(trueHMDEyeHeight, scale);
	if (!width || !height) {
		return allocation;
	}

	// Double-wide, matching GetRenderScreenSize' convention.
	return { static_cast<float>(width * 2u), static_cast<float>(height) };
}
