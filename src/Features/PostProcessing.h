#pragma once

#include "PostProcessing/PostProcessFeature.h"

#include "PostProcessing/CODBloom.h"
#include "PostProcessing/ColourTransforms.h"
#include "PostProcessing/DoF.h"
#include "PostProcessing/HistogramAutoExposure.h"
#include "PostProcessing/LUT.h"
#include "PostProcessing/LensFlare.h"
#include "PostProcessing/MotionBlur.h"
#include "PostProcessing/VanillaImagespace.h"
#include "PostProcessing/Vignette.h"
#include "PostProcessing/pCamera.h"

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
	virtual inline std::string GetShortName() override { return "PostProcessing"; }
	virtual inline std::string_view GetShaderDefineName() override { return "POSTPROCESS"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override
	{
		return t == RE::BSShader::Type::ImageSpace;
	};
	virtual std::string_view GetCategory() const override { return "Post-Processing"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Post Processing provides advanced image effects and enhancements to improve the visual quality of the game.",
			{
				"Customizable post-processing effects",
				"Supports various presets for different visual styles",
				"Improves overall image quality and immersion",
				"Includes features like bloom, depth of field, and color grading"
			}
		};
	}

	virtual bool SupportsVR() { return true; }

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	json pendingSettings = {};

	void ProcessSettings(json& o_json);

	std::vector<std::string> presets = {};
	std::vector<std::string> LoadPresets();
	void SavePresetTo(std::string a_name);
	void LoadPresetFrom(std::string a_name);

	enum class FeaturePipelineIndex : size_t
	{
		AutoExposure,
		MotionBlur,
		DoF,
		CODBloom,
		LensFlare,
		VanillaImagespace,
		LUT,
		Vignette,
		Camera,
		COUNT
	};

	std::array<std::unique_ptr<PostProcessFeature>, static_cast<size_t>(FeaturePipelineIndex::COUNT)> pipeline;

	virtual void ClearShaderCache() override;

	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void PostPostLoad() override;
	virtual void Prepass() override;

	void PreProcess();
	void UpdateToD();

	/////////////////////////////////////////////////////////////////////////////////

	bool bypass = false;
	bool isrefraction = false;

	struct ImageSpaceManager
	{
		float timeOfDay[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };  // 0: dawn, 1: sunrise, 2: day, 3: sunset, 4: dusk, 5: night
		RE::ImageSpaceData gameISData;
	};

	ImageSpaceManager* imageSpaceManager;

	// std::vector<std::unique_ptr<PostProcessFeature>> feats = {};
	std::vector<std::unique_ptr<PostProcessFeature>> colorTransformsFeats = {};

	eastl::unique_ptr<Texture2D> texCopy = nullptr;
	eastl::unique_ptr<Texture2D> texAfterTAA = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> copyCS = nullptr;

	/////////////////////////////////////////////////////////////////////////////////

	struct BSImagespaceShaderHDRTonemapBlendCinematic_SetupTechnique
	{
		static void thunk(RE::BSShader* a_shader, RE::BSShaderMaterial* a_material)
		{
			if (globals::features::postProcessing.loaded)
				globals::features::postProcessing.PreProcess();
			func(a_shader, a_material);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderHDRTonemapBlendCinematicFade_SetupTechnique
	{
		static void thunk(RE::BSShader* a_shader, RE::BSShaderMaterial* a_material)
		{
			if (globals::features::postProcessing.loaded)
				globals::features::postProcessing.PreProcess();
			func(a_shader, a_material);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

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