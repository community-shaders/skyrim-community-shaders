#pragma once

#include "PostProcessing/PostProcessFeature.h"

#include "PostProcessing/BokehResources.h"
#include "PostProcessing/Border.h"
#include "PostProcessing/CODBloom.h"
#include "PostProcessing/Camera.h"
#include "PostProcessing/CinematicCamera.h"
#include "PostProcessing/ColorGrading.h"
#include "PostProcessing/Composite.h"
#include "PostProcessing/DoF.h"
#include "PostProcessing/HistogramAutoExposure.h"
#include "PostProcessing/LUT.h"
#include "PostProcessing/LensFlare.h"
#include "PostProcessing/LocalExposure.h"
#include "PostProcessing/MotionBlur.h"
#include "PostProcessing/PhysicalGlare.h"
#include "PostProcessing/PhysicalLens.h"
#include "PostProcessing/Vignette.h"

#include <chrono>
#include <map>
#include <optional>
#include <tuple>

struct PostProcessing : Feature
{
	static PostProcessing* GetSingleton()
	{
		static PostProcessing singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Post Processing"; }
	virtual inline std::string GetDisplayName() override { return T("feature.post_processing.name", "Post Processing"); }
	virtual inline std::string GetShortName() override { return "PostProcessing"; }
	virtual inline std::string_view GetShaderDefineName() override { return "POSTPROCESS"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override
	{
		return t == RE::BSShader::Type::ImageSpace;
	};
	virtual std::string_view GetCategory() const override { return FeatureCategories::kPostProcessing; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			T("feature.post_processing.description", "Post Processing provides advanced image effects and enhancements to improve the visual quality of the game."),
			{ T("feature.post_processing.key_feature_1", "Customizable post-processing effects"),
				T("feature.post_processing.key_feature_2", "Supports various presets for different visual styles"),
				T("feature.post_processing.key_feature_3", "Improves overall image quality and immersion"),
				T("feature.post_processing.key_feature_4", "Includes features like bloom, depth of field, and color grading") }
		};
	}

	virtual void DrawSettings() override;
	virtual bool HasDebugSection() const override { return true; }
	virtual void DrawDebugSection() override;

	/** @brief The effect whose settings page is open when it has debug views, or null (the debug tabs then show Post Processing only). */
	PostProcessFeature* GetOpenEffectWithDebug() const;

	/** @brief Draws the game's current image space values as cards (the Post Processing debug tab). */
	void DrawImageSpaceDebug();
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void ApplyContextualDefaults(json& a_defaults) const override;

	/**
	 * @brief Whether Post Processing wants to replace the vanilla tonemap this frame.
	 */
	bool WantsTonemapOwnership() const;

	/**
	 * @brief Whether the sub-feature pipeline runs this frame: Post Processing mode and owner of the tonemap.
	 *
	 * Otherwise PreProcess draws no sub-features, so every other entry point must bail out too.
	 */
	bool IsPipelineRunning() const;

	/**
	 * @brief Whether Color Grading wants Pumbo AutoHDR this frame (SDR tonemapper + HDR Display).
	 */
	bool WantsAutoHDR() const;

	/// Settings loaded before the pipeline exists.
	json pendingSettings = {};

	void ProcessSettings(json& o_json);

	/// Sets each sub-feature's on/off state to the shipped default. Their values default to their
	/// Settings initializers, so this and RestoreDefaultSettings need no defaults file.
	void ApplyDefaultEnabledStates();

	/** @brief Creates every sub-feature at its shipped defaults, without GPU resources. */
	void BuildPipeline();

	enum class FeaturePipelineIndex : size_t
	{
		DoF,
		Vignette,
		LocalExposure,
		AutoExposure,
		MotionBlur,
		PhysicalGlare,
		CODBloom,
		LensFlare,
		PhysicalLens,
		Composite,
		ColorGrading,
		LUT,
		Camera,
		Border,
		COUNT
	};

	void UpdateVanillaEffects(bool active);

	/// shared_ptr, not unique_ptr: see PostProcessFeature's weak_ptr callback contract.
	std::array<std::shared_ptr<PostProcessFeature>, static_cast<size_t>(FeaturePipelineIndex::COUNT)> pipeline;

	BokehResources bokehResources;

	CinematicCamera::Controller cinematicCamera;

	[[nodiscard]] const CinematicCamera::PhysicalCameraState* GetActivePhysicalCameraState() const
	{
		return cinematicCamera.GetState();
	}

	[[nodiscard]] CinematicCamera::Controller& GetCinematicCamera() { return cinematicCamera; }

	template <typename T>
	T* GetPipelineFeature(FeaturePipelineIndex idx)
	{
		return static_cast<T*>(pipeline[static_cast<size_t>(idx)].get());
	}

	virtual void ClearShaderCache() override;

	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void PostPostLoad() override;
	virtual void Prepass() override;

	/// Returns the untouched scene target when ISHDR must tonemap it directly.
	std::optional<RE::RENDER_TARGET> PreProcess(RE::RENDER_TARGET a_input);
	void DrawBeforeUpscaling();
	void DrawFeature(PostProcessFeature& feature, PostProcessFeature::TextureInfo& lastTexColor);

	/// Render-thread scratch for the serial color chain. The returned target
	/// differs from input, which must be the only chain texture still needed.
	Texture2D* GetColorTarget(ID3D11Texture2D* input, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

	enum class Gamut : uint
	{
		Rec709,
		ACEScg,
		Rec2020
	};
	struct alignas(16) CopyCB
	{
		Gamut inputGamut = Gamut::Rec709;
		Gamut outputGamut = Gamut::Rec709;
		float gamma = 1.f;
		float scale = 1.f;
	};

	/////////////////////////////////////////////////////////////////////////////////

	bool bypass = false;
	bool isrefraction = false;

	struct ImageSpaceManager
	{
		RE::ImageSpaceData gameISData;
	};

	std::unique_ptr<ImageSpaceManager> imageSpaceManager = std::make_unique<ImageSpaceManager>();

	/// Format-conversion copy pass (fullscreen triangle PS draw).
	winrt::com_ptr<ID3D11PixelShader> copyPS = nullptr;
	std::unique_ptr<ConstantBuffer> copyCB;

	/// Shared fullscreen-triangle vertex shader for every raster pass in the
	/// pipeline (compiled from PostProcessing/fullscreen.hlsli).
	winrt::com_ptr<ID3D11VertexShader> fullscreenVS = nullptr;

	/**
	 * @brief Vertex shader every rasterized sub-feature draws with.
	 * @return The shared fullscreen-triangle VS, or null until SetupResources
	 *         succeeds; raster passes must skip when null.
	 */
	ID3D11VertexShader* GetFullscreenVS() const { return fullscreenVS.get(); }

	/////////////////////////////////////////////////////////////////////////////////

	// Tonemap-time entry is driven by PostProcessingExtensions::Main_HDRTonemapBlendCinematic_Render
	// (Hooks.cpp), which arbitrates between this feature and Effects11. Only the refraction
	// hook remains here, and it just flags which buffer the scene currently lives in.

	struct BSImagespaceShaderRefraction_SetupTechnique
	{
		static void thunk(RE::BSShader* a_shader, RE::BSShaderMaterial* a_material)
		{
			globals::features::postProcessing.isrefraction = true;
			func(a_shader, a_material);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

private:
	struct RenderTargetCopy
	{
		ID3D11Texture2D* target;
		PostProcessFeature::TextureInfo source;
		Texture2D* scratch;
		CopyCB conversion;
	};

	std::optional<RenderTargetCopy> PrepareRenderTargetCopy(
		ID3D11Texture2D* target, PostProcessFeature::TextureInfo source, const CopyCB& conversion);
	void CopyToRenderTarget(const RenderTargetCopy& copy);
	bool BeginLinearProcessing(PostProcessFeature::TextureInfo& texture);
	Texture2D* GetColorTarget(ID3D11Texture2D* input, UINT width, UINT height, DXGI_FORMAT format);

	D3D11_TEXTURE2D_DESC sceneColorDesc{};
	D3D11_TEXTURE2D_DESC mainColorDesc{};
	std::vector<std::unique_ptr<Texture2D>> colorTargets;
	std::map<std::tuple<UINT, UINT, DXGI_FORMAT, UINT>, std::chrono::steady_clock::time_point> colorTargetRetry;
};
