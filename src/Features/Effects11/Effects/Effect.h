#pragma once

#include <Effects11/d3dx11effect.h>
#include <filesystem>
#include <winrt/base.h>

#include "Profiler.h"
#include "../TextureManager.h"

class Effect
{
public:
	Effect() = default;
	virtual ~Effect() = default;

	// UI technique structure (defined early for use in method declarations)
	struct UITechnique
	{
		std::string techniqueName;  // Actual technique name
		std::string displayName;    // UIName annotation
	};

	// Settings methods
	bool Load();
	void Save();

	// Effect lifecycle
	virtual bool Apply();   // Clear resources, load settings, recompile, create resources
	virtual void Unload();  // Clear all resources

	bool IsCompiled() const { return filePresent && errors.empty(); }
	bool IsFilePresent() const { return filePresent; }
	const std::vector<std::string>& GetErrors() const { return errors; }

	virtual void Execute() = 0;
	virtual void UpdateEffectVariables() {}

	// Virtual texture creation function for derived classes to override
	virtual void CreateEffectTextures() {}

	// UI System
	virtual void RenderImGui();
	void LoadUIVariables();
	void UpdateUIVariables();

	// Technique selection
	std::string GetSelectedTechnique() const;

	// Pure virtual methods for derived classes to implement
	virtual std::string GetName() const = 0;
	virtual bool IsRequired() const { return false; }

	struct TechniqueBinding
	{
		std::string variableName;
		bool inverted = false;
		int resolvedIndex = -1;
	};

	struct TechniqueInfo
	{
		winrt::com_ptr<ID3DX11EffectTechnique> technique;
		std::string renderTargetName;
		std::vector<TechniqueBinding> bindings;
		uint32_t passCount = 0;
	};

	Profiler* profiler = nullptr;

	winrt::com_ptr<ID3DX11Effect> effect;
	std::unordered_map<std::string, std::vector<TechniqueInfo>> techniques;
	std::unordered_map<std::string, winrt::com_ptr<ID3DX11EffectVariable>> variables;

	std::unordered_map<std::string, TextureManager::Texture> effectTextureCache;
	std::unordered_map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> customTextureCache;

	// UI Variable System
	enum class UIVariableType
	{
		Float,
		Float2,
		Int,
		Bool,
		Float3,
		Float4
	};

	enum class UIWidgetType
	{
		Default,
		Dropdown,
		Vector,
		Quality,
		Color
	};

	struct UIBindingInfo
	{
		std::string target;
		std::string file;
		std::string property;
		std::string condition;
		bool inverted = false;
	};

	struct UIVariable
	{
		UIVariableType type;
		UIWidgetType widgetType;
		std::string name;
		std::string displayName;
		std::string group;
		winrt::com_ptr<ID3DX11EffectVariable> effectVariable;

		// Value storage
		union
		{
			float floatValue;
			int intValue;
			bool boolValue;
		};

		float vectorValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		// UI properties
		float floatMin = 0.0f;
		float floatMax = 1.0f;
		int intMin = 0;
		int intMax = 100;
		int ordering = 0;
		int sourceOrder = INT_MAX;
		bool isLabel = false;
		bool isReadOnly = false;
		bool isDefine = false;
		bool isHidden = false;
		bool isTopLevel = false;
		std::string uniqueName;
		std::vector<UIBindingInfo> uiBindings;
		bool ignorePerfMode = false;
		bool isWeatherString = false;
		bool isWeatherOnlyString = false;

		// Weather separation ("ExteriorWeather" or "Weather")
		std::string separation;
		// Parsed time period from UIName (e.g. "Dawn", "Day", "Night", "Interior")
		std::string timePeriod;

		std::vector<std::string> dropdownItems;
	};

	std::vector<UIVariable> uiVariables;

	struct GroupMeta
	{
		std::string displayName;
		int ordering = 0;
		bool defaultOpen = false;
		bool hasOrdering = false;
		bool isTopLevel = false;
	};
	std::unordered_map<std::string, GroupMeta> groupMeta;

	struct TechniqueDropdownMeta
	{
		std::string name = "Technique";
		std::string group;
		std::string groupName;
		bool groupOpen = false;
		bool visible = true;
		bool topLevel = false;
		int ordering = 1;
	};
	TechniqueDropdownMeta techniqueDropdown;

	// UI technique selection (indexed by uint, only includes annotated techniques)
	std::vector<UITechnique> uiTechniques;
	uint32_t selectedTechniqueIndex = 0;

	// Error tracking
	bool filePresent = false;
	std::vector<std::string> errors;

	// Source-parsed group map and declaration order (compiled effect reorders variable types)
	std::unordered_map<std::string, std::string> sourceGroupMap;
	std::unordered_map<std::string, int> sourceOrderMap;

	// Separators stored separately from UI variables (first-class tree nodes)
	struct SeparatorInfo
	{
		std::string name;
		std::string group;
		int sourceOrder = INT_MAX;
		int ordering = 0;
		bool hasOrdering = false;
		bool isTopLevel = false;
	};
	std::vector<SeparatorInfo> separators;

	// Extern bindings (camera matrices etc. updated per-frame)
	struct ExternBindingInfo
	{
		std::string bindingName;
		winrt::com_ptr<ID3DX11EffectVariable> variable;
	};
	std::vector<ExternBindingInfo> externBindings;

	struct UIDefineInfo
	{
		std::string defineName;
		std::string displayName;
		std::string group;
		std::string type;
		std::string value;
		std::string widget;
		std::string list;
		std::string annotations;
		int intMin = 0;
		int intMax = 100;
		float floatMin = 0.0f;
		float floatMax = 1.0f;
		int ordering = 0;
		bool hasExplicitOrdering = false;
	};

	std::vector<UIDefineInfo> uiDefines;

	// INI file modification time tracking to skip redundant reloads
	std::filesystem::file_time_type lastIniWriteTime{};

	struct TechniqueSequenceResult
	{
		bool executed = false;
		bool inOutput = false;
	};

	// Execute a technique sequence with ping-pong rendering
	TechniqueSequenceResult ExecuteTechniqueSequence(const std::string& a_baseTechniqueName, ID3D11ShaderResourceView* a_input, TextureManager::Texture& a_output, TextureManager::Texture& a_temp);

	// Execute a single technique
	void ExecuteTechnique(const std::string& techniqueName, TextureManager::Texture& output);

	// Allow EffectManager to setup common variables
	ID3DX11Effect* GetEffect() const { return effect.get(); }

	// Helper function to set shader resource variables (non-static version for this effect)
	bool SetShaderResourceVariable(const std::string& variableName, ID3D11ShaderResourceView* resource);

	// Texture creation helper
	static TextureManager::Texture CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName);

	// Static helper functions for any effect
	static bool SetShaderResourceVariable(ID3DX11Effect* effect, const std::string& variableName, ID3D11ShaderResourceView* resource);
	static bool SetVectorVariable(ID3DX11Effect* effect, const std::string& variableName, const void* data, uint32_t size);

	// Helper function for safe vector variable access
	bool SetVectorVariable(const std::string& variableName, const void* data, uint32_t size);

	void UpdateExternBindings();
	void RenderPasses(ID3DX11EffectTechnique* technique, ID3D11RenderTargetView* outputRTV, uint32_t passOffset = 0);

	// UI annotation helpers (public for ENBExtender access)
	std::string GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName);
	static std::string GetTechniqueAnnotation(ID3DX11EffectTechnique* technique, const std::string& annotationName);
	static std::string GetGroupAnnotation(ID3DX11EffectGroup* group, const std::string& annotationName);

	ID3DX11EffectVariable* GetCachedVariable(const std::string& name);
	TextureManager::Texture* GetCachedCommonTexture(const std::string& name);
	void ClearVariableCache();

	virtual bool IsTechniqueEnabled(TechniqueInfo&) { return true; }

protected:
	static bool IsPerComponentVector(const UIVariable& uiVar);
	std::string GetVariableIniKey(const UIVariable& uiVar);

private:
	bool LoadFXFile();

	std::unordered_map<std::string, ID3DX11EffectVariable*> variableCache;
	std::unordered_map<std::string, TextureManager::Texture*> commonTexturePointerCache;
	std::unordered_map<ID3D11RenderTargetView*, std::pair<uint32_t, uint32_t>> rtvDimensionCache;

	void EnumerateAllVariables();

	void SetupCustomTextures();
	ID3D11ShaderResourceView* LoadTextureFromFile(const std::string& filename);

	void LoadTechniques();
	void LoadUITechniques();
	ID3D11RenderTargetView* GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback);

	void LoadUIVariableValue(UIVariable& uiVar);
	void LoadVariableFromString(UIVariable& uiVar, const std::string& value);
};