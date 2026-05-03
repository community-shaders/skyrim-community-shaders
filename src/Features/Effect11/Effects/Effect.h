#pragma once

#include <Effects11/d3dx11effect.h>
#include <filesystem>
#include <winrt/base.h>

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

	bool IsCompiled() const { return errors.empty(); }
	const std::vector<std::string>& GetErrors() const { return errors; }

	virtual void Execute() = 0;
	virtual void UpdateEffectVariables() {}

	// Virtual texture creation function for derived classes to override
	virtual void CreateEffectTextures() {}

	// UI System
	void RenderImGui();
	void LoadUIVariables();
	void UpdateUIVariables();

	// Technique selection
	std::string GetSelectedTechnique() const;

	// Pure virtual methods for derived classes to implement
	virtual std::string GetName() const = 0;

	struct TechniqueInfo
	{
		winrt::com_ptr<ID3DX11EffectTechnique> technique;
		std::string renderTargetName;
	};

	winrt::com_ptr<ID3DX11Effect> effect;
	std::unordered_map<std::string, std::vector<TechniqueInfo>> techniques;
	std::unordered_map<std::string, winrt::com_ptr<ID3DX11EffectVariable>> variables;

	std::unordered_map<std::string, TextureManager::Texture> effectTextureCache;
	std::unordered_map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> customTextureCache;

	// UI Variable System
	enum class UIVariableType
	{
		Float,
		Int,
		Bool,
		Color3,
		Color4
	};

	enum class UIWidgetType
	{
		Default,
		Spinner,
		Dropdown,
		Vector,
		Quality,
		Color
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

		// Color value storage
		float colorValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		// UI properties
		float floatMin = 0.0f;
		float floatMax = 1.0f;
		float floatStep = 0.01f;
		int intMin = 0;
		int intMax = 100;
		int ordering = 0;
		int sourceOrder = INT_MAX;
		bool isSeparator = false;
		bool isReadOnly = false;
		bool isTopLevel = false;
		bool isHidden = false;
		std::string uniqueName;
		std::string uiBinding;
		std::string uiBindingFile;
		std::string uiBindingProperty;
		std::string uiBindingCondition;
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

	// Group metadata (from first variable defining each group)
	std::unordered_map<std::string, std::string> groupDisplayNames;
	std::unordered_map<std::string, bool> groupDefaultOpen;
	std::unordered_map<std::string, int> groupOrdering;

	// Technique dropdown metadata (from first technique's annotations)
	std::string techniqueDropdownName = "Technique";
	std::string techniqueDropdownGroup;
	std::string techniqueDropdownGroupName;
	bool techniqueDropdownGroupOpen = true;
	bool techniqueDropdownVisible = true;
	bool techniqueDropdownTopLevel = false;
	int techniqueDropdownOrdering = 1;

	// Technique selection (legacy)
	std::vector<std::string> availableTechniques;

	// UI technique selection (indexed by uint, only includes annotated techniques)
	std::vector<UITechnique> uiTechniques;
	uint32_t selectedTechniqueIndex = 0;

	// Error tracking
	std::vector<std::string> errors;

	// Whether the source file was KIEFX-encoded (determines merged vs standalone UI)
	bool isKIEFX = false;

	// Pre-compiled group map from source preprocessing (variable name → group path)
	std::unordered_map<std::string, std::string> sourceGroupMap;
	// Source declaration order (variable name → declaration index)
	std::unordered_map<std::string, int> sourceOrderMap;

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

	static void UpdateSizeVariables(ID3DX11Effect* effect, uint32_t outputWidth, uint32_t outputHeight);

	// UI annotation helpers (public for ENBExtender access)
	std::string GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName);
	static std::string GetTechniqueAnnotation(ID3DX11EffectTechnique* technique, const std::string& annotationName);
	UIWidgetType ParseWidgetType(const std::string& widget);
	std::vector<std::string> ParseDropdownList(const std::string& list);

protected:
	ID3DX11EffectVariable* GetCachedVariable(const std::string& name);
	TextureManager::Texture* GetCachedCommonTexture(const std::string& name);
	void ClearVariableCache();

private:
	bool LoadFXFile();

	std::unordered_map<std::string, ID3DX11EffectVariable*> variableCache;
	std::unordered_map<std::string, TextureManager::Texture*> commonTexturePointerCache;

	void EnumerateAllVariables();

	void SetupCustomTextures();
	ID3D11ShaderResourceView* LoadTextureFromFile(const std::string& filename);
	std::string GetResourceNameFromVariable(ID3DX11EffectVariable* variable);

	void LoadTechniques();
	std::vector<std::string> GetBaseTechniqueNames();

	std::string GetRenderTargetFromTechnique(ID3DX11EffectTechnique* technique);
	std::string GetUINameFromTechnique(ID3DX11EffectTechnique* technique);
	void LoadUITechniques();
	TextureManager::Texture* GetEffectTexture(const std::string& name);
	ID3D11RenderTargetView* GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback);

	// UI Variable helpers (private)
	void LoadUIVariableValue(UIVariable& uiVar);
	void LoadVariableFromString(UIVariable& uiVar, const std::string& value);
};