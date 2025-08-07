#pragma once

#include "FidelityFX.h"
#include "Streamline.h"
#include "XeSS.h"
#include <d3d12.h>
#include <winrt/base.h>

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
		kXESS,
		kDLSS
	};

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kDLSS;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kXESS;
		uint upscaleMethodNothing = (uint)UpscaleMethod::kTAA;
		uint upscalePreset = 1;  // Default to Quality (1=Quality, 2=Balanced, 3=Performance, 0=Native AA)
		uint frameLimitMode = 1;
		uint frameGenerationMode = 1;
		uint frameGenerationForceEnable = 0;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
	};

	Settings settings;

	bool isWindowed = false;
	bool lowRefreshRate = false;

	bool fidelityFXMissing = false;

	bool d3d12Interop = false;
	double refreshRate = 0.0f;
	float resolutionScale = 1.0f;
	bool allowUpscaling = false;

	// FG FPS Measurement for Overlay
	bool IsFrameGenerationActive() const;
	float GetFrameGenerationFrameTime() const;

	void DrawSettings();
	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);
	void RestoreDefaultSettings();

	UpscaleMethod GetUpscaleMethod();

	void CheckResources();

	ID3D11ComputeShader* encodeTexturesCS = nullptr;
	ID3D11ComputeShader* GetEncodeTexturesCS();
	
	ID3D11ComputeShader* encodeTexturesTransparencyCS = nullptr;
	ID3D11ComputeShader* GetEncodeTexturesTransparencyCS();

	ID3D11ComputeShader* rcasCS = nullptr;
	ID3D11ComputeShader* GetRCASCS();

	ID3D11PixelShader* depthUpscalePS = nullptr;
	ID3D11PixelShader* GetDepthUpscalePS();

	ID3D11VertexShader* depthUpscaleVS = nullptr;
	ID3D11VertexShader* GetDepthUpscaleVS();

	struct ResolutionScaleCB
	{
		float4 ResolutionScale;
	};

	ConstantBuffer* resolutionScaleCB;

	ID3D11DepthStencilState* depthUpscaleState;
	ID3D11BlendState* depthUpscaleBlendState = nullptr;
	ID3D11RasterizerState* depthUpscaleRasterizerState = nullptr;

	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

	Texture2D* upscalingTexture;
	Texture2D* reactiveMaskTexture;
	Texture2D* transparencyCompositionMaskTexture;

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	// Shared D3D12 device and interop resources
	winrt::com_ptr<ID3D12Device> sharedD3D12Device;
	winrt::com_ptr<ID3D12CommandQueue> sharedD3D12CommandQueue;
	winrt::com_ptr<ID3D12CommandAllocator> sharedD3D12CommandAllocator;
	winrt::com_ptr<ID3D12GraphicsCommandList> sharedD3D12CommandList;
	winrt::com_ptr<ID3D12Fence> sharedD3D12Fence;
	HANDLE sharedFenceEvent = nullptr;
	UINT64 sharedFenceValue = 0;

	// D3D11/D3D12 shared fence for interop synchronization
	winrt::com_ptr<ID3D11Fence> sharedD3D11Fence;
	UINT64 sharedInteropFenceValue = 0;

	// Shared D3D12 resources for upscaling systems
	WrappedResource* HUDLessBufferShared12;
	WrappedResource* depthBufferShared12;
	WrappedResource* motionVectorBufferShared12;

	WrappedResource* inputColorBufferShared12;
	WrappedResource* outputColorBufferShared12;

	// Frame tracking to ensure shared resources are only copied once per frame
	Util::FrameChecker sharedResourcesFrameChecker;

	ID3D11ComputeShader* copyDepthToSharedBufferCS;

	bool useHUDLess = false;

	void CreateFrameGenerationResources();
	void CreateSharedD3D12Device(IDXGIAdapter* a_dxgiAdapter);
	void CreateSharedD3D12Resources();
	void CopyFrameGenerationResources();
	void CopySharedD3D12Resources();
	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state)
		{
			func(a_state);
			GetSingleton()->ConfigureUpscaling(a_state);
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

	struct Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager* a1, uint32_t a3, uint32_t er8_)
		{
			auto upscaling = globals::upscaling;
			auto upscaleMethod = upscaling->GetUpscaleMethod();

			if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA)
				upscaling->PerformUpscaling();

			auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
			GET_INSTANCE_MEMBER(BSImagespaceShaderISTemporalAA, imageSpaceManager);

			BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod == UpscaleMethod::kTAA;

			func(a1, a3, er8_);

			BSImagespaceShaderISTemporalAA->taaEnabled = upscaleMethod != UpscaleMethod::kNONE;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct SetScissorRect
	{
		static void thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom)
		{
			auto resolutionScale = globals::upscaling->resolutionScale;
			func(This, a_left * resolutionScale, a_top * resolutionScale, a_right * resolutionScale, a_bottom * resolutionScale);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPrecipitation
	{
		static void thunk()
		{
			auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
			runtimeData.dynamicResolutionLock = 1;
			func();
			runtimeData.dynamicResolutionLock = 0;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSFaceGenManager_UpdatePendingCustomizationTextures
	{
		static void thunk()
		{
			auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
			runtimeData.dynamicResolutionLock = 1;
			func();
			runtimeData.dynamicResolutionLock = 0;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
		bool isGOG = !GetModuleHandle(L"steam_api64.dll");
		stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

		// Calculates resolution and jitter
		stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));

		// Disables the original dynamic resolution system
		REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D, 0x25), REL::NOP5, sizeof(REL::NOP5));

		// Performs upscaling in between volumetric lighting and post processing
		stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7, 0x206));

		if (!REL::Module::IsVR()) {
			// Patches RSSetScissorRect calls to use dynamic resolution
			// This is a PC-specific function hence it was missing
			stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

			// Patches facegen texture generation to not use dynamic resolution
			stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

			// Patches precipitation camera to not use dynamic resolution
			stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1, 0x2FA));
		}

		logger::info("[Upscaling] Installed hooks");
	}
};
