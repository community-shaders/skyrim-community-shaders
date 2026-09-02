#pragma once

#include "PostProcessing/PostProcessFeature.h"

#include "PostProcessing/BokehResources.h"
#include "PostProcessing/Border.h"
#include "PostProcessing/CODBloom.h"
#include "PostProcessing/Camera.h"
#include "PostProcessing/ColorGrading.h"
#include "PostProcessing/Composite.h"
#include "PostProcessing/DoF.h"
#include "PostProcessing/HistogramAutoExposure.h"
#include "PostProcessing/LUT.h"
#include "PostProcessing/LensFlare.h"
#include "PostProcessing/LocalExposure.h"
#include "PostProcessing/MotionBlur.h"
#include "PostProcessing/PhysicalGlare.h"
#include "PostProcessing/Vignette.h"

struct PostProcessing : Feature
{
	static PostProcessing* GetSingleton()
	{
		static PostProcessing singleton;
		return &singleton;
	}

	struct alignas(16) Settings
	{
		uint DisableVanillaTonemapping = 1;
		uint pad[3];
	} settings;

	const std::string ppPresetPath = "Data\\SKSE\\Plugins\\CommunityShaders\\PostProcessing";

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
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/**
	 * @brief Whether Post Processing wants to replace the vanilla tonemap this frame.
	 *
	 * Queried by State::GetTonemapOwner() to arbitrate against Effects11. Note this is
	 * narrower than "is the pipeline active": with DisableVanillaTonemapping off the
	 * pipeline still runs its effects and then hands off to the vanilla tonemap.
	 */
	bool WantsTonemapOwnership() const;

	/**
	 * @brief Whether Effects11 replaced the tonemap this frame.
	 *
	 * The pipeline's output is discarded in that case, so every entry point that would
	 * write to a game render target must bail out rather than do work nothing consumes.
	 */
	bool IsTonemapOwnedByEffects11() const;

	/**
	 * @brief Builds the shared-buffer payload, masking flags the arbiter has revoked.
	 *
	 * DisableVanillaTonemapping is forced to 0 unless Post Processing actually owns the
	 * tonemap, so ISHDR and HDROutputCS do not assume a linear, already-tonemapped scene
	 * when another feature produced the image.
	 */
	Settings GetCommonBufferData();

	json pendingSettings = {};

	void ProcessSettings(json& o_json);

	std::vector<std::string> presets = {};
	std::vector<std::string> LoadPresets();
	void SavePresetTo(std::string a_name);
	void LoadPresetFrom(std::string a_name);

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
		Composite,
		ColorGrading,
		LUT,
		Camera,
		Border,
		COUNT
	};

	/// shared_ptr, not unique_ptr: see PostProcessFeature's weak_ptr callback contract.
	std::array<std::shared_ptr<PostProcessFeature>, static_cast<size_t>(FeaturePipelineIndex::COUNT)> pipeline;

	BokehResources bokehResources;

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

	void PreProcess(RE::RENDER_TARGET a_input);
	void DrawBeforeUpscaling();
	void ClearBorderMotionVectorsForFrameGen();
	void DrawFeature(PostProcessFeature& feature, PostProcessFeature::TextureInfo& lastTexColor);

	/**
	 * @brief Copies the pipeline output into a game render target, converting the
	 *        format via the copyPS fullscreen pass when the formats differ.
	 *
	 * Same-format copies go through CopySubresourceRegion directly; otherwise the
	 * source is rendered into convertTex first and then copied.
	 *
	 * @param targetRT  Game render target receiving the image.
	 * @param convertTex  Intermediate texture for format conversion (needs an RTV).
	 * @param srcTex  Texture holding the pipeline output.
	 * @param srcSRV  SRV of srcTex, sampled by the conversion pass.
	 */
	void CopyToRenderTarget(
		RE::BSGraphics::RenderTargetData& targetRT,
		Texture2D* convertTex,
		ID3D11Texture2D* srcTex,
		ID3D11ShaderResourceView* srcSRV);

	/////////////////////////////////////////////////////////////////////////////////

	bool bypass = false;
	bool isrefraction = false;

	struct ImageSpaceManager
	{
		RE::ImageSpaceData gameISData;
	};

	std::unique_ptr<ImageSpaceManager> imageSpaceManager = std::make_unique<ImageSpaceManager>();

	eastl::unique_ptr<Texture2D> texCopyMain = nullptr;
	eastl::unique_ptr<Texture2D> texCopyMainCopy = nullptr;

	/// Format-conversion copy pass (fullscreen triangle PS draw).
	winrt::com_ptr<ID3D11PixelShader> copyPS = nullptr;

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
};
