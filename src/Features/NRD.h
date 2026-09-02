#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "NRDReblurIntegration.h"

#include <NRDSettings.h>

/**
 * NRD: NVIDIA Real-Time Denoisers — core top-level service.
 *
 * Owns the per-frame guide textures shared by every NRD-consuming feature
 * (motion vectors, viewZ, packed normal+roughness) and runs the guide
 * preparation compute pass once per frame. Other features (ScreenSpaceGI,
 * ScreenSpaceReflections, future denoising clients) construct their own
 * NRDReblurIntegration instances and consume the guides published here.
 */
struct NRD : Feature
{
	virtual inline std::string GetName() override { return "NRD"; }
	virtual inline std::string GetShortName() override { return "NRD"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return std::make_pair(
			std::string(
				"NVIDIA Real-Time Denoisers (NRD) integration. Provides the shared "
				"viewZ / normal+roughness / motion-vector guide textures and the "
				"REBLUR denoiser plumbing used by Screen Space GI, Screen Space "
				"Reflections, and other features that produce noisy radiance signals."),
			std::vector<std::string>{
				"Shared NRD guide textures",
				"REBLUR denoiser infrastructure",
				"Common camera/jitter/frame-index state" });
	}

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	// Per-frame service entry point. Called once at the start of the deferred
	// pass so every consumer sees consistent guides for this frame.
	void PrepareGuides();

	// True when PrepareGuides has run successfully for this frame. Consumers
	// should skip their dispatches if guides aren't ready (e.g. world not
	// loaded, shader failed to compile).
	bool AreGuidesReady() const { return guidesReadyThisFrame; }

	// Build a CommonSettings block shared by every denoiser instance this
	// frame. Centralizing this guarantees frameIndex and prev-frame matrices
	// stay in lockstep across consumers.
	const nrd::CommonSettings& GetCommonSettings();

	// Per-frame ReBLUR settings published by NRD with the consumer-tuned
	// parameters folded in. Consumers call this just before SetDenoiserSettings.
	struct REBLURSettings
	{
		uint32_t MaxAccumulatedFrameNum = 30;
		uint32_t MaxFastAccumulatedFrameNum = 6;
		uint32_t MaxStabilizedFrameNum = nrd::REBLUR_MAX_HISTORY_FRAME_NUM;
		uint32_t HistoryFixFrameNum = 3;
		uint32_t HistoryFixBasePixelStride = 14;
		uint32_t HistoryFixAlternatePixelStride = 14;
		float FastHistoryClampingSigmaScale = 2.0f;
		float MinHitDistanceWeight = 0.1f;
		float MinBlurRadius = 1.0f;
		float MaxBlurRadius = 30.0f;
		float LobeAngleFraction = 0.15f;
		float RoughnessFraction = 0.15f;
		float PlaneDistanceSensitivity = 0.02f;
		float SplitScreen = 0.0f;
		uint32_t HitDistanceReconstructionMode = 0;
		bool EnableValidation = false;
		bool ReturnHistoryLength = false;
	};

	// Translate the UI struct into an nrd::ReblurSettings, applying common
	// clamps. Callers can still overwrite specific fields (e.g. hitDistanceParameters
	// for specular) after this returns.
	void ApplyReblurSettings(nrd::ReblurSettings& out, const REBLURSettings& in, nrd::CheckerboardMode checkerboard) const;

	// Render an ImGui block for a REBLURSettings instance. Returns true if any
	// value changed. Centralized so SSGI/SSR (and future consumers) all expose
	// the same controls with the same labels and ranges.
	bool DrawReblurSettings(REBLURSettings& s, bool showAdvanced, const char* tag);

	// Guide accessors. Valid after PrepareGuides() returns guidesReadyThisFrame=true.
	ID3D11ShaderResourceView* GetViewZSRV() const { return texNRDViewZ ? texNRDViewZ->srv.get() : nullptr; }
	ID3D11ShaderResourceView* GetNormalRoughnessSRV() const { return texNRDNormalRoughness ? texNRDNormalRoughness->srv.get() : nullptr; }
	ID3D11ShaderResourceView* GetMotionVectorSRV() const { return texNRDMV ? texNRDMV->srv.get() : nullptr; }
	ID3D11UnorderedAccessView* GetMotionVectorUAV() const { return texNRDMV ? texNRDMV->uav.get() : nullptr; }

	//////////////////////////////////////////////////////////////////////////

	struct Settings
	{
		bool Enabled = true;
	} settings;

	// Shared per-frame state. Tracked here so every consumer sees identical
	// prev-frame matrices and a single monotonic frame counter.
	Matrix worldToViewMat{};
	Matrix prevWorldToViewMat{};
	Matrix prevProjMatrix{};
	float2 prevJitter{};
	nrd::CommonSettings commonSettings{};
	bool commonSettingsValidThisFrame = false;
	bool guidesReadyThisFrame = false;
	bool hasCommonFrameHistory = false;
	uint32_t lastCommonGameFrame = 0;
	uint16_t prevResourceSize[2] = {};
	uint16_t prevRectSize[2] = {};

	// Shared guide textures owned by NRD.
	eastl::unique_ptr<Texture2D> texNRDViewZ = nullptr;
	eastl::unique_ptr<Texture2D> texNRDNormalRoughness = nullptr;
	eastl::unique_ptr<Texture2D> texNRDMV = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> prepareNRDGuidesCompute = nullptr;
};

// REBLURSettings is consumed by every feature that owns an NRDReblurIntegration
// instance (ScreenSpaceGI, ScreenSpaceReflections, ...). Its serializer must be
// visible in every such translation unit, so the macro lives here, not in NRD.cpp.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	NRD::REBLURSettings,
	MaxAccumulatedFrameNum,
	MaxFastAccumulatedFrameNum,
	MaxStabilizedFrameNum,
	HistoryFixFrameNum,
	HistoryFixBasePixelStride,
	HistoryFixAlternatePixelStride,
	FastHistoryClampingSigmaScale,
	MinHitDistanceWeight,
	MinBlurRadius,
	MaxBlurRadius,
	LobeAngleFraction,
	RoughnessFraction,
	PlaneDistanceSensitivity,
	SplitScreen,
	HitDistanceReconstructionMode,
	EnableValidation,
	ReturnHistoryLength)
