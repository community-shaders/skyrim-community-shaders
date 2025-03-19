#pragma once

#include "FidelityFX.h"
#include "Streamline.h"

class Upscaling
{
public:
	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	std::string GetShortName() { return "Upscaling"; }

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
		uint upscaleMethod = static_cast<uint>(UpscaleMethod::kTAA);
		uint upscaleMethodNoDLSS = static_cast<uint>(UpscaleMethod::kTAA);
		uint upscaleMethodNoFSR = static_cast<uint>(UpscaleMethod::kTAA);
		float sharpness = 0.0f;
		uint dlssPreset = 1;
		uint frameLimitMode = 1;
		uint frameGenerationMode = 1;
		uint frameGenerationForceEnable = 0;
	};

	Settings settings;

	bool isWindowed = false;
	bool lowRefreshRate = false;

	bool streamlineMissing = false;
	bool fidelityFXMissing = false;

	bool d3d12Interop = false;
	double refreshRate = 0.0f;

	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();

	UpscaleMethod GetUpscaleMethod();

	void CheckResources();

	ID3D11ComputeShader* encodeTexturesCS;
	ID3D11ComputeShader* GetEncodeTexturesCS();

	ID3D11ComputeShader* rcasCS;
	ID3D11ComputeShader* GetRCASCS();

	void UpdateJitter();
	void Upscale();
	void SharpenTAA();
	void ApplyHDR();

	Texture2D* upscalingTexture;
	Texture2D* alphaMaskTexture;

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	Texture2D* HUDLessBufferShared;
	Texture2D* depthBufferShared;
	Texture2D* motionVectorBufferShared;

	winrt::com_ptr<ID3D12Resource> HUDLessBufferShared12;
	winrt::com_ptr<ID3D12Resource> depthBufferShared12;
	winrt::com_ptr<ID3D12Resource> motionVectorBufferShared12;

	ID3D11ComputeShader* copyDepthToSharedBufferCS;

	bool useHUDLess = false;

	void CreateFrameGenerationResources();
	void CopyBuffersToSharedResources();
	void PostDisplay();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

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
	std::mutex settingsMutex; // Mutex to protect settings access

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
			GetSingleton()->PostDisplay();
			func(a1);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
		if (!globals::state->upscalerLoaded) {
			bool isGOG = !GetModuleHandle(L"steam_api64.dll");

			stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));
			stl::write_thunk_call<TAA_BeginTechnique>(REL::RelocationID(100540, 107270).address() + REL::Relocate(0x3E9, 0x3EA, 0x448));
			stl::write_thunk_call<TAA_EndTechnique>(REL::RelocationID(100540, 107270).address() + REL::Relocate(0x3F3, 0x3F4, 0x452));
			stl::write_thunk_call<BSImageSpacerShader_RenderPassImmediately>(REL::RelocationID(100951, 107733).address() + REL::Relocate(0x82, 0x78, 0x7E));

			stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

			logger::info("[Upscaling] Installed hooks");
		} else {
			logger::info("[Upscaling] Not installing hooks due to Skyrim Upscaler");
		}
	}
};
