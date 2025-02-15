#pragma once

#define NV_WINDOWS
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_matrix_helpers.h>
#include <sl_reflex.h>

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

	double refreshRate = 60.0;

	sl::ViewportHandle viewport{ 0 };
	sl::FrameToken* frameToken;

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
	PFun_slGetNewFrameToken* slGetNewFrameToken{};
	PFun_slSetD3DDevice* slSetD3DDevice{};

	// DLSS specific functions
	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
	PFun_slDLSSGetState* slDLSSGetState{};
	PFun_slDLSSSetOptions* slDLSSSetOptions{};

	// DLSSG specific functions
	PFun_slDLSSGGetState* slDLSSGGetState{};
	PFun_slDLSSGSetOptions* slDLSSGSetOptions{};

	// Reflex specific functions
	PFun_slReflexGetState* slReflexGetState{};
	PFun_slReflexSetMarker* slReflexSetMarker{};
	PFun_slReflexSleep* slReflexSleep{};
	PFun_slReflexSetOptions* slReflexSetOptions{};

	Texture2D* colorBufferShared;
	Texture2D* depthBufferShared;

	ID3D11ComputeShader* copyDepthToSharedBufferCS;

	void DrawSettings();

	void LoadInterposer();
	void Initialize();
	void PostDevice(DXGI_SWAP_CHAIN_DESC* a_swapChainDesc);

	HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory);

	HRESULT CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software,
		UINT Flags,
		const D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
		IDXGISwapChain** ppSwapChain,
		ID3D11Device** ppDevice,
		D3D_FEATURE_LEVEL* pFeatureLevel,
		ID3D11DeviceContext** ppImmediateContext);

	void SetupResources();

	void CopyResourcesToSharedBuffers();
	void Present();

	void Upscale(Texture2D* a_color, Texture2D* a_alphaMask, sl::DLSSPreset a_preset);
	void UpdateConstants();

	void SaveSettings(json& o_json);
	void LoadSettings(json& o_json);

	void RestoreDefaultSettings();

	void DestroyDLSSResources();

	void BeginFrame();

	struct Main_Update_Start
	{
		static void thunk(INT64 a_unk)
		{
			GetSingleton()->BeginFrame();
			func(a_unk);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderWorld
	{
		static void thunk(bool a1);
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
		stl::write_thunk_call<Main_Update_Start>(REL::RelocationID(35565, 36564).address() + REL::Relocate(0x1E, 0x3E, 0x33));
		stl::write_thunk_call<Main_RenderWorld>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x831, 0x841, 0x791));
		stl::write_thunk_call<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084).address() + REL::Relocate(0x7E, 0x83, 0x97));
	}
};
