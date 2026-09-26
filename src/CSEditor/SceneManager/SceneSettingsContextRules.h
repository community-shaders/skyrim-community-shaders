#pragma once

#include "SceneSettingsManager.h"

#include <tuple>

/// Shared rules that decide which layer a scene context writes to and how its entries group.
namespace SceneSettingsContextRules
{
	/// Identity of the logical control a stored setting belongs to. Aggregate members share one key.
	using CopyGroupKey = std::tuple<std::string, std::vector<std::string>, std::string,
		std::int8_t, std::uint8_t, SceneSettingsManager::SettingControlType>;

	CopyGroupKey GetCopyGroupKey(const SceneSettingsManager::SettingIdentity& identity);

	bool IsValidCopyConflictPolicy(SceneSettingsManager::CopyConflictPolicy policy);

	const char* GetCopyPeriodName(SceneSettingsManager::TimeOfDayPeriod period);

	const char* GetCopyLocationTypeName(SceneSettingsManager::LocationTargetType type);

	/** @brief A context addresses one saved set: its period, or Count for the flat set. */
	bool EntryBelongsToContext(const SceneSettingsManager::SettingEntry& entry,
		const SceneSettingsManager::SceneContextId& context);

	/// The stored SceneType behind a non-weather, non-location context.
	SceneSettingsManager::SceneType ContextSceneType(SceneSettingsManager::SceneContextType type);

	/// What a context validates an entry against: the layer it lands in, whether that layer blends,
	/// and the name its validation logs carry.
	struct SceneContextRules
	{
		SceneSettingsManager::SceneType sceneType;
		bool requireNumeric;
		const char* label;
	};

	/// One rule set per context, so an add, a copy and a tombstone all judge an address alike.
	SceneContextRules GetSceneContextRules(const SceneSettingsManager::SceneContextId& context);
}
