#pragma once

#include "Buffer.h"
#include "State.h"

#include <d3d11_4.h>
#include <d3d12.h>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
// Streamline Core
#include <sl.h>
#include <sl_consts.h>
#include <sl_hooks.h>
#include <sl_version.h>

// Streamline Features
#include <sl_deepdvc.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_matrix_helpers.h>
#include <sl_nis.h>
#include <sl_reflex.h>
#pragma warning(pop)

class Streamline
{
public:
	static Streamline* GetSingleton()
	{
		static Streamline singleton;
		return &singleton;
	}

	inline std::string GetShortName() { return "Streamline"; }

	bool enabledAtBoot = false;
	bool initialized = false;

	bool featureDLSS = false;
	bool featureDLSSG = false;
	bool featureReflex = false;
	bool featureNIS = false;

	bool reflex = true;

	sl::ViewportHandle viewport{ 0 };
	sl::FrameToken* frameToken;
	sl::FrameToken* frameTokenPrevious;

	PFun_slGetNewFrameToken* slGetNewFrameToken{};

	uint32_t frameID = 1;

	sl::FrameToken* GetFrameToken(uint32_t a_frameID)
	{
		static uint32_t lastframeID = 0;
		if (lastframeID < a_frameID) {
			slGetNewFrameToken(frameToken, &a_frameID);
			frameTokenPrevious = frameToken;
		}
		lastframeID = frameID;
		return frameToken;
	}

	struct Settings
	{
		sl::DLSSGMode frameGenerationMode = sl::DLSSGMode::eOn;
		int frameLimitMode = 0;
	};

	Settings settings{};

	HMODULE interposer = NULL;

	// SL Interposer Functions
	PFun_slInit* slInit{};
	PFun_slShutdown* slShutdown{};
	PFun_slIsFeatureSupported* slIsFeatureSupported{};
	PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
	PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
	PFun_slEvaluateFeature* slEvaluateFeature{};
	PFun_slAllocateResources* slAllocateResources{};
	PFun_slFreeResources* slFreeResources{};
	PFun_slSetTag* slSetTag{};
	PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
	PFun_slGetFeatureVersion* slGetFeatureVersion{};
	PFun_slUpgradeInterface* slUpgradeInterface{};
	PFun_slSetConstants* slSetConstants{};
	PFun_slGetNativeInterface* slGetNativeInterface{};
	PFun_slGetFeatureFunction* slGetFeatureFunction{};
	PFun_slSetD3DDevice* slSetD3DDevice{};
	decltype(&D3D12CreateDevice) slD3D12CreateDevice{};

	// DLSS specific functions
	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
	PFun_slDLSSGetState* slDLSSGetState{};
	PFun_slDLSSSetOptions* slDLSSSetOptions{};

	// DLSSG specific functions
	PFun_slDLSSGGetState* slDLSSGGetState{};
	PFun_slDLSSGSetOptions* slDLSSGSetOptions{};

	// Reflex specific functions
	PFun_slReflexGetState* slReflexGetState{};
	PFun_slReflexSleep* slReflexSleep{};
	PFun_slReflexSetOptions* slReflexSetOptions{};

	// NIS specific functions
	PFun_slNISSetOptions* slNISSetOptions{};
	PFun_slNISGetState* slNISGetState{};

	PFun_slPCLSetMarker* slPCLSetMarker2{};

	void DrawSettings();

	void LoadInterposer();
	void Initialize();

	HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory);

	void CheckFeatures(DXGI_ADAPTER_DESC adapterDesc);

	void Present();

	void Upscale(Texture2D* a_color, Texture2D* a_alphaMask, sl::DLSSPreset a_preset, float a_sharpness);
	void Sharpen(Texture2D* a_sharpenTexture, float a_sharpness);
	void UpdateConstants();

	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);

	void RestoreDefaultSettings();

	void DestroyDLSSResources();

	struct Main_RenderWorld
	{
		static void thunk(bool a1);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks(){
		stl::write_thunk_call<Main_RenderWorld>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x831, 0x841, 0x791));
	};
};
