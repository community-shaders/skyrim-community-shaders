#pragma once

#include <string>

#include "SceneSettingsManager.h"

/// Draws a feature's real settings UI bound to one scene context.
namespace SceneFeatureReplica
{
	/**
	 * @brief Draws one feature's DrawSettings with every control bound to the given scene context.
	 * @param featureShortName Feature to replicate, as returned by Feature::GetShortName().
	 * @param contextId Scene context every edit is written to.
	 * @param perPeriod False writes each edit to all six periods, which is the flat/global mode.
	 */
	void Draw(const std::string& featureShortName,
		const SceneSettingsManager::SceneContextId& contextId, bool perPeriod);
}
