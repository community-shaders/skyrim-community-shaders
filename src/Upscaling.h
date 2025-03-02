#pragma once

#include "FidelityFX.h"
#include "Streamline.h"

class Upscaling : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (a_event->menuName == RE::LoadingMenu::MENU_NAME ||
			a_event->menuName == RE::MapMenu::MENU_NAME ||
			a_event->menuName == RE::LockpickingMenu::MENU_NAME ||
			a_event->menuName == RE::MainMenu::MENU_NAME ||
			a_event->menuName == RE::MistMenu::MENU_NAME)
			reset = true;
		return RE::BSEventNotifyControl::kContinue;
	}

	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	inline std::string GetShortName() { return "Upscaling"; }

	bool reset = false;
	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kTAA;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kTAA;
		uint upscaleMethodNoFSR = (uint)UpscaleMethod::kTAA;
		float sharpness = 0.5f;
		uint dlssPreset = (uint)sl::DLSSPreset::ePresetE;
	};

	Settings settings;

	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();

	UpscaleMethod GetUpscaleMethod();

	void CheckResources();

	ID3D11ComputeShader* rcasCS;
	ID3D11ComputeShader* GetRCASCS();

	ID3D11ComputeShader* encodeTexturesCS;
	ID3D11ComputeShader* encodeTexturesDLSSCS;
	ID3D11ComputeShader* GetEncodeTexturesCS();
	ID3D11ComputeShader* GetEncodeTexturesDLSSCS();

	void UpdateJitter();
	void Upscale();
	void SharpenTAA();

	Texture2D* upscalingTexture;
	Texture2D* alphaMaskTexture;

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state)
		{
			func(a_state);
			GetSingleton()->UpdateJitter();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	bool validTaaPass = false;
	std::mutex settingsMutex;  // Mutex to protect settings access

	struct TAA_BeginTechnique
	{
		static void thunk(RE::BSImagespaceShaderISTemporalAA* a_shader, RE::BSTriShape* a_null)
		{
			func(a_shader, a_null);
			GetSingleton()->validTaaPass = true;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TAA_EndTechnique
	{
		static void thunk(RE::BSImagespaceShaderISTemporalAA* a_shader, RE::BSTriShape* a_null)
		{
			auto singleton = GetSingleton();
			auto upscaleMode = singleton->GetUpscaleMethod();
			if ((upscaleMode != UpscaleMethod::kTAA && upscaleMode != UpscaleMethod::kNONE) && singleton->validTaaPass)
				singleton->Upscale();
			else
				func(a_shader, a_null);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImageSpacerShader_RenderPassImmediately
	{
		static void thunk(RE::BSRenderPass* Pass, uint32_t Technique, bool AlphaTest, uint32_t RenderFlags)
		{
			func(Pass, Technique, AlphaTest, RenderFlags);
			auto singleton = GetSingleton();
			auto upscaleMode = singleton->GetUpscaleMethod();
			if (singleton->validTaaPass && upscaleMode == UpscaleMethod::kTAA)
				singleton->SharpenTAA();
			singleton->validTaaPass = false;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks();
};
