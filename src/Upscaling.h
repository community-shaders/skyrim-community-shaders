#pragma once

#include "FidelityFX.h"
#include "Streamline.h"

/**
 * @brief Installs hooks for Direct3D 11 device context operations.
 *
 * Sets up hooks on the provided ID3D11DeviceContext to intercept and extend D3D11 Map and Unmap operations, enabling custom frame buffer management and resource tracking.
 *
 * @param a_context Pointer to the Direct3D 11 device context to hook.
 */
class Upscaling
{
public:
	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	inline std::string GetShortName() { return "Upscaling"; }

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
		uint upscalePreset = (uint)FfxFsr3QualityMode::FFX_FSR3_QUALITY_MODE_QUALITY;
		float sharpness = 0.0f;
		uint dlssPreset = 1;
		uint frameLimitMode = 1;
		uint frameGenerationMode = 1;
		uint frameGenerationForceEnable = 0;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
	};

	Settings settings;

	bool isWindowed = false;
	bool lowRefreshRate = false;

	bool streamlineMissing = false;
	bool fidelityFXMissing = false;
	
	bool frameGenEnabled = false;
	double refreshRate = 0.0f;

	// FG FPS Measurement for Overlay
	bool IsFrameGenerationActive() const;
	float GetFrameGenerationFrameTime() const;

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

	uint outputSize[2];
	uint renderSize[2];

	void CreateFrameGenerationResources();
	void CopyBuffersToSharedResources();
	void PostDisplay();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

	void ConfigureUpscaling();

	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state)
		{
			func(a_state);
			GetSingleton()->UpdateJitter();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void PostInitD3D();

	std::mutex settingsMutex;  // Mutex to protect settings access

	struct MenuManagerDrawInterfaceStartHook
	{
		static void thunk(int64_t a1)
		{
			GetSingleton()->PostDisplay();
			func(a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct SetupWindowHook
	{
		static void thunk(int64_t a1)
		{
			func(a1);
			GetSingleton()->PostInitD3D();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GetWindowRectHook
	{
		static bool thunk(HWND hWnd, LPRECT lpRect)
		{
			auto ret = func(hWnd, lpRect);

			auto upscaling = globals::upscaling;

			lpRect->right = (LONG)upscaling->renderSize[0];
			lpRect->bottom = (LONG)upscaling->renderSize[1];

			return ret;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GetClientRectHook
	{
		static bool thunk(HWND hWnd, LPRECT lpRect)
		{
			auto ret = func(hWnd, lpRect);

			auto upscaling = globals::upscaling;

			lpRect->right = (LONG)upscaling->renderSize[0];
			lpRect->bottom = (LONG)upscaling->renderSize[1];

			return ret;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};


	static void InstallHooks()
	{
		if (!globals::state->upscalerLoaded) {
			bool isGOG = !GetModuleHandle(L"steam_api64.dll");

			stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));
			stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

			stl::write_thunk_call<SetupWindowHook>(REL::RelocationID(75595, 77226).address() + REL::Relocate(0x50, 0x2BC));
			stl::write_thunk_call<GetClientRectHook, 6>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0x192, 0x18B));  // D6A0C0 (D6A252), DA5A00 (DA5B8B)

			//*(uintptr_t*)&GetWindowRectHook::func = Detours::X64::DetourFunction((uintptr_t) & ::GetWindowRect, (uintptr_t)&GetWindowRectHook::thunk);
			//*(uintptr_t*)&GetClientRectHook::func = Detours::X64::DetourFunction((uintptr_t) & ::GetClientRect, (uintptr_t)&GetClientRectHook::thunk);

			// Always enable TAA jitters, even without TAA

			static REL::Relocation<uintptr_t> updateJitterHook{ REL::RelocationID(75709, 77518) };          // D7CFB0, DB96E0
			static REL::Relocation<uintptr_t> buildCameraStateDataHook{ REL::RelocationID(75711, 77520) };  // D7D130, DB9850

			uint8_t patch1[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
			uint8_t patch2[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

			REL::safe_write<uint8_t>(updateJitterHook.address() + REL::Relocate(0xE, 0x11), patch1);
			REL::safe_write<uint8_t>(buildCameraStateDataHook.address() + REL::Relocate(0x1D5, 0x1D5), patch2);

			logger::info("[Upscaling] Installed hooks");
		} else {
			logger::info("[Upscaling] Not installing hooks due to Skyrim Upscaler");
		}
	}

	void InstallD3DHooks(ID3D11DeviceContext* a_context);

	struct FrameBuffer
	{
		Matrix CameraView;
		Matrix CameraProj;
		Matrix CameraViewProj;
		Matrix CameraViewProjUnjittered;
		Matrix CameraPreviousViewProjUnjittered;
		Matrix CameraProjUnjittered;
		Matrix CameraProjUnjitteredInverse;
		Matrix CameraViewInverse;
		Matrix CameraViewProjInverse;
		Matrix CameraProjInverse;
		float4 CameraPosAdjust;
		float4 CameraPreviousPosAdjust;
		float4 FrameParams;
		float4 DynamicResolutionParams1;
		float4 DynamicResolutionParams2;
	};

	D3D11_MAPPED_SUBRESOURCE* mappedFrameBuffer = nullptr;
	FrameBuffer frameBufferCached{};

	void CacheFramebuffer();

	struct ID3D11DeviceContext_Map
	{
		static HRESULT thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Unmap
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource);
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
