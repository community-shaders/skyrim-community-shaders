#include "WeatherTransition.h"

#include "Globals.h"
#include "SceneSettingsManager.h"

#include <array>
#include <bit>
#include <cassert>

namespace
{
	/// Raised by Sky::SetWeather to rush the running transition; CommonLib leaves it unnamed.
	constexpr auto kAccelerateFlag = static_cast<RE::Sky::Flags>(1 << 3);

	/// GMSTs bounding a weather's transition in game hours, picked between by its transDelta.
	constexpr std::array kDurationSettingNames = { "fWeatherTransMin", "fWeatherTransMax" };

	struct DurationSetting
	{
		RE::Setting* setting = nullptr;
		float baseline = 0.0f;
	};

	std::array<DurationSetting, kDurationSettingNames.size()> durationSettings;
	bool durationBaselineCaptured = false;
	float appliedDurationScale = SceneSettingsManager::kDefaultWeatherTransitionScale;

	float lastRawPct = 1.0f;
	float lastEasedPct = 1.0f;
	bool accelerateSnapshotRestored = false;

	/// The engine keeps the blend at the moment acceleration began in CommonLib's lastMoonPhaseUpdate slot.
	float GetAccelerateSnapshot(const RE::Sky* sky) { return std::bit_cast<float>(sky->lastMoonPhaseUpdate); }

	/** @brief Overwrites the accelerate start blend. */
	void SetAccelerateSnapshot(RE::Sky* sky, float pct) { sky->lastMoonPhaseUpdate = std::bit_cast<std::uint32_t>(pct); }

	/// Captured on first use rather than at load, as plugins may override the GMSTs.
	void CaptureDurationBaseline()
	{
		durationBaselineCaptured = true;
		auto* collection = globals::game::gameSettingCollection;
		for (size_t index = 0; index < kDurationSettingNames.size(); ++index) {
			auto* setting = collection ? collection->GetSetting(kDurationSettingNames[index]) : nullptr;
			if (!setting || setting->GetType() != RE::Setting::Type::kFloat) {
				logger::warn("[SceneSettings] {} is unavailable; weather transition speed cannot change it", kDurationSettingNames[index]);
				continue;
			}
			durationSettings[index] = { setting, setting->GetFloat() };
		}
	}

	/** @brief Rescales the duration GMSTs from their baseline when the user's scale changed. */
	void SyncDurationScale(float scale)
	{
		if (scale == appliedDurationScale)
			return;
		if (!durationBaselineCaptured)
			CaptureDurationBaseline();
		appliedDurationScale = scale;
		for (const auto& [setting, baseline] : durationSettings)
			if (setting)
				setting->SetFloat(baseline * scale);
	}
}

namespace WeatherTransition
{
	void BeforeUpdate(RE::Sky* sky)
	{
		auto* manager = globals::sceneSettingsManager;
		assert(manager && "the scene manager is created before the game loop starts");
		SyncDurationScale(manager->GetTransitionControl(SceneSettingsManager::TransitionControl::WeatherScale));

		// The engine returns early without recomputing when there is no climate, so it must not keep our eased value.
		if (sky->currentWeatherPct == lastEasedPct)
			sky->currentWeatherPct = lastRawPct;

		// SetWeather snapshots the blend we eased between frames; the engine resumes from it on its linear scale.
		const bool accelerating = sky->flags.any(kAccelerateFlag);
		if (accelerating && !accelerateSnapshotRestored && GetAccelerateSnapshot(sky) == lastEasedPct)
			SetAccelerateSnapshot(sky, lastRawPct);
		accelerateSnapshotRestored = accelerating;
	}

	void AfterUpdate(RE::Sky* sky)
	{
		lastRawPct = sky->currentWeatherPct;
		lastEasedPct = globals::sceneSettingsManager->EaseTransition(lastRawPct);
		sky->currentWeatherPct = lastEasedPct;
	}
}
