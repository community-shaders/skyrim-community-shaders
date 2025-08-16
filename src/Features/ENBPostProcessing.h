#pragma once

#include "Features/ENBPostProcessing/Effect11.h"
#include <unordered_map>

struct ENBPostProcessing : Feature
{
public:
	struct Settings
	{
		bool Enabled = false;
		std::string EffectPath = "";
		std::string SelectedTechnique = "";
		std::unordered_map<std::string, float> FloatVariables;
		std::unordered_map<std::string, int> IntVariables;
		std::unordered_map<std::string, bool> BoolVariables;
	};

	Settings settings;

	virtual inline std::string GetName() override { return "ENB Post Processing"; }
	virtual inline std::string GetShortName() override { return "ENBPostProcessing"; }
	virtual std::string_view GetCategory() const override { return "Post-Processing"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"ENB Post Processing provides a framework for loading and executing ENBSeries-compatible FX effect files.\n"
			"This allows for advanced post-processing effects and visual enhancements using DirectX 11 Effect (.fx) files.",
			{ "ENBSeries-compatible FX support",
				"DirectX 11 Effect file loading",
				"Advanced post-processing pipeline",
				"Custom technique execution",
				"Dynamic UI variable system" }
		};
	}

	virtual void SaveSettings(json&) override;
	virtual void LoadSettings(json&) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void PostPostLoad() override;

	Effect11& GetEffect11() { return effect11; }

private:
	Effect11 effect11;
};