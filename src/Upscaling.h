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

	bool d3d12Interop = false;
	double refreshRate = 0.0f;
	float resolutionScale = 1.0f;

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

	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
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
			GetSingleton()->ConfigureUpscaling(a_state);
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
			GetSingleton()->PostDisplay();
			func(a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void CustomUpscale();

	struct UpsampleDynamicResolution_Render
	{
		static void thunk(RE::BSImagespaceShader* a1, RE::BSTriShape* a2)
		{		
			globals::state->BeginPerfEvent(std::format("{} Draw", magic_enum::enum_name(RE::ImageSpaceManager::ISUpsampleDynamicResolution)));
				
			auto upscaling = globals::upscaling;
			auto upscaleMethod = upscaling->GetUpscaleMethod();

			if (upscaleMethod == UpscaleMethod::kFSR || upscaleMethod == UpscaleMethod::kDLSS)
				upscaling->CustomUpscale();
			else
				func(a1, a2);

			globals::state->EndPerfEvent();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CopyScreenshot
	{
		static void thunk(RE::ImageSpaceManager*,
			uint32_t,
			uint32_t,
			uint32_t,
			RE::ImageSpaceShaderParam*)
		{
			auto renderer = globals::game::renderer;
			auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
			auto& screenshot = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kSCREENSHOT];

			ID3D11Resource* swapChainResource;
			main.RTV->GetResource(&swapChainResource);

			globals::d3d::context->CopyResource(screenshot.texture, swapChainResource);
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
			
			stl::write_thunk_call<UpsampleDynamicResolution_Render>(REL::RelocationID(100548, 107733).address() + REL::Relocate(0x152, 0x78, 0x7E));
			stl::write_thunk_call<CopyScreenshot>(REL::RelocationID(35556, 35556).address() + REL::Relocate(0x3E6, 0x3E6));

			std::uint8_t nop5[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
			REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D, 0x25), nop5, sizeof(nop5));

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
