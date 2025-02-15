#pragma once

#define TDM_API_COMMONLIB
#include "TDM/TrueDirectionalMovementAPI.h"

#include "Buffer.h"
#include "PostProcessFeature.h"

struct DoF : public PostProcessFeature
{
    virtual inline std::string GetType() const override { return "Depth of Field"; }
	virtual inline std::string GetDesc() const override { return "Depth of Field, based on CinematicDOF by Frans Bouma"; }

    struct Settings
    {
        bool AutoFocus = true;
        uint8_t pad[3];
        float TransitionSpeed = 0.5f;
        float2 FocusCoord = float2(0.5f, 0.5f);
        float ManualFocusPlane = 0.4f;
        float FocalLength = 50.0f;
        float FNumber = 2.8f;
        float FarPlaneMaxBlur = 1.0f;
        float NearPlaneMaxBlur = 1.0f;
        float BlurQuality = 7.0f;
        float NearFarDistanceCompensation = 1.0f;
        float BokehBusyFactor = 0.5f;
        float HighlightBoost = 0.0f;
        float PostBlurSmoothing = 0.0f;
		bool  targetFocus = false;
		float targetFocusFocalLength = 50.0f;
        bool consoleSelection = false;
    } settings;

    struct alignas(16) DoFCB
    {
        float TransitionSpeed;
        float2 FocusCoord;
        float ManualFocusPlane;
        float FocalLength;
        float FNumber;
        float FarPlaneMaxBlur;
        float NearPlaneMaxBlur;
        float BlurQuality;
        float NearFarDistanceCompensation;
        float BokehBusyFactor;
        float HighlightBoost;
        float PostBlurSmoothing;
        float Width;
        float Height;
        bool AutoFocus;
        uint8_t pad[3];
    };

    eastl::unique_ptr<ConstantBuffer> dofCB = nullptr;

    eastl::unique_ptr<Texture2D> texOutput = nullptr;
    eastl::unique_ptr<Texture2D> texPreBlurred = nullptr;
    eastl::unique_ptr<Texture2D> texFarBlurred = nullptr;
    eastl::unique_ptr<Texture2D> texNearBlurred = nullptr;
    eastl::unique_ptr<Texture2D> texBlurredFiltered = nullptr;
    eastl::unique_ptr<Texture2D> texBlurredFull = nullptr;
    eastl::unique_ptr<Texture2D> texPostSmooth = nullptr;
    eastl::unique_ptr<Texture2D> texPostSmooth2 = nullptr;
    eastl::unique_ptr<Texture2D> texFocus = nullptr;
    eastl::unique_ptr<Texture2D> texPreFocus = nullptr;
    eastl::unique_ptr<Texture2D> texCoC = nullptr;
    eastl::unique_ptr<Texture2D> texCoCTileTmp = nullptr;
    eastl::unique_ptr<Texture2D> texCoCTileTmp2 = nullptr;
    eastl::unique_ptr<Texture2D> texCoCTileNeighbor = nullptr;
    eastl::unique_ptr<Texture2D> texCoCBlur1 = nullptr;
    eastl::unique_ptr<Texture2D> texCoCBlur2 = nullptr;

    winrt::com_ptr<ID3D11ComputeShader> UpdateFocusCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> CalculateCoCCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> CoCTile1CS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> CoCTile2CS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> CoCTileNeighbor = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> CoCGaussian1CS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> CoCGaussian2CS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> BlurCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> FarBlurCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> NearBlurCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> TentFilterCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> CombinerCS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> PostSmoothing1CS = nullptr;
    winrt::com_ptr<ID3D11ComputeShader> PostSmoothing2AndFocusingCS = nullptr;

    winrt::com_ptr<ID3D11SamplerState> colorSampler = nullptr;
    winrt::com_ptr<ID3D11SamplerState> depthSampler = nullptr;

    virtual void SetupResources() override;
    virtual void ClearShaderCache() override;
    void CompileComputeShaders();

    virtual void RestoreDefaultSettings() override;
    virtual void LoadSettings(json&) override;
    virtual void SaveSettings(json&) override;

    virtual void DrawSettings() override;

    virtual void Draw(TextureInfo&) override;

    RE::NiPoint3 GetCameraPos();
	bool GetInDialogue();
	// float GetDistanceToDialogueTarget();
	// float targetFocusPercent;
	bool GetTargetLockEnabled();
	// float GetDistanceToLockedTarget();
    float GetDistanceToReference(RE::TESObjectREFR* a_ref);
    float debugDistance = 0.0f;
    float debugFocusPlane = 0.0f;
    uint currentRef = 0;

	TDM_API::IVTDM2* g_TDM = nullptr;
};