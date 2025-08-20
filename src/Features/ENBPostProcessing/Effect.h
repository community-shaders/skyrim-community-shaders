#pragma once

#include <Effects11/d3dx11effect.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>
// Forward declarations
class EffectManager;

using Microsoft::WRL::ComPtr;

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

	// UI System
	void RenderImGui();
	void LoadUIVariables();
	void UpdateUIVariables();

	// Technique selection (legacy)
	std::string GetSelectedTechnique() const;
	const std::vector<std::string>& GetAvailableTechniques() const { return availableTechniques; }

	// UI technique selection (indexed access)
	uint32_t GetSelectedTechniqueIndex() const { return selectedTechniqueIndex; }
	void SetSelectedTechniqueIndex(uint32_t index);
	const std::vector<UITechnique>& GetUITechniques() const { return uiTechniques; }
	std::string GetTechniqueNameByIndex(uint32_t index) const;

	// Pure virtual methods for derived classes to implement
	virtual LPCSTR GetSourceTexture() const = 0;
	virtual std::string GetName() const = 0;

	struct TechniqueInfo
	{
		ComPtr<ID3DX11EffectTechnique> technique;
		std::string renderTargetName;
	};

	ComPtr<ID3DX11Effect> effect;
	std::unordered_map<std::string, std::vector<TechniqueInfo>> techniques;
	std::unordered_map<std::string, ComPtr<ID3DX11EffectVariable>> variables;

	struct Texture
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
	};

	std::unordered_map<std::string, Texture> effectTextureCache;
	std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> customTextureCache;

	// UI Variable System
	enum class UIVariableType
	{
		Float,
		Int,
		Bool
	};

	enum class UIWidgetType
	{
		Default,
		Spinner,
		Dropdown
	};

	struct UIVariable
	{
		UIVariableType type;
		UIWidgetType widgetType;
		LPCSTR name;
		std::string displayName;
		ComPtr<ID3DX11EffectVariable> effectVariable;

		// Value storage
		union
		{
			float floatValue;
			int intValue;
			bool boolValue;
		};

		// UI properties
		float floatMin = 0.0f;
		float floatMax = 1.0f;
		float floatStep = 0.01f;
		int intMin = 0;
		int intMax = 100;
		std::vector<std::string> dropdownItems;
	};

	std::vector<UIVariable> uiVariables;

	// Technique selection (legacy)
	std::vector<std::string> availableTechniques;

	// UI technique selection (indexed by uint, only includes annotated techniques)
	std::vector<UITechnique> uiTechniques;
	uint32_t selectedTechniqueIndex = 0;

	// Error tracking
	std::vector<std::string> errors;

	// Execute a technique sequence with ping-pong rendering
	void ExecuteTechniqueSequence(const std::string& a_baseTechniqueName, Texture& a_swap, Texture& a_output);

	// Execute a single technique
	void ExecuteTechnique(const std::string& techniqueName, Texture& input, Texture& output);

	// Allow EffectManager to setup common variables
	ID3DX11Effect* GetEffect() const { return effect.Get(); }

	// Helper function to set shader resource variables (non-static version for this effect)
	bool SetShaderResourceVariable(const std::string& variableName, ID3D11ShaderResourceView* resource);

	// Static helper functions for any effect
	static bool SetShaderResourceVariable(ID3DX11Effect* effect, const std::string& variableName, ID3D11ShaderResourceView* resource);
	static bool SetVectorVariable(ID3DX11Effect* effect, const std::string& variableName, const void* data, uint32_t size);

private:
	bool LoadFXFile();

	void EnumerateAllVariables();

	void SetupCustomTextures();
	ID3D11ShaderResourceView* LoadTextureFromFile(const std::string& filename);
	std::string GetResourceNameFromVariable(ID3DX11EffectVariable* variable);

	void LoadTechniques();
	std::vector<std::string> GetBaseTechniqueNames();

	std::string GetRenderTargetFromTechnique(ID3DX11EffectTechnique* technique);
	std::string GetUINameFromTechnique(ID3DX11EffectTechnique* technique);
	void LoadUITechniques();
	Effect::Texture* GetEffectTexture(const std::string& name);
	ID3D11RenderTargetView* GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback);

	// UI Variable helpers
	std::string GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName);
	UIWidgetType ParseWidgetType(const std::string& widget);
	std::vector<std::string> ParseDropdownList(const std::string& list);
	void LoadUIVariableValue(UIVariable& uiVar);
	void LoadVariableFromString(UIVariable& uiVar, const std::string& value);
};