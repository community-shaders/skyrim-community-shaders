#include "SceneSettingsContextRules.h"

namespace SceneSettingsContextRules
{
	CopyGroupKey GetCopyGroupKey(const SceneSettingsManager::SettingIdentity& identity)
	{
		SceneSettingsManager::SettingEntry entry{
			.featureShortName = identity.featureShortName,
			.settingPath = identity.settingPath,
			.settingKey = identity.settingKey,
		};
		SceneSettingsManager::SettingControlInfo info;
		const bool aggregate = SceneSettingsManager::GetSettingControlInfo(entry, info) &&
		                       info.controlType != SceneSettingsManager::SettingControlType::Scalar;
		return { identity.featureShortName,
			aggregate ? info.settingPath : identity.settingPath,
			aggregate ? info.settingKey : identity.settingKey,
			aggregate ? info.componentStart : -1,
			aggregate ? info.componentCount : 0,
			aggregate ? info.controlType : SceneSettingsManager::SettingControlType::Scalar };
	}

	bool IsValidCopyConflictPolicy(SceneSettingsManager::CopyConflictPolicy policy)
	{
		return policy == SceneSettingsManager::CopyConflictPolicy::SkipExisting ||
		       policy == SceneSettingsManager::CopyConflictPolicy::OverwriteExisting ||
		       policy == SceneSettingsManager::CopyConflictPolicy::Cancel;
	}

	const char* GetCopyPeriodName(SceneSettingsManager::TimeOfDayPeriod period)
	{
		switch (period) {
		case SceneSettingsManager::TimeOfDayPeriod::Dawn:
			return T("feature.scene_manager.period.dawn", "Dawn");
		case SceneSettingsManager::TimeOfDayPeriod::Sunrise:
			return T("feature.scene_manager.period.sunrise", "Sunrise");
		case SceneSettingsManager::TimeOfDayPeriod::Day:
			return T("feature.scene_manager.period.day", "Day");
		case SceneSettingsManager::TimeOfDayPeriod::Sunset:
			return T("feature.scene_manager.period.sunset", "Sunset");
		case SceneSettingsManager::TimeOfDayPeriod::Dusk:
			return T("feature.scene_manager.period.dusk", "Dusk");
		case SceneSettingsManager::TimeOfDayPeriod::Night:
			return T("feature.scene_manager.period.night", "Night");
		default:
			return "";
		}
	}

	const char* GetCopyLocationTypeName(SceneSettingsManager::LocationTargetType type)
	{
		switch (type) {
		case SceneSettingsManager::LocationTargetType::Worldspace:
			return T("feature.scene_manager.location.target_worldspace", "Worldspace");
		case SceneSettingsManager::LocationTargetType::LocationType:
			return T("feature.scene_manager.location.target_location_type", "Location Type");
		case SceneSettingsManager::LocationTargetType::Region:
			return T("feature.scene_manager.location.target_region", "Region");
		case SceneSettingsManager::LocationTargetType::Location:
			return T("feature.scene_manager.location.target_location", "Location");
		case SceneSettingsManager::LocationTargetType::Cell:
			return T("feature.scene_manager.location.target_cell", "Cell");
		default:
			return "";
		}
	}

	bool EntryBelongsToContext(const SceneSettingsManager::SettingEntry& entry,
		const SceneSettingsManager::SceneContextId& context)
	{
		return entry.period == context.period;
	}

	SceneSettingsManager::SceneType ContextSceneType(SceneSettingsManager::SceneContextType type)
	{
		return type == SceneSettingsManager::SceneContextType::Interior ?
		           SceneSettingsManager::SceneType::InteriorOnly :
		           SceneSettingsManager::SceneType::TimeOfDay;
	}

	SceneContextRules GetSceneContextRules(const SceneSettingsManager::SceneContextId& context)
	{
		using SceneContextType = SceneSettingsManager::SceneContextType;
		using SceneType = SceneSettingsManager::SceneType;
		switch (context.type) {
		case SceneContextType::Interior:
			return { SceneType::InteriorOnly, false, "InteriorOnly" };
		case SceneContextType::Location:
			// A per-period location entry blends, so it takes the location whitelist's transitionable floats only.
			return { SceneType::Location, context.period != SceneSettingsManager::TimeOfDayPeriod::Count, "Location" };
		case SceneContextType::Weather:
			// Weather stores into the time-of-day layer but names itself in its own logs.
			return { SceneType::TimeOfDay, true, "Weather" };
		default:
			// A periodic layer blends across the period, so it only takes transitionable floats.
			return { SceneType::TimeOfDay, true, "TimeOfDay" };
		}
	}
}
