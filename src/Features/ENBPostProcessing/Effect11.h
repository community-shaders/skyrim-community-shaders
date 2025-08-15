#pragma once

#include <d3d11.h>
#include <Effects11/d3dx11effect.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;


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

    void ExecuteTechniqueSequence(const std::string& baseTechniqueName, ID3D11RenderTargetView* renderTarget);
    
    // UI System
    void RenderImGui();
    void LoadUIVariables();
    void UpdateUIVariables();

    // Technique selection
    const std::string& GetSelectedTechnique() const { return selectedTechnique; }
    const std::vector<std::string>& GetAvailableTechniques() const { return availableTechniques; }

private:
    struct TechniqueInfo {
        ComPtr<ID3DX11EffectTechnique> technique;
        std::string renderTargetName;
    };

    ComPtr<ID3DX11Effect> effect;
	std::unordered_map<std::string, std::vector<TechniqueInfo>> techniques;
	std::unordered_map<std::string, ComPtr<ID3DX11EffectVariable>> variables;
	
	struct Texture {
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
	};

	std::unordered_map<std::string, Texture> commonTextureCache;
	
    std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> customTextureCache;

    // UI Variable System
    enum class UIVariableType {
        Float,
        Int,
        Bool
    };

    enum class UIWidgetType {
        Default,
        Spinner,
        Dropdown
    };

    struct UIVariable {
        UIVariableType type;
        UIWidgetType widgetType;
        std::string name;
        std::string displayName;
        ComPtr<ID3DX11EffectVariable> effectVariable;
        
        // Value storage
        union {
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

	ComPtr<ID3DX11EffectVariable> Timer;
	ComPtr<ID3DX11EffectVariable> ScreenSize;
	ComPtr<ID3DX11EffectVariable> AdaptiveQuality;
	ComPtr<ID3DX11EffectVariable> Weather;
	ComPtr<ID3DX11EffectVariable> TimeOfDay1;
	ComPtr<ID3DX11EffectVariable> TimeOfDay2;
	ComPtr<ID3DX11EffectVariable> ENightDayFactor;
	ComPtr<ID3DX11EffectVariable> EInteriorFactor;

	ComPtr<ID3DX11EffectVariable> Params01;
	ComPtr<ID3DX11EffectVariable> ENBParams01;
    
    ComPtr<ID3D11Buffer> quadVertexBuffer;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11RasterizerState> rasterizerState;
    ComPtr<ID3D11BlendState> blendState;

    void CreateQuadGeometry();
    void CreateRenderStates();
    void SetupCommonTextures();
    std::vector<uint8_t> LoadFileToMemory(const std::string& filePath);

    void SetupCommonVariables();
    void UpdateCommonVariables();

	void SetupEffectVariables();
	void UpdateEffectVariables();

    void LoadResourceNameTextures();
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