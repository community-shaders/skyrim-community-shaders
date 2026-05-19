
/*
* This file defines a new feature template for Community Shader.
* Copy the .h and .cpp files to src/Features and rename them to your feature's name.
* Replace all NewFeature occurances in both files as well, and change the metadata accordingly.
* Don't forget to add the feature singleton to src/Feature.cpp, Globals.h & Globals.cpp
* and copy and rename the "New Feature" folder and contents to features/ so it gets registered.
*
* The naming and coding style are adapted to my personal practice,
* but we don't really have a strict, solidified guideline on that.
* So take your liberties within reason.
*
* Cheers,
* ProfJack
* 2025-06-28
*/

#pragma once

#include "OverlayFeature.h"

struct SceneGraphExplorer : public OverlayFeature
{
	////////////////////////////////////////////////// Boilerplate
	// Metadata
	virtual inline std::string GetName() override { return "Scene GraphExplorer"; }
	virtual inline std::string GetShortName() override { return "SceneGraphExplorer"; }
	virtual inline std::string_view GetCategory() const override { return "Other"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"This is a terse description.",
			{
				"This is a subfeature.",
				"This is another subfeature.",
				"Cheese.",
			}
		};
	}

	// Functionality
	virtual bool inline SupportsVR() override { return true; }
	virtual inline std::string_view GetShaderDefineName() override { return "SHADER_MACRO"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; };

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	// Overlay
	virtual bool IsInMenu() const override { return true; }
	virtual bool IsOverlayVisible() const override { return settings.Enabled; };
	virtual void DrawOverlay() override;

	void DrawObject(RE::NiAVObject* object, bool root = false);

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool Enabled = false;
	} settings;
};