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

	// Technique selection
	const std::string& GetSelectedTechnique() const { return selectedTechnique; }
	const std::vector<std::string>& GetAvailableTechniques() const { return availableTechniques; }

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
		std::string name;
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

	// Technique selection
	std::string selectedTechnique;
	std::vector<std::string> availableTechniques;

	// Error tracking
	std::vector<std::string> errors;

	// Execute a technique sequence with ping-pong rendering
	void ExecuteTechniqueSequence(const std::string& baseTechniqueName, Texture& input, Texture& output, Texture& swap);

	// Execute a single technique
	void ExecuteTechnique(const std::string& techniqueName, Texture& input, Texture& output);

	// Allow EffectManager to setup common variables
	ID3DX11Effect* GetEffect() const { return effect.Get(); }

private:
	bool LoadFXFile();

	void EnumerateAllVariables();

	void SetupCustomTextures();
	ID3D11ShaderResourceView* LoadTextureFromFile(const std::string& filename);
	std::string GetResourceNameFromVariable(ID3DX11EffectVariable* variable);

	void LoadTechniques();
	std::vector<std::string> GetBaseTechniqueNames();

	std::string GetRenderTargetFromTechnique(ID3DX11EffectTechnique* technique);
	ID3D11RenderTargetView* GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback);

	// UI Variable helpers
	std::string GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName);
	UIWidgetType ParseWidgetType(const std::string& widget);
	std::vector<std::string> ParseDropdownList(const std::string& list);
	void LoadUIVariableValue(UIVariable& uiVar);
};