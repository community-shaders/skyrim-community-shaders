#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "Effects/Effect.h"

namespace ENBExtender
{
	// Shared helpers
	int SafeStoi(const std::string& s, int fallback = 0);
	float SafeStof(const std::string& s, float fallback = 0.0f);

	// KIEFX encoding
	bool IsKIEFX(const std::string& content);
	std::string DecodeKIEFX(const std::string& content);

	// Source preprocessing
	void ConvertExtenderSyntax(std::string& content, const std::filesystem::path& enbseriesPath, std::vector<Effect::UIDefineInfo>& uiDefines, const std::string& iniPath = "", const std::string& iniSection = "");
	void ConvertFxGroups(std::string& content);

	// UI variable processing
	void ParseSourceGroupScopes(const std::string& preprocessedSource, Effect& effect);
	bool ProcessExtenderStringVariable(ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc, std::vector<std::string>& groupStack, Effect& effect);
	void ApplyExtenderAnnotations(Effect::UIVariable& uiVar, ID3DX11EffectVariable* variable, const std::vector<std::string>& groupStack, Effect& effect);
	void InsertUIDefines(Effect& effect);
	void ParseTimePeriod(Effect::UIVariable& uiVar);

	// Post-load processing
	void RecoverGroupsFromINI(Effect& effect, const std::filesystem::path& enbseriesPath);
	void LoadTechniqueDropdownMetadata(Effect& effect);
	void ApplyTimeOfDayInterpolation(Effect& effect);

	// UI rendering
	void RenderUI(std::span<Effect*> effects);
	void RenderUI(Effect& effect);
}
