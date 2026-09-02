#pragma once

#define TDM_API_COMMONLIB
#include "TDM/TrueDirectionalMovementAPI.h"

#include "Buffer.h"
#include "PostProcessFeature.h"

#include <array>

struct DoF : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Depth of Field"; }
	virtual inline std::string GetDisplayName() const override { return T("feature.post_processing.do_f.name", "Depth of Field"); }
	virtual inline std::string GetDesc() const override { return T("feature.post_processing.do_f.description", "Depth of Field, based on CinematicDOF by Frans Bouma."); }
	virtual inline bool DisableInMainLoadingMenu() const override { return true; }

	struct Settings
	{
		bool AutoFocus = true;
		float TransitionSpeed = 0.5f;
		float2 FocusCoord = float2(0.5f, 0.5f);
		float ManualFocusPlane = 0.4f;
		float FocalLength = 50.0f;
		float FNumber = 2.8f;
		float FarPlaneMaxBlur = 1.0f;
		float NearPlaneMaxBlur = 1.0f;
		bool UseAdaptiveGather = true;
		int GatherQuality = 0;
		int BokehMode = 0;
		int BokehBladeCount = 6;
		float BokehBladeRoundness = 1.0f;
		float BlurQuality = 7.0f;
		float NearFarDistanceCompensation = 1.0f;
		float BokehBusyFactor = 0.5f;
		float HighlightBoost = 0.0f;
		float PostBlurSmoothing = 0.0f;
		float PetzvalStrength = 0.0f;
		int HighlightShape = 0;
		float HighlightShapeRotationAngle = 0.0f;
		// Max blur disc radius, as a fraction of the screen width.
		float MaxNearCoCRadius = 0.025f;
		float MaxFarCoCRadius = 0.025f;
		bool targetFocus = false;
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
		uint HighlightShape;
		float HighlightShapeRotationAngle;
		float PetzvalStrength;
		uint AutoFocus;
		float MaxNearCoCRadius;
		float MaxFarCoCRadius;
		uint TileDilateRadius;
		// packs as HLSL `uint2 CoCTileDim; uint2 HalfResDim;`
		uint CoCTileDimX;
		uint CoCTileDimY;
		uint HalfResDimX;
		uint HalfResDimY;
		uint BokehMode;
		float CustomShapeRadiusScale;
		float BokehMaxRadius;
		float NearMaxReachPx;
		uint BokehBladeCount;
		float BokehBladeRoundness;
		float ProceduralBokehAreaScale;
		uint pad;
	};
	static_assert(sizeof(DoFCB) == 128, "DoFCB must match the cbuffer layout in dof.cs.hlsl");

	eastl::unique_ptr<ConstantBuffer> dofCB = nullptr;
	eastl::unique_ptr<StructuredBuffer> proceduralBokehSamples = nullptr;
	int cachedBokehBladeCount = -1;
	float cachedBokehBladeRoundness = -1.0f;
	float proceduralBokehMaxRadius = 1.0f;
	float proceduralBokehAreaScale = 1.0f;

	eastl::unique_ptr<Texture2D> texOutput = nullptr;
	eastl::unique_ptr<Texture2D> texPreBlurred = nullptr;
	eastl::unique_ptr<Texture2D> texFarBlurred = nullptr;
	eastl::unique_ptr<Texture2D> texNearBlurred = nullptr;
	eastl::unique_ptr<Texture2D> texBlurredFiltered = nullptr;
	eastl::unique_ptr<Texture2D> texPostSmooth = nullptr;
	eastl::unique_ptr<Texture2D> texPostSmooth2 = nullptr;
	eastl::unique_ptr<Texture2D> texFocus = nullptr;
	eastl::unique_ptr<Texture2D> texPreFocus = nullptr;
	eastl::unique_ptr<Texture2D> texCoC = nullptr;
	eastl::unique_ptr<Texture2D> texCoCHalf = nullptr;
	eastl::unique_ptr<Texture2D> texCoCTile = nullptr;
	eastl::unique_ptr<Texture2D> texCoCTileTmp = nullptr;
	eastl::unique_ptr<Texture2D> texCoCTileDilated = nullptr;
	// Quarter/eighth/sixteenth-resolution gather inputs. Color and signed CoC stay separate because
	// the scene color target format is not guaranteed to have an alpha channel.
	std::array<eastl::unique_ptr<Texture2D>, 3> texGatherColor = {};
	std::array<eastl::unique_ptr<Texture2D>, 3> texGatherCoC = {};
	// Bokeh shapes are provided by PostProcessing::bokehResources (shared with LensFlare)

	winrt::com_ptr<ID3D11ComputeShader> UpdateFocusCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> CalculateCoCCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> CoCTileFlattenCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> CoCTileDilateHCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> CoCTileDilateVCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> DownsampleCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> DownsampleLegacyCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> ReduceColorCoCCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> ReduceColorCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> FarBlurCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> NearBlurCS = nullptr;
	std::array<winrt::com_ptr<ID3D11ComputeShader>, 2> FarGatherCS = {};
	std::array<winrt::com_ptr<ID3D11ComputeShader>, 2> NearGatherCS = {};
	winrt::com_ptr<ID3D11ComputeShader> GatherPostfilterCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> CombinerCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> PostSmoothing1CS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> PostSmoothing2AndFocusingCS = nullptr;

	winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
	void UpdateProceduralBokehSamples(bool force = false);

	RE::NiPoint3 GetCameraPos();
	RE::NiPoint3 GetReferenceFocusPosition(RE::TESObjectREFR* a_ref);
	bool GetReferenceFocusCoord(RE::TESObjectREFR* a_ref, float2& a_focusCoord);
	bool GetInDialogue();
	bool GetTargetLockEnabled();
	float GetDistanceToReference(RE::TESObjectREFR* a_ref);
	float debugDistance = 0.0f;
	float debugFocusPlane = 0.0f;
	uint currentRef = 0;

	TDM_API::IVTDM2* g_TDM = nullptr;
};
