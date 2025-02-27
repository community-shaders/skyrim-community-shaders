#pragma once

#include "Buffer.h"
#include "State.h"

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
		uint dlssPreset = (uint)sl::DLSSPreset::ePresetC;
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

	Texture2D* colorBufferShared;
	Texture2D* depthBufferShared;

	winrt::com_ptr<ID3D12Resource> colorBufferShared12;
	winrt::com_ptr<ID3D12Resource> depthBufferShared12;

	ID3D11ComputeShader* copyDepthToSharedBufferCS;

	void CreateFrameGenerationResources();
	void CopyResourcesToSharedBuffers();

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

	struct MenuManagerDrawInterfaceStartHook
	{
		static void thunk(int64_t a1)
		{
			GetSingleton()->CopyResourcesToSharedBuffers();
			func(a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
		if (!State::GetSingleton()->upscalerLoaded) {
			bool isGOG = !GetModuleHandle(L"steam_api64.dll");

			stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));
			stl::write_thunk_call<TAA_BeginTechnique>(REL::RelocationID(100540, 107270).address() + REL::Relocate(0x3E9, 0x3EA, 0x448));
			stl::write_thunk_call<TAA_EndTechnique>(REL::RelocationID(100540, 107270).address() + REL::Relocate(0x3F3, 0x3F4, 0x452));
			stl::write_thunk_call<BSImageSpacerShader_RenderPassImmediately>(REL::RelocationID(100951, 107733).address() + REL::Relocate(0x82, 0x78, 0x7E));
			
			stl::write_thunk_call<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084).address() + REL::Relocate(0x7E, 0x83, 0x97));

			logger::info("[Upscaling] Installed hooks");

			RE::UI::GetSingleton()->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(Upscaling::GetSingleton());
			logger::info("[Upscaling] Registered for MenuOpenCloseEvent");
		} else {
			logger::info("[Upscaling] Not installing hooks due to Skyrim Upscaler");
		}
	}
};
