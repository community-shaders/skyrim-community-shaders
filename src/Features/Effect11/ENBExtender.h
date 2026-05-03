#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Effects/Effect.h"

namespace ENBExtender
{
	struct UIDefineInfo
	{
		std::string defineName;
		std::string displayName;
		std::string group;
		std::string type;
		std::string value;
		std::string widget;
		std::string list;
		int intMin = 0;
		int intMax = 100;
		float floatMin = 0.0f;
		float floatMax = 1.0f;
		float floatStep = 0.01f;
		int ordering = 0;
	};

	// KIEFX encoding
	bool IsKIEFX(const std::string& content);
	std::string DecodeKIEFX(const std::string& content);

	// Source preprocessing
	void ConvertExtenderSyntax(std::string& content, const std::filesystem::path& enbseriesPath, const std::string& iniPath = "", const std::string& iniSection = "");
	void ConvertFxGroups(std::string& content);
	const std::vector<UIDefineInfo>& GetLastUIDefines();

	// UI variable processing
	void ParseSourceGroupScopes(const std::string& preprocessedSource, Effect& effect);
	bool ProcessExtenderStringVariable(ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc, std::vector<std::string>& groupStack, Effect& effect);
	void ApplyExtenderAnnotations(Effect::UIVariable& uiVar, ID3DX11EffectVariable* variable, const std::vector<std::string>& groupStack, Effect& effect);
	void InsertUIDefines(Effect& effect);
	void ParseTimePeriod(Effect::UIVariable& uiVar);

	// Merged UI rendering
	void RenderMergedEffectsList(Effect* effects[], int effectCount);
}
