#pragma once

#include "PostProcessing/PostProcessFeature.h"

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
		uint AdvancedMode = 0;  // placeholder
		uint pad[2];
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

	virtual void ClearShaderCache() override;

	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void PostPostLoad() override;
	virtual void Prepass() override;

	void PreProcess();

	void DrawAfterTAA(Texture2D* inout_tex);

	/////////////////////////////////////////////////////////////////////////////////

	bool bypass = false;
	bool isrefraction = false;

	std::vector<std::unique_ptr<PostProcessFeature>> feats = {};

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