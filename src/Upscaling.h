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
		uint dlssPreset = 1;
		uint vsyncMode = 1;
		uint frameLimitMode = 1;
		uint frameGenerationMode = 1;
		uint frameGenerationForceEnable = 0;
	};

	Settings settings;

	bool isWindowed = false;
	bool lowRefreshRate = false;

	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();

	UpscaleMethod GetUpscaleMethod();

	void CheckResources();

	ID3D11ComputeShader* encodeTexturesCS;
	ID3D11ComputeShader* GetEncodeTexturesCS();

	void UpdateJitter();
	void Upscale();

	Texture2D* alphaMaskTexture;

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	Texture2D* colorBufferShared;
	Texture2D* depthBufferShared;
	Texture2D* motionVectorBufferShared;

	winrt::com_ptr<ID3D12Resource> colorBufferShared12;
	winrt::com_ptr<ID3D12Resource> depthBufferShared12;
	winrt::com_ptr<ID3D12Resource> motionVectorBufferShared12;

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

	static void InstallHooks()
	{
		if (!State::GetSingleton()->upscalerLoaded) {
			bool isGOG = !GetModuleHandle(L"steam_api64.dll");

			stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));

			logger::info("[Upscaling] Installed hooks");

			RE::UI::GetSingleton()->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(Upscaling::GetSingleton());
			logger::info("[Upscaling] Registered for MenuOpenCloseEvent");
		} else {
			logger::info("[Upscaling] Not installing hooks due to Skyrim Upscaler");
		}
	}
};
