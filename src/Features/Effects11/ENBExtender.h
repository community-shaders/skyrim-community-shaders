#pragma once

#include <d3dcompiler.h>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_set>
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
	void ExpandStringificationMacros(std::string& source);
	void StripStringifyDefines(std::string& source, std::vector<std::string>& macroNames);
	void ExpandStringifyMacros(std::string& source, const std::vector<std::string>& macroNames);

	// File preprocessing
	void StripLineDirectives(std::string& source);
	std::string InlineIncludes(const std::string& source,
		const std::filesystem::path& basePath,
		const std::string& iniPath,
		const std::string& iniSection,
		std::vector<std::filesystem::path>& includeDirs,
		std::unordered_set<std::string>& visited,
		std::vector<Effect::UIDefineInfo>& uiDefines,
		int depth = 0);

	class PresetInclude : public ID3DInclude
	{
	public:
		PresetInclude(const std::filesystem::path& a_basePath, std::vector<Effect::UIDefineInfo>& a_uiDefines, const std::string& a_iniPath = "", const std::string& a_iniSection = "");
		HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes) override;
		HRESULT __stdcall Close(LPCVOID pData) override;

		const std::vector<std::string>& GetStringifyMacros() const { return stringifyMacros; }

	private:
		std::filesystem::path basePath;
		std::vector<Effect::UIDefineInfo>& uiDefines;
		std::string iniPath;
		std::string iniSection;
		std::vector<std::filesystem::path> includeDirs;
		std::vector<std::string> stringifyMacros;
	};

	// UI variable processing
	void ParseSourceGroupScopes(const std::string& preprocessedSource, Effect& effect);
	bool ProcessExtenderStringVariable(ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc, std::vector<std::string>& groupStack, Effect& effect);
	bool CreateUIVariable(Effect::UIVariable& out, ID3DX11EffectVariable* variable, const D3DX11_EFFECT_VARIABLE_DESC& varDesc, const D3DX11_EFFECT_TYPE_DESC& typeDesc, const std::vector<std::string>& groupStack, Effect& effect);
	void InsertUIDefines(Effect& effect);

	// Post-load processing
	void LoadTechniqueDropdownMetadata(Effect& effect);
}
