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

	ID3D11ComputeShader* encodeTexturesCS;
	ID3D11ComputeShader* GetEncodeTexturesCS();

	ID3D11ComputeShader* rcasCS;
	ID3D11ComputeShader* GetRCASCS();

	ID3D11PixelShader* depthUpscalePS;
	ID3D11PixelShader* GetDepthUpscalePS();

	struct ResolutionScaleCB
	{
		float4 ResolutionScale;
	};

	ConstantBuffer* resolutionScaleCB;

	ID3D11DepthStencilState* depthUpscaleState;

	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

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

	struct Main_HDRTonemapBlendCinematic_Render
	{
		static void thunk(RE::ImageSpaceManager* a1, RE::ImageSpaceEffect* a2, uint32_t a3, uint32_t a4, RE::ImageSpaceShaderParam* a5)
		{
			func(a1, a2, a3, a4, a5);
			globals::upscaling->UpscaleDepth();
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


	static void InstallHooks()
	{
		if (!globals::state->upscalerLoaded) {
			bool isGOG = !GetModuleHandle(L"steam_api64.dll");
			stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

			// Calculates resolution and jitter
			stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2, 0x104));

			// Disables the original dynamic resolution system
			std::uint8_t nop5[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
			REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D, 0x25), nop5, sizeof(nop5));
			
			// Performs upscaling inbetween volumetric lighting and post processing
			stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 100430).address() + REL::Relocate(0x1F0, 0x1C5));
			
			// Performs depth upscaling after the final main post processing pass
			stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 99023).address() + REL::Relocate(0x1EA, 0x1C5));
			stl::write_thunk_call<Main_HDRTonemapBlendCinematic_Render>(REL::RelocationID(99023, 99023).address() + REL::Relocate(0x230, 0x1C5));

			// Patches RSSetScissorRect calls to use dynamic resolution
			// This is a PC-specific function hence it was missing
			stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 75564));

			// Fix precipitation camera using dynamic resolution when it shouldn't
			stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 35560).address() + REL::Relocate(0x3A1, 0x3A1));

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
