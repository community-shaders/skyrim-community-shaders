#include "Menu/FeatureUndo.h"

#include "CSEditor/SceneManager/SceneSettingsManager.h"
#include "Feature.h"
#include "Globals.h"
#include "State.h"

namespace FeatureUndo
{
	UndoHistory::Target MakeTarget(Feature* feature)
	{
		UndoHistory::Target target{ .key = "feature:" + feature->GetShortName(), .label = feature->GetName() };
		target.capture = [feature]() -> UndoHistory::SnapshotPtr {
			if (!feature->loaded)
				return nullptr;
			SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
			nlohmann::json values;
			feature->SaveSettings(values);
			return UndoHistory::Capture(std::move(values));
		};
		target.restore = [feature](const UndoHistory::Snapshot& snapshot) {
			SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
			nlohmann::json values = UndoHistory::Get<nlohmann::json>(snapshot);
			feature->LoadSettings(values);
			globals::state->MarkSettingsChanged();
			globals::state->InvalidateEditsUnderBaselines(feature);
		};
		return target;
	}
}
