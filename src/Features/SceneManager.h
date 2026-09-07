#pragma once

#include "Feature.h"
#include "SceneSettingsManager.h"

struct SceneManager : Feature, SceneSettingsManager
{
	std::string GetName() override { return "Scene Manager"; }
	std::string GetDisplayName() override { return T("feature.scene_manager.name", "Scene Manager"); }
	std::string GetShortName() override { return "SceneManager"; }
	std::string_view GetCategory() const override { return FeatureCategories::kUtility; }
	bool IsCore() const override { return true; }
	bool IsAlwaysEnabled() const override { return true; }
	bool IsDisabledByDefault() const override { return false; }
	bool UsesMainSettings() const override { return false; }

	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;
	/// Debug view of the resolver's live state until the real Scene Manager UI lands.
	void DrawSettings() override;
	void SetupResources() override;
	void DataLoaded() override;
	void Update();
};
