#pragma once

#include <wrl/client.h>
#include <d3d11.h>
#include <Effects11/d3dx11effect.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <set>
#include <algorithm>

// Forward declarations
namespace RE {
    namespace BSGraphics {
        struct RenderTargetData;
    }
}


/**
 * @brief Framework for loading and executing ENBSeries-compatible FX effect files
 * 
 * This class provides a complete framework for loading DirectX 11 Effect (.fx) files
 * compatible with ENBSeries, executing them with proper render target handling,
 * and managing shader techniques and variables.
 */
class Effect11
{
public:

	void Initialize();

	bool LoadFXFile(std::filesystem::path a_filePath);
	
	bool ReloadEffect();
	void ClearEffect();
	bool IsEffectLoaded() const { return effect != nullptr; }
	
	std::string GetLoadedEffectPath() const { return loadedEffectPath; }

	void Execute(RE::BSGraphics::RenderTargetData& input, RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output);
    
    // UI System
    void RenderImGui();
    void LoadUIVariables();
    void UpdateUIVariables();

    // Technique selection
    const std::string& GetSelectedTechnique() const { return selectedTechnique; }
    const std::vector<std::string>& GetAvailableTechniques() const { return availableTechniques; }

    // Preset management
    bool SavePreset(const std::string& presetName);
    bool LoadPreset(const std::string& presetName);
    std::vector<std::string> GetAvailablePresets();
    void CreateDefaultPreset();

private:
    struct TechniqueInfo {
        Microsoft::WRL::ComPtr<ID3DX11EffectTechnique> technique;
        std::string renderTargetName;
    };

    Microsoft::WRL::ComPtr<ID3DX11Effect> effect;
	std::unordered_map<std::string, std::vector<TechniqueInfo>> techniques;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DX11EffectVariable>> variables;
	
	struct Texture {
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
	};

	std::unordered_map<std::string, Texture> commonTextureCache;
	
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> customTextureCache;

    // UI Variable System
    enum class UIVariableType {
        Float,
        Int,
        Bool,
        Float2,
        Float3,
        Float4
    };

    enum class UIWidgetType {
        Default,
        Spinner,
        Dropdown
    };

    enum class UIVariableCategory {
        General,
        Camera,
        TimeOfDay,
        Adaptation,
        Colors,
        ToneMapping,
        LensEffects,
        Processing,
        Lighting,
        Atmosphere,
        PostProcessing,
        Custom,
        Other
    };

    struct UIVariable {
        UIVariableType type;
        UIWidgetType widgetType;
        UIVariableCategory category;
        std::string name;
        std::string displayName;
        std::string description; // UI tooltip description
        Microsoft::WRL::ComPtr<ID3DX11EffectVariable> effectVariable;
        
        // Value storage
        union {
            float floatValue;
            int intValue;
            bool boolValue;
            float float2Value[2];
            float float3Value[3];
            float float4Value[4];
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
    std::string loadedEffectPath;
    
    // Preset management
    std::string currentPresetName = "Default";
    std::vector<std::string> availablePresets;
	
    Microsoft::WRL::ComPtr<ID3DX11EffectVariable> TextureColor;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> TextureBloom;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> TextureLens;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> TextureAdaptation;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> TextureAperture;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> Timer;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> ScreenSize;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> AdaptiveQuality;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> Weather;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> TimeOfDay1;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> TimeOfDay2;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> ENightDayFactor;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> EInteriorFactor;

	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> Params01;
	Microsoft::WRL::ComPtr<ID3DX11EffectVariable> ENBParams01;
    
    Microsoft::WRL::ComPtr<ID3D11Buffer> quadVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;
	
    void ExecuteTechniqueSequence(const std::string& baseTechniqueName, RE::BSGraphics::RenderTargetData& input, RE::BSGraphics::RenderTargetData& swap, RE::BSGraphics::RenderTargetData& output);

	void CreateQuadGeometry();
    void CreateRenderStates();
    void SetupCommonTextures();
    std::vector<uint8_t> LoadFileToMemory(const std::string& filePath);

    void SetupCommonVariables();
    void UpdateCommonVariables();

	void SetupEffectVariables();
	void UpdateEffectVariables();
	
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
    UIVariableCategory CategorizeVariable(const std::string& variableName, const std::string& uiName);
    std::string CategoryToString(UIVariableCategory category);
    std::vector<std::string> ParseDropdownList(const std::string& list);
    void LoadUIVariableValue(UIVariable& uiVar);
    void RenderVariableUI(UIVariable& uiVar, float labelWidth, float inputWidth, bool& valuesChanged);
    
    // Tab system
    void RenderGeneralTab(bool& valuesChanged);
    void RenderCategoryTab(UIVariableCategory category, const std::string& tabName, bool& valuesChanged);
    void RenderAboutTab();
    void RenderCondensedAboutTab();
    std::vector<UIVariableCategory> GetAvailableCategories();
    
    // Preset management implementation helpers
    void RefreshPresetList();
};